LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
  libbinder \
  liblog \
  libutils \
  libmedia \
  libstagefright \
  libstagefright_foundation \
  libgui \

ifneq ($(TARGET_GE_MARSHMALLOW),)
export SILK_PLAYER_EXTRA_CFLAGS=-DTARGET_GE_MARSHMALLOW
endif

include $(BUILD_NODE_MODULE)
