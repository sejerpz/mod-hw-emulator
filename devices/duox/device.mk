# MOD Duo X: the mod-audio controller (scripts/fetch-firmware.sh duox clones it into external/)

FW_DUOX    ?= external/mod-duox-controller
FW_REPO     = $(FW_DUOX)
FW_CONFIG   = config-modduox.h
# the displays are UC1701 (GLCD_DRIVER), ks0108.c is the driver of another board
FW_DRIVERS  = actuator.c ledz.c uc1701.c
