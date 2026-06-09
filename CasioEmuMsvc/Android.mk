LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

APP_CPPFLAGS += -std=c++20

LOCAL_MODULE := main

SDL_PATH := ../SDL

LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include $(LOCAL_PATH)/../SDK_image/include

# Add your application source files here...
LOCAL_SRC_FILES := src/casioemu.cpp

LOCAL_SHARED_LIBRARIES := SDL2 SDL_image

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lmediandk

include $(BUILD_SHARED_LIBRARY)
