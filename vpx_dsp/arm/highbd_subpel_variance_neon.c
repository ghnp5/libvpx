/*
 *  Copyright (c) 2023 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>

#include "./vpx_dsp_rtcd.h"
#include "./vpx_config.h"

#include "vpx/vpx_integer.h"
#include "vpx_dsp/arm/mem_neon.h"

// The bilinear filters look like this:
//
// {{ 128,  0 }, { 112, 16 }, { 96, 32 }, { 80,  48 },
//  {  64, 64 }, {  48, 80 }, { 32, 96 }, { 16, 112 }}
//
// We can factor out the highest common multiple, such that the sum of both
// weights will be 8 instead of 128. The benefits of this are two-fold:
//
// 1) We can infer the filter values from the filter_offset parameter in the
// bilinear filter functions below - we don't have to actually load the values
// from memory:
// f0 = 8 - filter_offset
// f1 = filter_offset
//
// 2) Scaling the pixel values by 8, instead of 128 enables us to operate on
// 16-bit data types at all times, rather than widening out to 32-bit and
// requiring double the number of data processing instructions. (12-bit * 8 =
// 15-bit.)

// Process a block exactly 4 wide and a multiple of 2 high.
static void highbd_var_filter_block2d_bil_w4(const uint16_t *src_ptr,
                                             uint16_t *dst_ptr, int src_stride,
                                             int pixel_step, int dst_height,
                                             int filter_offset) {
  const uint16x8_t f0 = vdupq_n_u16(8 - filter_offset);
  const uint16x8_t f1 = vdupq_n_u16(filter_offset);

  int i = dst_height;
  do {
    uint16x8_t s0 = load_unaligned_u16q(src_ptr, src_stride);
    uint16x8_t s1 = load_unaligned_u16q(src_ptr + pixel_step, src_stride);

    uint16x8_t blend = vmulq_u16(s0, f0);
    blend = vmlaq_u16(blend, s1, f1);
    blend = vrshrq_n_u16(blend, 3);

    vst1q_u16(dst_ptr, blend);

    src_ptr += 2 * src_stride;
    dst_ptr += 8;
    i -= 2;
  } while (i != 0);
}

// Process a block which is a multiple of 8 and any height.
static void highbd_var_filter_block2d_bil_large(const uint16_t *src_ptr,
                                                uint16_t *dst_ptr,
                                                int src_stride, int pixel_step,
                                                int dst_width, int dst_height,
                                                int filter_offset) {
  const uint16x8_t f0 = vdupq_n_u16(8 - filter_offset);
  const uint16x8_t f1 = vdupq_n_u16(filter_offset);

  int i = dst_height;
  do {
    int j = 0;
    do {
      uint16x8_t s0 = vld1q_u16(src_ptr + j);
      uint16x8_t s1 = vld1q_u16(src_ptr + j + pixel_step);

      uint16x8_t blend = vmulq_u16(s0, f0);
      blend = vmlaq_u16(blend, s1, f1);
      blend = vrshrq_n_u16(blend, 3);

      vst1q_u16(dst_ptr + j, blend);

      j += 8;
    } while (j < dst_width);

    src_ptr += src_stride;
    dst_ptr += dst_width;
  } while (--i != 0);
}

static void highbd_var_filter_block2d_bil_w8(const uint16_t *src_ptr,
                                             uint16_t *dst_ptr, int src_stride,
                                             int pixel_step, int dst_height,
                                             int filter_offset) {
  highbd_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride, pixel_step,
                                      8, dst_height, filter_offset);
}
static void highbd_var_filter_block2d_bil_w16(const uint16_t *src_ptr,
                                              uint16_t *dst_ptr, int src_stride,
                                              int pixel_step, int dst_height,
                                              int filter_offset) {
  highbd_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride, pixel_step,
                                      16, dst_height, filter_offset);
}
static void highbd_var_filter_block2d_bil_w32(const uint16_t *src_ptr,
                                              uint16_t *dst_ptr, int src_stride,
                                              int pixel_step, int dst_height,
                                              int filter_offset) {
  highbd_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride, pixel_step,
                                      32, dst_height, filter_offset);
}
static void highbd_var_filter_block2d_bil_w64(const uint16_t *src_ptr,
                                              uint16_t *dst_ptr, int src_stride,
                                              int pixel_step, int dst_height,
                                              int filter_offset) {
  highbd_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride, pixel_step,
                                      64, dst_height, filter_offset);
}

#define HBD_SUBPEL_VARIANCE_WXH_NEON(w, h, padding)                          \
  unsigned int vpx_highbd_8_sub_pixel_variance##w##x##h##_neon(              \
      const uint8_t *src, int src_stride, int xoffset, int yoffset,          \
      const uint8_t *ref, int ref_stride, uint32_t *sse) {                   \
    uint16_t tmp0[w * (h + padding)];                                        \
    uint16_t tmp1[w * h];                                                    \
    uint16_t *src_ptr = CONVERT_TO_SHORTPTR(src);                            \
                                                                             \
    highbd_var_filter_block2d_bil_w##w(src_ptr, tmp0, src_stride, 1,         \
                                       (h + padding), xoffset);              \
    highbd_var_filter_block2d_bil_w##w(tmp0, tmp1, w, w, h, yoffset);        \
                                                                             \
    return vpx_highbd_8_variance##w##x##h(CONVERT_TO_BYTEPTR(tmp1), w, ref,  \
                                          ref_stride, sse);                  \
  }                                                                          \
                                                                             \
  unsigned int vpx_highbd_10_sub_pixel_variance##w##x##h##_neon(             \
      const uint8_t *src, int src_stride, int xoffset, int yoffset,          \
      const uint8_t *ref, int ref_stride, uint32_t *sse) {                   \
    uint16_t tmp0[w * (h + padding)];                                        \
    uint16_t tmp1[w * h];                                                    \
    uint16_t *src_ptr = CONVERT_TO_SHORTPTR(src);                            \
                                                                             \
    highbd_var_filter_block2d_bil_w##w(src_ptr, tmp0, src_stride, 1,         \
                                       (h + padding), xoffset);              \
    highbd_var_filter_block2d_bil_w##w(tmp0, tmp1, w, w, h, yoffset);        \
                                                                             \
    return vpx_highbd_10_variance##w##x##h(CONVERT_TO_BYTEPTR(tmp1), w, ref, \
                                           ref_stride, sse);                 \
  }                                                                          \
  unsigned int vpx_highbd_12_sub_pixel_variance##w##x##h##_neon(             \
      const uint8_t *src, int src_stride, int xoffset, int yoffset,          \
      const uint8_t *ref, int ref_stride, uint32_t *sse) {                   \
    uint16_t tmp0[w * (h + padding)];                                        \
    uint16_t tmp1[w * h];                                                    \
    uint16_t *src_ptr = CONVERT_TO_SHORTPTR(src);                            \
                                                                             \
    highbd_var_filter_block2d_bil_w##w(src_ptr, tmp0, src_stride, 1,         \
                                       (h + padding), xoffset);              \
    highbd_var_filter_block2d_bil_w##w(tmp0, tmp1, w, w, h, yoffset);        \
                                                                             \
    return vpx_highbd_12_variance##w##x##h(CONVERT_TO_BYTEPTR(tmp1), w, ref, \
                                           ref_stride, sse);                 \
  }

// 4x<h> blocks are processed two rows at a time, so require an extra row of
// padding.
HBD_SUBPEL_VARIANCE_WXH_NEON(4, 4, 2)
HBD_SUBPEL_VARIANCE_WXH_NEON(4, 8, 2)

HBD_SUBPEL_VARIANCE_WXH_NEON(8, 4, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(8, 8, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(8, 16, 1)

HBD_SUBPEL_VARIANCE_WXH_NEON(16, 8, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(16, 16, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(16, 32, 1)

HBD_SUBPEL_VARIANCE_WXH_NEON(32, 16, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(32, 32, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(32, 64, 1)

HBD_SUBPEL_VARIANCE_WXH_NEON(64, 32, 1)
HBD_SUBPEL_VARIANCE_WXH_NEON(64, 64, 1)

// Combine bilinear filter with vpx_highbd_comp_avg_pred for blocks having
// width 4.
static void highbd_avg_pred_var_filter_block2d_bil_w4(
    const uint16_t *src_ptr, uint16_t *dst_ptr, int src_stride, int pixel_step,
    int dst_height, int filter_offset, const uint16_t *second_pred) {
  const uint16x8_t f0 = vdupq_n_u16(8 - filter_offset);
  const uint16x8_t f1 = vdupq_n_u16(filter_offset);

  int i = dst_height;
  do {
    uint16x8_t s0 = load_unaligned_u16q(src_ptr, src_stride);
    uint16x8_t s1 = load_unaligned_u16q(src_ptr + pixel_step, src_stride);
    uint16x8_t p = vld1q_u16(second_pred);

    uint16x8_t blend = vmulq_u16(s0, f0);
    blend = vmlaq_u16(blend, s1, f1);
    blend = vrshrq_n_u16(blend, 3);

    vst1q_u16(dst_ptr, vrhaddq_u16(blend, p));

    src_ptr += 2 * src_stride;
    dst_ptr += 2 * 4;
    second_pred += 2 * 4;
    i -= 2;
  } while (i != 0);
}

// Combine bilinear filter with vpx_highbd_comp_avg_pred for large blocks.
static void highbd_avg_pred_var_filter_block2d_bil_large(
    const uint16_t *src_ptr, uint16_t *dst_ptr, int src_stride, int pixel_step,
    int dst_width, int dst_height, int filter_offset,
    const uint16_t *second_pred) {
  const uint16x8_t f0 = vdupq_n_u16(8 - filter_offset);
  const uint16x8_t f1 = vdupq_n_u16(filter_offset);

  int i = dst_height;
  do {
    int j = 0;
    do {
      uint16x8_t s0 = vld1q_u16(src_ptr + j);
      uint16x8_t s1 = vld1q_u16(src_ptr + j + pixel_step);
      uint16x8_t p = vld1q_u16(second_pred);

      uint16x8_t blend = vmulq_u16(s0, f0);
      blend = vmlaq_u16(blend, s1, f1);
      blend = vrshrq_n_u16(blend, 3);

      vst1q_u16(dst_ptr + j, vrhaddq_u16(blend, p));

      j += 8;
      second_pred += 8;
    } while (j < dst_width);

    src_ptr += src_stride;
    dst_ptr += dst_width;
  } while (--i != 0);
}

static void highbd_avg_pred_var_filter_block2d_bil_w8(
    const uint16_t *src_ptr, uint16_t *dst_ptr, int src_stride, int pixel_step,
    int dst_height, int filter_offset, const uint16_t *second_pred) {
  highbd_avg_pred_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride,
                                               pixel_step, 8, dst_height,
                                               filter_offset, second_pred);
}
static void highbd_avg_pred_var_filter_block2d_bil_w16(
    const uint16_t *src_ptr, uint16_t *dst_ptr, int src_stride, int pixel_step,
    int dst_height, int filter_offset, const uint16_t *second_pred) {
  highbd_avg_pred_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride,
                                               pixel_step, 16, dst_height,
                                               filter_offset, second_pred);
}
static void highbd_avg_pred_var_filter_block2d_bil_w32(
    const uint16_t *src_ptr, uint16_t *dst_ptr, int src_stride, int pixel_step,
    int dst_height, int filter_offset, const uint16_t *second_pred) {
  highbd_avg_pred_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride,
                                               pixel_step, 32, dst_height,
                                               filter_offset, second_pred);
}
static void highbd_avg_pred_var_filter_block2d_bil_w64(
    const uint16_t *src_ptr, uint16_t *dst_ptr, int src_stride, int pixel_step,
    int dst_height, int filter_offset, const uint16_t *second_pred) {
  highbd_avg_pred_var_filter_block2d_bil_large(src_ptr, dst_ptr, src_stride,
                                               pixel_step, 64, dst_height,
                                               filter_offset, second_pred);
}

#define SUBPEL_AVG_VARIANCE_WXH_NEON(w, h, padding)                            \
  uint32_t vpx_highbd_8_sub_pixel_avg_variance##w##x##h##_neon(                \
      const uint8_t *src, int src_stride, int xoffset, int yoffset,            \
      const uint8_t *ref, int ref_stride, uint32_t *sse,                       \
      const uint8_t *second_pred) {                                            \
    uint16_t tmp0[w * (h + padding)];                                          \
    uint16_t tmp1[w * h];                                                      \
    uint16_t *src_ptr = CONVERT_TO_SHORTPTR(src);                              \
                                                                               \
    highbd_var_filter_block2d_bil_w##w(src_ptr, tmp0, src_stride, 1,           \
                                       (h + padding), xoffset);                \
    highbd_avg_pred_var_filter_block2d_bil_w##w(                               \
        tmp0, tmp1, w, w, h, yoffset, CONVERT_TO_SHORTPTR(second_pred));       \
                                                                               \
    return vpx_highbd_8_variance##w##x##h##_neon(CONVERT_TO_BYTEPTR(tmp1), w,  \
                                                 ref, ref_stride, sse);        \
  }                                                                            \
                                                                               \
  uint32_t vpx_highbd_10_sub_pixel_avg_variance##w##x##h##_neon(               \
      const uint8_t *src, int src_stride, int xoffset, int yoffset,            \
      const uint8_t *ref, int ref_stride, uint32_t *sse,                       \
      const uint8_t *second_pred) {                                            \
    uint16_t tmp0[w * (h + padding)];                                          \
    uint16_t tmp1[w * h];                                                      \
    uint16_t *src_ptr = CONVERT_TO_SHORTPTR(src);                              \
                                                                               \
    highbd_var_filter_block2d_bil_w##w(src_ptr, tmp0, src_stride, 1,           \
                                       (h + padding), xoffset);                \
    highbd_avg_pred_var_filter_block2d_bil_w##w(                               \
        tmp0, tmp1, w, w, h, yoffset, CONVERT_TO_SHORTPTR(second_pred));       \
                                                                               \
    return vpx_highbd_10_variance##w##x##h##_neon(CONVERT_TO_BYTEPTR(tmp1), w, \
                                                  ref, ref_stride, sse);       \
  }                                                                            \
                                                                               \
  uint32_t vpx_highbd_12_sub_pixel_avg_variance##w##x##h##_neon(               \
      const uint8_t *src, int src_stride, int xoffset, int yoffset,            \
      const uint8_t *ref, int ref_stride, uint32_t *sse,                       \
      const uint8_t *second_pred) {                                            \
    uint16_t tmp0[w * (h + padding)];                                          \
    uint16_t tmp1[w * h];                                                      \
    uint16_t *src_ptr = CONVERT_TO_SHORTPTR(src);                              \
                                                                               \
    highbd_var_filter_block2d_bil_w##w(src_ptr, tmp0, src_stride, 1,           \
                                       (h + padding), xoffset);                \
    highbd_avg_pred_var_filter_block2d_bil_w##w(                               \
        tmp0, tmp1, w, w, h, yoffset, CONVERT_TO_SHORTPTR(second_pred));       \
                                                                               \
    return vpx_highbd_12_variance##w##x##h##_neon(CONVERT_TO_BYTEPTR(tmp1), w, \
                                                  ref, ref_stride, sse);       \
  }

// 4x<h> blocks are processed two rows at a time, so require an extra row of
// padding.
SUBPEL_AVG_VARIANCE_WXH_NEON(4, 4, 2)
SUBPEL_AVG_VARIANCE_WXH_NEON(4, 8, 2)

SUBPEL_AVG_VARIANCE_WXH_NEON(8, 4, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(8, 8, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(8, 16, 1)

SUBPEL_AVG_VARIANCE_WXH_NEON(16, 8, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(16, 16, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(16, 32, 1)

SUBPEL_AVG_VARIANCE_WXH_NEON(32, 16, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(32, 32, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(32, 64, 1)

SUBPEL_AVG_VARIANCE_WXH_NEON(64, 32, 1)
SUBPEL_AVG_VARIANCE_WXH_NEON(64, 64, 1)
