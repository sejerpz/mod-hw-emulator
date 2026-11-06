# mod-hw-emulator
#
#   make                 firmware modules and the headless runner
#   make gui             Qt5 user interface (pkg-config Qt5Widgets)
#   make FW_DWARF=path   another checkout of mod-dwarf-controller (default: the aLabs custom images one)
#
# Works with GCC/Clang on Linux and macOS and with MinGW on Windows, as mod-desktop (PawPaw) does.

FW_DWARF ?= ../alabs-mod-custom-images/mod-dwarf-controller
BUILD    ?= build

CC       ?= gcc
CXX      ?= g++
AWK      ?= awk

UNAME := $(shell uname -s 2>/dev/null)
ifneq (,$(findstring MINGW,$(UNAME))$(findstring MSYS,$(UNAME))$(findstring mingw,$(CC)))
WINDOWS = true
endif

ifeq ($(WINDOWS),true)
SO_EXT   = .dll
EXE_EXT  = .exe
HOST_LIBS = -lwinmm -lws2_32
else ifeq ($(UNAME),Darwin)
SO_EXT   = .dylib
HOST_LIBS =
else
SO_EXT   = .so
HOST_LIBS = -ldl
endif

COMMON_CFLAGS = -D_GNU_SOURCE -std=gnu11 -O2 -g -pthread -fPIC -Wall -MMD -MP
MODULE_LDFLAGS = -shared -pthread
ifneq ($(WINDOWS),true)
COMMON_CFLAGS += -fvisibility=hidden
endif

# mod-system-control: its sys_host.c (the mod-host side: labels, leds, widget bitmaps through /sys_msgs) is
# built into the hosts when the checkout is there; POSIX shared memory, so not on Windows yet
MSC ?= ../mod-system-control
ifneq ($(WINDOWS),true)
ifneq (,$(wildcard $(MSC)/src/sys_host.c))
SYS_HOST_SRC    = $(MSC)/src/sys_host.c
SYS_HOST_CFLAGS = -DMHE_WITH_SYS_HOST -D_MOD_DEVICE_DWARF -Ihost/common/msc-shim -I$(MSC)/src
endif
endif

all: $(BUILD)/fw-dwarf$(SO_EXT) $(BUILD)/mhe-run$(EXE_EXT)

# header dependencies of every object built so far
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

# ---------------------------------------------------------------------------------------------------------------
# emulation layer (compiled into every LPC177x module)

EMU_SRC = $(wildcard lpc177x/src/*.c) chips/chip_st7565p.c

# ---------------------------------------------------------------------------------------------------------------
# Dwarf firmware module

DWARF_BUILD = $(BUILD)/dwarf
DWARF_STAGE = $(DWARF_BUILD)/stage
DWARF_GIT_HASH := $(shell git -C $(FW_DWARF) rev-parse --short=8 HEAD 2>/dev/null)

# Staged headers. A quoted #include looks first in the directory of the including file, so the CMSIS core,
# the FreeRTOS port and config cannot be replaced through -I: they are replaced in copies of their folders.
# config.h is the device configuration the firmware Makefile links for its target.
$(DWARF_STAGE)/.stamp: $(wildcard $(FW_DWARF)/freertos/inc/*.h $(FW_DWARF)/nxp-lpc/CMSISv2p00_LPC177x_8x/inc/*.h) \
                       $(wildcard lpc177x/include/*.h) $(FW_DWARF)/app/inc/config-moddwarf.h
	@rm -rf $(DWARF_STAGE) && mkdir -p $(DWARF_STAGE)/freertos $(DWARF_STAGE)/cmsis $(DWARF_STAGE)/config
	cp $(FW_DWARF)/freertos/inc/*.h $(DWARF_STAGE)/freertos/
	mv $(DWARF_STAGE)/freertos/FreeRTOSConfig.h $(DWARF_STAGE)/freertos/FreeRTOSConfig_fw.h
	cp lpc177x/include/FreeRTOSConfig.h lpc177x/include/portmacro.h $(DWARF_STAGE)/freertos/
	cp $(FW_DWARF)/nxp-lpc/CMSISv2p00_LPC177x_8x/inc/*.h $(DWARF_STAGE)/cmsis/
	cp lpc177x/include/core_cm3.h $(DWARF_STAGE)/cmsis/
	cp $(FW_DWARF)/app/inc/config-moddwarf.h $(DWARF_STAGE)/config/config.h
	@touch $@

DWARF_INC = -I$(DWARF_STAGE)/freertos -I$(DWARF_STAGE)/cmsis -I$(DWARF_STAGE)/config \
            -Ilpc177x/src -Ichips -Iinclude \
            -I$(FW_DWARF)/nxp-lpc \
            -I$(FW_DWARF)/nxp-lpc/LPC177x_8xLib/inc \
            -I$(FW_DWARF)/drivers/inc \
            -I$(FW_DWARF)/app/inc \
            -I$(FW_DWARF)/mod-controller-proto

DWARF_CFLAGS = $(COMMON_CFLAGS) $(DWARF_INC) -DLPC177x_8x -DCCC_ANALYZER -DMOD_HW_EMULATOR \
               -DVERSION_HASH=\"v.$(DWARF_GIT_HASH)\" -fcommon

# the firmware is not ours to clean up: keep its warnings out of the way
FW_CFLAGS = -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
            -Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-address-of-packed-member \
            -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types

DWARF_FW_SRC = $(filter-out %/syscalls.c %/main.c,$(wildcard $(FW_DWARF)/app/src/*.c)) \
               $(addprefix $(FW_DWARF)/drivers/src/,actuator.c ledz.c st7565p.c) \
               $(addprefix $(FW_DWARF)/freertos/src/,tasks.c queue.c list.c)

DWARF_OBJ = $(patsubst $(FW_DWARF)/%.c,$(DWARF_BUILD)/fw/%.o,$(DWARF_FW_SRC)) \
            $(DWARF_BUILD)/gen/main.o \
            $(patsubst %.c,$(DWARF_BUILD)/emu/%.o,$(EMU_SRC) devices/dwarf/board.c)

$(DWARF_BUILD)/gen/main.c: $(FW_DWARF)/app/src/main.c scripts/adapt-main.awk
	@mkdir -p $(dir $@)
	$(AWK) -f scripts/adapt-main.awk $< > $@

$(DWARF_BUILD)/gen/main.o: $(DWARF_BUILD)/gen/main.c | $(DWARF_STAGE)/.stamp
	$(CC) $(DWARF_CFLAGS) $(FW_CFLAGS) -c $< -o $@

$(DWARF_BUILD)/fw/%.o: $(FW_DWARF)/%.c | $(DWARF_STAGE)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(DWARF_CFLAGS) $(FW_CFLAGS) -c $< -o $@

$(DWARF_BUILD)/emu/%.o: %.c | $(DWARF_STAGE)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(DWARF_CFLAGS) -Wextra -Wno-unused-parameter -c $< -o $@

$(BUILD)/fw-dwarf$(SO_EXT): $(DWARF_OBJ)
	$(CC) $(MODULE_LDFLAGS) -o $@ $^ -lm

# ---------------------------------------------------------------------------------------------------------------
# headless runner

RUN_SRC = host/run/mhe-run.c host/common/module_loader.c host/common/png.c host/common/syscontrol.c \
          host/common/console.c host/common/tcp_serial.c

$(BUILD)/mhe-run$(EXE_EXT): $(RUN_SRC) $(SYS_HOST_SRC) include/mhe/emu_module.h $(wildcard host/common/*.h)
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -Wall -Iinclude $(SYS_HOST_CFLAGS) -o $@ $(RUN_SRC) $(SYS_HOST_SRC) \
	    -pthread $(HOST_LIBS)

# ---------------------------------------------------------------------------------------------------------------
# user interface (Qt5 Widgets, as shipped by mod-desktop)

PKG_CONFIG ?= pkg-config
QT_CFLAGS = $(shell $(PKG_CONFIG) --cflags Qt5Widgets)
QT_LIBS   = $(shell $(PKG_CONFIG) --libs Qt5Widgets)

GUI_CXX_SRC = host/gui/main.cpp host/gui/emulator.cpp host/gui/controls.cpp
GUI_C_SRC   = host/common/module_loader.c host/common/syscontrol.c host/common/console.c host/common/tcp_serial.c
GUI_OBJ     = $(patsubst %.cpp,$(BUILD)/gui/%.o,$(GUI_CXX_SRC)) $(patsubst %.c,$(BUILD)/gui/%.o,$(GUI_C_SRC)) \
              $(if $(SYS_HOST_SRC),$(BUILD)/gui/msc/sys_host.o)

gui: $(BUILD)/mod-hw-emulator$(EXE_EXT)

$(BUILD)/gui/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -g -Wall -fPIC -MMD -MP -Iinclude $(QT_CFLAGS) -c $< -o $@

$(BUILD)/gui/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -Wall -fPIC -MMD -MP -Iinclude $(SYS_HOST_CFLAGS) -c $< -o $@

$(BUILD)/gui/msc/sys_host.o: $(SYS_HOST_SRC)
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -fPIC -MMD -MP $(SYS_HOST_CFLAGS) -c $< -o $@

$(BUILD)/mod-hw-emulator$(EXE_EXT): $(GUI_OBJ)
	$(CXX) -o $@ $^ $(QT_LIBS) -pthread $(HOST_LIBS)

# ---------------------------------------------------------------------------------------------------------------

clean:
	rm -rf $(BUILD)

# ---------------------------------------------------------------------------------------------------------------
# tools: mhe-fake-host plays mod-host on /sys_msgs (needs a mod-host checkout for its sys_host.h)

MOD_HOST ?= ../mod-host

tools: $(BUILD)/mhe-fake-host

$(BUILD)/mhe-fake-host: host/tools/mhe-fake-host.c $(MOD_HOST)/src/sys_host.h
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -Wall -I$(MOD_HOST)/src -o $@ $< -pthread

.PHONY: all clean gui tools
