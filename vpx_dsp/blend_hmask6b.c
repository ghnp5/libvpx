/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "vpx/vpx_integer.h"
#include "vpx_ports/mem.h"
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_dsp/blend_mask.h"

#include "./vpx_dsp_rtcd.h"

void vpx_blend_hmask6b_c(uint8_t *dst, uint32_t dst_stride,
                         uint8_t *src0, uint32_t src0_stride,
                         uint8_t *src1, uint32_t src1_stride,
                         const uint8_t *mask, int h, int w) {
  int i, j;

  assert(IMPLIES(src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES(src1 == dst, src1_stride == dst_stride));

  assert(h >= 4);
  assert(w >= 4);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      const int m0 = mask[j];
      const int m1 = (1 << MASK_BITS6) - m0;
      dst[i * dst_stride + j] =
          ROUND_POWER_OF_TWO(src0[i * src0_stride + j] * m0 +
                             src1[i * src1_stride + j] * m1, MASK_BITS6);
    }
  }
}

#if CONFIG_VP9_HIGHBITDEPTH
void vpx_highbd_blend_hmask6b_c(uint8_t *dst_8, uint32_t dst_stride,
                                uint8_t *src0_8, uint32_t src0_stride,
                                uint8_t *src1_8, uint32_t src1_stride,
                                const uint8_t *mask, int h, int w, int bd) {
  int i, j;
  uint16_t *dst = CONVERT_TO_SHORTPTR(dst_8);
  uint16_t *src0 = CONVERT_TO_SHORTPTR(src0_8);
  uint16_t *src1 = CONVERT_TO_SHORTPTR(src1_8);

  assert(IMPLIES(src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES(src1 == dst, src1_stride == dst_stride));

  assert(h >= 2);
  assert(w >= 2);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  assert(bd == 8 || bd == 10 || bd == 12);

  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      const int m0 = mask[j];
      const int m1 = (1 << MASK_BITS6) - m0;
      dst[i * dst_stride + j] =
          ROUND_POWER_OF_TWO(src0[i * src0_stride + j] * m0 +
                             src1[i * src1_stride + j] * m1, MASK_BITS6);
    }
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH
