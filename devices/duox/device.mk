# MOD Duo X: the mod-audio controller, from the sejerpz fork (the deps/mod-duox-controller submodule)

FW_DUOX    ?= deps/mod-duox-controller
FW_REPO     = $(FW_DUOX)
FW_CONFIG   = config-modduox.h
# the displays are UC1701 (GLCD_DRIVER), ks0108.c is the driver of another board
FW_DRIVERS  = actuator.c ledz.c uc1701.c
