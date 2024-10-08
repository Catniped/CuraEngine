import subprocess
from PIL import Image
import skimage
import sys

layers = []
l_buf = []
x_buf = []
y_buf = []
xh = None
yh = None
scale = int(sys.argv[2])

print("Starting slice.")

"""get output"""
try:
    output = subprocess.check_output(f"./build/Release/CuraEngine slice -v -j /home/catniped/Downloads/fdmprinter.def.json -s extruder_nr=1 -l /home/catniped/Downloads/{sys.argv[1]}", shell=True)
except subprocess.CalledProcessError as e:
    output = e.output
except:
    pass

for line in output.decode().split("\n"):
    if line:
        if line == "nl":
            layers.append(l_buf)
            l_buf = []
        if line == "np":
            l_buf.append(x_buf)
            l_buf.append(y_buf)
            x_buf = []
            y_buf = []
        if line[0].isnumeric():
            x,y = line.split(" ")
            x2 = int(int(x)/scale)
            y2 = int(int(y)/scale)
            if not xh: xh = x2
            if not yh: yh = y2
            if x2 < xh: xh = x2
            if y2 < yh: yh = y2
            x_buf.append(x2)
            y_buf.append(y2)

print(f"Object contains {len(layers)} layers.")
for p in range(len(layers)):
    try:
        img = Image.new( '1', (2100,2100), "white")
        pixels = img.load()

        for i in range(0, len(layers[p])-1, 2):
            xx,yy = skimage.draw.polygon(layers[p][i],layers[p][i+1])

            for x,y in zip(xx,yy):
                pixels[x-xh,y-yh] = 0

        img.save(f"./output/layer{p}.bmp")
    except Exception as e:
        print(f"Exception at layer {p}: {e}")

