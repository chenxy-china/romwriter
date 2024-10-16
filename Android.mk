LOCAL_PATH:= $(call my-dir)

################### romw #######################
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:=romw

LOCAL_SRC_FILES:= \
	romwriter.cpp
	
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH) \

LOCAL_SHARED_LIBRARIES:= \

LOCAL_STATIC_LIBRARIES := \

LOCAL_CPPFLAGS += -DANDROID

include $(BUILD_EXECUTABLE)
