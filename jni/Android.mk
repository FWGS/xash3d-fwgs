# override some setup.mk defines

override TARGET_arm_release_CFLAGS :=
override TARGET_thumb_release_CFLAGS :=
override TARGET_arm_debug_CFLAGS :=
override TARGET_thumb_debug_CFLAGS :=

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a-hard)
override TARGET_CFLAGS := $(CFLAGS_OPT) $(CFLAGS_OPT_ARM) $(CFLAGS_HARDFP)
endif

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
override TARGET_CFLAGS := $(CFLAGS_OPT) $(CFLAGS_OPT_ARM)
endif

ifeq ($(TARGET_ARCH_ABI),armeabi)
override TARGET_CFLAGS := $(CFLAGS_OPT) $(CFLAGS_OPT_ARMv5)
endif

ifeq ($(TARGET_ARCH_ABI),x86)
override TARGET_CFLAGS := $(CFLAGS_OPT) $(CFLAGS_OPT_X86)
endif

# Compatibility trick, don't need all projects to be updated

CFLAGS_OPT := 

CFLAGS_OPT_ARM := 

CFLAGS_OPT_ARMv5 := 

CFLAGS_OPT_X86 := 

CFLAGS_HARDFP := 


include $(call all-subdir-makefiles)
