#
# Makefile for the input core drivers.
#
#TARGET_PRODUCT := full_oppo6765_18511
# Each configuration option enables a list of files.
export TARGET_PRODUCT := full_oppo6765
export OPPO_TARGET_DEVICE := oppo6765
obj-$(CONFIG_INPUT)		+= input-core.o
input-core-y := input.o input-compat.o input-mt.o ff-core.o

obj-$(CONFIG_INPUT_FF_MEMLESS)	+= ff-memless.o
obj-$(CONFIG_INPUT_POLLDEV)	+= input-polldev.o
obj-$(CONFIG_INPUT_SPARSEKMAP)	+= sparse-keymap.o
obj-$(CONFIG_INPUT_MATRIXKMAP)	+= matrix-keymap.o

obj-$(CONFIG_INPUT_LEDS)	+= input-leds.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o
obj-$(CONFIG_INPUT_EVBUG)	+= evbug.o

obj-$(CONFIG_INPUT_KEYBOARD)	+= keyboard/
obj-$(CONFIG_INPUT_MOUSE)	+= mouse/
obj-$(CONFIG_INPUT_JOYSTICK)	+= joystick/
obj-$(CONFIG_INPUT_TABLET)	+= tablet/
obj-$(CONFIG_INPUT_TOUCHSCREEN)	+= touchscreen/
obj-$(CONFIG_INPUT_MISC)	+= misc/
obj-$(CONFIG_MTK_FINGERPRINT_SUPPORT)  += fingerprint/

obj-$(CONFIG_INPUT_APMPOWER)	+= apm-power.o
obj-$(CONFIG_INPUT_KEYRESET)	+= keyreset.o
obj-$(CONFIG_INPUT_KEYCOMBO)	+= keycombo.o

obj-$(CONFIG_RMI4_CORE)		+= rmi4/

#ifdef VENDOR_EDIT
#oujinrong@BSP.Fingerprint.Basic, 2018/12/29, Add for Sec common driver
#obj-$(CONFIG_OPPO_FINGERPRINT)  += oppo_fp_drivers/
obj-$(CONFIG_OPPO_FINGERPRINT)  += oppo_seccommon_drivers/
#endif
