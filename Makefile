# mod-hw-emulator
#
#   make                 firmware modules and the headless runner
#   make gui             Qt5 user interface (pkg-config Qt5Widgets)
#   make FW_DWARF=path   another checkout of mod-dwarf-controller (default: the aLabs custom images one)
#   make FW_REF=stls18   a tag, branch or commit of that checkout, without touching it
#   make firmware        only the firmware module
#
# Each firmware build is named after `git describe` of its sources: build/fw-dwarf-stls18.so,
# build/fw-dwarf-stls18-21-g9b9e9f7-dirty.so, … build/fw-dwarf.so points to the last one built.
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
ifeq ($(WINDOWS),true)
# the GCC runtimes go in each file; libwinpthread-1.dll stays one, shared by the host and the modules
MODULE_LDFLAGS += -static-libgcc
HOST_LIBS += -static-libgcc -static-libstdc++
endif
ifneq ($(WINDOWS),true)
COMMON_CFLAGS += -fvisibility=hidden
endif

# mod-system-control: its sys_host.c (the mod-host side: labels, leds, widget bitmaps through /sys_msgs) is
# built into the hosts when the checkout is there; over TCP on Windows (MOD_SYS_SERIAL_PORT)
MSC ?= ../mod-system-control
ifneq (,$(wildcard $(MSC)/src/sys_host.c))
SYS_HOST_SRC    = $(MSC)/src/sys_host.c
SYS_HOST_CFLAGS = -DMHE_WITH_SYS_HOST -D_MOD_DEVICE_DWARF -Ihost/common/msc-shim -I$(MSC)/src
endif

all: firmware $(BUILD)/mhe-run$(EXE_EXT)

# header dependencies of every object built so far
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

# ---------------------------------------------------------------------------------------------------------------
# emulation layer (compiled into every LPC177x module)

EMU_SRC = $(wildcard lpc177x/src/*.c) chips/chip_st7565p.c

# ---------------------------------------------------------------------------------------------------------------
# Dwarf firmware module

# FW_DWARF is built in place, local changes included (describe ends in -dirty); FW_REF builds one of its
# commits instead, exported once to $(BUILD)/src/ by scripts/export-firmware.sh
FW_REF ?=
ifeq ($(FW_REF),)
DWARF_REV := HEAD
DWARF_DESCRIBE := $(shell git -C $(FW_DWARF) describe --tags --always --dirty 2>/dev/null)
else
DWARF_REV := $(FW_REF)^{commit}
# a tag keeps its own name, even when another tag is on the same commit
DWARF_REF_TAG := $(shell git -C $(FW_DWARF) rev-parse -q --verify refs/tags/$(FW_REF) >/dev/null && echo $(FW_REF))
DWARF_DESCRIBE := $(or $(DWARF_REF_TAG),$(shell git -C $(FW_DWARF) describe --tags --always $(DWARF_REV) 2>/dev/null))
ifeq ($(DWARF_DESCRIBE),)
$(error FW_REF=$(FW_REF) is not a commit of $(FW_DWARF))
endif
endif
DWARF_DESCRIBE := $(or $(subst /,_,$(DWARF_DESCRIBE)),unknown)
DWARF_TAG      := $(or $(DWARF_REF_TAG),$(shell git -C $(FW_DWARF) describe --tags --abbrev=0 $(DWARF_REV) 2>/dev/null || \
                               git -C $(FW_DWARF) rev-parse --short $(DWARF_REV) 2>/dev/null),unknown)
DWARF_COMMIT   := $(shell git -C $(FW_DWARF) rev-parse $(DWARF_REV) 2>/dev/null)
DWARF_DATE     := $(shell git -C $(FW_DWARF) log -1 --format=%cI $(DWARF_REV) 2>/dev/null)
DWARF_GIT_HASH := $(shell git -C $(FW_DWARF) rev-parse --short=8 $(DWARF_REV) 2>/dev/null)

ifeq ($(FW_REF),)
DWARF_SRC = $(FW_DWARF)
else
DWARF_SRC = $(BUILD)/src/dwarf-$(DWARF_DESCRIBE)
# exported while the Makefile is read: the source lists below are wildcards on it
ifeq ($(wildcard $(DWARF_SRC)/.exported),)
ifneq ($(shell sh scripts/export-firmware.sh $(FW_DWARF) $(FW_REF) $(DWARF_SRC) >&2 && echo ok),ok)
$(error cannot export $(FW_REF) of $(FW_DWARF))
endif
endif
endif

DWARF_MODULE = $(BUILD)/fw-dwarf-$(DWARF_DESCRIBE)$(SO_EXT)
DWARF_BUILD  = $(BUILD)/dwarf-$(DWARF_DESCRIBE)
DWARF_STAGE  = $(DWARF_BUILD)/stage

# Staged headers. A quoted #include looks first in the directory of the including file, so the CMSIS core,
# the FreeRTOS port and config cannot be replaced through -I: they are replaced in copies of their folders.
# config.h is the device configuration the firmware Makefile links for its target.
$(DWARF_STAGE)/.stamp: $(wildcard $(DWARF_SRC)/freertos/inc/*.h $(DWARF_SRC)/nxp-lpc/CMSISv2p00_LPC177x_8x/inc/*.h) \
                       $(wildcard lpc177x/include/*.h) $(DWARF_SRC)/app/inc/config-moddwarf.h
	@rm -rf $(DWARF_STAGE) && mkdir -p $(DWARF_STAGE)/freertos $(DWARF_STAGE)/cmsis $(DWARF_STAGE)/config
	cp $(DWARF_SRC)/freertos/inc/*.h $(DWARF_STAGE)/freertos/
	mv $(DWARF_STAGE)/freertos/FreeRTOSConfig.h $(DWARF_STAGE)/freertos/FreeRTOSConfig_fw.h
	cp lpc177x/include/FreeRTOSConfig.h lpc177x/include/portmacro.h $(DWARF_STAGE)/freertos/
	cp $(DWARF_SRC)/nxp-lpc/CMSISv2p00_LPC177x_8x/inc/*.h $(DWARF_STAGE)/cmsis/
	cp lpc177x/include/core_cm3.h $(DWARF_STAGE)/cmsis/
	cp $(DWARF_SRC)/app/inc/config-moddwarf.h $(DWARF_STAGE)/config/config.h
	@touch $@

DWARF_INC = -I$(DWARF_STAGE)/freertos -I$(DWARF_STAGE)/cmsis -I$(DWARF_STAGE)/config \
            -Ilpc177x/src -Ichips -Iinclude \
            -I$(DWARF_SRC)/nxp-lpc \
            -I$(DWARF_SRC)/nxp-lpc/LPC177x_8xLib/inc \
            -I$(DWARF_SRC)/drivers/inc \
            -I$(DWARF_SRC)/app/inc \
            -I$(DWARF_SRC)/mod-controller-proto

DWARF_CFLAGS = $(COMMON_CFLAGS) $(DWARF_INC) -DLPC177x_8x -DCCC_ANALYZER -DMOD_HW_EMULATOR \
               -DVERSION_HASH=\"v.$(DWARF_GIT_HASH)\" -fcommon \
               -DMHE_FW_DESCRIBE=\"$(DWARF_DESCRIBE)\" -DMHE_FW_TAG=\"$(DWARF_TAG)\" \
               -DMHE_FW_COMMIT=\"$(DWARF_COMMIT)\" -DMHE_FW_DATE=\"$(DWARF_DATE)\"

# the firmware is not ours to clean up: keep its warnings out of the way
FW_CFLAGS = -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
            -Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-address-of-packed-member \
            -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types

DWARF_FW_SRC = $(filter-out %/syscalls.c %/main.c,$(wildcard $(DWARF_SRC)/app/src/*.c)) \
               $(addprefix $(DWARF_SRC)/drivers/src/,actuator.c ledz.c st7565p.c) \
               $(addprefix $(DWARF_SRC)/freertos/src/,tasks.c queue.c list.c)

DWARF_OBJ = $(patsubst $(DWARF_SRC)/%.c,$(DWARF_BUILD)/fw/%.o,$(DWARF_FW_SRC)) \
            $(DWARF_BUILD)/gen/main.o \
            $(patsubst %.c,$(DWARF_BUILD)/emu/%.o,$(EMU_SRC) devices/dwarf/board.c)

$(DWARF_BUILD)/gen/main.c: $(DWARF_SRC)/app/src/main.c scripts/adapt-main.awk
	@mkdir -p $(dir $@)
	$(AWK) -f scripts/adapt-main.awk $< > $@

$(DWARF_BUILD)/gen/main.o: $(DWARF_BUILD)/gen/main.c | $(DWARF_STAGE)/.stamp
	$(CC) $(DWARF_CFLAGS) $(FW_CFLAGS) -c $< -o $@

$(DWARF_BUILD)/fw/%.o: $(DWARF_SRC)/%.c | $(DWARF_STAGE)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(DWARF_CFLAGS) $(FW_CFLAGS) -c $< -o $@

$(DWARF_BUILD)/emu/%.o: %.c | $(DWARF_STAGE)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(DWARF_CFLAGS) -Wextra -Wno-unused-parameter -c $< -o $@

$(DWARF_MODULE): $(DWARF_OBJ)
	$(CC) $(MODULE_LDFLAGS) -o $@ $^ -lm

# the unversioned name, for scripts and habits: a link to the last build (a copy on Windows), redone each time
firmware: $(DWARF_MODULE)
ifeq ($(WINDOWS),true)
	cp -f $(DWARF_MODULE) $(BUILD)/fw-dwarf$(SO_EXT)
else
	ln -sfn $(notdir $(DWARF_MODULE)) $(BUILD)/fw-dwarf$(SO_EXT)
endif
	@echo "firmware $(DWARF_DESCRIBE) ($(DWARF_COMMIT), $(DWARF_DATE)): $(DWARF_MODULE)"

# the firmware builds there are
firmwares:
	@ls -1 $(BUILD)/fw-*-*$(SO_EXT) 2>/dev/null || echo "none yet: make, or make FW_REF=<tag>"

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

.PHONY: all clean gui tools firmware firmwares
