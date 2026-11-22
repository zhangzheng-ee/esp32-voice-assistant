import sys, os
from PIL import Image, ImageOps

SIZE = 240
SRC = r"D:\zz187\Arduino\terminal\pics"
OUT = os.path.join(SRC, "images.h")

files = ["p1_listen.jpg", "p2_think.jpg", "p3_speak.jpg"]
names = ["IMG_LISTEN", "IMG_THINK", "IMG_SPEAK"]

lines = []
lines.append("#pragma once")
lines.append("// 240x240 RGB565 (big-endian) 图片数据，由 make_images.py 生成")
lines.append("#define IMG_W 240")
lines.append("#define IMG_H 240")
lines.append("")

for fn, nm in zip(files, names):
    p = os.path.join(SRC, fn)
    im = Image.open(p).convert("RGB")
    # 居中裁成正方形再缩放，避免变形
    im = ImageOps.fit(im, (SIZE, SIZE), method=Image.LANCZOS, centering=(0.5, 0.5))
    px = im.load()
    vals = []
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            vals.append(v)
    lines.append("const uint16_t %s[%d] = {" % (nm, SIZE*SIZE))
    for i in range(0, len(vals), 12):
        lines.append("  " + ",".join("0x%04X" % v for v in vals[i:i+12]) + ",")
    lines.append("};")
    lines.append("")
    print("OK", fn, "->", nm)

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines))
print("written", OUT, os.path.getsize(OUT), "bytes")
