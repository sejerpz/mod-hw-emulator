# MOD Dwarf panel skin

A picture of the top panel of the MOD Dwarf, for the skin layout of the user interface. Each control sits at a
rectangle given in `dwarf-panel.json`.

| File | What it is |
| --- | --- |
| `dwarf-panel@1x.png` | 1010 x 510 px, 5 px/mm, RGBA (transparent corners) |
| `dwarf-panel@2x.png` | 2020 x 1020 px, 10 px/mm |
| `dwarf-panel.svg` | the source drawing, units in mm, origin at the centre of the panel |
| `dwarf-panel.json` | for each PNG, the rectangles `[x, y, w, h]` in pixels, named as in `../board.c` |
| `mkskin.py` | generates the SVG and the JSON: `python mkskin.py dwarf-panel.svg dwarf-panel.json` |
| `LICENSE` | full text of CC BY 4.0 |

Rectangles: `Knob 1..3`, `Subpage I..III`, `Shift`, `Footswitch A..C`, `LED Footswitch A..C`, `LED In 1/2`,
`LED Out 1/2`, `display` (the 128x64 pixel area, 2:1, where the LCD widget goes) and `display_window` (the
opening in the panel).

The picture already draws the controls (knobs, buttons, footswitches, LEDs off, display glass), so it reads on
its own; the widgets are laid over their rectangles. It carries no MOD logo.

The UI uses Qt5 Widgets only (no QtSvg), so it loads the PNGs. After changing the SVG, render them again at the
two sizes, for example with headless Chrome:

    chrome --headless=new --hide-scrollbars --default-background-color=00000000 \
        --window-size=1010,510 --screenshot=dwarf-panel@1x.png page.html

where `page.html` shows `dwarf-panel.svg` at 1010 x 510 px (2020 x 1020 for `@2x`).

## License

The panel geometry comes from the MOD Dwarf exterior model:

> Panel geometry based on the MOD Dwarf exterior model by MOD Audio
> (https://github.com/mod-audio/hw-mod-dwarf/tree/master/enclosure), licensed under CC BY 4.0
> (https://creativecommons.org/licenses/by/4.0/).
> Changes: top view traced from the 3D model and redrawn as a flat, coloured panel with its silkscreen.

The files in this folder (the drawing, the PNGs, the rectangles and `mkskin.py`) are licensed under CC BY 4.0
as well, see [`LICENSE`](LICENSE). They are not covered by the AGPLv3+ of the rest of the project.

The MOD logo is a registered trademark of MOD Audio (EU trade mark no. 016228827; UK trade mark no.
UK00916228827). The licence gives no right to use the MOD name or logo, and this skin does not carry the logo.
mod-hw-emulator is not made or endorsed by MOD Audio.
