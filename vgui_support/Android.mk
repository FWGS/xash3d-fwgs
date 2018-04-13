LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE = vgui_support

include $(XASH3D_CONFIG)

LOCAL_CFLAGS = -fsigned-char -DVGUI_TOUCH_SCROLL -DNO_STL -DXASH_GLES -DINTERNAL_VGUI_SUPPORT -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable

LOCAL_CPPFLAGS = -frtti -fno-exceptions -Wno-write-strings -Wno-invalid-offsetof -std=gnu++98

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../common \
	$(LOCAL_PATH)/../engine \
	$(HLSDK_PATH)/utils/vgui/include \
	$(VGUI_DIR)/include 

LOCAL_SRC_FILES := vgui_clip.cpp vgui_font.cpp vgui_input.cpp vgui_int.cpp vgui_surf.cpp

LOCAL_STATIC_LIBRARIES := vgui

include $(BUILD_STATIC_LIBRARY)
