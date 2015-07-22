/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_COMMON_VP9_IDCT_H_
#define VP9_COMMON_VP9_IDCT_H_

#include <assert.h>

#include "./vpx_config.h"
#include "vpx_dsp/txfm_common.h"
#if CONFIG_VP9_HIGHBITDEPTH
#include "vpx_dsp/vpx_dsp_common.h"
#endif  // CONFIG_VP9_HIGHBITDEPTH
#include "vpx_ports/mem.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

static INLINE tran_low_t check_range(tran_high_t input) {
#if CONFIG_COEFFICIENT_RANGE_CHECKING
  // For valid VP9 input streams, intermediate stage coefficients should always
  // stay within the range of a signed 16 bit integer. Coefficients can go out
  // of this range for invalid/corrupt VP9 streams. However, strictly checking
  // this range for every intermediate coefficient can burdensome for a decoder,
  // therefore the following assertion is only enabled when configured with
  // --enable-coefficient-range-checking.
  assert(INT16_MIN <= input);
  assert(input <= INT16_MAX);
#endif  // CONFIG_COEFFICIENT_RANGE_CHECKING
  return (tran_low_t)input;
}

static INLINE tran_low_t dct_const_round_shift(tran_high_t input) {
  tran_high_t rv = ROUND_POWER_OF_TWO(input, DCT_CONST_BITS);
  return check_range(rv);
}

#if CONFIG_VP9_HIGHBITDEPTH
static INLINE tran_low_t highbd_check_range(tran_high_t input,
                                            int bd) {
#if CONFIG_COEFFICIENT_RANGE_CHECKING
  // For valid highbitdepth VP9 streams, intermediate stage coefficients will
  // stay within the ranges:
  // - 8 bit: signed 16 bit integer
  // - 10 bit: signed 18 bit integer
  // - 12 bit: signed 20 bit integer
  const int32_t int_max = (1 << (7 + bd)) - 1;
  const int32_t int_min = -int_max - 1;
  assert(int_min <= input);
  assert(input <= int_max);
  (void) int_min;
#endif  // CONFIG_COEFFICIENT_RANGE_CHECKING
  (void) bd;
  return (tran_low_t)input;
}

static INLINE tran_low_t highbd_dct_const_round_shift(tran_high_t input,
                                                      int bd) {
  tran_high_t rv = ROUND_POWER_OF_TWO(input, DCT_CONST_BITS);
  return highbd_check_range(rv, bd);
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

typedef void (*transform_1d)(const tran_low_t*, tran_low_t*);

typedef struct {
  transform_1d cols, rows;  // vertical and horizontal
} transform_2d;

#if CONFIG_VP9_HIGHBITDEPTH
typedef void (*highbd_transform_1d)(const tran_low_t*, tran_low_t*, int bd);

typedef struct {
  highbd_transform_1d cols, rows;  // vertical and horizontal
} highbd_transform_2d;
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_EMULATE_HARDWARE
// When CONFIG_EMULATE_HARDWARE is 1 the transform performs a
// non-normative method to handle overflows. A stream that causes
// overflows  in the inverse transform is considered invalid in VP9,
// and a hardware implementer is free to choose any reasonable
// method to handle overflows. However to aid in hardware
// verification they can use a specific implementation of the
// WRAPLOW() macro below that is identical to their intended
// hardware implementation (and also use configure options to trigger
// the C-implementation of the transform).
//
// The particular WRAPLOW implementation below performs strict
// overflow wrapping to match common hardware implementations.
// bd of 8 uses trans_low with 16bits, need to remove 16bits
// bd of 10 uses trans_low with 18bits, need to remove 14bits
// bd of 12 uses trans_low with 20bits, need to remove 12bits
// bd of x uses trans_low with 8+x bits, need to remove 24-x bits
#define WRAPLOW(x, bd) ((((int32_t)(x)) << (24 - bd)) >> (24 - bd))
#else
#define WRAPLOW(x, bd) ((int32_t)(x))
#endif  // CONFIG_EMULATE_HARDWARE

void vp9_iwht4x4_add(const tran_low_t *input, uint8_t *dest, int stride,
                     int eob);
void vp9_idct4x4_add(const tran_low_t *input, uint8_t *dest, int stride,
                     int eob);
void vp9_idct8x8_add(const tran_low_t *input, uint8_t *dest, int stride,
                     int eob);
void vp9_idct16x16_add(const tran_low_t *input, uint8_t *dest, int stride, int
                       eob);
void vp9_idct32x32_add(const tran_low_t *input, uint8_t *dest, int stride,
                       int eob);

void vp9_iht4x4_add(TX_TYPE tx_type, const tran_low_t *input, uint8_t *dest,
                    int stride, int eob);
void vp9_iht8x8_add(TX_TYPE tx_type, const tran_low_t *input, uint8_t *dest,
                    int stride, int eob);
void vp9_iht16x16_add(TX_TYPE tx_type, const tran_low_t *input, uint8_t *dest,
                      int stride, int eob);

#if CONFIG_VP9_HIGHBITDEPTH
void vp9_highbd_idct4(const tran_low_t *input, tran_low_t *output, int bd);
void vp9_highbd_idct8(const tran_low_t *input, tran_low_t *output, int bd);
void vp9_highbd_idct16(const tran_low_t *input, tran_low_t *output, int bd);
void vp9_highbd_iwht4x4_add(const tran_low_t *input, uint8_t *dest, int stride,
                            int eob, int bd);
void vp9_highbd_idct4x4_add(const tran_low_t *input, uint8_t *dest, int stride,
                            int eob, int bd);
void vp9_highbd_idct8x8_add(const tran_low_t *input, uint8_t *dest, int stride,
                            int eob, int bd);
void vp9_highbd_idct16x16_add(const tran_low_t *input, uint8_t *dest,
                              int stride, int eob, int bd);
void vp9_highbd_idct32x32_add(const tran_low_t *input, uint8_t *dest,
                              int stride, int eob, int bd);
void vp9_highbd_iht4x4_add(TX_TYPE tx_type, const tran_low_t *input,
                           uint8_t *dest, int stride, int eob, int bd);
void vp9_highbd_iht8x8_add(TX_TYPE tx_type, const tran_low_t *input,
                           uint8_t *dest, int stride, int eob, int bd);
void vp9_highbd_iht16x16_add(TX_TYPE tx_type, const tran_low_t *input,
                             uint8_t *dest, int stride, int eob, int bd);
static INLINE uint16_t highbd_clip_pixel_add(uint16_t dest, tran_high_t trans,
                                             int bd) {
  trans = WRAPLOW(trans, bd);
  return clip_pixel_highbd(WRAPLOW(dest + trans, bd), bd);
}
#endif  // CONFIG_VP9_HIGHBITDEPTH
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP9_COMMON_VP9_IDCT_H_
