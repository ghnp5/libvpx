/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>
#include <assert.h>

#include "./vp9_rtcd.h"
#include "./vpx_config.h"
#include "vp9/common/vp9_common.h"
#include "vpx_dsp/arm/idct_neon.h"
#include "vpx_dsp/arm/mem_neon.h"
#include "vpx_dsp/txfm_common.h"

static INLINE void iadst4(int16x8_t *const io) {
  const int32x4_t c3 = vdupq_n_s32(sinpi_3_9);
  int16x4_t c[4], x[4];
  int32x4_t s[8], output[4];

  c[1] = vdup_n_s16(sinpi_1_9);
  c[2] = vdup_n_s16(sinpi_2_9);
  c[3] = vdup_n_s16(sinpi_3_9);
  c[4] = vdup_n_s16(sinpi_4_9);

  x[0] = vget_low_s16(io[0]);
  x[1] = vget_low_s16(io[1]);
  x[2] = vget_high_s16(io[0]);
  x[3] = vget_high_s16(io[1]);

  s[0] = vmull_s16(c[1], x[0]);
  s[1] = vmull_s16(c[2], x[0]);
  s[2] = vmull_s16(c[3], x[1]);
  s[3] = vmull_s16(c[4], x[2]);
  s[4] = vmull_s16(c[1], x[2]);
  s[5] = vmull_s16(c[2], x[3]);
  s[6] = vmull_s16(c[4], x[3]);
  s[7] = vaddl_s16(x[0], x[3]);
  s[7] = vsubw_s16(s[7], x[2]);

  s[0] = vaddq_s32(s[0], s[3]);
  s[0] = vaddq_s32(s[0], s[5]);
  s[1] = vsubq_s32(s[1], s[4]);
  s[1] = vsubq_s32(s[1], s[6]);
  s[3] = s[2];
  s[2] = vmulq_s32(c3, s[7]);

  output[0] = vaddq_s32(s[0], s[3]);
  output[1] = vaddq_s32(s[1], s[3]);
  output[2] = s[2];
  output[3] = vaddq_s32(s[0], s[1]);
  output[3] = vsubq_s32(output[3], s[3]);
  dct_const_round_shift_low_8dual(output, &io[0], &io[1]);
}

void vp9_iht4x4_16_add_neon(const tran_low_t *input, uint8_t *dest, int stride,
                            int tx_type) {
  int16x8_t a[2];
  uint8x8_t s[2], d[2];
  uint16x8_t sum[2];

  assert(!((intptr_t)dest % sizeof(uint32_t)));
  assert(!(stride % sizeof(uint32_t)));

  a[0] = load_tran_low_to_s16q(input);
  a[1] = load_tran_low_to_s16q(input + 8);
  transpose_s16_4x4q(&a[0], &a[1]);

  switch (tx_type) {
    case 0:  // DCT_DCT
      idct4x4_16_kernel_bd8(a);
      a[1] = vcombine_s16(vget_high_s16(a[1]), vget_low_s16(a[1]));
      transpose_s16_4x4q(&a[0], &a[1]);
      idct4x4_16_kernel_bd8(a);
      a[1] = vcombine_s16(vget_high_s16(a[1]), vget_low_s16(a[1]));
      break;

    case 1:  // ADST_DCT
      idct4x4_16_kernel_bd8(a);
      a[1] = vcombine_s16(vget_high_s16(a[1]), vget_low_s16(a[1]));
      transpose_s16_4x4q(&a[0], &a[1]);
      iadst4(a);
      break;

    case 2:  // DCT_ADST
      iadst4(a);
      transpose_s16_4x4q(&a[0], &a[1]);
      idct4x4_16_kernel_bd8(a);
      a[1] = vcombine_s16(vget_high_s16(a[1]), vget_low_s16(a[1]));
      break;

    case 3:  // ADST_ADST
      iadst4(a);
      transpose_s16_4x4q(&a[0], &a[1]);
      iadst4(a);
      break;

    default: assert(0); break;
  }

  a[0] = vrshrq_n_s16(a[0], 4);
  a[1] = vrshrq_n_s16(a[1], 4);
  s[0] = load_u8(dest, stride);
  s[1] = load_u8(dest + 2 * stride, stride);
  sum[0] = vaddw_u8(vreinterpretq_u16_s16(a[0]), s[0]);
  sum[1] = vaddw_u8(vreinterpretq_u16_s16(a[1]), s[1]);
  d[0] = vqmovun_s16(vreinterpretq_s16_u16(sum[0]));
  d[1] = vqmovun_s16(vreinterpretq_s16_u16(sum[1]));
  store_u8(dest, stride, d[0]);
  store_u8(dest + 2 * stride, stride, d[1]);
}
