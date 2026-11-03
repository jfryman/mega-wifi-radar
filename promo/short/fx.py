# Zany 90s overlay for the Short: wipes over every cut, bouncing pop-up stickers, the end card's fine print.
# Run in $W (short.sh fx): writes fx/%05d.png (1080x1920 RGBA, 30 fps) and sfx.txt (time|effect|gain) for mix.sh.
# Times are output seconds of base.mp4: keep TRANS on base.sh's cuts and POPS on the cues.txt lines.
import math, os, textwrap
from multiprocessing import Pool
from PIL import Image, ImageDraw, ImageFont

W, H, FPS, DUR = 1080, 1920, 30, 49.23
FINE_AT = 38.2                                      # infomercial fine print
HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, '../../print/fonts/')
BLK = R + 'BarlowCondensed-BlackItalic.ttf'
PX = R + 'PressStart2P-Regular.ttf'
TEAL, MAG, YEL, PUR, RED, CYN, INK = '#00c9c8', '#ff2a9d', '#ffd800', '#7a2cff', '#ff2a2a', '#28e0ff', '#111111'
OUT = 'fx'

def star_pts(cx, cy, r_out, r_in, n, rot=0.0):
    pts = []
    for i in range(2 * n):
        r = r_out if i % 2 == 0 else r_in
        a = rot + math.pi * i / n - math.pi / 2
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

# ---------- transitions: c in 0..1 = coverage (1 at the cut) ----------
def tr_star(d, c, t):
    r = c * 2300
    d.polygon(star_pts(W / 2, H / 2, r, r * 0.55, 5, t * 3), fill=MAG, outline=YEL, width=18)
    d.polygon(star_pts(W / 2, H / 2, r * 0.6, r * 0.33, 5, -t * 4), fill=YEL, outline=INK, width=10)

def tr_checker(d, c, t):
    s = 120
    for gy in range(H // s + 1):
        for gx in range(W // s + 1):
            k = (gx + gy) / (W // s + H // s)          # diagonal sweep
            p = max(0.0, min(1.0, c * 2 - k))
            if p <= 0: continue
            h = s * p / 2
            cx, cy = gx * s + s / 2, gy * s + s / 2
            d.rectangle([cx - h, cy - h, cx + h, cy + h], fill=TEAL if (gx + gy) % 2 else PUR)

def tr_iris(d, c, t):
    cols = [YEL, MAG, TEAL, PUR, RED]
    for i in range(5):
        r = max(0.0, c * 1.25 - i * 0.12) * 1300
        if r > 0: d.ellipse([W / 2 - r, H / 2 - r, W / 2 + r, H / 2 + r], fill=cols[i], outline=INK, width=8)

def tr_stripes(d, c, t):
    cols = [MAG, YEL, TEAL, PUR]
    bw = 220
    for i in range(-6, 16):
        p = max(0.0, min(1.0, c * 1.6 - (i % 4) * 0.1))
        x0 = i * bw - 1200 + p * 1200
        d.polygon([(x0, 0), (x0 + bw * p, 0), (x0 + bw * p - 900, H), (x0 - 900, H)], fill=cols[i % 4])

def tr_zigzag(d, c, t):
    for k, col in enumerate([YEL, RED, INK]):
        x = -200 + (c * 1.15 - k * 0.06) * (W + 400)
        pts = [(-50, 0), (x, 0)]
        for j in range(17):
            pts.append((x + (60 if j % 2 else -60), j * H / 16))
        pts += [(-50, H)]
        d.polygon(pts, fill=col)

def tr_blast(d, c, t):
    n = 18
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n + t, 2 * math.pi * (i + 0.5) / n + t
        r = c * 2400
        d.polygon([(W / 2, H / 2), (W / 2 + r * math.cos(a0), H / 2 + r * math.sin(a0)),
                   (W / 2 + r * math.cos(a1), H / 2 + r * math.sin(a1))], fill=YEL if i % 2 else MAG)
    rr = c * 700
    d.ellipse([W / 2 - rr, H / 2 - rr, W / 2 + rr, H / 2 + rr], fill='#ffffff')

TRANS = [(5.2, tr_star), (9.8, tr_checker), (15.2, tr_iris), (16.6, tr_stripes),
         (20.2, tr_zigzag), (23.2, tr_star), (29.3, tr_stripes), (31.9, tr_blast)]
TD = 0.5                                            # each covers cut ± 0.25 s

# ---------- stickers ----------
def font(path, size): return ImageFont.truetype(path, size)

def text_block(draw, cx, cy, txt, f, fill, stroke=INK, sw=8, anaglyph=False):
    if anaglyph:
        draw.text((cx - 10, cy), txt, font=f, fill='#ff2040', anchor='mm', stroke_width=sw, stroke_fill=INK)
        draw.text((cx + 10, cy), txt, font=f, fill=CYN, anchor='mm', stroke_width=sw, stroke_fill=INK)
    draw.text((cx, cy), txt, font=f, fill=fill, anchor='mm', stroke_width=sw, stroke_fill=stroke)

def burst(txt, r, bg, fg, size, anaglyph=False, sub=None):
    im = Image.new('RGBA', (int(r * 2.4), int(r * 2.4)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    c = im.width / 2
    d.polygon(star_pts(c + 10, c + 12, r, r * 0.74, 14), fill=INK)          # drop shadow
    d.polygon(star_pts(c, c, r, r * 0.74, 14), fill=bg, outline=INK, width=8)
    text_block(d, c, c - (size * 0.15 if sub else 0), txt, font(BLK, size), fg, anaglyph=anaglyph)
    if sub: text_block(d, c, c + size * 0.55, sub, font(BLK, int(size * 0.38)), fg, sw=5)
    return im

def sticker(txt, bg, fg, size, pad=34):
    f = font(BLK, size)
    bb = ImageDraw.Draw(Image.new('RGBA', (1, 1))).textbbox((0, 0), txt, font=f, stroke_width=8)
    w, h = bb[2] - bb[0] + pad * 2, bb[3] - bb[1] + pad * 2
    im = Image.new('RGBA', (w + 30, h + 30), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([12, 14, w + 12, h + 14], 24, fill=INK)
    d.rounded_rectangle([0, 0, w, h], 24, fill=bg, outline=INK, width=8)
    text_block(d, w / 2, h / 2, txt, f, fg)
    return im

def stamp(txt, sub, col):
    r = 190
    im = Image.new('RGBA', (2 * r + 20, 2 * r + 20), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c = r + 10
    d.ellipse([c - r, c - r, c + r, c + r], fill='#ffffff', outline=col, width=16)
    d.ellipse([c - r + 28, c - r + 28, c + r - 28, c + r - 28], outline=col, width=6)
    text_block(d, c, c - 30, txt, font(BLK, 120), col, stroke='#ffffff', sw=0)
    text_block(d, c, c + 70, sub, font(BLK, 60), col, stroke='#ffffff', sw=0)
    return im

# (start, end, image, centre x, centre y, base angle, sfx)
POPS = [
    (0.6, 4.9, burst('NEW!', 120, YEL, RED, 92), 905, 745, 12, 'pop'),
    (2.7, 4.9, burst('WI-FI!!', 210, MAG, YEL, 120), 320, 1215, -10, 'boing'),
    (6.0, 9.6, sticker('LIVE!', RED, '#ffffff', 96), 210, 770, -12, 'zap'),
    (7.2, 9.6, sticker('REAL PLANES!', YEL, RED, 92), 640, 1395, 6, 'pop'),
    (13.7, 15.1, burst('WHOA!', 170, CYN, YEL, 110), 230, 1250, -14, ''),
    (17.1, 20.1, burst('3-D!!', 190, YEL, '#ffffff', 150, anaglyph=True), 860, 790, 10, 'zap'),
    (20.5, 23.1, sticker('TOTALLY RADICAL!', MAG, YEL, 96), 540, 1400, -7, 'scratch'),
    (23.8, 29.1, burst('x9!', 140, TEAL, YEL, 120, sub='THEMES'), 900, 800, 14, 'ding'),
    (29.6, 31.8, sticker('WAY PAST COOL!', PUR, YEL, 92), 540, 1400, 5, 'pop'),
    (33.0, 49.1, stamp('100%', 'FREE!*', RED).resize((280, 280), Image.LANCZOS), 160, 455, -14, 'ding'),
]

FINE_F = ImageFont.truetype(PX, 13)
FINE = textwrap.wrap('*' + open(os.path.join(HERE, 'fine.txt')).read().strip().upper(), 72)

def pop_scale(u, life):
    # elastic pop in (0.3 s), hold, shrink out (0.15 s)
    if u < 0.3:
        x = u / 0.3
        return 1 + math.sin(x * math.pi * 2.5) * (1 - x) * 0.6 - (1 - x) ** 3
    if u > life - 0.15:
        return max(0.0, (life - u) / 0.15)
    return 1.0

def frame(i):
    t = i / FPS
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for s, e, img, x, y, ang, _ in POPS:
        if s <= t < e:
            u = t - s
            k = pop_scale(u, e - s)
            if k <= 0.01: continue
            a = ang + 4 * math.sin(u * 7)                        # wobble
            sc = k * (1 + 0.04 * math.sin(u * 11))               # pulse
            p = img.resize((max(1, int(img.width * sc)), max(1, int(img.height * sc))), Image.BICUBIC)
            p = p.rotate(a, resample=Image.BICUBIC, expand=True)
            im.alpha_composite(p, (int(x - p.width / 2), int(y - p.height / 2)))
    if t >= FINE_AT:
        a = min(1.0, (t - FINE_AT) / 0.3)
        fp = Image.new('RGBA', (W, H), (0, 0, 0, 0)); fd = ImageDraw.Draw(fp)
        fd.rectangle([40, 1395, W - 40, 1395 + 22 + 20 * len(FINE)], fill=(11, 11, 12, 255))
        for k, ln in enumerate(FINE):
            fd.text((W / 2, 1408 + 20 * k), ln, font=FINE_F, fill='#9a9aa0', anchor='mt')
        fp.putalpha(fp.getchannel('A').point(lambda v: int(v * a)))
        im.alpha_composite(fp)
    d = ImageDraw.Draw(im)
    for cut, fn in TRANS:
        dt = abs(t - cut)
        if dt < TD / 2:
            fn(d, 1 - dt / (TD / 2), t)
    im.save(f'{OUT}/{i:05d}.png', compress_level=1)

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    n = math.ceil(DUR * FPS)
    with Pool() as pool: pool.map(frame, range(n), chunksize=8)
    sfx = [(c - 0.22, ['whoosh', 'whoosh2', 'zap'][k % 3], 0.8) for k, (c, _) in enumerate(TRANS[:-1])]
    sfx.append((TRANS[-1][0] - 0.1, 'hit', 1.0))
    sfx += [(s, fx, 0.7) for s, _, _, _, _, _, fx in POPS if fx]
    open('sfx.txt', 'w').write(''.join(f'{t:.2f}|{n}|{g}\n' for t, n, g in sorted(sfx)))
    print(n, 'frames')
