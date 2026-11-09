"""Generate the MOD Dwarf panel skin (SVG, units = mm) for mod-hw-emulator.

Geometry from the MOD Dwarf exterior model by MOD Audio (CC BY 4.0),
github.com/mod-audio/hw-mod-dwarf/enclosure/dwarf-exterior.step.
Coordinates: x right, z towards the player; origin at the panel centre.
"""
import json, sys

OUT_SVG, OUT_JSON = sys.argv[1], sys.argv[2]
X0, Z0, W, H = -101, -51, 202, 102
PXMM = 5                                   # base raster: 1010 x 510

KNOBS_X = [16.7, 48.7, 80.7]; KNOB_Z = -25.3
BTN_Z = -5.3; BTN_W, BTN_H = 14.8, 8.8
SHIFT = (-87.5, -16.8, 13.2, 13.2)
FS_X = [-80.7, 0.0, 80.7]; FS_Z = 39.3; FSLED_Z = 17.0
JACKLED_X = [-80.7, -59.7, 59.7, 80.7]; JACKLED_Z = -44.5
WIN = (-71.9, -36.3, -0.9, 2.7)            # display window in the top panel
# 128x64 pixel area: 2:1, centred in the window, 3 mm bezel at the sides
LCD_W = 64.0; LCD_H = 32.0
LCD_CX = (WIN[0] + WIN[2]) / 2; LCD_CZ = (WIN[1] + WIN[3]) / 2

ink = '#b9bcc2'          # silkscreen
s = []
a = s.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{X0} {Z0} {W} {H}" width="{W*PXMM}" height="{H*PXMM}">')
a('''<defs>
 <linearGradient id="body" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#2a2b2f"/><stop offset="1" stop-color="#1c1d20"/></linearGradient>
 <linearGradient id="ramp" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#1e1f22"/><stop offset="1" stop-color="#151618"/></linearGradient>
 <radialGradient id="knob" cx="0.4" cy="0.35" r="0.75">
  <stop offset="0" stop-color="#4a4c52"/><stop offset="0.6" stop-color="#202124"/><stop offset="1" stop-color="#0c0c0d"/></radialGradient>
 <radialGradient id="chrome" cx="0.38" cy="0.32" r="0.8">
  <stop offset="0" stop-color="#f4f5f7"/><stop offset="0.45" stop-color="#a9adb4"/><stop offset="1" stop-color="#4b4e54"/></radialGradient>
 <linearGradient id="nut" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="#d8dade"/><stop offset="1" stop-color="#6c7077"/></linearGradient>
 <linearGradient id="btn" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#2c2d31"/><stop offset="1" stop-color="#121214"/></linearGradient>
 <filter id="sh" x="-30%" y="-30%" width="160%" height="160%"><feDropShadow dx="0.4" dy="0.8" stdDeviation="0.7" flood-color="#000" flood-opacity="0.7"/></filter>
</defs>''')
# enclosure: outer bezel, top panel, front ramp (fold at z=19.9 and 31.1)
a(f'<rect x="-101" y="-51" width="202" height="102" rx="4.5" fill="#101012"/>')
a(f'<rect x="-100" y="-50" width="200" height="100" rx="4" fill="url(#body)" stroke="#3a3b40" stroke-width="0.3"/>')
a(f'<rect x="-99.6" y="19.9" width="199.2" height="29.7" rx="0" fill="url(#ramp)"/>')
a(f'<line x1="-99.6" y1="19.9" x2="99.6" y2="19.9" stroke="#3b3c41" stroke-width="0.35"/>')
a(f'<line x1="-99.6" y1="31.1" x2="99.6" y2="31.1" stroke="#0b0b0c" stroke-width="0.3"/>')
a(f'<rect x="-100" y="49.2" width="200" height="0.8" fill="#0d0d0e"/>')
# display: recess, then the glass (the LcdWidget paints the pixels on the LCD rect)
wx0, wz0, wx1, wz1 = WIN
a(f'<rect x="{wx0-0.6}" y="{wz0-0.6}" width="{wx1-wx0+1.2}" height="{wz1-wz0+1.2}" rx="2.2" fill="#0a0a0b" stroke="#45464b" stroke-width="0.3"/>')
a(f'<rect x="{wx0+1}" y="{wz0+1}" width="{wx1-wx0-2}" height="{wz1-wz0-2}" rx="1.2" fill="#0f1a20"/>')
a(f'<rect x="{LCD_CX-LCD_W/2}" y="{LCD_CZ-LCD_H/2}" width="{LCD_W}" height="{LCD_H}" fill="#7fc6e6"/>')
# knobs: collar, knob with knurl ring and pointer
for x in KNOBS_X:
    a(f'<circle cx="{x}" cy="{KNOB_Z}" r="10.6" fill="#0b0b0c" stroke="#3d3e43" stroke-width="0.3"/>')
    a(f'<circle cx="{x}" cy="{KNOB_Z}" r="9.0" fill="url(#knob)" filter="url(#sh)"/>')
    a(f'<circle cx="{x}" cy="{KNOB_Z}" r="7.2" fill="none" stroke="#3a3b40" stroke-width="0.9" stroke-dasharray="0.35 0.35"/>')
    a(f'<circle cx="{x}" cy="{KNOB_Z}" r="5.9" fill="#1a1b1e" stroke="#2f3034" stroke-width="0.3"/>')
    a(f'<line x1="{x}" y1="{KNOB_Z-5.4}" x2="{x}" y2="{KNOB_Z-2.8}" stroke="#d6d8dc" stroke-width="0.6" stroke-linecap="round"/>')
# buttons under the knobs (Subpage I, II, III) and Shift
def button(cx, cz, w, h):
    a(f'<rect x="{cx-w/2-0.5}" y="{cz-h/2-0.5}" width="{w+1}" height="{h+1}" rx="1.6" fill="#08080a"/>')
    a(f'<rect x="{cx-w/2+0.3}" y="{cz-h/2+0.3}" width="{w-0.6}" height="{h-0.6}" rx="1.2" fill="url(#btn)" stroke="#3a3b40" stroke-width="0.25" filter="url(#sh)"/>')
for x in KNOBS_X:
    button(x, BTN_Z, BTN_W, BTN_H)
button(*SHIFT)
# LEDs (dark when off; the LedWidget/SwitchWidget paint the colour)
def led(cx, cz, r):
    a(f'<circle cx="{cx}" cy="{cz}" r="{r+0.35}" fill="#0a0a0b"/><circle cx="{cx}" cy="{cz}" r="{r}" fill="#2a1c1c" stroke="#47484d" stroke-width="0.2"/>')
for x in FS_X: led(x, FSLED_Z, 1.6)
for x in JACKLED_X: led(x, JACKLED_Z, 1.1)
# footswitches: hex nut and chrome cap
import math
for x in FS_X:
    pts = ' '.join(f'{x+8.2*math.cos(math.radians(30+60*k)):.2f},{FS_Z+8.2*math.sin(math.radians(30+60*k)):.2f}' for k in range(6))
    a(f'<polygon points="{pts}" fill="url(#nut)" stroke="#3a3c40" stroke-width="0.25" filter="url(#sh)"/>')
    a(f'<circle cx="{x}" cy="{FS_Z}" r="6.2" fill="#55585e"/>')
    a(f'<circle cx="{x}" cy="{FS_Z}" r="5.3" fill="url(#chrome)" stroke="#2f3135" stroke-width="0.25"/>')
# silkscreen
font = 'font-family="DejaVu Sans, Segoe UI, Arial, sans-serif"'
for x, n in zip(FS_X, 'ABC'):
    a(f'<circle cx="{x-3.6}" cy="{FSLED_Z+6.2}" r="1.9" fill="none" stroke="{ink}" stroke-width="0.3"/>')
    a(f'<text x="{x-3.6}" y="{FSLED_Z+6.95}" font-size="2.3" text-anchor="middle" fill="{ink}" {font}>{n}</text>')
    a(f'<line x1="{x-0.9}" y1="{FSLED_Z+4.6}" x2="{x-0.9}" y2="{FSLED_Z+7.8}" stroke="{ink}" stroke-width="0.25"/>')
# A: bank/pages glyph, B: up, C: down (as printed next to the footswitches)
xa = FS_X[0] + 0.6; zg = FSLED_Z + 6.2
a(f'<rect x="{xa}" y="{zg-1.0}" width="2" height="2" fill="{ink}"/>')
a(f'<rect x="{xa+2.6}" y="{zg-1.0}" width="2" height="2" fill="none" stroke="{ink}" stroke-width="0.25"/>')
a(f'<rect x="{xa+5.2}" y="{zg-1.0}" width="2" height="2" fill="none" stroke="{ink}" stroke-width="0.25"/>')
xb = FS_X[1] + 0.8
a(f'<polygon points="{xb},{zg+1.1} {xb+2.4},{zg+1.1} {xb+1.2},{zg-1.1}" fill="{ink}"/>')
xc = FS_X[2] + 0.8
a(f'<polygon points="{xc},{zg-1.1} {xc+2.4},{zg-1.1} {xc+1.2},{zg+1.1}" fill="{ink}"/>')
# divider lines between the footswitches, with the tuner and the snapshot glyphs
for x0, x1 in ((FS_X[0]+11, FS_X[1]-11), (FS_X[1]+11, FS_X[2]-11)):
    xm = (x0 + x1) / 2
    a(f'<line x1="{x0}" y1="{FS_Z}" x2="{xm-4}" y2="{FS_Z}" stroke="{ink}" stroke-width="0.25"/>')
    a(f'<line x1="{xm+4}" y1="{FS_Z}" x2="{x1}" y2="{FS_Z}" stroke="{ink}" stroke-width="0.25"/>')
xm = (FS_X[0] + FS_X[1]) / 2   # tuning fork
a(f'<path d="M{xm-0.9},{FS_Z-2.2} L{xm-0.9},{FS_Z+0.1} Q{xm-0.9},{FS_Z+0.9} {xm},{FS_Z+0.9} Q{xm+0.9},{FS_Z+0.9} {xm+0.9},{FS_Z+0.1} L{xm+0.9},{FS_Z-2.2} M{xm},{FS_Z+0.9} L{xm},{FS_Z+2.4}" fill="none" stroke="{ink}" stroke-width="0.35" stroke-linecap="round" transform="rotate(30 {xm} {FS_Z})"/>')
xm = (FS_X[1] + FS_X[2]) / 2   # snapshot: dial = list
a(f'<circle cx="{xm-2.6}" cy="{FS_Z}" r="1.1" fill="none" stroke="{ink}" stroke-width="0.35"/>')
a(f'<text x="{xm}" y="{FS_Z+0.8}" font-size="2.2" text-anchor="middle" fill="{ink}" {font}>=</text>')
for k in range(3):
    a(f'<rect x="{xm+1.6}" y="{FS_Z-1.1+k*0.8}" width="2.4" height="0.45" fill="{ink}"/>')
# jack numbers on the back edge (inputs left, outputs right)
for x, t in zip(JACKLED_X, ['1', '2', '1', '2']):
    a(f'<text x="{x+ (-3.0 if t=="1" else 3.0)}" y="{JACKLED_Z+0.8}" font-size="2.2" text-anchor="middle" fill="{ink}" {font}>{t}</text>')
for xc, lab in ((-70.2, 'IN'), (70.2, 'OUT')):
    a(f'<text x="{xc}" y="{JACKLED_Z+0.75}" font-size="1.8" text-anchor="middle" fill="{ink}" letter-spacing="0.2" {font}>{lab}</text>')
a('</svg>')
open(OUT_SVG, 'w', encoding='utf-8').write('\n'.join(s))

def rect(cx, cz, w, h, k=PXMM):
    return [round((cx - w/2 - X0)*k), round((cz - h/2 - Z0)*k), round(w*k), round(h*k)]
def rects(k):
    r = {'display': rect(LCD_CX, LCD_CZ, LCD_W, LCD_H, k),
         'display_window': rect(LCD_CX, LCD_CZ, wx1-wx0, wz1-wz0, k)}
    for i, x in enumerate(KNOBS_X):
        r[f'Knob {i+1}'] = rect(x, KNOB_Z, 21.2, 21.2, k)
    for n, x in zip(['I', 'II', 'III'], KNOBS_X):
        r[f'Subpage {n}'] = rect(x, BTN_Z, BTN_W, BTN_H, k)
    r['Shift'] = rect(*SHIFT, k=k)
    for n, x in zip('ABC', FS_X):
        r[f'Footswitch {n}'] = rect(x, FS_Z, 16.4, 16.4, k)
        r[f'LED Footswitch {n}'] = rect(x, FSLED_Z, 3.2, 3.2, k)
    for n, x in zip(['In 1', 'In 2', 'Out 1', 'Out 2'], JACKLED_X):
        r[f'LED {n}'] = rect(x, JACKLED_Z, 2.2, 2.2, k)
    return r
json.dump({'source': 'MOD Dwarf exterior model by MOD Audio, CC BY 4.0',
           'skins': {f'dwarf-panel@{s}x.png': {'size': [W*PXMM*s, H*PXMM*s], 'px_per_mm': PXMM*s,
                                                 'rects_xywh': rects(PXMM*s)} for s in (1, 2)}},
          open(OUT_JSON, 'w'), indent=1)
print('ok')
