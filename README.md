# mod-hw-emulator

Runs the real MOD controller firmware (MOD Dwarf for now) on a desktop computer, to develop HMI features —
[aLabs HMI widgets](https://github.com/sejerpz/alabs-hmi-widgets) first — together with mod-desktop.

Experimental. See [DESIGN.md](DESIGN.md) for how it works.

## Build

Needs GCC or Clang (MinGW on Windows), GNU make and git.

```sh
make                           # build/fw-dwarf.so (.dll/.dylib) and build/mhe-run
```

The firmware is built from `../alabs-mod-custom-images/mod-dwarf-controller` (branch
`feature/mod-hw-emulator`: `feature/hmi-widgets` plus fixes found with the emulator, and its protocol
submodule on the widgets and ui-config branches merged); `make FW_DWARF=path` for another checkout.
`scripts/fetch-firmware.sh` clones the sources into `external/` where there is no checkout.

## Try it headless

```sh
build/mhe-run build/fw-dwarf.so "wait 1000" "send webgui boot 4 0 0 0 0 0" "wait 3000" "snap control.png" "click knob1" leds
```

`mhe-run` prints what the firmware sends on its serial channels (`[webgui] …`, `[system] …`) and accepts
commands as arguments or on stdin; see the top of [host/run/mhe-run.c](host/run/mhe-run.c).
The EEPROM image and the emulated system settings are kept in `mhe-state/` (`--state DIR` to change it).

The emulator plays the parts of the device around the controller:

- **Linux console** (cli line): boot messages and the `hmi` login the controller waits for before it
  accepts any input;
- **mod-system-control** (system line): answers the `sys_*` commands and keeps the values it is given;
- **mod-ui** (webgui line): yours, over TCP.

## With mod-ui

```sh
build/mhe-run --tcp webgui=9900 build/fw-dwarf.so
```

mod-ui needs a `tcp://` HMI port: branch `feature/mod-hw-emulator` of mod-ui (`feature/hmi-widgets` plus
[patches/mod-ui/0001-hmi-tcp-transport.patch](patches/mod-ui/0001-hmi-tcp-transport.patch); rebuild
`utils/` after switching to it). Then, from the mod-ui folder:

```sh
MOD_DEV_HOST=1 MOD_DEV_HMI=0 MOD_HMI_SERIAL_PORT=tcp://127.0.0.1:9900 \
MOD_HARDWARE_DESC_FILE=../mod-hw-emulator/devices/dwarf/mod-hardware-descriptor.json ./debug-server.sh
```

`MOD_DEV_HOST=1` runs without jackd and mod-host; the descriptor brings the `hmi_widget_*` keys through
`mod-hardware-descriptor.d/`.

## Plugin widgets (mod-host)

On the device mod-host hands the widget bitmaps to mod-system-control through the `/sys_msgs` shared
memory. The emulator builds mod-system-control's `sys_host.c` from `../mod-system-control` (branch
`feature/hmi-widgets`) and creates `/sys_msgs` itself. mod-host opens it only as a JACK internal client, as
on the device and in mod-desktop, and only when built with `make MOD_HMI_EMULATOR=1` (`../mod-host`,
branch `feature/mod-hw-emulator`). With `MOD_SYS_SERIAL_PORT=tcp://127.0.0.1:9901` in the environment of both,
the emulator listens there and mod-host connects, in place of the shared memory; on Windows it is the only way
and the default. Three terminals, in this order:

```sh
build/mod-hw-emulator --tcp webgui=9900 build/fw-dwarf.so      # or build/mhe-run, same options
scripts/run-mod-host.sh 5655                                   # jackd (dummy backend) with mod-host inside
MOD_DEV_HOST=0 MOD_DEVICE_HOST_PORT=5655 scripts/run-mod-ui.sh ../mod-ui
```

Then add a plugin with widgets (alabs-hmi-widgets `hmi-widget-draw`, in `../plugins/lv2`) and place its
widgets with the Display dialog (Device menu). Notes:

- `MOD_DEV_HOST=0` is needed: mod-ui's `server.py` turns its development environment on, and with it
  the fake host, unless told otherwise.
- The mod-host port is 5555 by default; here 5655, because the Android emulator keeps 5555 (adb) on
  Windows and WSL mirrored networking shares the Windows ports.

`MHE_SYS_DEBUG=1` prints the traffic with mod-host. Without jackd, `make tools` builds `mhe-fake-host`, which
draws on an area as mod-host would:

```sh
build/mhe-run build/fw-dwarf.so      # then: send webgui boot 4 0 0 0 0 0
                                     #       send webgui wa_add 0 0 1 2 12 40 37
build/mhe-fake-host 1 40 37          # in another terminal
```

## User interface

```sh
make gui            # needs Qt5 Widgets (pkg-config Qt5Widgets)
build/mod-hw-emulator --tcp webgui=9900 build/fw-dwarf.so
```

![The Dwarf skin](docs/gui-skin.png)

The controls sit on a picture of the Dwarf panel, `devices/dwarf/skin/` (see [its README](devices/dwarf/skin/README.md)):
the program looks for `DEVICE-panel.json` in `skins/DEVICE` next to it, then in `devices/DEVICE/skin` of the
source tree. `--skin DIR` takes another one; `--simple` stacks the controls with no picture, the layout of
devices with no skin. The window can be resized, the panel keeps its proportions.

Wheel or drag on a knob turns it, a click pushes it; Q/A W/S E/D turn the knobs from the keyboard, 1 2 3
push them, Z X C are the footswitches A B C, F1-F3 the subpage buttons I II III, F4 Shift (the menu).

Two footswitches together (B+C for the pedalboard and bank navigation, A+B for the tool mode): press both keys
at once (X+C, Z+X), or Ctrl+click a footswitch to arm it (dashed ring) and click the other one. `mhe-run` has
`chord footswitchb footswitchc`.

## On Windows, with MOD Desktop

The Windows package (`scripts/package-win64.sh`) holds the emulator and what `mhe-desktop.cmd` lays over an
installed MOD Desktop (0.0.13, the latest Windows release): mod-host with the HMI control, mod-ui's `hmi.py` with
the TCP line, a descriptor with the Dwarf actuators. MOD Desktop itself is not changed: the script keeps a copy
of it in `%LOCALAPPDATA%\mod-hw-emulator\mod-desktop`, starts the emulator and then the copy. Pedalboards,
plugins and settings are the usual ones; quit the installed MOD Desktop first.

```bat
mhe-desktop.cmd                      :: -ModDesktop DIR if it is not in Program Files
```

The package is built in mod-desktop's CI container (ubuntu:22.04) with mod-desktop's PawPaw for win64, so
mod-host links the same jack and lilv as the MOD Desktop it goes in; see the top of the script.

## Layout

| Path | What |
|---|---|
| `include/mhe/emu_module.h` | ABI between the host and a firmware module |
| `lpc177x/` | LPC177x peripheral API emulation and the FreeRTOS host port |
| `chips/` | models of the chips on the boards (ST7565P display controller) |
| `devices/dwarf/` | Dwarf board: pins, panel, module entry; `skin/` the panel picture of the user interface (CC BY 4.0) |
| `host/` | Qt UI (`gui/`), headless runner (`run/`), mod-host stand-in (`tools/`), shared host code (`common/`: console, system-control, TCP lines) |
| `scripts/` | launchers for mod-host and mod-ui, firmware fetch, build adaptations of the firmware sources |
| `patches/` | the mod-ui change the emulator needs (also on mod-ui branch `feature/mod-hw-emulator`) |

## License

mod-hw-emulator is licensed under AGPLv3+, see [LICENSE](LICENSE).

What it is built with:

- the MOD Dwarf controller firmware (AGPLv3+), into the firmware module;
- the FreeRTOS kernel that comes with the firmware (MIT); the host port in `lpc177x/` is part of this project;
- `sys_host.c` of mod-system-control (AGPLv3+), into the runner and the user interface;
- Qt5 (LGPLv3), dynamically linked by the user interface;
- the Dwarf panel skin in `devices/dwarf/skin/` (CC BY 4.0), drawn from the MOD Dwarf exterior model by MOD Audio,
  see [its README](devices/dwarf/skin/README.md).

## Use of AI

Parts of this project, code and documentation, may be written with the help of AI tools,
mainly Claude by Anthropic. Every change is read and reviewed by a human developer before it
is committed, and the maintainer is responsible for it like for any other code. AI tools are
not credited as authors in the commit history.
