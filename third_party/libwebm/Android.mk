LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE:= libwebm
LOCAL_CPPFLAGS:=-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS
LOCAL_CPPFLAGS+=-D__STDC_LIMIT_MACROS
LOCAL_C_INCLUDES:= . third_party/libwebm

LOCAL_SRC_FILES:= common/file_util.cc \
                  common/hdr_util.cc \
                  mkvparser/mkvparser.cc \
                  mkvparser/mkvreader.cc \
                  mkvmuxer/mkvmuxer.cc \
                  mkvmuxer/mkvmuxerutil.cc \
                  mkvmuxer/mkvwriter.cc
include $(BUILD_STATIC_LIBRARY)
