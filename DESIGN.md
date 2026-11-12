# mod-hw-emulator — design

An emulator of the MOD hardware controllers (Dwarf, Duo X and Duo) that runs the **real controller
firmware** on a desktop computer, next to [mod-desktop](https://github.com/mod-audio/mod-desktop), so that
HMI features — first of all [aLabs HMI widgets](https://github.com/sejerpz/alabs-hmi-widgets) — can be developed
and tested without a device.

Status: experiment.

## Goals

- Run the unmodified firmware sources (`mod-dwarf-controller`, later `mod-duo(x)-controller`): every screen,
  menu, popup and command of the controller works because it is the controller code itself.
- Cross platform (Linux, macOS, Windows), self contained, built with the same toolchain and libraries as
  mod-desktop (PawPaw: GCC/MinGW, Qt5) so it can ship next to it.
- Close the loop with mod-desktop: mod-ui talks to the emulated controller over the HMI protocol, mod-host
  talks to it through an emulated mod-system-control.
- Several devices: the device specific parts are data (a profile) plus one firmware module per device.

Non goals: cycle accuracy, emulating the ARM core, the bootloader, firmware updates.

## Architecture

```
 mod-ui  ──(tcp: HMI protocol, "webgui" serial)──┐
                                                 ▼
 mod-host ─(tcp: sys_msgs)──► system-control ─► ┌──────────────────────────────────────────┐
                              bridge (host)     │ firmware module  (fw-dwarf.so/.dll/.dylib)│
                                                │  firmware app/ + drivers/ (unmodified)    │
                                                │  FreeRTOS kernel (unmodified)             │
                                                │  ── emulation layer ──                    │
                                                │  FreeRTOS host port (threads)             │
                                                │  LPC177x peripheral API (GPIO, TIM, UART, │
                                                │  EEPROM, NVIC, ADC…) + chip models        │
                                                └──────────────┬────────────────────────────┘
                                                               │ emu_module.h (C ABI)
                                                ┌──────────────┴────────────────────────────┐
                                                │ host: mod-hw-emulator                     │
                                                │  UI (Qt5) or headless runner              │
                                                │  serial channels → tcp                    │
                                                └───────────────────────────────────────────┘
```

### Where the line between firmware and emulator is drawn

The firmware reaches the hardware through two layers:

1. the NXP driver library (`lpc177x_8x_gpio.h`, `_timer.h`, `_uart.h`, `_eeprom.h`, …) and a few CMSIS core
   functions (`NVIC_*`, `__get_PSP`, …);
2. FreeRTOS, whose CPU specific part is the *port* (`port.c`, `portmacro.h`).

The emulator replaces exactly these two layers and nothing above them:

- **Peripheral API emulation.** The NXP headers are kept (types and constants), their `.c` are replaced by an
  emulation: virtual GPIO ports, timers that fire `TIMERn_IRQHandler`, UARTs with FIFOs and interrupt
  identification that fire `UARTn_IRQHandler`, a file backed EEPROM. `hardware.c`, `serial.c`, `actuator.c`,
  `ledz.c` and the display driver are compiled unchanged.
- **Chip models.** Devices hanging on GPIO are modeled at their bus: the ST7565P display controller decodes
  the 8080 parallel bus (A0, WR, data lines) into its display RAM; LEDs are sampled on their pins (the firmware
  does software PWM, so the colour is the duty cycle); buttons and encoders drive input pins (encoders produce
  the quadrature sequence the firmware decodes).
- **FreeRTOS host port.** The real kernel (`tasks.c`, `queue.c`, `list.c`) runs unchanged on top of a port
  where every task is a host thread and only the thread of `pxCurrentTCB` runs, so the kernel keeps its single
  core, priority based semantics. Interrupt handlers run on one "interrupt controller" thread; critical
  sections and `portDISABLE_INTERRUPTS` exclude them, as masking interrupts does on the MCU.

Because the line is drawn at the hardware API, the same emulation layer serves every firmware that uses the
LPC177x family, and a new firmware revision needs no work on the emulator.

### FreeRTOS host port

- A task is a host thread waiting on its own run semaphore. A context switch is `vTaskSwitchContext()` followed
  by waking the thread of the new `pxCurrentTCB` and parking the current one.
- Preemption happens at preemption points instead of at any instruction: kernel calls (critical section exit,
  yields) and hardware accesses (GPIO, UART, …). An interrupt that wakes a higher priority task, or a tick
  that unblocks one, raises a pending switch that the running task takes at its next preemption point. A
  task that loops without touching the kernel or the hardware would not be preempted; the firmware has none
  (its busy loops poll hardware).
- A task that yields with nothing else ready (the display task loops at the lowest priority) sleeps for a
  short host time before going on, so the emulator does not spin a core.
- The tick and the hardware timers run on virtual time driven by the host clock; work that falls behind
  (e.g. the ~30 kHz LED timer) is run in batches.
- Heap: `pvPortMalloc`/`vPortFree` map to the host allocator; `vPortDefineHeapRegions` is a no-op.

### Firmware sources

The firmware is compiled from its own repository (the `deps/mod-dwarf-controller` submodule) with:

- the include path of the emulation layer first, so its `core_cm3.h`, `portmacro.h` and `FreeRTOSConfig.h`
  wrapper win over the ARM ones;
- `CCC_ANALYZER` defined, the firmware's own switch for host builds (it removes the inline ARM assembly);
- a short list of source adaptations applied at build time to the copies in the build tree, never to the
  repository: `main()` renamed, the ARM fault handlers that read `LR` replaced. These are the candidates to
  upstream as `#ifdef MOD_HW_EMULATOR` guards.

### Firmware versions

A build is identified by `git describe --tags --always --dirty` of its sources, read by the Makefile from
the checkout before compiling: it names the module (`build/fw-dwarf-<describe>.so`) and its object folder
(`build/dwarf-<describe>/`), so the builds of several versions coexist and each stays incremental.
`build/fw-dwarf.so` links to the last build. `FW_REF` picks a commit of the checkout without touching it:
`scripts/export-firmware.sh` writes it out with `git archive`, plus the protocol submodule at its pinned
commit, from the submodule clone in the checkout (the aLabs pins are not all published). A tag named in
`FW_REF` names the build even when another tag sits on the same commit.

Checked: `alabs10`, `alabs15`, `stls18`, `feature/mod-hw-emulator` and mod-audio's `master` build with the
same adaptations and boot. `master` has no `HardFault_Analisys`: the main.c adaptation cuts from
`HardFault_Handler` there. mod-audio does not tag the controller (its releases are tagged in the OS image),
so an upstream build is named after its commit hash.

The module exports `mhe_firmware_info()` (device, describe, nearest tag, full commit, commit date), callable
before the entry so the host can choose the state folder: `mhe-state/<tag>/` by default, because the
EEPROM layout may change between versions while the commits on one tag share it. The info is also in
`mhe_module.info`. `VERSION_HASH`, which the firmware draws on its display, stays `v.<hash8>`: there is no
room for a longer string.

A module cannot start twice in one process (the firmware threads are parked, not joined), so the GUI
Firmware menu starts a new process with the same arguments on the chosen module and quits.

### Module ABI (`include/mhe/emu_module.h`)

One firmware per shared library, because firmwares share symbol names (`glcd_*`, `naveg_*`, …). The host loads
it and calls `mhe_module_entry(const mhe_host*)`, which returns the module interface. Both structs start with
their `size` and are only ever appended, like the hmi-widgets API.

- host → module: start/stop, input events (encoder steps and push, buttons/footswitches, potentiometers), bytes
  received on a serial channel;
- module → host: display frames (Mono1 rows, any number of displays), LED colours, bytes sent on a serial
  channel, log lines.

### Device profile

`devices/<device>/` holds what is specific to a device:

- `device.mk`: the firmware checkout (`FW_REPO`), its `config-*.h` and its drivers (`make DEVICE=<device>`);
- `board.c`, compiled with the firmware's own `config-*.h`, that binds pins to chip models and inputs (display
  bus, LED pins, encoder and button pins) — no pin number is copied by hand;
- `profile.json`: panel description (displays, encoders, buttons, footswitches, pots, LEDs with ids and labels,
  optional skin rectangles), serial channels and their role, mod-ui hardware descriptor to use;
- `mod-hardware-descriptor.json` with the `hmi_widget_*` keys.

**Duo X** (`devices/duox`, mod-audio `mod-duox-controller`, LPC1777): its sources build unchanged with the
same adaptations as the Dwarf. Two UC1701 displays share SSP0, one chip select each, with a command/data pin:
the UC1701 has the ST7565P command set, so `chip_st7565p` models both, the UC1701 through
`st7565p_chip_create_spi()` fed by the SSP emulation (`ssp.c`, transmit only, never busy). Eight
potentiometers are ADC channels (`adc.c`, burst mode, the host gives the levels). Two places read or write
peripheral registers instead of calling the driver API: the potentiometers (`LPC_ADC->DR[ch]`) and the analog
pin setup (`LPC_IOCON_BASE`); the emulator's `LPC177x_8x.h` wraps the firmware's own and points both to
blocks the emulation keeps. The four buttons under the knobs (footswitches for the firmware) and the three
between the endless knobs (Snapshot 1, Page, Snapshot 2) are the firmware's FOOTSWITCH0..6, two encoders,
seven RGB leds; the names are those of the skin (`devices/duox/skin`), which lists the four footswitches as
`square`, drawn as buttons.

`devices/duox/mod-hardware-descriptor.json` is not the device's own file (none is published): it is written
from the firmware and from what mod-ui reads. Actuator ids are the firmware's hw_id (0-1 the endless knobs,
2-5 the buttons, 6-13 the potentiometers, as `TOTAL_ACTUATORS` counts them), six addressing pages and no
subpages (`PAGES_COUNT`, `pa`), and only the boot data and names its protocol handles: master volume in
`boot`, the pedalboard name (`pn`); no snapshot name, tuner input or reference (it registers none of
those commands). The `/hmi/...` URIs follow the Dwarf ones: pedalboards addressed on a real Duo X may name
other URIs. The HMI widget areas (`mod-hardware-descriptor.d/`) follow the alabs-hmi-widgets specification:
two displays in `hmi_widget_displays`, side by side on a surface 256 wide, each with the controls it shows and the
15 areas of the stock layout (knobs, endless knob band, button footers, full screen), in display coordinates.
mod-ui sends them on the surface, x 128 and on for the right display, and an area stays on one display
(the firmware's `wip/dwarf-backport` refuses one that crosses).

sys_host.c of mod-system-control is built for one device (`_MOD_DEVICE_DWARF`, `_MOD_DEVICE_DUOX`: pages,
subpages, actuators of its page cache), and the hosts load any device. So it is built once per device
(`build/msc/sys_host-<device>.o`), its public functions renamed by `msc-shim/sys_host_device.h`
(`mhe_sys_host_duox_setup`...), and the system-control emulation calls the copy of the device the module
declares; a device with none runs without the mod-host side.

Checked: boot splash across the two displays, `boot` answered with the master volume screen, the page button
and its led, headless and in the user interface (skin and `--simple`). Built, not run yet: the descriptor
with mod-ui and a pedalboard, sys_host.c for the Duo X with mod-host. The jack leds of the skin
(Input/Output 1/2): the device profile has no led for them, they stay off.

**Duo** (`devices/duo`, mod-audio `mod-duo-controller`, LPC1759): the other MCU family, LPC17xx, with its own
NXP driver library (`lpc17xx_*.h`) in the firmware tree. `device.mk` names it (`FW_SERIE = LPC17xx`), and
the Makefile takes the CMSIS headers and the library of that family and defines `LPC17xx`; the emulation
layer includes the firmware's `device.h`, which picks the headers of the family, so the same sources serve
both. The driver calls the Duo makes have the same signatures on both families, with three differences
handled in place: `PINSEL_SetPinFunc` returns nothing, the ADC data registers are `ADDR0..7` instead of
`DR[]`, there is no SSP2; and no EEPROM (`eeprom.c` only on the LPC177x). Its `LPC17xx.h` is not wrapped: the
Duo firmware reads no peripheral register directly. The board is the Duo X one cut down: two UC1701 displays on
SSP0, two encoders with a push, two footswitches, two RGB leds that light on a low pin (`LED_TURN_ON_WITH_ZERO`),
two serial lines (webgui, cli). The display brightness is software PWM of the backlight pins in `TIMER0`, so
the board samples it in 1/16 steps; each display keeps its own frame, so that a brightness change alone sends
the right frame again (the Duo X had one frame for both: a dimmed display showed the other one's picture).

It speaks the protocol of its time: no `sys_*` line, `is` for the pedalboards of the bank, `bc` for the
footswitch bank functions, `boot` with brightness, quick bypass, tuner mute and profile; `plb`/`ple` of newer
mod-ui are answered "not found" and ignored. `devices/duo/mod-hardware-descriptor.json` is written from the
firmware: four actuators (hw_id 0-1 the knobs, 2-3 the footswitches), bank navigation, `hmi_set_index`, no
addressing pages, no EEPROM. Firmware bug found: `bank_config_footer()` reads the footswitch pedalboard names
before mod-ui has sent any (`bc` comes before `is`), a NULL read; fixed on the fork's `feature/mod-hw-emulator`.

Checked: boot logo, login on the console line, mod-ui (fake host) end to end with `pcl`, `bc`, `is`, `boot`;
a control on a knob and a footswitch headless; the user interface with the Duo skin.

### UI

- Controls are widgets bound to logical ids: LCD (scaled framebuffer, pixel grid, backlight colour), encoder
  (wheel/drag to turn, click to push), button/footswitch with its LED, pot (270 degrees, wheel/drag, double
  click for the middle).
- The layout is separate from the controls: `SkinLayout` (device picture as background, each control at the
  rectangle of its name in `devices/DEVICE/skin/DEVICE-panel.json`, leds at "LED name", displays at
  "display", "display 2"… in the order of their ids; skinned controls draw only what changes: knob and pot
  pointers, led colours, pressed and armed switches) and `SimpleLayout` (displays side by side on top,
  encoders, then buttons, pots in rows of four, footswitches) for devices with no skin or `--simple`.
- Keyboard shortcuts for every control.
- Toolkit: Qt5 Widgets (already shipped by mod-desktop). A headless runner (`mhe-run`) exists for tests and
  for CI: it can dump the display as PBM/PNG and drive inputs from a script.

### Connecting mod-desktop

- **mod-ui** reaches the controller on a TCP socket: `MOD_HMI_SERIAL_PORT=tcp://127.0.0.1:<port>` (small patch
  in `mod/hmi.py`, using tornado's `IOStream`, no pyserial needed), `MOD_DEV_HMI=0`, and
  `MOD_HARDWARE_DESC_FILE` pointing to the device descriptor.
- **mod-host** is built with `MOD_HMI_EMULATOR`: it enables the HMI control code without the device I/O
  processing. It opens `/sys_msgs` as on the device, POSIX shared memory, or with `MOD_SYS_SERIAL_PORT=tcp://HOST:PORT`
  (always on Windows, 127.0.0.1:9901 by default) connects to the emulator over TCP: `sys_serial_tcp.h`, in
  mod-system-control and in mod-host's copy of `sys_host.h`. Each side keeps the shared memory layout in its
  own memory and two threads copy what one side writes to the other over the socket, so the code that writes
  and reads the ring buffers does not change and mod-host still never blocks writing from a plugin. On the
  wire a message is the bytes it is in a ring buffer. It also covers macOS, where `mod-semaphore.h` has no
  semaphores shared between processes.
- **mod-system-control** is not needed: the emulator bridges `sys_msgs` to the firmware system serial and
  answers the system commands of the controller (gain, settings, Wi-Fi…) with emulated values.
- **Delivery without rebuilding mod-desktop**: mod-host is a single JACK internal client
  (`jack/mod-host.(dll|so)`), mod-ui's `mod` package is a plain folder next to the frozen executable, and the
  launcher passes its environment to jackd and mod-ui. `mhe-desktop` (Windows: `scripts/mhe-desktop.ps1`)
  keeps a copy of the installation (the user data is in the user's folders, shared), lays mod-host, `hmi.py`,
  the addressing dialog (MOD Desktop locks its Device tab and shows a card for the hardware; with actuators in the
  descriptor it stays open), the launcher with an Open Emulator button (shown only with `MOD_HW_EMULATOR`, the
  command line of the emulator) and a descriptor (MOD Desktop's name and binaries, the Dwarf actuators) over it, starts the emulator and
  then the copy. The files are built for one mod-desktop release (`scripts/package-win64.sh`, in mod-desktop's
  CI container with its PawPaw), so mod-host links the jack and lilv it is loaded with; mod-ui stays the
  release's own but for the files `patches/mod-ui` changes, whose originals are checked by hash.
- **Standalone on Windows**: with no Windows release of MOD Desktop 0.14, the package can instead hold a whole
  MOD Desktop built from `main` with those files laid over it at build time (`package-win64.sh --standalone`).
  The launcher keeps a `MOD_DATA_DIR` given to it (`patches/mod-desktop/0002`), so the zip keeps its data next
  to it and never mixes with an installed MOD Desktop; the descriptor is still written at each start, from
  MOD Desktop's own one kept in `devices\mod-desktop.json`, for the device chosen.
- **Standalone on Linux**: the same package for Linux (`package-linux.sh`, `mhe-desktop.sh`), MOD Desktop
  built for linux-x86_64 in its CI container. There too both sides of `/sys_msgs` use TCP
  (`MOD_SYS_SERIAL_PORT`), so it needs no shared memory between the emulator and jackd.

## Milestones

1. **Done** — spike: the Dwarf firmware boots headless, the display is dumped to an image, the controller
   answers on its serial channels, inputs reach it.
2. **Written, not built yet** — Qt UI with the simple layout; encoders, buttons, footswitches, LEDs.
3. **Done** — TCP channels; mod-ui branch `feature/mod-hw-emulator` (`tcp://` HMI port): mod-ui run
   with `debug-server.sh` and `MOD_DEV_HOST=1` goes through its whole HMI initialization down to `boot`,
   and the controller shows its pedalboard.
4. **Done** — hmi-widgets end to end: mod-ui (Display dialog) → mod-host → plugin `hmi-widget-draw` →
   `/sys_msgs` → mod-system-control's own `sys_host.c` built into the emulator (page cache, resends,
   bitmaps in bands) → firmware → widget areas on the emulated display. mod-host branch
   `feature/mod-hw-emulator` builds with `MOD_HMI_EMULATOR=1` (HMI control without the device audio
   processing) and runs as a JACK internal client, the only mode where it opens `/sys_msgs`.
5. **Done on Windows** — `/sys_msgs` over TCP; the emulator built with MinGW and the Qt of PawPaw runs natively;
   `mhe-desktop.cmd` over MOD Desktop built from mod-desktop main (the 0.14 release): mod-ui on the HMI line
   (9900), mod-host on `/sys_msgs` (9901), the pedalboard on the emulated display. Plugin widgets there: not
   tried (MOD Desktop's mod-ui has no Display dialog). Linux overlay: to do.
6. **Done** — Skin layout, Dwarf, Duo X and Duo panel pictures and profiles (the Duo X without mod-ui yet; the
   Duo with mod-ui, on the LPC17xx family).

## What the spike taught

- **The controller needs the rest of the device to come alive.** It follows the Linux boot on its console
  line and accepts no input until it has logged in as `hmi`; it asks mod-system-control for values during
  boot and waits for the answers. Both are emulated in `host/common/` (`console.c`, `syscontrol.c`).
- **Busy waits.** `sys_comm_wait_response()` spins on a flag set by another task. On the MCU an interrupt
  preempts the spinning task; here the port forces a pending switch after 2 ms (`emu_port_watchdog`), and
  the old thread parks at its next preemption point, `vTaskSuspendAll()` included (`portSOFTWARE_BARRIER`).
- **NULL reads and double frees** go unnoticed on the LPC1778, where address 0 is readable flash and the
  FreeRTOS heap does not check frees, and crash on a PC. Latent firmware bugs found this way, fixed on the
  firmware branch `feature/mod-hw-emulator`: `CM_set_foot_led(NULL)` after `pcl`, `cb_menu_item_changed()`
  reading past the arguments of `c`, `data_free_banks_list()` freeing the flags twice (bank list in the
  navigation mode), and `foot_control_rm()` reading an empty footswitch (a pedalboard loaded from the
  navigation mode). The emulator reports any invalid access in the firmware with the firmware describe and
  its module offset (for `addr2line`). Older firmwares (the aLabs tags, mod-audio `master`) have these bugs
  and are built as they are, never patched: a crash in one of the places above is this list, not the
  emulator.
- **Integer division by zero** returns 0 on the Cortex-M3 and traps on x86-64. The emulator completes the
  instruction the ARM way and logs it once: `screen_page_index()` divides by the number of available pages,
  0 when `boot` arrives before `pa` — a latent firmware bug.
- **CPU.** The display task redraws the whole frame through GPIO in a `taskYIELD()` loop: explicit yields
  are paced (one per 10 ms per task, ended early by a context switch request) and fast timers run in
  batches. Idle load is about 6% of a core.
- **Build.** A quoted `#include` looks next to the including file first, so the CMSIS core, the FreeRTOS
  port and config are replaced in staged copies of their folders, not through `-I`. The protocol commit
  pinned by the firmware branch is not published: the fetch script rebuilds it by merging the widgets and
  ui-config branches of mod-controller-proto.

## Open points

- Upstream the build adaptations to the firmware as `MOD_HW_EMULATOR` guards.
- Upstream to mod-audio the generic parts (TCP HMI transport, `MOD_HMI_EMULATOR`), so only the widgets need
  forks.
- Licence: the firmware is AGPLv3; a module built from it is too.
