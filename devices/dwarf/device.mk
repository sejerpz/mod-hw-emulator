# MOD Dwarf: the aLabs controller with HMI widgets (scripts/fetch-firmware.sh where there is no checkout)

FW_DWARF   ?= ../alabs-mod-custom-images/mod-dwarf-controller
FW_REPO     = $(FW_DWARF)
FW_CONFIG   = config-moddwarf.h
FW_DRIVERS  = actuator.c ledz.c st7565p.c
