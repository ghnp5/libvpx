/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_COMMON_VP9_RECONINTER_H_
#define VP9_COMMON_VP9_RECONINTER_H_

#include "vpx/vpx_integer.h"
#include "vp9/common/vp9_onyxc_int.h"

struct subpix_fn_table;

void vp9_build_inter_predictors_sby(MACROBLOCKD *xd,
                                    int mb_row,
                                    int mb_col,
                                    BLOCK_SIZE_TYPE bsize);

void vp9_build_inter_predictors_sbuv(MACROBLOCKD *xd,
                                     int mb_row,
                                     int mb_col,
                                     BLOCK_SIZE_TYPE bsize);

void vp9_build_inter_predictors_sb(MACROBLOCKD *mb,
                                   int mb_row, int mb_col,
                                   BLOCK_SIZE_TYPE bsize);

#if CONFIG_MASKED_COMPOUND_INTER
void vp9_build_inter_predictors_singlemv_sby(MACROBLOCKD *mb,
                                             int mb_row, int mb_col,
                                             BLOCK_SIZE_TYPE bsize,
                                             int singlemv,
                                             uint8_t *dst, int dst_stride);
void vp9_build_inter_predictors_singlemv_sbuv(MACROBLOCKD *xd,
                                              int mi_row,
                                              int mi_col,
                                              BLOCK_SIZE_TYPE bsize,
                                              int which_mv,
                                              uint8_t *dstu, int ustride,
                                              uint8_t *dstv, int vstride);
int vp9_get_3mv_mask_y(MACROBLOCKD *xd, int mi_row, int mi_col,
                       BLOCK_SIZE_TYPE bsize,
                       uint8_t *mask, int mask_stride);
#endif

void vp9_setup_interp_filters(MACROBLOCKD *xd,
                              INTERPOLATIONFILTERTYPE filter,
                              VP9_COMMON *cm);

void vp9_setup_scale_factors_for_frame(struct scale_factors *scale,
                                       int other_w, int other_h,
                                       int this_w, int this_h);

void vp9_build_inter_predictor(const uint8_t *src, int src_stride,
                               uint8_t *dst, int dst_stride,
                               const int_mv *mv_q3,
                               const struct scale_factors *scale,
                               int w, int h, int do_avg,
                               const struct subpix_fn_table *subpix);

void vp9_build_inter_predictor_q4(const uint8_t *src, int src_stride,
                                  uint8_t *dst, int dst_stride,
                                  const int_mv *mv_q4,
                                  const struct scale_factors *scale,
                                  int w, int h, int do_avg,
                                  const struct subpix_fn_table *subpix);

static int scale_value_x_with_scaling(int val,
                                      const struct scale_factors *scale) {
  return val * scale->x_num / scale->x_den;
}

static int scale_value_y_with_scaling(int val,
                                      const struct scale_factors *scale) {
  return val * scale->y_num / scale->y_den;
}

static int unscaled_value(int val, const struct scale_factors *scale) {
  (void) scale;
  return val;
}

static int scaled_buffer_offset(int x_offset, int y_offset, int stride,
                                const struct scale_factors *scale) {
  const int x = scale ? scale->scale_value_x(x_offset, scale) : x_offset;
  const int y = scale ? scale->scale_value_y(y_offset, scale) : y_offset;
  return y * stride + x;
}

static void setup_pred_plane(struct buf_2d *dst,
                             uint8_t *src, int stride,
                             int mi_row, int mi_col,
                             const struct scale_factors *scale,
                             int subsampling_x, int subsampling_y) {
  const int x = (MI_SIZE * mi_col) >> subsampling_x;
  const int y = (MI_SIZE * mi_row) >> subsampling_y;
  dst->buf = src + scaled_buffer_offset(x, y, stride, scale);
  dst->stride = stride;
}

// TODO(jkoleszar): audit all uses of this that don't set mb_row, mb_col
static void setup_dst_planes(MACROBLOCKD *xd,
                             const YV12_BUFFER_CONFIG *src,
                             int mi_row, int mi_col) {
  uint8_t *buffers[3] = {src->y_buffer, src->u_buffer, src->v_buffer};
  int strides[3] = {src->y_stride, src->uv_stride, src->uv_stride};
  int i;

  for (i = 0; i < MAX_MB_PLANE; ++i) {
    struct macroblockd_plane *pd = &xd->plane[i];
    setup_pred_plane(&pd->dst, buffers[i], strides[i], mi_row, mi_col, NULL,
                     pd->subsampling_x, pd->subsampling_y);
  }
}

static void setup_pre_planes(MACROBLOCKD *xd,
                             const YV12_BUFFER_CONFIG *src0,
                             const YV12_BUFFER_CONFIG *src1,
                             int mi_row, int mi_col,
                             const struct scale_factors *scale,
                             const struct scale_factors *scale_uv) {
  const YV12_BUFFER_CONFIG *srcs[2] = {src0, src1};
  int i, j;

  for (i = 0; i < 2; ++i) {
    const YV12_BUFFER_CONFIG *src = srcs[i];
    if (src) {
      uint8_t* buffers[3] = {src->y_buffer, src->u_buffer, src->v_buffer};
      int strides[3] = {src->y_stride, src->uv_stride, src->uv_stride};

      for (j = 0; j < MAX_MB_PLANE; ++j) {
        struct macroblockd_plane *pd = &xd->plane[j];
        const struct scale_factors *sf = j ? scale_uv : scale;
        setup_pred_plane(&pd->pre[i],
                         buffers[j], strides[j],
                         mi_row, mi_col, sf ? &sf[i] : NULL,
                         pd->subsampling_x, pd->subsampling_y);
      }
    }
  }
}

static void set_scale_factors(MACROBLOCKD *xd,
    int ref0, int ref1,
    struct scale_factors scale_factor[MAX_REF_FRAMES]) {

  xd->scale_factor[0] = scale_factor[ref0 >= 0 ? ref0 : 0];
  xd->scale_factor[1] = scale_factor[ref1 >= 0 ? ref1 : 0];
  xd->scale_factor_uv[0] = xd->scale_factor[0];
  xd->scale_factor_uv[1] = xd->scale_factor[1];
}

static void set_offsets_with_scaling(struct scale_factors *scale,
                                     int row, int col) {
  const int x_q4 = 16 * col;
  const int y_q4 = 16 * row;

  scale->x_offset_q4 = (x_q4 * scale->x_num / scale->x_den) & 0xf;
  scale->y_offset_q4 = (y_q4 * scale->y_num / scale->y_den) & 0xf;
}

static void set_offsets_without_scaling(struct scale_factors *scale,
                                        int row, int col) {
  scale->x_offset_q4 = 0;
  scale->y_offset_q4 = 0;
}

static int_mv32 motion_vector_q3_to_q4_with_scaling(
    const int_mv *src_mv,
    const struct scale_factors *scale) {
  // returns mv * scale + offset
  int_mv32 result;
  const int32_t mv_row_q4 = src_mv->as_mv.row << 1;
  const int32_t mv_col_q4 = src_mv->as_mv.col << 1;

  /* TODO(jkoleszar): make fixed point, or as a second multiply? */
  result.as_mv.row =  mv_row_q4 * scale->y_num / scale->y_den
                      + scale->y_offset_q4;
  result.as_mv.col =  mv_col_q4 * scale->x_num / scale->x_den
                      + scale->x_offset_q4;
  return result;
}

static int_mv32 motion_vector_q3_to_q4_without_scaling(
    const int_mv *src_mv,
    const struct scale_factors *scale) {
  // returns mv * scale + offset
  int_mv32 result;

  result.as_mv.row = src_mv->as_mv.row << 1;
  result.as_mv.col = src_mv->as_mv.col << 1;
  return result;
}

static int32_t motion_vector_component_q4_with_scaling(int mv_q4,
                                                       int num,
                                                       int den,
                                                       int offset_q4) {
  // returns the scaled and offset value of the mv component.

  /* TODO(jkoleszar): make fixed point, or as a second multiply? */
  return mv_q4 * num / den + offset_q4;
}

static int32_t motion_vector_component_q4_without_scaling(int mv_q4,
                                                          int num,
                                                          int den,
                                                          int offset_q4) {
  // returns the scaled and offset value of the mv component.
  (void)num;
  (void)den;
  (void)offset_q4;
  return mv_q4;
}

#if CONFIG_MASKED_COMPOUND_INTER
void vp9_generate_masked_weight(int mask_index, BLOCK_SIZE_TYPE sb_type,
                                int h, int w, uint8_t *mask, int stride);
void vp9_generate_hard_mask(int mask_index, BLOCK_SIZE_TYPE sb_type,
                            int h, int w, uint8_t *mask, int stride);
#endif
#endif  // VP9_COMMON_VP9_RECONINTER_H_
