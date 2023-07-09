// Copyright (c) 2023 UltiMaker
// CuraEngine is released under the terms of the AGPLv3 or higher

#ifndef PLUGINS_PLUGINPROXY_H
#define PLUGINS_PLUGINPROXY_H

#include <thread>

#include <agrpc/asio_grpc.hpp>
#include <agrpc/grpc_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <fmt/format.h>
#include <range/v3/utility/semiregular_box.hpp>
#include <spdlog/spdlog.h>
#include <string>

#include "Application.h"
#include "plugins/exception.h"
#include "plugins/metadata.h"
#include "utils/format/thread_id.h"
#include "utils/types/generic.h"

#include "cura/plugins/slots/handshake/v0/handshake.pb.h"
#include "cura/plugins/v0/slot_id.pb.h"

namespace cura::plugins
{

/**
 * @brief A class template representing a proxy for a plugin.
 *
 * The PluginProxy class template facilitates communication with plugins by providing
 * an interface for sending requests and receiving responses. It uses gRPC for communication.
 *
 * @tparam Slot The plugin slot ID.
 * @tparam Validator The type used for validating the plugin.
 * @tparam Stub The process stub type.
 * @tparam Prepare The prepare type.
 * @tparam Request The gRPC convertible request type.
 * @tparam Response The gRPC convertible response type.
 */
template<plugins::v0::SlotID SlotID, details::CharRangeLiteral SlotVersionRng, class Stub, class ValidatorTp, utils::grpc_convertable RequestTp, utils::grpc_convertable ResponseTp>
class PluginProxy
{
public:
    // type aliases for easy use
    using value_type = typename ResponseTp::native_value_type;
    using validator_type = ValidatorTp;

    using req_msg_type = typename RequestTp::value_type;
    using rsp_msg_type = typename ResponseTp::value_type;

    using req_converter_type = RequestTp;
    using rsp_converter_type = ResponseTp;

    using stub_t = Stub;

private:
    validator_type valid_{}; ///< The validator object for plugin validation.
    req_converter_type req_{}; ///< The request converter object.
    rsp_converter_type rsp_{}; ///< The response converter object.

    ranges::semiregular_box<stub_t> stub_; ///< The gRPC stub for communication.

    slot_metadata slot_info_{ .slot_id = SlotID, .version_range = SlotVersionRng.value, .engine_uuid = Application::getInstance().instance_uuid };
    std::optional<plugin_metadata> plugin_info_{ std::nullopt }; ///< The plugin info object.

public:
    /**
     * @brief Constructs a PluginProxy object.
     *
     * This constructor initializes the PluginProxy object by establishing communication
     * channels with the plugin identified by the given slot ID. It performs plugin validation
     * and checks for communication errors.
     *
     * @param channel A shared pointer to the gRPC channel for communication with the plugin.
     *
     * @throws std::runtime_error if the plugin fails validation or communication errors occur.
     */
    constexpr PluginProxy() = default;

    explicit PluginProxy(std::shared_ptr<grpc::Channel> channel) : stub_(channel)
    {
        // Connect to the plugin and exchange a handshake
        agrpc::GrpcContext grpc_context;
        grpc::Status status;
        slots::handshake::v0::HandshakeService::Stub handshake_stub(channel);

        boost::asio::co_spawn(
            grpc_context,
            [this, &status, &grpc_context, &handshake_stub]() -> boost::asio::awaitable<void>
            {
                using RPC = agrpc::RPC<&slots::handshake::v0::HandshakeService::Stub::PrepareAsyncCall>;
                grpc::ClientContext client_context{};
                prep_client_context(client_context);

                // Construct request
                handshake_request handshake_req;
                handshake_request::value_type request{ handshake_req(slot_info_) };

                // Make unary request
                handshake_response::value_type response;
                status = co_await RPC::request(grpc_context, handshake_stub, client_context, request, response, boost::asio::use_awaitable);
                handshake_response handshake_rsp;
                plugin_info_ = handshake_rsp(response, client_context.peer());
                valid_ = validator_type{ slot_info_, plugin_info_.value() };
                if (valid_)
                {
                    spdlog::info("Using plugin: '{}-{}' running at [{}] for slot {}", plugin_info_->plugin_name, plugin_info_->plugin_version, plugin_info_->peer, slot_info_.slot_id);
                }
            },
            boost::asio::detached);
        grpc_context.run();

        if (! status.ok()) // TODO: handle different kind of status codes
        {
            if (plugin_info_.has_value())
            {
                throw exceptions::RemoteException(slot_info_, plugin_info_.value(), status.error_message());
            }
            throw exceptions::RemoteException(slot_info_, status.error_message());
        }

        if (! valid_)
        {
            if (plugin_info_.has_value())
            {
                throw exceptions::ValidatorException(valid_, slot_info_, plugin_info_.value());
            }
            throw exceptions::ValidatorException(valid_, slot_info_);
        }
    };

    constexpr PluginProxy(const PluginProxy&) = default;
    constexpr PluginProxy(PluginProxy&&) noexcept = default;
    constexpr PluginProxy& operator=(const PluginProxy& other)
    {
        if (this != &other)
        {
            valid_ = other.valid_;
            stub_ = other.stub_;
            plugin_info_ = other.plugin_info_;
            slot_info_ = other.slot_info_;
        }
        return *this;
    }
    constexpr PluginProxy& operator=(PluginProxy&& other)
    {
        if (this != &other)
        {
            valid_ = std::move(other.valid_);
            stub_ = std::move(other.stub_);
            plugin_info_ = std::move(other.plugin_info_);
            slot_info_ = std::move(other.slot_info_);
        }
        return *this;
    }
    ~PluginProxy() = default;

    /**
     * @brief Executes the plugin operation.
     *
     * This operator allows the PluginProxy object to be invoked as a callable, which sends
     * a request to the plugin and waits for the response. The response is converted using
     * the response_converter_ object, and the converted value is returned.
     *
     * @tparam Args The argument types for the plugin request.
     * @param args The arguments for the plugin request.
     * @return The converted response value.
     *
     * @throws std::runtime_error if communication with the plugin fails.
     */
    value_type operator()(auto&&... args)
    {
        agrpc::GrpcContext grpc_context;
        value_type ret_value{};
        grpc::Status status;

        boost::asio::co_spawn(
            grpc_context,
            [this, &status, &grpc_context, &ret_value, &args...]() -> boost::asio::awaitable<void>
            {
                using RPC = agrpc::RPC<&stub_t::PrepareAsyncCall>;
                grpc::ClientContext client_context{};
                prep_client_context(client_context);

                // Construct request
                auto request{ req_(std::forward<decltype(args)>(args)...) };

                // Make unary request
                rsp_msg_type response;
                status = co_await RPC::request(grpc_context, stub_, client_context, request, response, boost::asio::use_awaitable);
                ret_value = rsp_(response);
            },
            boost::asio::detached);
        grpc_context.run();

        if (! status.ok()) // TODO: handle different kind of status codes
        {
            if (plugin_info_.has_value())
            {
                throw exceptions::RemoteException(slot_info_, plugin_info_.value(), status.error_message());
            }
            throw exceptions::RemoteException(slot_info_, status.error_message());
        }
        return ret_value;
    }

    void prep_client_context(grpc::ClientContext& client_context, std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
    {
        // Set time-out
        client_context.set_deadline(std::chrono::system_clock::now() + timeout);

        // Metadata
        client_context.AddMetadata("cura-engine-uuid", slot_info_.engine_uuid.data());
        client_context.AddMetadata("cura-thread-id", fmt::format("{}", std::this_thread::get_id()));
    }
};
} // namespace cura::plugins

#endif // PLUGINS_PLUGINPROXY_H
