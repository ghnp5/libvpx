SCALE_SRCS-yes += vpx_scale.mk
SCALE_SRCS-yes += vpx_scale.h
SCALE_SRCS-yes += generic/vpx_scale.c
SCALE_SRCS-$(CONFIG_SPATIAL_RESAMPLING) += generic/gen_scalers.c
SCALE_SRCS-yes += vpx_scale_rtcd.c

SCALE_SRCS-no += $(SCALE_SRCS_REMOVE-yes)

$(eval $(call rtcd_h_template,vpx_scale_rtcd,vpx_scale/vpx_scale_rtcd.sh))
