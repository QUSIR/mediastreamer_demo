LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := ortp
LOCAL_SRC_FILES :=	ortp/avprofile.c \
			ortp/b64.c \
			ortp/event.c \
			ortp/jitterctl.c \
			ortp/ortp.c \
			ortp/payloadtype.c \
			ortp/port.c \
			ortp/posixtimer.c \
			ortp/rtcp.c \
			ortp/rtcpparse.c \
			ortp/rtpparse.c \
			ortp/rtpsession.c \
			ortp/rtpsession_inet.c \
			ortp/rtpsignaltable.c \
			ortp/rtptimer.c \
			ortp/scheduler.c \
			ortp/sessionset.c \
			ortp/srtp.c \
			ortp/str_utils.c \
			ortp/stun.c \
			ortp/stun_udp.c \
			ortp/telephonyevents.c \
			ortp/utils.c
LOCAL_C_INCLUDES += $(LOCAL_PATH)/ortp/ortp
LOCAL_CFLAGS += -DHAVE_CONFIG_H
include $(BUILD_STATIC_LIBRARY)