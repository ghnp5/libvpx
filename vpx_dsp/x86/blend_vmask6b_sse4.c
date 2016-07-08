/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <smmintrin.h>  // SSE4.1

#include <assert.h>

#include "vpx/vpx_integer.h"
#include "vpx_ports/mem.h"
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_dsp/blend_mask.h"

#include "vpx_dsp/x86/synonyms.h"

#include "./vpx_dsp_rtcd.h"

//////////////////////////////////////////////////////////////////////////////
// Common kernels
//////////////////////////////////////////////////////////////////////////////

static INLINE __m128i blend_4(uint8_t*src0, uint8_t *src1,
                              const __m128i v_m0_w, const __m128i v_m1_w) {
  const __m128i v_s0_b = xx_loadl_32(src0);
  const __m128i v_s1_b = xx_loadl_32(src1);
  const __m128i v_s0_w = _mm_cvtepu8_epi16(v_s0_b);
  const __m128i v_s1_w = _mm_cvtepu8_epi16(v_s1_b);

  const __m128i v_p0_w = _mm_mullo_epi16(v_s0_w, v_m0_w);
  const __m128i v_p1_w = _mm_mullo_epi16(v_s1_w, v_m1_w);

  const __m128i v_sum_w = _mm_add_epi16(v_p0_w, v_p1_w);

  const __m128i v_res_w = xx_roundn_epu16(v_sum_w, MASK_BITS6);

  return v_res_w;
}

static INLINE __m128i blend_8(uint8_t*src0, uint8_t *src1,
                              const __m128i v_m0_w, const __m128i v_m1_w) {
  const __m128i v_s0_b = xx_loadl_64(src0);
  const __m128i v_s1_b = xx_loadl_64(src1);
  const __m128i v_s0_w = _mm_cvtepu8_epi16(v_s0_b);
  const __m128i v_s1_w = _mm_cvtepu8_epi16(v_s1_b);

  const __m128i v_p0_w = _mm_mullo_epi16(v_s0_w, v_m0_w);
  const __m128i v_p1_w = _mm_mullo_epi16(v_s1_w, v_m1_w);

  const __m128i v_sum_w = _mm_add_epi16(v_p0_w, v_p1_w);

  const __m128i v_res_w = xx_roundn_epu16(v_sum_w, MASK_BITS6);

  return v_res_w;
}

//////////////////////////////////////////////////////////////////////////////
// Implementation - No sub-sampling
//////////////////////////////////////////////////////////////////////////////

static void blend_vmask6b_w4_sse4_1(
    uint8_t *dst, uint32_t dst_stride,
    uint8_t *src0, uint32_t src0_stride,
    uint8_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  const __m128i v_maxval_w = _mm_set1_epi16(1 << MASK_BITS6);

  (void)w;

  do {
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);

    const __m128i v_res_w = blend_4(src0, src1, v_m0_w, v_m1_w);

    const __m128i v_res_b = _mm_packus_epi16(v_res_w, v_res_w);

    xx_storel_32(dst, v_res_b);

    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

static void blend_vmask6b_w8_sse4_1(
    uint8_t *dst, uint32_t dst_stride,
    uint8_t *src0, uint32_t src0_stride,
    uint8_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  const __m128i v_maxval_w = _mm_set1_epi16(1 << MASK_BITS6);

  (void)w;

  do {
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);

    const __m128i v_res_w = blend_8(src0, src1, v_m0_w, v_m1_w);

    const __m128i v_res_b = _mm_packus_epi16(v_res_w, v_res_w);

    xx_storel_64(dst, v_res_b);

    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

static void blend_vmask6b_w16n_sse4_1(
    uint8_t *dst, uint32_t dst_stride,
    uint8_t *src0, uint32_t src0_stride,
    uint8_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  const __m128i v_maxval_w = _mm_set1_epi16(1 << MASK_BITS6);

  do {
    int c;
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);
    for (c = 0; c < w; c += 16) {
      const __m128i v_resl_w = blend_8(src0 + c, src1 + c,
                                       v_m0_w, v_m1_w);
      const __m128i v_resh_w = blend_8(src0 + c + 8, src1 + c + 8,
                                       v_m0_w, v_m1_w);

      const __m128i v_res_b = _mm_packus_epi16(v_resl_w, v_resh_w);

      xx_storeu_128(dst + c, v_res_b);
    }
    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

//////////////////////////////////////////////////////////////////////////////
// Dispatch
//////////////////////////////////////////////////////////////////////////////

void vpx_blend_vmask6b_sse4_1(uint8_t *dst, uint32_t dst_stride,
                              uint8_t *src0, uint32_t src0_stride,
                              uint8_t *src1, uint32_t src1_stride,
                              const uint8_t *mask, int h, int w) {
  typedef  void (*blend_fn)(uint8_t *dst, uint32_t dst_stride,
                            uint8_t *src0, uint32_t src0_stride,
                            uint8_t *src1, uint32_t src1_stride,
                            const uint8_t *mask, int h, int w);

  static blend_fn blend[3] = {  // width_index
    blend_vmask6b_w16n_sse4_1,   // w % 16 == 0
    blend_vmask6b_w4_sse4_1,     // w == 4
    blend_vmask6b_w8_sse4_1,     // w == 8
  };

  assert(IMPLIES(src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES(src1 == dst, src1_stride == dst_stride));

  assert(h >= 4);
  assert(w >= 4);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  blend[(w >> 2) & 3](dst, dst_stride,
                      src0, src0_stride,
                      src1, src1_stride,
                      mask, h, w);
}

#if CONFIG_VP9_HIGHBITDEPTH
//////////////////////////////////////////////////////////////////////////////
// Common kernels
//////////////////////////////////////////////////////////////////////////////

typedef __m128i (*blend_unit_fn)(uint16_t*src0, uint16_t *src1,
                                 const __m128i v_m0_w, const __m128i v_m1_w);

static INLINE __m128i blend_4_b10(uint16_t*src0, uint16_t *src1,
                                  const __m128i v_m0_w, const __m128i v_m1_w) {
  const __m128i v_s0_w = xx_loadl_64(src0);
  const __m128i v_s1_w = xx_loadl_64(src1);

  const __m128i v_p0_w = _mm_mullo_epi16(v_s0_w, v_m0_w);
  const __m128i v_p1_w = _mm_mullo_epi16(v_s1_w, v_m1_w);

  const __m128i v_sum_w = _mm_add_epi16(v_p0_w, v_p1_w);

  const __m128i v_res_w = xx_roundn_epu16(v_sum_w, MASK_BITS6);

  return v_res_w;
}

static INLINE __m128i blend_8_b10(uint16_t*src0, uint16_t *src1,
                                  const __m128i v_m0_w, const __m128i v_m1_w) {
  const __m128i v_s0_w = xx_loadu_128(src0);
  const __m128i v_s1_w = xx_loadu_128(src1);

  const __m128i v_p0_w = _mm_mullo_epi16(v_s0_w, v_m0_w);
  const __m128i v_p1_w = _mm_mullo_epi16(v_s1_w, v_m1_w);

  const __m128i v_sum_w = _mm_add_epi16(v_p0_w, v_p1_w);

  const __m128i v_res_w = xx_roundn_epu16(v_sum_w, MASK_BITS6);

  return v_res_w;
}

static INLINE __m128i blend_4_b12(uint16_t*src0, uint16_t *src1,
                                  const __m128i v_m0_w, const __m128i v_m1_w) {
  const __m128i v_s0_w = xx_loadl_64(src0);
  const __m128i v_s1_w = xx_loadl_64(src1);

  // Interleave
  const __m128i v_m01_w = _mm_unpacklo_epi16(v_m0_w, v_m1_w);
  const __m128i v_s01_w = _mm_unpacklo_epi16(v_s0_w, v_s1_w);

  // Multiply-Add
  const __m128i v_sum_d = _mm_madd_epi16(v_s01_w, v_m01_w);

  // Scale
  const __m128i v_ssum_d = _mm_srli_epi32(v_sum_d, MASK_BITS6 - 1);

  // Pack
  const __m128i v_pssum_d = _mm_packs_epi32(v_ssum_d, v_ssum_d);

  // Round
  const __m128i v_res_w = xx_round_epu16(v_pssum_d);

  return v_res_w;
}

static INLINE __m128i blend_8_b12(uint16_t*src0, uint16_t *src1,
                                  const __m128i v_m0_w, const __m128i v_m1_w) {
  const __m128i v_s0_w = xx_loadu_128(src0);
  const __m128i v_s1_w = xx_loadu_128(src1);

  // Interleave
  const __m128i v_m01l_w = _mm_unpacklo_epi16(v_m0_w, v_m1_w);
  const __m128i v_m01h_w = _mm_unpackhi_epi16(v_m0_w, v_m1_w);
  const __m128i v_s01l_w = _mm_unpacklo_epi16(v_s0_w, v_s1_w);
  const __m128i v_s01h_w = _mm_unpackhi_epi16(v_s0_w, v_s1_w);

  // Multiply-Add
  const __m128i v_suml_d = _mm_madd_epi16(v_s01l_w, v_m01l_w);
  const __m128i v_sumh_d = _mm_madd_epi16(v_s01h_w, v_m01h_w);

  // Scale
  const __m128i v_ssuml_d = _mm_srli_epi32(v_suml_d, MASK_BITS6 - 1);
  const __m128i v_ssumh_d = _mm_srli_epi32(v_sumh_d, MASK_BITS6 - 1);

  // Pack
  const __m128i v_pssum_d = _mm_packs_epi32(v_ssuml_d, v_ssumh_d);

  // Round
  const __m128i v_res_w = xx_round_epu16(v_pssum_d);

  return v_res_w;
}

//////////////////////////////////////////////////////////////////////////////
// Implementation - No sub-sampling
//////////////////////////////////////////////////////////////////////////////

static INLINE void blend_vmask6b_bn_w4_sse4_1(
    uint16_t *dst, uint32_t dst_stride,
    uint16_t *src0, uint32_t src0_stride,
    uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, blend_unit_fn blend) {
  const __m128i v_maxval_w = _mm_set1_epi16(1 << MASK_BITS6);

  do {
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);

    const __m128i v_res_w = blend(src0, src1, v_m0_w, v_m1_w);

    xx_storel_64(dst, v_res_w);

    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

static void blend_vmask6b_b10_w4_sse4_1(
    uint16_t *dst, uint32_t dst_stride,
    uint16_t *src0, uint32_t src0_stride,
    uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  (void)w;
  blend_vmask6b_bn_w4_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                             src1_stride, mask, h,
                             blend_4_b10);
}

static void blend_vmask6b_b12_w4_sse4_1(
    uint16_t *dst, uint32_t dst_stride,
    uint16_t *src0, uint32_t src0_stride,
    uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  (void)w;
  blend_vmask6b_bn_w4_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                             src1_stride, mask, h,
                             blend_4_b12);
}

static inline void blend_vmask6b_bn_w8n_sse4_1(
    uint16_t *dst, uint32_t dst_stride,
    uint16_t *src0, uint32_t src0_stride,
    uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w, blend_unit_fn blend) {
  const __m128i v_maxval_w = _mm_set1_epi16(1 << MASK_BITS6);

  do {
    int c;
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);
    for (c = 0; c < w; c += 8) {
      const __m128i v_res_w = blend(src0 + c, src1 + c, v_m0_w, v_m1_w);

      xx_storeu_128(dst + c, v_res_w);
    }
    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

static void blend_vmask6b_b10_w8n_sse4_1(
    uint16_t *dst, uint32_t dst_stride,
    uint16_t *src0, uint32_t src0_stride,
    uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  blend_vmask6b_bn_w8n_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                              src1_stride, mask, h, w,
                              blend_8_b10);
}

static void blend_vmask6b_b12_w8n_sse4_1(
    uint16_t *dst, uint32_t dst_stride,
    uint16_t *src0, uint32_t src0_stride,
    uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, int w) {
  blend_vmask6b_bn_w8n_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                              src1_stride, mask, h, w,
                              blend_8_b12);
}

//////////////////////////////////////////////////////////////////////////////
// Dispatch
//////////////////////////////////////////////////////////////////////////////

void vpx_highbd_blend_vmask6b_sse4_1(uint8_t *dst_8, uint32_t dst_stride,
                                     uint8_t *src0_8, uint32_t src0_stride,
                                     uint8_t *src1_8, uint32_t src1_stride,
                                     const uint8_t *mask,
                                     int h, int w, int bd) {
  uint16_t *const dst = CONVERT_TO_SHORTPTR(dst_8);
  uint16_t *const src0 = CONVERT_TO_SHORTPTR(src0_8);
  uint16_t *const src1 = CONVERT_TO_SHORTPTR(src1_8);

  typedef  void (*blend_fn)(uint16_t *dst, uint32_t dst_stride,
                            uint16_t *src0, uint32_t src0_stride,
                            uint16_t *src1, uint32_t src1_stride,
                            const uint8_t *mask, int h, int w);

  static blend_fn blend[2][2] = {  // bd_index X width_index
    {     // bd == 8 or 10
      blend_vmask6b_b10_w8n_sse4_1,  // w % 8 == 0
      blend_vmask6b_b10_w4_sse4_1,   // w == 4
    }, {  // bd == 12
      blend_vmask6b_b12_w8n_sse4_1,  // w % 8 == 0
      blend_vmask6b_b12_w4_sse4_1,   // w == 4
    }
  };

  assert(IMPLIES(src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES(src1 == dst, src1_stride == dst_stride));

  assert(h >= 4);
  assert(w >= 4);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  assert(bd == 8 || bd == 10 || bd == 12);

  blend[bd == 12][(w >> 2) & 1](dst, dst_stride,
                                src0, src0_stride,
                                src1, src1_stride,
                                mask, h, w);
}
#endif  // CONFIG_VP9_HIGHBITDEPTH
