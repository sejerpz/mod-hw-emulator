# MOD Duo panel skin

A picture of the top panel of the MOD Duo, for the skin layout of the user interface. Each control sits at a
rectangle given in `duo-panel.json`, read like the Dwarf skin (see `../../dwarf/skin/README.md`).

| File | What it is |
| --- | --- |
| `duo-panel@1x.png` | 940 x 800 px, 5 px/mm, RGBA (transparent corners) |
| `duo-panel@2x.png` | 1880 x 1600 px, 10 px/mm |
| `duo-panel.svg` | the source drawing, units in mm, origin at the top left (side jacks included) |
| `duo-panel.json` | for each PNG, the rectangles `[x, y, w, h]` in pixels |
| `mkskin.py` | generates the SVG and the JSON: `python mkskin.py duo-panel.svg duo-panel.json` |

Rectangles:

- `display_window` and `display 2_window` (left and right display, the whole glass), with `display` and
  `display 2` (the 128x64 pixel area, 2:1);
- `Knob 1`, `Knob 2` (left, right, as numbered on the panel): the collar of radius R, the cap is 0.557 R as the
  encoder widget draws it;
- `Footswitch 1`, `Footswitch 2`, with `LED Footswitch 1`, `LED Footswitch 2` above them;
- `LED Out 1`, `LED Out 2` (left side) and `LED In 1`, `LED In 2` (right side), the audio meters by the jacks.

The names follow the Dwarf skin. The Duo device profile (`board.c`) is not written yet: when it is, its inputs
and LEDs take these names, or the JSON is renamed to match.

The picture draws every control (knobs, footswitches, LEDs off, display glass) and the handle, so it reads on its
own; the widgets are laid over their rectangles. It carries no MOD logo.

The PNGs are rendered from the SVG with a headless browser, as for the Dwarf: `duo-panel.svg` opened at
940 x 800 px (device scale factor 2 for `@2x`), saved with `--screenshot`.

## Sources and license

The drawing is our own and is licensed with the rest of the project (AGPLv3+). No picture of the device was
copied. The measurements come from:

- the dimensioned drawing of the case on the MOD wiki (MOD Duo Technical Specs): the 180 x 160 mm top, the
  knobs and footswitches 51.9 mm from the side and 76.2 mm apart, the knobs 78.6 mm from the back, the handle,
  the display windows;
- the controller board of the Duo (https://github.com/mod-audio/mod-hw-duo, `hw-design/top-board`,
  rev. 2019-04-15): the LEDs 25.0 and 45.05 mm below the knob shafts, the jack LEDs 150 mm apart; that
  repository is CC BY-NC-SA 4.0 and none of its files is included here.

The MOD logo is a registered trademark of MOD Audio (EU trade mark no. 016228827; UK trade mark no.
UK00916228827). This skin does not carry it. mod-hw-emulator is not made or endorsed by MOD Audio.
