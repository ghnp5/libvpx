/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP10_COMMON_RECONINTER_H_
#define VP10_COMMON_RECONINTER_H_

#include "vp10/common/filter.h"
#include "vp10/common/onyxc_int.h"
#include "vp10/common/vp10_convolve.h"
#include "vpx/vpx_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

static INLINE void inter_predictor(const uint8_t *src, int src_stride,
                                   uint8_t *dst, int dst_stride,
                                   const int subpel_x,
                                   const int subpel_y,
                                   const struct scale_factors *sf,
                                   int w, int h, int ref,
                                   const INTERP_FILTER interp_filter,
                                   int xs, int ys) {
  InterpFilterParams interp_filter_params =
      vp10_get_interp_filter_params(interp_filter);
  if (interp_filter_params.taps == SUBPEL_TAPS) {
    const int16_t *kernel_x =
        vp10_get_interp_filter_subpel_kernel(interp_filter_params, subpel_x);
    const int16_t *kernel_y =
        vp10_get_interp_filter_subpel_kernel(interp_filter_params, subpel_y);
#if CONFIG_EXT_INTERP && SUPPORT_NONINTERPOLATING_FILTERS
    if (IsInterpolatingFilter(interp_filter)) {
      // Interpolating filter
      sf->predict[subpel_x != 0][subpel_y != 0][ref](
          src, src_stride, dst, dst_stride,
          kernel_x, xs, kernel_y, ys, w, h);
    } else {
      sf->predict_ni[subpel_x != 0][subpel_y != 0][ref](
          src, src_stride, dst, dst_stride,
          kernel_x, xs, kernel_y, ys, w, h);
    }
#else
    sf->predict[subpel_x != 0][subpel_y != 0][ref](
        src, src_stride, dst, dst_stride,
        kernel_x, xs, kernel_y, ys, w, h);
#endif  // CONFIG_EXT_INTERP && SUPPORT_NONINTERPOLATING_FILTERS
  } else {
    // ref > 0 means this is the second reference frame
    // first reference frame's prediction result is already in dst
    // therefore we need to average the first and second results
    int avg = ref > 0;
    vp10_convolve(src, src_stride, dst, dst_stride, w, h, interp_filter_params,
                  subpel_x, xs, subpel_y, ys, avg);
  }
}

#if CONFIG_VP9_HIGHBITDEPTH
static INLINE void high_inter_predictor(const uint8_t *src, int src_stride,
                                        uint8_t *dst, int dst_stride,
                                        const int subpel_x,
                                        const int subpel_y,
                                        const struct scale_factors *sf,
                                        int w, int h, int ref,
                                        const INTERP_FILTER interp_filter,
                                        int xs, int ys, int bd) {
  InterpFilterParams interp_filter_params =
      vp10_get_interp_filter_params(interp_filter);
  if (interp_filter_params.taps == SUBPEL_TAPS) {
    const int16_t *kernel_x =
        vp10_get_interp_filter_subpel_kernel(interp_filter_params, subpel_x);
    const int16_t *kernel_y =
        vp10_get_interp_filter_subpel_kernel(interp_filter_params, subpel_y);
#if CONFIG_EXT_INTERP && SUPPORT_NONINTERPOLATING_FILTERS
    if (IsInterpolatingFilter(interp_filter)) {
      // Interpolating filter
      sf->highbd_predict[subpel_x != 0][subpel_y != 0][ref](
          src, src_stride, dst, dst_stride,
          kernel_x, xs, kernel_y, ys, w, h, bd);
    } else {
      sf->highbd_predict_ni[subpel_x != 0][subpel_y != 0][ref](
          src, src_stride, dst, dst_stride,
          kernel_x, xs, kernel_y, ys, w, h, bd);
    }
#else
    sf->highbd_predict[subpel_x != 0][subpel_y != 0][ref](
        src, src_stride, dst, dst_stride,
        kernel_x, xs, kernel_y, ys, w, h, bd);
#endif  // CONFIG_EXT_INTERP && SUPPORT_NONINTERPOLATING_FILTERS
  } else {
    // ref > 0 means this is the second reference frame
    // first reference frame's prediction result is already in dst
    // therefore we need to average the first and second results
    int avg = ref > 0;
    vp10_highbd_convolve(src, src_stride, dst, dst_stride, w, h,
                         interp_filter_params, subpel_x, xs, subpel_y, ys, avg,
                         bd);
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

static INLINE int round_mv_comp_q4(int value) {
  return (value < 0 ? value - 2 : value + 2) / 4;
}

static MV mi_mv_pred_q4(const MODE_INFO *mi, int idx) {
  MV res = { round_mv_comp_q4(mi->bmi[0].as_mv[idx].as_mv.row +
                              mi->bmi[1].as_mv[idx].as_mv.row +
                              mi->bmi[2].as_mv[idx].as_mv.row +
                              mi->bmi[3].as_mv[idx].as_mv.row),
             round_mv_comp_q4(mi->bmi[0].as_mv[idx].as_mv.col +
                              mi->bmi[1].as_mv[idx].as_mv.col +
                              mi->bmi[2].as_mv[idx].as_mv.col +
                              mi->bmi[3].as_mv[idx].as_mv.col) };
  return res;
}

static INLINE int round_mv_comp_q2(int value) {
  return (value < 0 ? value - 1 : value + 1) / 2;
}

static MV mi_mv_pred_q2(const MODE_INFO *mi, int idx, int block0, int block1) {
  MV res = { round_mv_comp_q2(mi->bmi[block0].as_mv[idx].as_mv.row +
                              mi->bmi[block1].as_mv[idx].as_mv.row),
             round_mv_comp_q2(mi->bmi[block0].as_mv[idx].as_mv.col +
                              mi->bmi[block1].as_mv[idx].as_mv.col) };
  return res;
}

// TODO(jkoleszar): yet another mv clamping function :-(
static INLINE MV clamp_mv_to_umv_border_sb(const MACROBLOCKD *xd,
                                           const MV *src_mv,
                                           int bw, int bh, int ss_x, int ss_y) {
  // If the MV points so far into the UMV border that no visible pixels
  // are used for reconstruction, the subpel part of the MV can be
  // discarded and the MV limited to 16 pixels with equivalent results.
  const int spel_left = (VP9_INTERP_EXTEND + bw) << SUBPEL_BITS;
  const int spel_right = spel_left - SUBPEL_SHIFTS;
  const int spel_top = (VP9_INTERP_EXTEND + bh) << SUBPEL_BITS;
  const int spel_bottom = spel_top - SUBPEL_SHIFTS;
  MV clamped_mv = {
    src_mv->row * (1 << (1 - ss_y)),
    src_mv->col * (1 << (1 - ss_x))
  };
  assert(ss_x <= 1);
  assert(ss_y <= 1);

  clamp_mv(&clamped_mv,
           xd->mb_to_left_edge * (1 << (1 - ss_x)) - spel_left,
           xd->mb_to_right_edge * (1 << (1 - ss_x)) + spel_right,
           xd->mb_to_top_edge * (1 << (1 - ss_y)) - spel_top,
           xd->mb_to_bottom_edge * (1 << (1 - ss_y)) + spel_bottom);

  return clamped_mv;
}

static INLINE MV average_split_mvs(const struct macroblockd_plane *pd,
                                   const MODE_INFO *mi, int ref, int block) {
  const int ss_idx = ((pd->subsampling_x > 0) << 1) | (pd->subsampling_y > 0);
  MV res = {0, 0};
  switch (ss_idx) {
    case 0:
      res = mi->bmi[block].as_mv[ref].as_mv;
      break;
    case 1:
      res = mi_mv_pred_q2(mi, ref, block, block + 2);
      break;
    case 2:
      res = mi_mv_pred_q2(mi, ref, block, block + 1);
      break;
    case 3:
      res = mi_mv_pred_q4(mi, ref);
      break;
    default:
      assert(ss_idx <= 3 && ss_idx >= 0);
  }
  return res;
}

void build_inter_predictors(MACROBLOCKD *xd, int plane,
#if CONFIG_OBMC
                            int mi_col_offset, int mi_row_offset,
#endif  // CONFIG_OBMC
                            int block,
                            int bw, int bh,
                            int x, int y, int w, int h,
                            int mi_x, int mi_y);

void vp10_build_inter_predictor_sub8x8(MACROBLOCKD *xd, int plane,
                                       int i, int ir, int ic,
                                       int mi_row, int mi_col);

void vp10_build_inter_predictors_sby(MACROBLOCKD *xd, int mi_row, int mi_col,
                                     BLOCK_SIZE bsize);

void vp10_build_inter_predictors_sbp(MACROBLOCKD *xd, int mi_row, int mi_col,
                                     BLOCK_SIZE bsize, int plane);

void vp10_build_inter_predictors_sbuv(MACROBLOCKD *xd, int mi_row, int mi_col,
                                      BLOCK_SIZE bsize);

void vp10_build_inter_predictors_sb(MACROBLOCKD *xd, int mi_row, int mi_col,
                                    BLOCK_SIZE bsize);

#if CONFIG_SUPERTX
void vp10_build_inter_predictors_sb_sub8x8(MACROBLOCKD *xd,
                                           int mi_row, int mi_col,
                                           BLOCK_SIZE bsize, int block);
struct macroblockd_plane;
void vp10_build_masked_inter_predictor_complex(
    MACROBLOCKD *xd,
    uint8_t *dst, int dst_stride, uint8_t *dst2, int dst2_stride,
    const struct macroblockd_plane *pd, int mi_row, int mi_col,
    int mi_row_ori, int mi_col_ori, BLOCK_SIZE bsize, BLOCK_SIZE top_bsize,
    PARTITION_TYPE partition, int plane);

#endif  // CONFIG_SUPERTX

void vp10_build_inter_predictor(const uint8_t *src, int src_stride,
                               uint8_t *dst, int dst_stride,
                               const MV *mv_q3,
                               const struct scale_factors *sf,
                               int w, int h, int do_avg,
                               const INTERP_FILTER interp_filter,
                               enum mv_precision precision,
                               int x, int y);

#if CONFIG_VP9_HIGHBITDEPTH
void vp10_highbd_build_inter_predictor(const uint8_t *src, int src_stride,
                                      uint8_t *dst, int dst_stride,
                                      const MV *mv_q3,
                                      const struct scale_factors *sf,
                                      int w, int h, int do_avg,
                                      const INTERP_FILTER interp_filter,
                                      enum mv_precision precision,
                                      int x, int y, int bd);
#endif

static INLINE int scaled_buffer_offset(int x_offset, int y_offset, int stride,
                                       const struct scale_factors *sf) {
  const int x = sf ? sf->scale_value_x(x_offset, sf) : x_offset;
  const int y = sf ? sf->scale_value_y(y_offset, sf) : y_offset;
  return y * stride + x;
}

static INLINE void setup_pred_plane(struct buf_2d *dst,
                                    uint8_t *src, int stride,
                                    int mi_row, int mi_col,
                                    const struct scale_factors *scale,
                                    int subsampling_x, int subsampling_y) {
  const int x = (MI_SIZE * mi_col) >> subsampling_x;
  const int y = (MI_SIZE * mi_row) >> subsampling_y;
  dst->buf = src + scaled_buffer_offset(x, y, stride, scale);
  dst->stride = stride;
}

void vp10_setup_dst_planes(struct macroblockd_plane planes[MAX_MB_PLANE],
                          const YV12_BUFFER_CONFIG *src,
                          int mi_row, int mi_col);

void vp10_setup_pre_planes(MACROBLOCKD *xd, int idx,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const struct scale_factors *sf);

#if CONFIG_EXT_INTERP
static INLINE int vp10_is_interp_needed(const MACROBLOCKD *const xd) {
  MODE_INFO *const mi = xd->mi[0];
  MB_MODE_INFO *const mbmi = &mi->mbmi;
  const BLOCK_SIZE bsize = mbmi->sb_type;
  const int is_compound = has_second_ref(mbmi);
  int intpel_mv;
  int plane;

#if SUPPORT_NONINTERPOLATING_FILTERS
  // TODO(debargha): This is is currently only for experimentation
  // with non-interpolating filters. Remove later.
  // If any of the filters are non-interpolating, then indicate the
  // interpolation filter always.
  int i;
  for (i = 0; i < SWITCHABLE_FILTERS; ++i) {
    if (!IsInterpolatingFilter(i)) return 1;
  }
#endif

  // For scaled references, interpolation filter is indicated all the time.
  if (vp10_is_scaled(&xd->block_refs[0]->sf))
    return 1;
  if (is_compound && vp10_is_scaled(&xd->block_refs[1]->sf))
    return 1;

  if (bsize == BLOCK_4X4) {
    for (plane = 0; plane < 2; ++plane) {
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      MV mv0 = average_split_mvs(pd, mi, 0, 0);
      MV mv1 = average_split_mvs(pd, mi, 0, 1);
      MV mv2 = average_split_mvs(pd, mi, 0, 2);
      MV mv3 = average_split_mvs(pd, mi, 0, 3);
      intpel_mv =
          !mv_has_subpel(&mv0) &&
          !mv_has_subpel(&mv1) &&
          !mv_has_subpel(&mv2) &&
          !mv_has_subpel(&mv3);
      if (is_compound && intpel_mv) {
        mv0 = average_split_mvs(pd, mi, 1, 0);
        mv1 = average_split_mvs(pd, mi, 1, 1);
        mv2 = average_split_mvs(pd, mi, 1, 2);
        mv3 = average_split_mvs(pd, mi, 1, 3);
        intpel_mv =
            !mv_has_subpel(&mv0) &&
            !mv_has_subpel(&mv1) &&
            !mv_has_subpel(&mv2) &&
            !mv_has_subpel(&mv3);
      }
      if (!intpel_mv) break;
    }
  } else if (bsize == BLOCK_4X8) {
    for (plane = 0; plane < 2; ++plane) {
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      MV mv0 = average_split_mvs(pd, mi, 0, 0);
      MV mv1 = average_split_mvs(pd, mi, 0, 1);
      intpel_mv =
          !mv_has_subpel(&mv0) &&
          !mv_has_subpel(&mv1);
      if (is_compound && intpel_mv) {
        mv0 = average_split_mvs(pd, mi, 1, 0);
        mv1 = average_split_mvs(pd, mi, 1, 1);
        intpel_mv =
            !mv_has_subpel(&mv0) &&
            !mv_has_subpel(&mv1);
      }
      if (!intpel_mv) break;
    }
  } else if (bsize == BLOCK_8X4) {
    for (plane = 0; plane < 2; ++plane) {
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      MV mv0 = average_split_mvs(pd, mi, 0, 0);
      MV mv1 = average_split_mvs(pd, mi, 0, 2);
      intpel_mv =
          !mv_has_subpel(&mv0) &&
          !mv_has_subpel(&mv1);
      if (is_compound && intpel_mv) {
        mv0 = average_split_mvs(pd, mi, 1, 0);
        mv1 = average_split_mvs(pd, mi, 1, 2);
        intpel_mv =
            !mv_has_subpel(&mv0) &&
            !mv_has_subpel(&mv1);
      }
      if (!intpel_mv) break;
    }
  } else {
    intpel_mv = !mv_has_subpel(&mbmi->mv[0].as_mv);
    if (is_compound && intpel_mv) {
      intpel_mv &= !mv_has_subpel(&mbmi->mv[1].as_mv);
    }
  }
  return !intpel_mv;
}
#endif  // CONFIG_EXT_INTERP

#if CONFIG_OBMC
void vp10_build_obmc_inter_prediction(VP10_COMMON *cm,
                                      MACROBLOCKD *xd, int mi_row, int mi_col,
                                      int use_tmp_dst_buf,
                                      uint8_t *final_buf[MAX_MB_PLANE],
                                      int final_stride[MAX_MB_PLANE],
                                      uint8_t *tmp_buf1[MAX_MB_PLANE],
                                      int tmp_stride1[MAX_MB_PLANE],
                                      uint8_t *tmp_buf2[MAX_MB_PLANE],
                                      int tmp_stride2[MAX_MB_PLANE]);
#endif  // CONFIG_OBMC

#if CONFIG_EXT_INTER
#define WEDGE_BITS_SML    3
#define WEDGE_BITS_MED    4
#define WEDGE_BITS_BIG    5
#define WEDGE_NONE       -1
#define WEDGE_WEIGHT_BITS 6

static INLINE int get_wedge_bits(BLOCK_SIZE sb_type) {
  if (sb_type < BLOCK_8X8)
    return 0;
  if (sb_type <= BLOCK_8X8)
    return WEDGE_BITS_SML;
  else if (sb_type <= BLOCK_32X32)
    return WEDGE_BITS_MED;
  else
    return WEDGE_BITS_BIG;
}

#define MASK_MASTER_SIZE   (2 * CU_SIZE)
#define MASK_MASTER_STRIDE (2 * CU_SIZE)

void vp10_init_wedge_masks();

const uint8_t *vp10_get_soft_mask(int wedge_index,
                                  BLOCK_SIZE sb_type,
                                  int h, int w);

void vp10_build_interintra_predictors(MACROBLOCKD *xd,
                                      uint8_t *ypred,
                                      uint8_t *upred,
                                      uint8_t *vpred,
                                      int ystride,
                                      int ustride,
                                      int vstride,
                                      BLOCK_SIZE bsize);
void vp10_build_interintra_predictors_sby(MACROBLOCKD *xd,
                                          uint8_t *ypred,
                                          int ystride,
                                          BLOCK_SIZE bsize);
void vp10_build_interintra_predictors_sbc(MACROBLOCKD *xd,
                                          uint8_t *upred,
                                          int ustride,
                                          int plane,
                                          BLOCK_SIZE bsize);
void vp10_build_interintra_predictors_sbuv(MACROBLOCKD *xd,
                                           uint8_t *upred,
                                           uint8_t *vpred,
                                           int ustride, int vstride,
                                           BLOCK_SIZE bsize);
#endif  // CONFIG_EXT_INTER

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP10_COMMON_RECONINTER_H_
