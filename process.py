import subprocess
from PIL import Image
from bresenham import bresenham

layers = []
l_buf = []
p_buf = []


"""get output"""
try:
    output = subprocess.check_output("./build/Release/CuraEngine slice -v -j /home/catniped/Downloads/fdmprinter.def.json -s extruder_nr=1 -l /home/catniped/Downloads/stlp_bludisko.stl", shell=True)
except subprocess.CalledProcessError as e:
    output = e.output

for line in output.decode().split("\n"):
    if line:
        if line == "nl":
            layers.append(l_buf)
            l_buf = []
        if line == "np":
            l_buf.append(p_buf)
            p_buf = []
        if line[0].isnumeric():
            x,y = line.split(" ")
            p_buf.append([int(int(x)/50),int(int(y)/50)])
 
img = Image.new( 'RGB', (2100,2100), "white")
pixels = img.load()

"""draw lines"""
for part in layers[2]:
    for i in range(len(part)):
        if i < len(part)-1:
            x1 = part[i][0]
            y1 = part[i][1]
            x2 = part[i+1][0]
            y2 = part[i+1][1]

            for x,y in bresenham(x1,y1,x2,y2):
                pixels[int(x),int(y)] = (int(x),int(y),0)

        else:
            x1 = part[i][0]
            y1 = part[i][1]
            x2 = part[0][0]
            y2 = part[0][1]

            for x,y in bresenham(x1,y1,x2,y2):
                pixels[int(x),int(y)] = (int(x),int(y),0)

v_buf = ()

"""fill polygons"""
for y0 in range(2100):
    for x0 in range(2100):
        if pixels[x0,y0] != (255,255,255):
            if v_buf:
                for x in range(v_buf[0],x0):
                    pixels[x,y0] = (x,y0,0)
                v_buf = ()
            else:
                v_buf = (x0,y0)
    v_buf = ()

img.show()
