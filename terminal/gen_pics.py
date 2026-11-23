from PIL import Image, ImageDraw, ImageFont
import os, math

W = H = 240
OUT = r"D:\zz187\Arduino\terminal\pics"

def font(sz):
    for p in [r"C:\Windows\Fonts\msyhbd.ttc", r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\consola.ttf"]:
        try: return ImageFont.truetype(p, sz)
        except: pass
    return ImageFont.load_default()

def wave(path, label, color):
    img = Image.new("RGB", (W, H), (8, 10, 18))
    d = ImageDraw.Draw(img)
    # 正弦波
    pts = []
    for x in range(W):
        y = H//2 + int(52 * math.sin(x / 240 * 2 * math.pi * 3.2) * math.exp(-((x-120)/160)**2))
        pts.append((x, y))
    d.line(pts, fill=color, width=3)
    # 网格线
    for gx in range(0, W, 40): d.line([(gx,0),(gx,H)], fill=(28,32,44))
    d.line([(0,H//2),(W,H//2)], fill=(45,50,66))
    d.text((14, 14), label, font=font(22), fill=color)
    img.save(path, quality=92)

wave(os.path.join(OUT, "p1_listen.jpg"),      "LISTEN", (90, 220, 160))
wave(os.path.join(OUT, "p2_think.jpg"),       "THINK",  (110, 170, 255))
wave(os.path.join(OUT, "p3_speak.jpg"),   "SPEAK",  (255, 180, 90))

for f in ["p1_listen.jpg","p2_think.jpg","p3_speak.jpg"]:
    p = os.path.join(OUT, f)
    print(f"{os.path.getsize(p):8,d}  {f}")
