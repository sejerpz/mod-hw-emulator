"""Generate the MOD Duo X panel skin (SVG, units = mm) for mod-hw-emulator.

Positions of the controls: the pick and place file of the Duo X controller board (mod-audio/mod-hw-duoX,
hw-design/top-board/Assembly, rev. 2022-02-28), centre of each part in board millimetres, Y up.
Outline of the case: about 180 x 165 mm (the side jacks add 4 mm each side; MOD wiki, Duo X Technical Specs).
The drawing is our own: no picture of the device is copied, and it carries no MOD logo.

Canvas coordinates: x right, z down, origin at the top left of the canvas (side jacks included).
A board point (X, Y) is at x = X + BX, z = BZ - Y.
"""
import json, math, sys

OUT_SVG, OUT_JSON = sys.argv[1], sys.argv[2]
W, H = 188, 165                      # canvas, mm
PX0, PZ0, PW, PH = 4, 0, 180, 165    # panel
PXMM = 5                             # base raster: 940 x 825
BX, BZ = 17.85, 146.15               # board -> canvas


def bp(X, Y):
    return X + BX, BZ - Y


# board positions (pick and place), see the module docstring
DISPLAYS = [(37.72, 112.3), (113.72, 112.3)]            # GLCD1, GLCD2 (window centre)
WIN_W, WIN_H = 66.0, 37.0
ENCODERS = [(37.97, 76.03), (113.97, 76.03)]             # E1B (left), E1A (right)
TOP_BUTTONS = [('Snapshot 1', 60.72), ('Page', 75.72), ('Snapshot 2', 90.72)]   # SW2A-C
TOP_Y = 76.03
POTS_X = [18.72, 56.72, 94.72, 132.72]
POT_ROWS = [45.53 + 5.3, 16.53 + 5.3]                    # VR1A-D, VR1E-H: shaft is 5.3 mm above the part centre
BUTTON_Y = 0.0                                           # SW2D-G
LEDS = {'Output 1': (-4.83, 46.94), 'Output 2': (-4.83, 26.74),   # LED4, LED3
        'Input 1': (156.27, 46.94), 'Input 2': (156.27, 26.74)}   # LED5, LED6

ENC_R = 9.5          # collar radius: the EncoderWidget rectangle; its cap is 0.557 R (see controls.cpp)
POT_R = 9.5          # collar radius of a knob
BTN = 13.0           # bottom buttons
TBTN = 10.0          # snapshot / page buttons

ink = '#b9bcc2'
font = 'font-family="DejaVu Sans, Segoe UI, Arial, sans-serif"'
s = []
a = s.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W*PXMM}" height="{H*PXMM}">')
a('''<defs>
 <linearGradient id="body" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#2b2c31"/><stop offset="1" stop-color="#1e1f23"/></linearGradient>
 <radialGradient id="knob" cx="0.4" cy="0.35" r="0.75">
  <stop offset="0" stop-color="#9a9ea6"/><stop offset="0.55" stop-color="#55585f"/><stop offset="1" stop-color="#2a2b30"/></radialGradient>
 <radialGradient id="pot" cx="0.4" cy="0.35" r="0.8">
  <stop offset="0" stop-color="#7d8188"/><stop offset="0.6" stop-color="#43454b"/><stop offset="1" stop-color="#232428"/></radialGradient>
 <linearGradient id="btn" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#6a6d75"/><stop offset="1" stop-color="#383a40"/></linearGradient>
 <linearGradient id="jack" x1="0" y1="0" x2="1" y2="0">
  <stop offset="0" stop-color="#3c3e44"/><stop offset="1" stop-color="#17181b"/></linearGradient>
 <filter id="sh" x="-30%" y="-30%" width="160%" height="160%"><feDropShadow dx="0.4" dy="0.8" stdDeviation="0.7" flood-color="#000" flood-opacity="0.7"/></filter>
</defs>''')

# side jacks (headphone/CV, output 1, output 2 on the left; EXP/CV, input 1, input 2 on the right)
for z in (69.2, 100.5, 119.0):
    a(f'<rect x="0.6" y="{z-5}" width="5" height="10" rx="1.2" fill="url(#jack)" stroke="#0c0c0d" stroke-width="0.3"/>')
    a(f'<rect x="{W-5.6}" y="{z-5}" width="5" height="10" rx="1.2" fill="url(#jack)" stroke="#0c0c0d" stroke-width="0.3" transform="rotate(180 {W-3.1} {z})"/>')

# case and panel
a(f'<rect x="{PX0}" y="{PZ0}" width="{PW}" height="{PH}" rx="6" fill="#111214"/>')
a(f'<rect x="{PX0+0.8}" y="{PZ0+0.8}" width="{PW-1.6}" height="{PH-1.6}" rx="5.4" fill="url(#body)" stroke="#3a3b40" stroke-width="0.3"/>')

# displays: bezel and window (the LcdWidget fills the whole window, "display_window")
for X, Y in DISPLAYS:
    cx, cz = bp(X, Y)
    a(f'<rect x="{cx-WIN_W/2-1.6}" y="{cz-WIN_H/2-1.6}" width="{WIN_W+3.2}" height="{WIN_H+3.2}" rx="1.8" fill="#16171a" stroke="#6e7076" stroke-width="0.3"/>')
    a(f'<rect x="{cx-WIN_W/2}" y="{cz-WIN_H/2}" width="{WIN_W}" height="{WIN_H}" rx="1.0" fill="#a6e1fa"/>')

# endless knobs: same look as the Dwarf knobs, the EncoderWidget draws the cap and its pointer over them
for X, Y in ENCODERS:
    x, z = bp(X, Y)
    R = ENC_R
    a(f'<circle cx="{x}" cy="{z}" r="{R}" fill="#18191c" stroke="#5c5e64" stroke-width="0.3"/>')
    a(f'<circle cx="{x}" cy="{z}" r="{R*0.85:.2f}" fill="url(#knob)" filter="url(#sh)"/>')
    a(f'<circle cx="{x}" cy="{z}" r="{R*0.68:.2f}" fill="none" stroke="#7a7d84" stroke-width="0.8" stroke-dasharray="0.35 0.35"/>')
    a(f'<circle cx="{x}" cy="{z}" r="{R*0.557:.2f}" fill="#46484e" stroke="#6c6f76" stroke-width="0.3"/>')
    a(f'<line x1="{x}" y1="{z-R*0.509:.2f}" x2="{x}" y2="{z-R*0.264:.2f}" stroke="#d6d8dc" stroke-width="{R*0.057:.2f}" stroke-linecap="round"/>')


def button(cx, cz, w, h):
    a(f'<rect x="{cx-w/2-0.6}" y="{cz-h/2-0.6}" width="{w+1.2}" height="{h+1.2}" rx="1.9" fill="#141517"/>')
    a(f'<rect x="{cx-w/2+0.3}" y="{cz-h/2+0.3}" width="{w-0.6}" height="{h-0.6}" rx="1.4" fill="url(#btn)" stroke="#7d8087" stroke-width="0.25" filter="url(#sh)"/>')


def led(cx, cz, r):
    a(f'<circle cx="{cx}" cy="{cz}" r="{r+0.35}" fill="#0a0a0b"/><circle cx="{cx}" cy="{cz}" r="{r}" fill="#2a1c1c" stroke="#47484d" stroke-width="0.2"/>')


# snapshot / page buttons and the bottom buttons, each with its led above it
for name, X in TOP_BUTTONS:
    x, z = bp(X, TOP_Y)
    button(x, z, TBTN, TBTN)
    led(x, z - TBTN/2 - 2.4, 0.9)
for X in POTS_X:
    x, z = bp(X, BUTTON_Y)
    button(x, z, BTN, BTN)
    led(x, z - BTN/2 - 2.4, 0.9)

# knobs (potentiometers): collar, knob, white dot at the start of the travel (7 o'clock)
for Y in POT_ROWS:
    for X in POTS_X:
        x, z = bp(X, Y)
        R = POT_R
        a(f'<circle cx="{x}" cy="{z}" r="{R}" fill="#18191c" stroke="#5c5e64" stroke-width="0.3"/>')
        a(f'<circle cx="{x}" cy="{z}" r="{R*0.84:.2f}" fill="none" stroke="#6a6d74" stroke-width="0.5"/>')
        a(f'<circle cx="{x}" cy="{z}" r="{R*0.66:.2f}" fill="url(#pot)" filter="url(#sh)"/>')
        t = math.radians(-135)
        a(f'<circle cx="{x+math.sin(t)*R*0.76:.2f}" cy="{z-math.cos(t)*R*0.76:.2f}" r="0.55" fill="#e8eaee"/>')

# jack leds and their silkscreen
for name, (X, Y) in LEDS.items():
    x, z = bp(X, Y)
    led(x, z, 1.4)
    left = name.startswith('Output')
    a(f'<text x="{x + (-3.2 if left else 3.2):.2f}" y="{z+1.0:.2f}" font-size="3.2" font-weight="bold" text-anchor="middle" fill="{ink}" {font}>{name[-1]}</text>')
ox, oz1 = bp(-4.83, 46.94); _, oz2 = bp(-4.83, 26.74)
a(f'<text x="{ox+0.2:.2f}" y="{(oz1+oz2)/2:.2f}" font-size="2.6" text-anchor="middle" fill="{ink}" {font} transform="rotate(90 {ox+0.2:.2f} {(oz1+oz2)/2:.2f})" dy="0.9">output</text>')
ix, _ = bp(156.27, 0)
a(f'<text x="{ix-0.2:.2f}" y="{(oz1+oz2)/2:.2f}" font-size="2.6" text-anchor="middle" fill="{ink}" {font} transform="rotate(-90 {ix-0.2:.2f} {(oz1+oz2)/2:.2f})" dy="0.9">input</text>')


def chevrons(x, z):
    for k in range(3):
        xx = x - 1.6 + k * 1.1
        a(f'<polyline points="{xx+0.7:.2f},{z-0.7:.2f} {xx:.2f},{z:.2f} {xx+0.7:.2f},{z+0.7:.2f}" fill="none" stroke="{ink}" stroke-width="0.35"/>')


# left: headphones <<< CV ; right: EXP <<< CV
hx, hz = 12.2, 64.5
a(f'<path d="M{hx-1.6},{hz+1.2} L{hx-1.6},{hz} A1.6,1.6 0 0 1 {hx+1.6},{hz} L{hx+1.6},{hz+1.2}" fill="none" stroke="{ink}" stroke-width="0.45"/>')
a(f'<rect x="{hx-2.0}" y="{hz+0.4}" width="0.9" height="1.6" rx="0.3" fill="{ink}"/><rect x="{hx+1.1}" y="{hz+0.4}" width="0.9" height="1.6" rx="0.3" fill="{ink}"/>')
chevrons(hx, hz + 4.0)
a(f'<text x="{hx}" y="{hz+7.6}" font-size="2.6" font-weight="bold" text-anchor="middle" fill="{ink}" {font}>CV</text>')
ex = W - 12.2
a(f'<text x="{ex}" y="{hz+1.8}" font-size="2.6" font-weight="bold" text-anchor="middle" fill="{ink}" {font}>EXP</text>')
chevrons(ex, hz + 4.0)
a(f'<text x="{ex}" y="{hz+7.6}" font-size="2.6" font-weight="bold" text-anchor="middle" fill="{ink}" {font}>CV</text>')
a('</svg>')
open(OUT_SVG, 'w', encoding='utf-8').write('\n'.join(s))


def rect(cx, cz, w, h, k):
    return [round((cx - w/2)*k), round((cz - h/2)*k), round(w*k), round(h*k)]


def rects(k):
    r = {}
    for i, (X, Y) in enumerate(DISPLAYS):
        cx, cz = bp(X, Y)
        name = 'display' if i == 0 else f'display {i+1}'
        r[name + '_window'] = rect(cx, cz, WIN_W, WIN_H, k)
        r[name] = rect(cx, cz, 2 * 31.0, 31.0, k)          # the 128x64 pixels, 2:1
    for i, (X, Y) in enumerate(ENCODERS):
        r[f'Endless Knob {i+1}'] = rect(*bp(X, Y), 2*ENC_R, 2*ENC_R, k)
    for name, X in TOP_BUTTONS:
        x, z = bp(X, TOP_Y)
        r[name] = rect(x, z, TBTN, TBTN, k)
        r[f'LED {name}'] = rect(x, z - TBTN/2 - 2.4, 2.4, 2.4, k)
    n = 1
    for Y in POT_ROWS:
        for X in POTS_X:
            r[f'Knob {n}'] = rect(*bp(X, Y), 2*POT_R, 2*POT_R, k)
            n += 1
    for i, X in enumerate(POTS_X):
        x, z = bp(X, BUTTON_Y)
        r[f'Button {i+1}'] = rect(x, z, BTN, BTN, k)
        r[f'LED Button {i+1}'] = rect(x, z - BTN/2 - 2.4, 2.4, 2.4, k)
    for name, (X, Y) in LEDS.items():
        r[f'LED {name}'] = rect(*bp(X, Y), 3.4, 3.4, k)
    return r


json.dump({'source': 'MOD Duo X controller board pick and place (mod-audio/mod-hw-duoX), own drawing',
           'skins': {f'duox-panel@{m}x.png': {'size': [W*PXMM*m, H*PXMM*m], 'px_per_mm': PXMM*m,
                                               'rects_xywh': rects(PXMM*m)} for m in (1, 2)}},
          open(OUT_JSON, 'w'), indent=1)
print('ok')
