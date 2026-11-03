# YouTube Short thumbnail, 1080x1920: grid, huge hook text, tilted radar shot, cockpit inset, bursts.
# Run in $W after base (ov/grid.png) and record (rec/): writes thumbnail.png + .jpg.
import os, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fx import burst, sticker, text_block, font, BLK, YEL, MAG, RED, CYN, INK

W, H = 1080, 1920
os.makedirs('thumb', exist_ok=True)
# radar at 39.2 s: sweep up, LOOK UP! box open, 30 aircraft; cockpit at 30 s
for src, t, out in (('rec/radar.avi', 39.2, 'thumb/radar.png'), ('rec/cockpit.avi', 30, 'thumb/cock.png')):
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(t), '-i', src, '-frames:v', '1', out], check=True)
im = Image.open('ov/grid.png').convert('RGBA')
d = ImageDraw.Draw(im)

# red/black header band, like the box art
d.rectangle([0, 70, W, 76], fill='#d7141a'); d.rectangle([0, 77, W, 167], fill='#0b0b0c'); d.rectangle([0, 168, W, 174], fill='#d7141a')
text_block(d, W / 2, 122, 'MEGA WIFI RADAR', font(BLK, 78), '#ffffff', sw=0)

# hook
text_block(d, W / 2, 300, 'MY GENESIS', font(BLK, 190), '#ffffff', sw=10)
text_block(d, W / 2, 490, 'HAS WI-FI?!', font(BLK, 205), YEL, sw=12)

def framed(src, scale, border=10):
    g = Image.open(src).convert('RGBA')
    g = g.resize((g.width * scale, g.height * scale), Image.NEAREST)
    f = Image.new('RGBA', (g.width + 4 * border, g.height + 4 * border), '#c9ccd1')
    ImageDraw.Draw(f).rectangle([border, border, f.width - border - 1, f.height - border - 1], fill='#d7141a')
    f.alpha_composite(g, (2 * border, 2 * border))
    return f

def place(card, cx, cy, ang, shadow=24):
    r = card.rotate(ang, resample=Image.BICUBIC, expand=True)
    sh = Image.new('RGBA', r.size, (0, 0, 0, 0)); sh.putalpha(r.getchannel('A').point(lambda v: int(v * 0.85)))
    sh = sh.filter(ImageFilter.GaussianBlur(10))
    im.alpha_composite(sh, (int(cx - r.width / 2 + shadow), int(cy - r.height / 2 + shadow)))
    im.alpha_composite(r, (int(cx - r.width / 2), int(cy - r.height / 2)))

place(framed('thumb/radar.png', 3), 540, 1010, -3)
place(framed('thumb/cock.png', 1, 6).resize((520, 380), Image.NEAREST), 700, 1500, 6)

def put(img, cx, cy, ang):
    r = img.rotate(ang, resample=Image.BICUBIC, expand=True)
    im.alpha_composite(r, (int(cx - r.width / 2), int(cy - r.height / 2)))

put(burst('LIVE!', 150, MAG, YEL, 120, sub='REAL PLANES'), 900, 690, 12)
put(burst('3-D!!', 140, YEL, RED, 120, anaglyph=True), 250, 1480, -12)
put(sticker('1989 HARDWARE!', CYN, '#ffffff', 74), 300, 1730, -5)

im.convert('RGB').save('thumbnail.png')
im.convert('RGB').save('thumbnail.jpg', quality=92)
print('ok')
