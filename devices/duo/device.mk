# MOD Duo: the mod-audio controller, from the sejerpz fork (the deps/mod-duo-controller submodule)

FW_DUO     ?= deps/mod-duo-controller
FW_REPO     = $(FW_DUO)
FW_CONFIG   = config-modduo.h
# an LPC1759: the LPC17xx driver library of the firmware
FW_SERIE    = LPC17xx
# the displays are UC1701 (GLCD_DRIVER), ks0108.c is the driver of another board
FW_DRIVERS  = actuator.c led.c uc1701.c
