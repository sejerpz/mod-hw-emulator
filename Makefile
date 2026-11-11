# mod-hw-emulator
#
#   make                 firmware module of DEVICE (dwarf by default) and the headless runner
#   make DEVICE=duox     the MOD Duo X instead (one devices/<device>/device.mk per device)
#   make gui             Qt5 user interface (pkg-config Qt5Widgets)
#   make FW_DWARF=path   another checkout of mod-dwarf-controller (default: ../mod-dwarf-controller);
#                        FW_DUOX for the Duo X (default: ../mod-duox-controller)
#   make FW_REF=stls18   a tag, branch or commit of that checkout, without touching it
#   make firmware        only the firmware module
#
# Each firmware build is named after `git describe` of its sources: build/fw-dwarf-stls18.so,
# build/fw-dwarf-stls18-21-g9b9e9f7-dirty.so, … build/fw-<device>.so points to the last one built.
#
# Works with GCC/Clang on Linux and macOS and with MinGW on Windows, as mod-desktop (PawPaw) does.

DEVICE   ?= dwarf
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

# ARCH_FLAGS: for every compile and link, such as -arch arm64 -mmacosx-version-min=10.15 (scripts/package-macos.sh)
ARCH_FLAGS ?=
COMMON_CFLAGS = -D_GNU_SOURCE -std=gnu11 -O2 -g -pthread -fPIC -Wall -MMD -MP $(ARCH_FLAGS)
MODULE_LDFLAGS = -shared -pthread $(ARCH_FLAGS)
ifeq ($(WINDOWS),true)
# the GCC runtimes go in each file; libwinpthread-1.dll stays one, shared by the host and the modules
MODULE_LDFLAGS += -static-libgcc
HOST_LIBS += -static-libgcc -static-libstdc++
endif
ifneq ($(WINDOWS),true)
COMMON_CFLAGS += -fvisibility=hidden
endif

# mod-system-control: its sys_host.c (the mod-host side: labels, leds, widget bitmaps through /sys_msgs) is
# built into the hosts when the checkout is there; over TCP on Windows (MOD_SYS_SERIAL_PORT). It is built for
# one device (_MOD_DEVICE_DWARF...), so once per device, its functions renamed (host/common/msc-shim/sys_host_device.h)
MSC ?= ../mod-system-control
SYS_HOST_DEVICES = dwarf duox
ifneq (,$(wildcard $(MSC)/src/sys_host.c))
SYS_HOST_SRC    = $(MSC)/src/sys_host.c
SYS_HOST_CFLAGS = -DMHE_WITH_SYS_HOST -Ihost/common/msc-shim -I$(MSC)/src
SYS_HOST_OBJ    = $(foreach d,$(SYS_HOST_DEVICES),$(BUILD)/msc/sys_host-$(d).o)
endif

all: firmware $(BUILD)/mhe-run$(EXE_EXT)

# header dependencies of every object built so far
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

# ---------------------------------------------------------------------------------------------------------------
# emulation layer (compiled into every LPC177x module)

EMU_SRC = $(wildcard lpc177x/src/*.c) $(wildcard chips/*.c)

# ---------------------------------------------------------------------------------------------------------------
# firmware module of DEVICE: devices/<device>/device.mk names its sources (FW_REPO, FW_CONFIG, FW_DRIVERS),
# devices/<device>/board.c binds its pins

ifeq ($(wildcard devices/$(DEVICE)/device.mk),)
$(error DEVICE=$(DEVICE): no devices/$(DEVICE)/device.mk)
endif
include devices/$(DEVICE)/device.mk

# FW_REPO is built in place, local changes included (describe ends in -dirty); FW_REF builds one of its
# commits instead, exported once to $(BUILD)/src/ by scripts/export-firmware.sh
FW_REF ?=
ifeq ($(FW_REF),)
MOD_REV := HEAD
MOD_DESCRIBE := $(shell git -C $(FW_REPO) describe --tags --always --dirty 2>/dev/null)
else
MOD_REV := $(FW_REF)^{commit}
# a tag keeps its own name, even when another tag is on the same commit
MOD_REF_TAG := $(shell git -C $(FW_REPO) rev-parse -q --verify refs/tags/$(FW_REF) >/dev/null && echo $(FW_REF))
MOD_DESCRIBE := $(or $(MOD_REF_TAG),$(shell git -C $(FW_REPO) describe --tags --always $(MOD_REV) 2>/dev/null))
ifeq ($(MOD_DESCRIBE),)
$(error FW_REF=$(FW_REF) is not a commit of $(FW_REPO))
endif
endif
MOD_DESCRIBE := $(or $(subst /,_,$(MOD_DESCRIBE)),unknown)
MOD_TAG      := $(or $(MOD_REF_TAG),$(shell git -C $(FW_REPO) describe --tags --abbrev=0 $(MOD_REV) 2>/dev/null || \
                             git -C $(FW_REPO) rev-parse --short $(MOD_REV) 2>/dev/null),unknown)
MOD_COMMIT   := $(shell git -C $(FW_REPO) rev-parse $(MOD_REV) 2>/dev/null)
MOD_DATE     := $(shell git -C $(FW_REPO) log -1 --format=%cI $(MOD_REV) 2>/dev/null)
MOD_GIT_HASH := $(shell git -C $(FW_REPO) rev-parse --short=8 $(MOD_REV) 2>/dev/null)

ifeq ($(FW_REF),)
MOD_SRC = $(FW_REPO)
else
MOD_SRC = $(BUILD)/src/$(DEVICE)-$(MOD_DESCRIBE)
# exported while the Makefile is read: the source lists below are wildcards on it
ifeq ($(wildcard $(MOD_SRC)/.exported),)
ifneq ($(shell sh scripts/export-firmware.sh $(FW_REPO) $(FW_REF) $(MOD_SRC) >&2 && echo ok),ok)
$(error cannot export $(FW_REF) of $(FW_REPO))
endif
endif
endif

MOD_MODULE = $(BUILD)/fw-$(DEVICE)-$(MOD_DESCRIBE)$(SO_EXT)
MOD_BUILD  = $(BUILD)/$(DEVICE)-$(MOD_DESCRIBE)
MOD_STAGE  = $(MOD_BUILD)/stage

# Staged headers. A quoted #include looks first in the directory of the including file, so the CMSIS core,
# the FreeRTOS port and config cannot be replaced through -I: they are replaced in copies of their folders.
# LPC177x_8x.h is wrapped (peripherals the firmware reads as registers); config.h is the device
# configuration the firmware Makefile links for its target.
CMSIS_INC = $(MOD_SRC)/nxp-lpc/CMSISv2p00_LPC177x_8x/inc
$(MOD_STAGE)/.stamp: $(wildcard $(MOD_SRC)/freertos/inc/*.h $(CMSIS_INC)/*.h) \
                     $(wildcard lpc177x/include/*.h) $(MOD_SRC)/app/inc/$(FW_CONFIG)
	@rm -rf $(MOD_STAGE) && mkdir -p $(MOD_STAGE)/freertos $(MOD_STAGE)/cmsis $(MOD_STAGE)/config
	cp $(MOD_SRC)/freertos/inc/*.h $(MOD_STAGE)/freertos/
	mv $(MOD_STAGE)/freertos/FreeRTOSConfig.h $(MOD_STAGE)/freertos/FreeRTOSConfig_fw.h
	cp lpc177x/include/FreeRTOSConfig.h lpc177x/include/portmacro.h $(MOD_STAGE)/freertos/
	cp $(CMSIS_INC)/*.h $(MOD_STAGE)/cmsis/
	mv $(MOD_STAGE)/cmsis/LPC177x_8x.h $(MOD_STAGE)/cmsis/LPC177x_8x_fw.h
	cp lpc177x/include/core_cm3.h lpc177x/include/LPC177x_8x.h $(MOD_STAGE)/cmsis/
	cp $(MOD_SRC)/app/inc/$(FW_CONFIG) $(MOD_STAGE)/config/config.h
	@touch $@

MOD_INC = -I$(MOD_STAGE)/freertos -I$(MOD_STAGE)/cmsis -I$(MOD_STAGE)/config \
          -Ilpc177x/src -Ichips -Iinclude \
          -I$(MOD_SRC)/nxp-lpc \
          -I$(MOD_SRC)/nxp-lpc/LPC177x_8xLib/inc \
          -I$(MOD_SRC)/drivers/inc \
          -I$(MOD_SRC)/app/inc \
          -I$(MOD_SRC)/mod-controller-proto

MOD_CFLAGS = $(COMMON_CFLAGS) $(MOD_INC) -DLPC177x_8x -DCCC_ANALYZER -DMOD_HW_EMULATOR \
             -DVERSION_HASH=\"v.$(MOD_GIT_HASH)\" -fcommon \
             -DMHE_FW_DESCRIBE=\"$(MOD_DESCRIBE)\" -DMHE_FW_TAG=\"$(MOD_TAG)\" \
             -DMHE_FW_COMMIT=\"$(MOD_COMMIT)\" -DMHE_FW_DATE=\"$(MOD_DATE)\"

# the firmware is not ours to clean up: keep its warnings out of the way
FW_WARN = -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
          -Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-address-of-packed-member \
          -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types

MOD_FW_SRC = $(filter-out %/syscalls.c %/main.c,$(wildcard $(MOD_SRC)/app/src/*.c)) \
             $(addprefix $(MOD_SRC)/drivers/src/,$(FW_DRIVERS)) \
             $(addprefix $(MOD_SRC)/freertos/src/,tasks.c queue.c list.c)

MOD_OBJ = $(patsubst $(MOD_SRC)/%.c,$(MOD_BUILD)/fw/%.o,$(MOD_FW_SRC)) \
          $(MOD_BUILD)/gen/main.o \
          $(patsubst %.c,$(MOD_BUILD)/emu/%.o,$(EMU_SRC) devices/$(DEVICE)/board.c)

$(MOD_BUILD)/gen/main.c: $(MOD_SRC)/app/src/main.c scripts/adapt-main.awk
	@mkdir -p $(dir $@)
	$(AWK) -f scripts/adapt-main.awk $< > $@

$(MOD_BUILD)/gen/main.o: $(MOD_BUILD)/gen/main.c | $(MOD_STAGE)/.stamp
	$(CC) $(MOD_CFLAGS) $(FW_WARN) -c $< -o $@

$(MOD_BUILD)/fw/%.o: $(MOD_SRC)/%.c | $(MOD_STAGE)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(MOD_CFLAGS) $(FW_WARN) -c $< -o $@

$(MOD_BUILD)/emu/%.o: %.c | $(MOD_STAGE)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(MOD_CFLAGS) -Wextra -Wno-unused-parameter -c $< -o $@

$(MOD_MODULE): $(MOD_OBJ)
	$(CC) $(MODULE_LDFLAGS) -o $@ $^ -lm

# the unversioned name, for scripts and habits: a link to the last build (a copy on Windows), redone each time
firmware: $(MOD_MODULE)
ifeq ($(WINDOWS),true)
	cp -f $(MOD_MODULE) $(BUILD)/fw-$(DEVICE)$(SO_EXT)
else
	ln -sfn $(notdir $(MOD_MODULE)) $(BUILD)/fw-$(DEVICE)$(SO_EXT)
endif
	@echo "firmware $(DEVICE) $(MOD_DESCRIBE) ($(MOD_COMMIT), $(MOD_DATE)): $(MOD_MODULE)"

# the firmware builds there are
firmwares:
	@ls -1 $(BUILD)/fw-*-*$(SO_EXT) 2>/dev/null || echo "none yet: make, or make FW_REF=<tag>"

# ---------------------------------------------------------------------------------------------------------------
# headless runner

RUN_SRC = host/run/mhe-run.c host/common/module_loader.c host/common/png.c host/common/syscontrol.c \
          host/common/console.c host/common/tcp_serial.c

$(BUILD)/mhe-run$(EXE_EXT): $(RUN_SRC) $(SYS_HOST_OBJ) include/mhe/emu_module.h $(wildcard host/common/*.h)
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -Wall $(ARCH_FLAGS) -Iinclude $(SYS_HOST_CFLAGS) -o $@ $(RUN_SRC) $(SYS_HOST_OBJ) \
	    -pthread $(HOST_LIBS)

# sys_host.c for one device: sys_host-dwarf.o is built with _MOD_DEVICE_DWARF, its functions mhe_sys_host_dwarf_*
$(BUILD)/msc/sys_host-%.o: $(SYS_HOST_SRC) host/common/msc-shim/sys_host_device.h
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -fPIC -MMD -MP $(ARCH_FLAGS) $(SYS_HOST_CFLAGS) \
	    -D_MOD_DEVICE_$(shell echo $* | tr a-z A-Z) -DMHE_SYS_HOST_DEVICE=$* \
	    -include host/common/msc-shim/sys_host_device.h -c $< -o $@

# ---------------------------------------------------------------------------------------------------------------
# user interface (Qt5 Widgets, as shipped by mod-desktop)

PKG_CONFIG ?= pkg-config
ifeq ($(shell $(PKG_CONFIG) --exists Qt5Widgets && echo yes),yes)
QT_CFLAGS = $(shell $(PKG_CONFIG) --cflags Qt5Widgets)
QT_LIBS   = $(shell $(PKG_CONFIG) --libs Qt5Widgets)
else
# PawPaw's Qt for macOS has frameworks and no Qt5Widgets.pc: found as mod-desktop's launcher finds it
QT5_PREFIX = $(shell $(PKG_CONFIG) --variable=prefix Qt5OpenGLExtensions)
QT_CFLAGS  = -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -I$(QT5_PREFIX)/include/qt5 -F$(QT5_PREFIX)/lib \
             $(foreach q,Core Gui Widgets,-I$(QT5_PREFIX)/include/qt5/Qt$(q) -I$(QT5_PREFIX)/lib/Qt$(q).framework/Headers)
ifeq ($(UNAME),Darwin)
QT_LIBS    = -F$(QT5_PREFIX)/lib -framework QtCore -framework QtGui -framework QtWidgets
else
QT_LIBS    = -L$(QT5_PREFIX)/lib -lQt5Core -lQt5Gui -lQt5Widgets
endif
endif

GUI_CXX_SRC = host/gui/main.cpp host/gui/emulator.cpp host/gui/controls.cpp
GUI_C_SRC   = host/common/module_loader.c host/common/syscontrol.c host/common/console.c host/common/tcp_serial.c
GUI_OBJ     = $(patsubst %.cpp,$(BUILD)/gui/%.o,$(GUI_CXX_SRC)) $(patsubst %.c,$(BUILD)/gui/%.o,$(GUI_C_SRC)) \
              $(SYS_HOST_OBJ)

gui: $(BUILD)/mod-hw-emulator$(EXE_EXT)

$(BUILD)/gui/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -g -Wall -fPIC -MMD -MP $(ARCH_FLAGS) -Iinclude $(QT_CFLAGS) -c $< -o $@

$(BUILD)/gui/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -D_GNU_SOURCE -std=gnu11 -O2 -g -Wall -fPIC -MMD -MP $(ARCH_FLAGS) -Iinclude $(SYS_HOST_CFLAGS) -c $< -o $@

$(BUILD)/mod-hw-emulator$(EXE_EXT): $(GUI_OBJ)
	$(CXX) $(ARCH_FLAGS) -o $@ $^ $(QT_LIBS) -pthread $(HOST_LIBS)

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
