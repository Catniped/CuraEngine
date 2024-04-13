import matplotlib.pyplot as plt
import subprocess

x = []
y = []

try:
    output = subprocess.check_output("./build/Release/CuraEngine slice -v -j /home/catniped/Downloads/fdmprinter.def.json -s extruder_nr=1 -l /home/catniped/Downloads/20mm_cube.stl", shell=True)
except subprocess.CalledProcessError as e:
    output = e.output

for line in output.decode().split("\n"):
    if line:
        if line[0].isnumeric():
            x1,y1 = line.split(" ")
            x.append(x1)
            y.append(y1)

print(x)
print(y)

plt.scatter(x, y)   
plt.show()