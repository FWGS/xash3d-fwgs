LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := gpgs_support

APP_PLATFORM := android-12

include $(XASH3D_CONFIG)

LOCAL_CPPFLAGS += -std=c++11

LOCAL_SRC_FILES := gpgs_support.cpp
LOCAL_STATIC_LIBRARIES := libgpg-1
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)
