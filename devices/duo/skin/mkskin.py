"""Generate the MOD Duo panel skin (SVG, units = mm) for mod-hw-emulator.

Sources, used as measurements only:
- the dimensioned drawing of the case on the MOD wiki (MOD Duo Technical Specs): 180 x 160 mm top, knobs 78.6 mm
  from the back edge, knobs and footswitches 51.9 mm from the side and 76.2 mm apart, handle 24.1 mm below the
  knobs, the display windows;
- the controller board (mod-audio/mod-hw-duo, hw-design/top-board, rev. 2019-04-15): the leds sit 25.0 and
  45.05 mm below the knob shafts, the jack leds 150 mm apart.
The drawing is our own: no picture of the device is copied, and it carries no MOD logo.

Canvas coordinates: x right, z down, mm, origin at the top left of the canvas (side jacks included);
the panel starts at x = PX0.
"""
import json, sys

OUT_SVG, OUT_JSON = sys.argv[1], sys.argv[2]
W, H = 188, 160                      # canvas, mm
PX0, PW, PH = 4, 180, 160            # panel
PXMM = 5                             # base raster: 940 x 800


def P(x, z):                         # case coordinates (from the back left corner) -> canvas
    return PX0 + x, z


SLOT_X = [51.9, 128.1]               # knobs, footswitches, displays, footswitch leds
WIN_Z, WIN_W, WIN_H = 37.95, 64.5, 36.7
LCD_W, LCD_H = 60.0, 30.0            # the 128x64 pixels, 2:1
KNOB_Z = 78.6
LED1_Z, LED2_Z = KNOB_Z + 25.0, KNOB_Z + 45.05
FS_Z = 144.0
JACK_LED_X = [15.0, 165.0]           # outputs on the left, inputs on the right
HANDLE_Z, HANDLE_W, HANDLE_H = 103.6, 85.0, 11.0

ENC_R = 10.6         # collar radius: the EncoderWidget rectangle; its cap is 0.557 R (see controls.cpp)
FS_R = 8.2           # footswitch nut

ink = '#b9bcc2'
font = 'font-family="DejaVu Sans, Segoe UI, Arial, sans-serif"'
s = []
a = s.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W*PXMM}" height="{H*PXMM}">')
a('''<defs>
 <linearGradient id="body" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#2b2c31"/><stop offset="1" stop-color="#1e1f23"/></linearGradient>
 <radialGradient id="knob" cx="0.4" cy="0.35" r="0.75">
  <stop offset="0" stop-color="#e2e4e8"/><stop offset="0.5" stop-color="#8d9098"/><stop offset="1" stop-color="#3a3c42"/></radialGradient>
 <radialGradient id="chrome" cx="0.38" cy="0.32" r="0.8">
  <stop offset="0" stop-color="#f4f5f7"/><stop offset="0.45" stop-color="#a9adb4"/><stop offset="1" stop-color="#4b4e54"/></radialGradient>
 <linearGradient id="nut" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="#d8dade"/><stop offset="1" stop-color="#6c7077"/></linearGradient>
 <linearGradient id="handle" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#e6e8eb"/><stop offset="0.45" stop-color="#a3a7ae"/><stop offset="1" stop-color="#55585f"/></linearGradient>
 <linearGradient id="jack" x1="0" y1="0" x2="1" y2="0">
  <stop offset="0" stop-color="#3c3e44"/><stop offset="1" stop-color="#17181b"/></linearGradient>
 <filter id="sh" x="-30%" y="-30%" width="160%" height="160%"><feDropShadow dx="0.4" dy="0.8" stdDeviation="0.7" flood-color="#000" flood-opacity="0.7"/></filter>
</defs>''')

# side jacks: headphone, output 1, output 2 on the left; input 1, input 2 on the right
for z in (70.2, 99.4, 119.4):
    a(f'<rect x="0.6" y="{z-5}" width="5" height="10" rx="1.2" fill="url(#jack)" stroke="#0c0c0d" stroke-width="0.3"/>')
for z in (99.4, 119.4):
    a(f'<rect x="{W-5.6}" y="{z-5}" width="5" height="10" rx="1.2" fill="url(#jack)" stroke="#0c0c0d" stroke-width="0.3" transform="rotate(180 {W-3.1} {z})"/>')

# case and panel (the top has a 5 mm bevel all around)
a(f'<rect x="{PX0}" y="0" width="{PW}" height="{PH}" rx="5" fill="#111214"/>')
a(f'<rect x="{PX0+0.8}" y="0.8" width="{PW-1.6}" height="{PH-1.6}" rx="4.4" fill="#26272b" stroke="#3a3b40" stroke-width="0.3"/>')
a(f'<rect x="{PX0+5.3}" y="5.3" width="{PW-10.6}" height="{PH-10.6}" rx="1.5" fill="url(#body)" stroke="#34353a" stroke-width="0.25"/>')

# displays: bezel and window (the LcdWidget fills the whole window, "display_window")
for x in SLOT_X:
    cx, cz = P(x, WIN_Z)
    a(f'<rect x="{cx-WIN_W/2-1.4}" y="{cz-WIN_H/2-1.4}" width="{WIN_W+2.8}" height="{WIN_H+2.8}" rx="2.2" fill="#16171a" stroke="#6e7076" stroke-width="0.3"/>')
    a(f'<rect x="{cx-WIN_W/2}" y="{cz-WIN_H/2}" width="{WIN_W}" height="{WIN_H}" rx="1.4" fill="#a6e1fa"/>')

# knobs (endless, push): chrome knob, the EncoderWidget draws the cap and its pointer over it
for n, x in enumerate(SLOT_X, 1):
    cx, cz = P(x, KNOB_Z)
    R = ENC_R
    a(f'<circle cx="{cx}" cy="{cz}" r="{R}" fill="#18191c" stroke="#5c5e64" stroke-width="0.3"/>')
    a(f'<circle cx="{cx}" cy="{cz}" r="{R*0.87:.2f}" fill="url(#knob)" filter="url(#sh)"/>')
    a(f'<circle cx="{cx}" cy="{cz}" r="{R*0.557:.2f}" fill="#46484e" stroke="#6c6f76" stroke-width="0.3"/>')
    a(f'<line x1="{cx}" y1="{cz-R*0.509:.2f}" x2="{cx}" y2="{cz-R*0.264:.2f}" stroke="#d6d8dc" stroke-width="{R*0.057:.2f}" stroke-linecap="round"/>')
    a(f'<text x="{cx}" y="{cz-R-1.6:.2f}" font-size="3.0" text-anchor="middle" fill="{ink}" {font}>{n}</text>')

# handle
hx, hz = P(90, HANDLE_Z)
a(f'<rect x="{hx-HANDLE_W/2}" y="{hz-HANDLE_H/2}" width="{HANDLE_W}" height="{HANDLE_H}" rx="{HANDLE_H/2}" fill="#0e0f11"/>')
a(f'<rect x="{hx-HANDLE_W/2+1}" y="{hz-HANDLE_H/2+1.6}" width="{HANDLE_W-2}" height="{HANDLE_H-3.2}" rx="{HANDLE_H/2-1.6}" fill="url(#handle)" filter="url(#sh)"/>')
for dx in (-HANDLE_W/2 + 5.5, HANDLE_W/2 - 5.5):
    a(f'<circle cx="{hx+dx}" cy="{hz}" r="2.6" fill="none" stroke="#5d6067" stroke-width="0.3"/>')


def led(cx, cz, r):
    a(f'<circle cx="{cx}" cy="{cz}" r="{r+0.35}" fill="#0a0a0b"/><circle cx="{cx}" cy="{cz}" r="{r}" fill="#2a1c1c" stroke="#47484d" stroke-width="0.2"/>')


# footswitch leds and footswitches: hex nut and chrome cap
for x in SLOT_X:
    led(*P(x, LED2_Z), 1.6)
    cx, cz = P(x, FS_Z)
    pts = ' '.join(f'{cx+FS_R*c:.2f},{cz+FS_R*d:.2f}' for c, d in
                   ((0.866, 0.5), (0, 1), (-0.866, 0.5), (-0.866, -0.5), (0, -1), (0.866, -0.5)))
    a(f'<polygon points="{pts}" fill="url(#nut)" stroke="#3a3c40" stroke-width="0.25" filter="url(#sh)"/>')
    a(f'<circle cx="{cx}" cy="{cz}" r="6.2" fill="#55585e"/>')
    a(f'<circle cx="{cx}" cy="{cz}" r="5.3" fill="url(#chrome)" stroke="#2f3135" stroke-width="0.25"/>')

# jack leds and their silkscreen: "1 <" / "2 <" pointing at the side jacks
for x, label in zip(JACK_LED_X, ('OUTPUTS', 'INPUTS')):
    cx, _ = P(x, 0)
    left = label == 'OUTPUTS'
    for n, z in ((1, LED1_Z), (2, LED2_Z)):
        led(cx, z, 1.4)
        tx = cx - 3.4 if left else cx + 3.4
        a(f'<text x="{tx:.2f}" y="{z-2.0:.2f}" font-size="2.6" text-anchor="middle" fill="{ink}" {font}>{n}</text>')
        ax = cx - 4.2 if left else cx + 4.2
        d = -1 if left else 1
        a(f'<polygon points="{ax+d*1.0:.2f},{z:.2f} {ax:.2f},{z-0.8:.2f} {ax:.2f},{z+0.8:.2f}" fill="{ink}"/>')
    a(f'<text x="{cx + (4 if left else -4):.2f}" y="{LED1_Z-8:.2f}" font-size="2.5" text-anchor="middle" fill="{ink}" {font}>{label}</text>')

# headphone glyph next to the headphone jack
hx, hz = P(9.5, 69.6)
a(f'<polygon points="{hx-2.6:.2f},{hz+0.6:.2f} {hx-1.6:.2f},{hz-0.2:.2f} {hx-1.6:.2f},{hz+1.4:.2f}" fill="{ink}"/>')
a(f'<path d="M{hx},{hz+1.6} L{hx},{hz} A1.8,1.8 0 0 1 {hx+3.6},{hz} L{hx+3.6},{hz+1.6}" fill="none" stroke="{ink}" stroke-width="0.45"/>')
a(f'<rect x="{hx-0.4}" y="{hz+0.5}" width="1.0" height="1.8" rx="0.3" fill="{ink}"/><rect x="{hx+3.0}" y="{hz+0.5}" width="1.0" height="1.8" rx="0.3" fill="{ink}"/>')
a('</svg>')
open(OUT_SVG, 'w', encoding='utf-8').write('\n'.join(s))


def rect(cx, cz, w, h, k):
    return [round((cx - w/2)*k), round((cz - h/2)*k), round(w*k), round(h*k)]


def rects(k):
    r = {}
    for i, x in enumerate(SLOT_X):
        cx, cz = P(x, WIN_Z)
        name = 'display' if i == 0 else f'display {i+1}'
        r[name + '_window'] = rect(cx, cz, WIN_W, WIN_H, k)
        r[name] = rect(cx, cz, LCD_W, LCD_H, k)
    for i, x in enumerate(SLOT_X, 1):
        r[f'Knob {i}'] = rect(*P(x, KNOB_Z), 2*ENC_R, 2*ENC_R, k)
        r[f'Footswitch {i}'] = rect(*P(x, FS_Z), 2*FS_R, 2*FS_R, k)
        r[f'LED Footswitch {i}'] = rect(*P(x, LED2_Z), 3.2, 3.2, k)
    for side, x in (('Out', JACK_LED_X[0]), ('In', JACK_LED_X[1])):
        for n, z in ((1, LED1_Z), (2, LED2_Z)):
            r[f'LED {side} {n}'] = rect(*P(x, z), 2.8, 2.8, k)
    return r


json.dump({'source': 'MOD Duo case drawing (MOD wiki) and controller board (mod-audio/mod-hw-duo), own drawing',
           'skins': {f'duo-panel@{m}x.png': {'size': [W*PXMM*m, H*PXMM*m], 'px_per_mm': PXMM*m,
                                              'rects_xywh': rects(PXMM*m)} for m in (1, 2)}},
          open(OUT_JSON, 'w'), indent=1)
print('ok')
