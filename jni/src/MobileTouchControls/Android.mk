TOUCHCONTROLS_PATH:= $(call my-dir)/MobileTouchControls


include $(TOUCHCONTROLS_PATH)/sigc++/Android.mk
include $(TOUCHCONTROLS_PATH)/TinyXML/Android.mk
include $(TOUCHCONTROLS_PATH)/libpng/Android.mk
include $(TOUCHCONTROLS_PATH)/libzip/Android.mk

include $(TOUCHCONTROLS_PATH)/Android_TouchControls.mk