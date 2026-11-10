# MOD Duo X panel skin

A picture of the top panel of the MOD Duo X, for the skin layout of the user interface. Each control sits at a
rectangle given in `duox-panel.json`, read like the Dwarf skin (see `../../dwarf/skin/README.md`).

| File | What it is |
| --- | --- |
| `duox-panel@1x.png` | 940 x 825 px, 5 px/mm, RGBA (transparent corners) |
| `duox-panel@2x.png` | 1880 x 1650 px, 10 px/mm |
| `duox-panel.svg` | the source drawing, units in mm, origin at the top left (side jacks included) |
| `duox-panel.json` | for each PNG, the rectangles `[x, y, w, h]` in pixels |
| `mkskin.py` | generates the SVG and the JSON: `python mkskin.py duox-panel.svg duox-panel.json` |

Rectangles:

- `display_window` and `display 2_window` (left and right display, the whole glass), with `display` and
  `display 2` (the 128x64 pixel area, 2:1);
- `Endless Knob 1`, `Endless Knob 2` (left, right): the collar of radius R, the cap is 0.557 R as the encoder
  widget draws it;
- `Snapshot 1`, `Page`, `Snapshot 2` (the three buttons between the endless knobs, as the MOD manual names them);
- `Knob 1..4` (top row) and `Knob 5..8` (bottom row), the potentiometers;
- `Button 1..4`, the buttons under the knobs;
- `LED <button>` for each of the seven buttons (a dot above it), and `LED Output 1/2`, `LED Input 1/2`.

The names follow the firmware's own ("ENDLESS KNOB", "KNOB", "BUTTON"). The Duo X device profile (`board.c`) is
not written yet: when it is, its inputs and LEDs take these names, or the JSON is renamed to match.

The picture draws every control (knobs, buttons, LEDs off, display glass), so it reads on its own; the widgets
are laid over their rectangles. It carries no MOD logo.

The PNGs are rendered from the SVG with a headless browser, as for the Dwarf: a page showing `duox-panel.svg`
at 940 x 825 px (1880 x 1650 for `@2x`), saved with `--screenshot`.

## Sources and license

The drawing is our own and is licensed with the rest of the project (AGPLv3+). No picture of the device was
copied. The positions of the controls are taken from the pick and place file of the Duo X controller board
(https://github.com/mod-audio/mod-hw-duoX, `hw-design/top-board/Assembly`, rev. 2022-02-28), as measurements;
that repository is CC BY-NC-SA 4.0 and none of its files is included here. The size of the case comes from the
MOD wiki (Duo X Technical Specs).

The MOD logo is a registered trademark of MOD Audio (EU trade mark no. 016228827; UK trade mark no.
UK00916228827). This skin does not carry it. mod-hw-emulator is not made or endorsed by MOD Audio.
