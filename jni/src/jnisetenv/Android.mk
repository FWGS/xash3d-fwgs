LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := jnisetenv

APP_PLATFORM := android-12

include $(XASH3D_CONFIG)

LOCAL_SRC_FILES := setenv.c

include $(BUILD_SHARED_LIBRARY)
