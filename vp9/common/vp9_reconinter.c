/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "./vpx_config.h"
#include "vpx/vpx_integer.h"
#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_filter.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_reconintra.h"

void vp9_setup_scale_factors_for_frame(struct scale_factors *scale,
                                       int other_w, int other_h,
                                       int this_w, int this_h) {
  scale->x_num = other_w;
  scale->x_den = this_w;
  scale->x_offset_q4 = 0;  // calculated per-mb
  scale->x_step_q4 = 16 * other_w / this_w;

  scale->y_num = other_h;
  scale->y_den = this_h;
  scale->y_offset_q4 = 0;  // calculated per-mb
  scale->y_step_q4 = 16 * other_h / this_h;

  if (scale->x_num == scale->x_den && scale->y_num == scale->y_den) {
    scale->scale_value_x = unscaled_value;
    scale->scale_value_y = unscaled_value;
    scale->set_scaled_offsets = set_offsets_without_scaling;
    scale->scale_motion_vector_q3_to_q4 =
        motion_vector_q3_to_q4_without_scaling;
    scale->scale_motion_vector_component_q4 =
        motion_vector_component_q4_without_scaling;
  } else {
    scale->scale_value_x = scale_value_x_with_scaling;
    scale->scale_value_y = scale_value_y_with_scaling;
    scale->set_scaled_offsets = set_offsets_with_scaling;
    scale->scale_motion_vector_q3_to_q4 =
        motion_vector_q3_to_q4_with_scaling;
    scale->scale_motion_vector_component_q4 =
        motion_vector_component_q4_with_scaling;
  }

  // TODO(agrange): Investigate the best choice of functions to use here
  // for EIGHTTAP_SMOOTH. Since it is not interpolating, need to choose what
  // to do at full-pel offsets. The current selection, where the filter is
  // applied in one direction only, and not at all for 0,0, seems to give the
  // best quality, but it may be worth trying an additional mode that does
  // do the filtering on full-pel.
  if (scale->x_step_q4 == 16) {
    if (scale->y_step_q4 == 16) {
      // No scaling in either direction.
      scale->predict[0][0][0] = vp9_convolve_copy;
      scale->predict[0][0][1] = vp9_convolve_avg;
      scale->predict[0][1][0] = vp9_convolve8_vert;
      scale->predict[0][1][1] = vp9_convolve8_avg_vert;
      scale->predict[1][0][0] = vp9_convolve8_horiz;
      scale->predict[1][0][1] = vp9_convolve8_avg_horiz;
    } else {
      // No scaling in x direction. Must always scale in the y direction.
      scale->predict[0][0][0] = vp9_convolve8_vert;
      scale->predict[0][0][1] = vp9_convolve8_avg_vert;
      scale->predict[0][1][0] = vp9_convolve8_vert;
      scale->predict[0][1][1] = vp9_convolve8_avg_vert;
      scale->predict[1][0][0] = vp9_convolve8;
      scale->predict[1][0][1] = vp9_convolve8_avg;
    }
  } else {
    if (scale->y_step_q4 == 16) {
      // No scaling in the y direction. Must always scale in the x direction.
      scale->predict[0][0][0] = vp9_convolve8_horiz;
      scale->predict[0][0][1] = vp9_convolve8_avg_horiz;
      scale->predict[0][1][0] = vp9_convolve8;
      scale->predict[0][1][1] = vp9_convolve8_avg;
      scale->predict[1][0][0] = vp9_convolve8_horiz;
      scale->predict[1][0][1] = vp9_convolve8_avg_horiz;
    } else {
      // Must always scale in both directions.
      scale->predict[0][0][0] = vp9_convolve8;
      scale->predict[0][0][1] = vp9_convolve8_avg;
      scale->predict[0][1][0] = vp9_convolve8;
      scale->predict[0][1][1] = vp9_convolve8_avg;
      scale->predict[1][0][0] = vp9_convolve8;
      scale->predict[1][0][1] = vp9_convolve8_avg;
    }
  }
  // 2D subpel motion always gets filtered in both directions
  scale->predict[1][1][0] = vp9_convolve8;
  scale->predict[1][1][1] = vp9_convolve8_avg;
}

void vp9_setup_interp_filters(MACROBLOCKD *xd,
                              INTERPOLATIONFILTERTYPE mcomp_filter_type,
                              VP9_COMMON *cm) {
  if (xd->mode_info_context) {
    MB_MODE_INFO *mbmi = &xd->mode_info_context->mbmi;

    set_scale_factors(xd,
                      mbmi->ref_frame - 1,
                      mbmi->second_ref_frame - 1,
                      cm->active_ref_scale);
  }

  switch (mcomp_filter_type) {
    case EIGHTTAP:
    case SWITCHABLE:
      xd->subpix.filter_x = xd->subpix.filter_y = vp9_sub_pel_filters_8;
      break;
    case EIGHTTAP_SMOOTH:
      xd->subpix.filter_x = xd->subpix.filter_y = vp9_sub_pel_filters_8lp;
      break;
    case EIGHTTAP_SHARP:
      xd->subpix.filter_x = xd->subpix.filter_y = vp9_sub_pel_filters_8s;
      break;
    case BILINEAR:
      xd->subpix.filter_x = xd->subpix.filter_y = vp9_bilinear_filters;
      break;
  }
  assert(((intptr_t)xd->subpix.filter_x & 0xff) == 0);
}

void vp9_copy_mem16x16_c(const uint8_t *src,
                         int src_stride,
                         uint8_t *dst,
                         int dst_stride) {
  int r;

  for (r = 0; r < 16; r++) {
#if !(CONFIG_FAST_UNALIGNED)
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
    dst[8] = src[8];
    dst[9] = src[9];
    dst[10] = src[10];
    dst[11] = src[11];
    dst[12] = src[12];
    dst[13] = src[13];
    dst[14] = src[14];
    dst[15] = src[15];

#else
    ((uint32_t *)dst)[0] = ((const uint32_t *)src)[0];
    ((uint32_t *)dst)[1] = ((const uint32_t *)src)[1];
    ((uint32_t *)dst)[2] = ((const uint32_t *)src)[2];
    ((uint32_t *)dst)[3] = ((const uint32_t *)src)[3];

#endif
    src += src_stride;
    dst += dst_stride;
  }
}

void vp9_copy_mem8x8_c(const uint8_t *src,
                       int src_stride,
                       uint8_t *dst,
                       int dst_stride) {
  int r;

  for (r = 0; r < 8; r++) {
#if !(CONFIG_FAST_UNALIGNED)
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
#else
    ((uint32_t *)dst)[0] = ((const uint32_t *)src)[0];
    ((uint32_t *)dst)[1] = ((const uint32_t *)src)[1];
#endif
    src += src_stride;
    dst += dst_stride;
  }
}

void vp9_copy_mem8x4_c(const uint8_t *src,
                       int src_stride,
                       uint8_t *dst,
                       int dst_stride) {
  int r;

  for (r = 0; r < 4; r++) {
#if !(CONFIG_FAST_UNALIGNED)
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
#else
    ((uint32_t *)dst)[0] = ((const uint32_t *)src)[0];
    ((uint32_t *)dst)[1] = ((const uint32_t *)src)[1];
#endif
    src += src_stride;
    dst += dst_stride;
  }
}

void vp9_build_inter_predictor(const uint8_t *src, int src_stride,
                               uint8_t *dst, int dst_stride,
                               const int_mv *mv_q3,
                               const struct scale_factors *scale,
                               int w, int h, int weight,
                               const struct subpix_fn_table *subpix) {
  int_mv32 mv = scale->scale_motion_vector_q3_to_q4(mv_q3, scale);
  src += (mv.as_mv.row >> 4) * src_stride + (mv.as_mv.col >> 4);
  scale->predict[!!(mv.as_mv.col & 15)][!!(mv.as_mv.row & 15)][weight](
      src, src_stride, dst, dst_stride,
      subpix->filter_x[mv.as_mv.col & 15], scale->x_step_q4,
      subpix->filter_y[mv.as_mv.row & 15], scale->y_step_q4,
      w, h);
}

void vp9_build_inter_predictor_q4(const uint8_t *src, int src_stride,
                                  uint8_t *dst, int dst_stride,
                                  const int_mv *mv_q4,
                                  const struct scale_factors *scale,
                                  int w, int h, int weight,
                                  const struct subpix_fn_table *subpix) {
  const int scaled_mv_row_q4 =
      scale->scale_motion_vector_component_q4(mv_q4->as_mv.row,
                                              scale->y_num, scale->y_den,
                                              scale->y_offset_q4);
  const int scaled_mv_col_q4 =
      scale->scale_motion_vector_component_q4(mv_q4->as_mv.col,
                                              scale->x_num, scale->x_den,
                                              scale->x_offset_q4);
  const int subpel_x = scaled_mv_col_q4 & 15;
  const int subpel_y = scaled_mv_row_q4 & 15;

  src += (scaled_mv_row_q4 >> 4) * src_stride + (scaled_mv_col_q4 >> 4);
  scale->predict[!!subpel_x][!!subpel_y][weight](
      src, src_stride, dst, dst_stride,
      subpix->filter_x[subpel_x], scale->x_step_q4,
      subpix->filter_y[subpel_y], scale->y_step_q4,
      w, h);
}

static INLINE int round_mv_comp_q4(int value) {
  return (value < 0 ? value - 2 : value + 2) / 4;
}

#define IDX1 2
#define IDX2 3

static int mi_mv_pred_row_q4(MACROBLOCKD *mb, int off, int idx) {
  const int temp = mb->mode_info_context->bmi[off + 0].as_mv[idx].as_mv.row +
                   mb->mode_info_context->bmi[off + 1].as_mv[idx].as_mv.row +
                   mb->mode_info_context->bmi[off + IDX1].as_mv[idx].as_mv.row +
                   mb->mode_info_context->bmi[off + IDX2].as_mv[idx].as_mv.row;
  return round_mv_comp_q4(temp);
}

static int mi_mv_pred_col_q4(MACROBLOCKD *mb, int off, int idx) {
  const int temp = mb->mode_info_context->bmi[off + 0].as_mv[idx].as_mv.col +
                   mb->mode_info_context->bmi[off + 1].as_mv[idx].as_mv.col +
                   mb->mode_info_context->bmi[off + IDX1].as_mv[idx].as_mv.col +
                   mb->mode_info_context->bmi[off + IDX2].as_mv[idx].as_mv.col;
  return round_mv_comp_q4(temp);
}

// TODO(jkoleszar): yet another mv clamping function :-(
MV clamp_mv_to_umv_border_sb(const MV *src_mv,
    int bwl, int bhl, int ss_x, int ss_y,
    int mb_to_left_edge, int mb_to_top_edge,
    int mb_to_right_edge, int mb_to_bottom_edge) {
  /* If the MV points so far into the UMV border that no visible pixels
   * are used for reconstruction, the subpel part of the MV can be
   * discarded and the MV limited to 16 pixels with equivalent results.
   */
  const int spel_left = (VP9_INTERP_EXTEND + (4 << bwl)) << 4;
  const int spel_right = spel_left - (1 << 4);
  const int spel_top = (VP9_INTERP_EXTEND + (4 << bhl)) << 4;
  const int spel_bottom = spel_top - (1 << 4);
  MV clamped_mv;

  assert(ss_x <= 1);
  assert(ss_y <= 1);
  clamped_mv.col = clamp(src_mv->col << (1 - ss_x),
                         (mb_to_left_edge << (1 - ss_x)) - spel_left,
                         (mb_to_right_edge << (1 - ss_x)) + spel_right);
  clamped_mv.row = clamp(src_mv->row << (1 - ss_y),
                         (mb_to_top_edge << (1 - ss_y)) - spel_top,
                         (mb_to_bottom_edge << (1 - ss_y)) + spel_bottom);
  return clamped_mv;
}

#if CONFIG_MASKED_COMPOUND_INTER

#define MASK_WEIGHT_BITS  6

#if USE_3MV_MASK
static int get_3mv_mask(uint8_t *dst1, int dst1_stride,
                        uint8_t *dst2, int dst2_stride,
                        uint8_t *mask, int mask_stride,
                        int plane, int h, int w) {
  int thresh, count;
  if (plane == 0)
    thresh = 96;
  else
    thresh = 64;
  do {
    int i, j;
    count = 0;
    for (i = 0; i < h; ++i)
      for (j = 0; j < w; ++j) {
        int diff = abs(dst1[i * dst1_stride + j] - dst2[i * dst2_stride + j]);
        mask[i * mask_stride + j] = (diff > thresh);
        count += mask[i * mask_stride + j];
      }
    thresh >>= 1;
  } while (count < h * w / 4 && thresh >= 8);
  // TODO(debargha): Morphological close ?
  return count;
}

#else

static int get_masked_weight(int m) {
#define SMOOTHER_LEN  16
  static const uint8_t smoothfn[2 * SMOOTHER_LEN + 1] = {
    1,  1,  1,  1,  2,  2,  3,  4,  5,  7,  9, 12, 15, 19, 23, 27,
    32, 37, 41, 45, 49, 52, 55, 57, 59, 60, 61, 62, 62, 63, 63, 63, 63,
  };
  if (m < -SMOOTHER_LEN)
    return 0;
  else if (m > SMOOTHER_LEN)
    return (1 << MASK_WEIGHT_BITS);
  else
    return smoothfn[m + SMOOTHER_LEN];
}

static int get_hard_mask(int m) {
  return m > 0;
}

// Equation of line: a[0]*(x - a[2]*w/4) + a[1]*(y - a[3]*h/4)
// a[2] and a[3] must be one of 0, 1, 2, 3, 4
static const int mask_params_sml[1 << MASK_BITS_SML][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},
};

static const int mask_params_med_hgtw[1 << MASK_BITS_MED][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},

  {-1,  2, 2, 1},
  { 1, -2, 2, 1},
  {-1,  2, 2, 3},
  { 1, -2, 2, 3},
  { 1,  2, 2, 1},
  {-1, -2, 2, 1},
  { 1,  2, 2, 3},
  {-1, -2, 2, 3},
};

static const int mask_params_med_hltw[1 << MASK_BITS_MED][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},

  {-2,  1, 1, 2},
  { 2, -1, 1, 2},
  {-2,  1, 3, 2},
  { 2, -1, 3, 2},
  { 2,  1, 1, 2},
  {-2, -1, 1, 2},
  { 2,  1, 3, 2},
  {-2, -1, 3, 2},
};

static const int mask_params_med_heqw[1 << MASK_BITS_MED][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},

  { 0,  2, 0, 1},
  { 0, -2, 0, 1},
  { 0,  2, 0, 3},
  { 0, -2, 0, 3},
  { 2,  0, 1, 0},
  {-2,  0, 1, 0},
  { 2,  0, 3, 0},
  {-2,  0, 3, 0},
};

static const int mask_params_big_hgtw[1 << MASK_BITS_BIG][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},

  {-1,  2, 2, 1},
  { 1, -2, 2, 1},
  {-1,  2, 2, 3},
  { 1, -2, 2, 3},
  { 1,  2, 2, 1},
  {-1, -2, 2, 1},
  { 1,  2, 2, 3},
  {-1, -2, 2, 3},

  {-2,  1, 1, 2},
  { 2, -1, 1, 2},
  {-2,  1, 3, 2},
  { 2, -1, 3, 2},
  { 2,  1, 1, 2},
  {-2, -1, 1, 2},
  { 2,  1, 3, 2},
  {-2, -1, 3, 2},

  { 0,  2, 0, 1},
  { 0, -2, 0, 1},
  { 0,  2, 0, 2},
  { 0, -2, 0, 2},
  { 0,  2, 0, 3},
  { 0, -2, 0, 3},
  { 2,  0, 2, 0},
  {-2,  0, 2, 0},
};

static const int mask_params_big_hltw[1 << MASK_BITS_BIG][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},

  {-1,  2, 2, 1},
  { 1, -2, 2, 1},
  {-1,  2, 2, 3},
  { 1, -2, 2, 3},
  { 1,  2, 2, 1},
  {-1, -2, 2, 1},
  { 1,  2, 2, 3},
  {-1, -2, 2, 3},

  {-2,  1, 1, 2},
  { 2, -1, 1, 2},
  {-2,  1, 3, 2},
  { 2, -1, 3, 2},
  { 2,  1, 1, 2},
  {-2, -1, 1, 2},
  { 2,  1, 3, 2},
  {-2, -1, 3, 2},

  { 0,  2, 0, 2},
  { 0, -2, 0, 2},
  { 2,  0, 1, 0},
  {-2,  0, 1, 0},
  { 2,  0, 2, 0},
  {-2,  0, 2, 0},
  { 2,  0, 3, 0},
  {-2,  0, 3, 0},
};

static const int mask_params_big_heqw[1 << MASK_BITS_BIG][4] = {
  {-1,  2, 2, 2},
  { 1, -2, 2, 2},
  {-2,  1, 2, 2},
  { 2, -1, 2, 2},
  { 2,  1, 2, 2},
  {-2, -1, 2, 2},
  { 1,  2, 2, 2},
  {-1, -2, 2, 2},

  {-1,  2, 2, 1},
  { 1, -2, 2, 1},
  {-1,  2, 2, 3},
  { 1, -2, 2, 3},
  { 1,  2, 2, 1},
  {-1, -2, 2, 1},
  { 1,  2, 2, 3},
  {-1, -2, 2, 3},

  {-2,  1, 1, 2},
  { 2, -1, 1, 2},
  {-2,  1, 3, 2},
  { 2, -1, 3, 2},
  { 2,  1, 1, 2},
  {-2, -1, 1, 2},
  { 2,  1, 3, 2},
  {-2, -1, 3, 2},

  { 0,  2, 0, 1},
  { 0, -2, 0, 1},
  { 0,  2, 0, 3},
  { 0, -2, 0, 3},
  { 2,  0, 1, 0},
  {-2,  0, 1, 0},
  { 2,  0, 3, 0},
  {-2,  0, 3, 0},
};

static const int *get_mask_params(int mask_index,
                                  BLOCK_SIZE_TYPE sb_type,
                                  int h, int w) {
  const int *a;
  const int mask_bits = get_mask_bits(sb_type);

  if (mask_index == MASK_NONE)
    return NULL;

  if (mask_bits == MASK_BITS_SML) {
    a = mask_params_sml[mask_index];
  } else if (mask_bits == MASK_BITS_MED) {
    if (h > w)
      a = mask_params_med_hgtw[mask_index];
    else if (h < w)
      a = mask_params_med_hltw[mask_index];
    else
      a = mask_params_med_heqw[mask_index];
  } else if (mask_bits == MASK_BITS_BIG) {
    if (h > w)
      a = mask_params_big_hgtw[mask_index];
    else if (h < w)
      a = mask_params_big_hltw[mask_index];
    else
      a = mask_params_big_heqw[mask_index];
  } else {
    assert(0);
  }
  return a;
}

void vp9_generate_masked_weight(int mask_index,
                                BLOCK_SIZE_TYPE sb_type,
                                int h, int w,
                                uint8_t *mask, int stride) {
  int i, j;
  const int *a = get_mask_params(mask_index, sb_type, h, w);
  if (!a) return;
  for (i = 0; i < h; ++i)
    for (j = 0; j < w; ++j) {
      int x = (j - (a[2] * w) / 4);
      int y = (i - (a[3] * h) / 4);
      int m = a[0] * x + a[1] * y;
      mask[i * stride + j] = get_masked_weight(m);
    }
}

void vp9_generate_hard_mask(int mask_index, BLOCK_SIZE_TYPE sb_type,
                            int h, int w, uint8_t *mask, int stride) {
  int i, j;
  const int *a = get_mask_params(mask_index, sb_type, h, w);
  if (!a) return;
  for (i = 0; i < h; ++i)
    for (j = 0; j < w; ++j) {
      int x = (j - (a[2] * w) / 4);
      int y = (i - (a[3] * h) / 4);
      int m = a[0] * x + a[1] * y;
      mask[i * stride + j] = get_hard_mask(m);
    }
}
#endif  // USE_3MV_MASK

static void build_masked_compound(uint8_t *dst, int dst_stride,
                                  uint8_t *dst2, int dst2_stride,
                                  uint8_t *mask, int mask_stride,
                                  int h, int w) {
  int i, j;
  for (i = 0; i < h; ++i)
    for (j = 0; j < w; ++j) {
      int m = mask[i * mask_stride + j];
      dst[i * dst_stride + j] = (dst[i * dst_stride + j] * m +
                                 dst2[i * dst2_stride + j] *
                                 ((1 << MASK_WEIGHT_BITS) - m) +
                                 (1 << (MASK_WEIGHT_BITS - 1))) >>
                                 MASK_WEIGHT_BITS;
    }
}
#endif  // CONFIG_MASKED_COMPOUND_INTER

struct build_inter_predictors_args {
  MACROBLOCKD *xd;
  int x;
  int y;
  uint8_t* dst[MAX_MB_PLANE];
  int dst_stride[MAX_MB_PLANE];
  uint8_t* pre[2][MAX_MB_PLANE];
  int pre_stride[2][MAX_MB_PLANE];
  int singlemv_used;
};
static void build_inter_predictors(int plane, int block,
                                   BLOCK_SIZE_TYPE bsize,
                                   int pred_w, int pred_h,
                                   void *argv) {
  const struct build_inter_predictors_args* const arg = argv;
  MACROBLOCKD * const xd = arg->xd;
  const int bwl = b_width_log2(bsize) - xd->plane[plane].subsampling_x;
  const int bhl = b_height_log2(bsize) - xd->plane[plane].subsampling_y;
  const int bh = 4 << bhl,  bw = 4 << bwl;
  const int x_idx = block & ((1 << bwl) - 1), y_idx = block >> bwl;
  const int x = x_idx * 4, y = y_idx * 4;
  const int use_second_ref = xd->mode_info_context->mbmi.second_ref_frame > 0;
  int which_mv;

  assert(x < bw);
  assert(y < bh);
  assert(xd->mode_info_context->mbmi.mode == SPLITMV || 4 << pred_w == bw);
  assert(xd->mode_info_context->mbmi.mode == SPLITMV || 4 << pred_h == bh);

  for (which_mv = 0; which_mv < 1 + use_second_ref; ++which_mv) {
    // source
    const uint8_t * const base_pre = arg->pre[which_mv][plane];
    const int pre_stride = arg->pre_stride[which_mv][plane];
    const uint8_t *const pre = base_pre +
        scaled_buffer_offset(x, y, pre_stride, &xd->scale_factor[which_mv]);
    struct scale_factors * const scale =
        plane == 0 ? &xd->scale_factor[which_mv] :
                 &xd->scale_factor_uv[which_mv];

    // dest
    uint8_t *const dst = arg->dst[plane] + arg->dst_stride[plane] * y + x;

    // motion vector
    const MV *mv;
    MV split_chroma_mv;
    int_mv clamped_mv;

    if (xd->mode_info_context->mbmi.mode == SPLITMV) {
      if (plane == 0) {
        mv = &xd->mode_info_context->bmi[block].as_mv[which_mv].as_mv;
      } else {
        const int y_block = (block & 2) * 4 + (block & 1) * 2;
        split_chroma_mv.row = mi_mv_pred_row_q4(xd, y_block, which_mv);
        split_chroma_mv.col = mi_mv_pred_col_q4(xd, y_block, which_mv);
        mv = &split_chroma_mv;
      }
    } else {
      mv = &xd->mode_info_context->mbmi.mv[which_mv].as_mv;
    }

    /* TODO(jkoleszar): This clamping is done in the incorrect place for the
     * scaling case. It needs to be done on the scaled MV, not the pre-scaling
     * MV. Note however that it performs the subsampling aware scaling so
     * that the result is always q4.
     */
    clamped_mv.as_mv = clamp_mv_to_umv_border_sb(mv, bwl, bhl,
                                                 xd->plane[plane].subsampling_x,
                                                 xd->plane[plane].subsampling_y,
                                                 xd->mb_to_left_edge,
                                                 xd->mb_to_top_edge,
                                                 xd->mb_to_right_edge,
                                                 xd->mb_to_bottom_edge);
    scale->set_scaled_offsets(scale, arg->y + y, arg->x + x);

#if CONFIG_MASKED_COMPOUND_INTER
    if (xd->mode_info_context->mbmi.use_masked_compound && which_mv) {
      uint8_t tmp_dst[4096];
      uint8_t mask[4096];

      assert(xd->mode_info_context->mbmi.mode != SPLITMV);
      vp9_build_inter_predictor_q4(pre, pre_stride,
                                   tmp_dst, 64,
                                   &clamped_mv, &xd->scale_factor[which_mv],
                                   4 << pred_w, 4 << pred_h, 0,
                                   &xd->subpix);
#if USE_3MV_MASK
      get_3mv_mask(dst, arg->dst_stride[plane], tmp_dst, 64, mask, 64,
                   plane, (4 << pred_h), (4 << pred_w));
      mv = &xd->mode_info_context->mbmi.mv3.as_mv;
      clamped_mv.as_mv = clamp_mv_to_umv_border_sb(
          mv, bwl, bhl,
          xd->plane[plane].subsampling_x,
          xd->plane[plane].subsampling_y,
          xd->mb_to_left_edge,
          xd->mb_to_top_edge,
          xd->mb_to_right_edge,
          xd->mb_to_bottom_edge);
      vp9_build_inter_predictor_q4(pre, pre_stride,
                                   tmp_dst, 64,
                                   &clamped_mv, &xd->scale_factor[which_mv],
                                   4 << pred_w, 4 << pred_h, 0,
                                   &xd->subpix);
#else
      vp9_generate_masked_weight(xd->mode_info_context->mbmi.mask_index,
                                 xd->mode_info_context->mbmi.sb_type,
                                 (4 << pred_h), (4 << pred_w),
                                 mask, 64);
#endif

      build_masked_compound(dst, arg->dst_stride[plane], tmp_dst, 64, mask, 64,
                            (4 << pred_h), (4 << pred_w));
    } else {
#endif  // CONFIG_MASKED_COMPOUND_INTER
      vp9_build_inter_predictor_q4(pre, pre_stride,
                                   dst, arg->dst_stride[plane],
                                   &clamped_mv, &xd->scale_factor[which_mv],
                                   4 << pred_w, 4 << pred_h, which_mv,
                                   &xd->subpix);
#if CONFIG_MASKED_COMPOUND_INTER
    }
#endif
  }
}

void vp9_build_inter_predictors_sby(MACROBLOCKD *xd,
                                    int mi_row,
                                    int mi_col,
                                    BLOCK_SIZE_TYPE bsize) {
  struct build_inter_predictors_args args = {
    xd, mi_col * MI_SIZE, mi_row * MI_SIZE,
    {xd->plane[0].dst.buf, NULL, NULL}, {xd->plane[0].dst.stride, 0, 0},
    {{xd->plane[0].pre[0].buf, NULL, NULL},
      {xd->plane[0].pre[1].buf, NULL, NULL}},
    {{xd->plane[0].pre[0].stride, 0, 0}, {xd->plane[0].pre[1].stride, 0, 0}},
    -1,
  };

  foreach_predicted_block_in_plane(xd, bsize, 0, build_inter_predictors, &args);
}

void vp9_build_inter_predictors_sbuv(MACROBLOCKD *xd,
                                     int mi_row,
                                     int mi_col,
                                     BLOCK_SIZE_TYPE bsize) {
  struct build_inter_predictors_args args = {
    xd, mi_col * MI_SIZE, mi_row * MI_SIZE,
    {NULL, xd->plane[1].dst.buf, xd->plane[2].dst.buf},
    {0, xd->plane[1].dst.stride, xd->plane[1].dst.stride},
    {{NULL, xd->plane[1].pre[0].buf, xd->plane[2].pre[0].buf},
      {NULL, xd->plane[1].pre[1].buf, xd->plane[2].pre[1].buf}},
    {{0, xd->plane[1].pre[0].stride, xd->plane[1].pre[0].stride},
      {0, xd->plane[1].pre[1].stride, xd->plane[1].pre[1].stride}},
    -1,
  };
  foreach_predicted_block_uv(xd, bsize, build_inter_predictors, &args);
}
void vp9_build_inter_predictors_sb(MACROBLOCKD *xd,
                                   int mi_row, int mi_col,
                                   BLOCK_SIZE_TYPE bsize) {

  vp9_build_inter_predictors_sby(xd, mi_row, mi_col, bsize);
  vp9_build_inter_predictors_sbuv(xd, mi_row, mi_col, bsize);
}

/*encoder only*/
void vp9_build_inter4x4_predictors_mbuv(MACROBLOCKD *xd,
                                        int mb_row, int mb_col) {
  vp9_build_inter_predictors_sbuv(xd, mb_row, mb_col,
                                  BLOCK_SIZE_MB16X16);
}

static void build_inter_predictors_singlemv(int plane, int block,
                                            BLOCK_SIZE_TYPE bsize,
                                            int pred_w, int pred_h,
                                            void *argv) {
  const struct build_inter_predictors_args* const arg = argv;
  MACROBLOCKD * const xd = arg->xd;
  const int bwl = b_width_log2(bsize) - xd->plane[plane].subsampling_x;
  const int bhl = b_height_log2(bsize) - xd->plane[plane].subsampling_y;
  const int bh = 4 << bhl,  bw = 4 << bwl;
  const int x_idx = block & ((1 << bwl) - 1), y_idx = block >> bwl;
  const int x = x_idx * 4, y = y_idx * 4;
  const int use_second_ref = xd->mode_info_context->mbmi.second_ref_frame > 0;
#if USE_3MV_MASK
  int use_3mv = (arg->singlemv_used == 2 ? 1 : 0);
#endif
  int which_mv = arg->singlemv_used == 2 ? 1 : arg->singlemv_used;

  const uint8_t * const base_pre = arg->pre[which_mv][plane];
  const int pre_stride = arg->pre_stride[which_mv][plane];
  const uint8_t *const pre = base_pre +
      scaled_buffer_offset(x, y, pre_stride, &xd->scale_factor[which_mv]);
  struct scale_factors * const scale =
      plane == 0 ? &xd->scale_factor[which_mv] :
      &xd->scale_factor_uv[which_mv];

  // dest
  uint8_t *const dst = arg->dst[plane] + arg->dst_stride[plane] * y + x;
  // motion vector
  const MV *mv;
  MV split_chroma_mv;
  int_mv clamped_mv;

  assert(x < bw);
  assert(y < bh);
  assert(xd->mode_info_context->mbmi.mode == SPLITMV || 4 << pred_w == bw);
  assert(xd->mode_info_context->mbmi.mode == SPLITMV || 4 << pred_h == bh);
  assert(use_second_ref);

  if (xd->mode_info_context->mbmi.mode == SPLITMV) {
#if USE_3MV_MASK
    assert(use_3mv == 0);
#endif
    if (plane == 0) {
      mv = &xd->mode_info_context->bmi[block].as_mv[which_mv].as_mv;
    } else {
      const int y_block = (block & 2) * 4 + (block & 1) * 2;
      split_chroma_mv.row = mi_mv_pred_row_q4(xd, y_block, which_mv);
      split_chroma_mv.col = mi_mv_pred_col_q4(xd, y_block, which_mv);
      mv = &split_chroma_mv;
    }
  } else {
#if USE_3MV_MASK
    if (use_3mv)
      mv = &xd->mode_info_context->mbmi.mv3.as_mv;
    else
      mv = &xd->mode_info_context->mbmi.mv[which_mv].as_mv;
#else
    mv = &xd->mode_info_context->mbmi.mv[which_mv].as_mv;
#endif
  }
  clamped_mv.as_mv = clamp_mv_to_umv_border_sb(mv, bwl, bhl,
                                               xd->plane[plane].subsampling_x,
                                               xd->plane[plane].subsampling_y,
                                               xd->mb_to_left_edge,
                                               xd->mb_to_top_edge,
                                               xd->mb_to_right_edge,
                                               xd->mb_to_bottom_edge);
  scale->set_scaled_offsets(scale, arg->y + y, arg->x + x);

  vp9_build_inter_predictor_q4(pre, pre_stride,
                               dst, arg->dst_stride[plane],
                               &clamped_mv, &xd->scale_factor[which_mv],
                               4 << pred_w, 4 << pred_h, 0,
                               &xd->subpix);
}

void vp9_build_inter_predictors_singlemv_sby(MACROBLOCKD *xd,
                                             int mi_row,
                                             int mi_col,
                                             BLOCK_SIZE_TYPE bsize,
                                             int which_mv,
                                             uint8_t *dst, int dst_stride) {
  struct build_inter_predictors_args args = {
    xd, mi_col * MI_SIZE, mi_row * MI_SIZE,
    {dst, NULL, NULL}, {dst_stride, 0, 0},
    {{xd->plane[0].pre[0].buf, NULL, NULL},
      {xd->plane[0].pre[1].buf, NULL, NULL}},
    {{xd->plane[0].pre[0].stride, 0, 0}, {xd->plane[0].pre[1].stride, 0, 0}},
    which_mv
  };

  foreach_predicted_block_in_plane(xd, bsize, 0,
                                   build_inter_predictors_singlemv, &args);
}

void vp9_build_inter_predictors_singlemv_sbuv(MACROBLOCKD *xd,
                                              int mi_row,
                                              int mi_col,
                                              BLOCK_SIZE_TYPE bsize,
                                              int which_mv,
                                              uint8_t *dstu, int ustride,
                                              uint8_t *dstv, int vstride) {
  struct build_inter_predictors_args args = {
    xd, mi_col * MI_SIZE, mi_row * MI_SIZE,
    {NULL, dstu, dstv},
    {0, ustride, vstride},
    {{NULL, xd->plane[1].pre[0].buf, xd->plane[2].pre[0].buf},
      {NULL, xd->plane[1].pre[1].buf, xd->plane[2].pre[1].buf}},
    {{0, xd->plane[1].pre[0].stride, xd->plane[1].pre[0].stride},
      {0, xd->plane[1].pre[1].stride, xd->plane[1].pre[1].stride}},
    which_mv
  };
  foreach_predicted_block_uv(xd, bsize,
                             build_inter_predictors_singlemv, &args);
}

#if USE_3MV_MASK
int vp9_get_3mv_mask_y(MACROBLOCKD *xd,
                       int mi_row,
                       int mi_col,
                       BLOCK_SIZE_TYPE bsize,
                       uint8_t *mask,
                       int mask_stride) {
  uint8_t dst0[4096], dst1[4096];
  int w = (4 << b_width_log2(bsize));
  int h = (4 << b_height_log2(bsize));
  vp9_build_inter_predictors_singlemv_sby(xd, mi_row, mi_col, bsize,
                                          0, dst0, 64);
  vp9_build_inter_predictors_singlemv_sby(xd, mi_row, mi_col, bsize,
                                          1, dst1, 64);
  return get_3mv_mask(dst0, 64, dst1, 64, mask, 64, 0, h, w);
}
#endif
