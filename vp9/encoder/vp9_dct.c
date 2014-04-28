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
#include <math.h>

#include "./vpx_config.h"
#include "./vp9_rtcd.h"

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_idct.h"
#include "vp9/common/vp9_systemdependent.h"

#include "vp9/encoder/vp9_dct.h"

static INLINE int fdct_round_shift(int input) {
  int rv = ROUND_POWER_OF_TWO(input, DCT_CONST_BITS);
  assert(INT16_MIN <= rv && rv <= INT16_MAX);
  return rv;
}

static void fdct4(const int16_t *input, int16_t *output) {
  int16_t step[4];
  int temp1, temp2;

  step[0] = input[0] + input[3];
  step[1] = input[1] + input[2];
  step[2] = input[1] - input[2];
  step[3] = input[0] - input[3];

  temp1 = (step[0] + step[1]) * cospi_16_64;
  temp2 = (step[0] - step[1]) * cospi_16_64;
  output[0] = fdct_round_shift(temp1);
  output[2] = fdct_round_shift(temp2);
  temp1 = step[2] * cospi_24_64 + step[3] * cospi_8_64;
  temp2 = -step[2] * cospi_8_64 + step[3] * cospi_24_64;
  output[1] = fdct_round_shift(temp1);
  output[3] = fdct_round_shift(temp2);
}

void vp9_fdct4x4_c(const int16_t *input, int16_t *output, int stride) {
  // The 2D transform is done with two passes which are actually pretty
  // similar. In the first one, we transform the columns and transpose
  // the results. In the second one, we transform the rows. To achieve that,
  // as the first pass results are transposed, we tranpose the columns (that
  // is the transposed rows) and transpose the results (so that it goes back
  // in normal/row positions).
  int pass;
  // We need an intermediate buffer between passes.
  int16_t intermediate[4 * 4];
  const int16_t *in = input;
  int16_t *out = intermediate;
  // Do the two transform/transpose passes
  for (pass = 0; pass < 2; ++pass) {
    /*canbe16*/ int input[4];
    /*canbe16*/ int step[4];
    /*needs32*/ int temp1, temp2;
    int i;
    for (i = 0; i < 4; ++i) {
      // Load inputs.
      if (0 == pass) {
        input[0] = in[0 * stride] * 16;
        input[1] = in[1 * stride] * 16;
        input[2] = in[2 * stride] * 16;
        input[3] = in[3 * stride] * 16;
        if (i == 0 && input[0]) {
          input[0] += 1;
        }
      } else {
        input[0] = in[0 * 4];
        input[1] = in[1 * 4];
        input[2] = in[2 * 4];
        input[3] = in[3 * 4];
      }
      // Transform.
      step[0] = input[0] + input[3];
      step[1] = input[1] + input[2];
      step[2] = input[1] - input[2];
      step[3] = input[0] - input[3];
      temp1 = (step[0] + step[1]) * cospi_16_64;
      temp2 = (step[0] - step[1]) * cospi_16_64;
      out[0] = fdct_round_shift(temp1);
      out[2] = fdct_round_shift(temp2);
      temp1 = step[2] * cospi_24_64 + step[3] * cospi_8_64;
      temp2 = -step[2] * cospi_8_64 + step[3] * cospi_24_64;
      out[1] = fdct_round_shift(temp1);
      out[3] = fdct_round_shift(temp2);
      // Do next column (which is a transposed row in second/horizontal pass)
      in++;
      out += 4;
    }
    // Setup in/out for next pass.
    in = intermediate;
    out = output;
  }

  {
    int i, j;
    for (i = 0; i < 4; ++i) {
      for (j = 0; j < 4; ++j)
        output[j + i * 4] = (output[j + i * 4] + 1) >> 2;
    }
  }
}

static void fadst4(const int16_t *input, int16_t *output) {
  int x0, x1, x2, x3;
  int s0, s1, s2, s3, s4, s5, s6, s7;

  x0 = input[0];
  x1 = input[1];
  x2 = input[2];
  x3 = input[3];

  if (!(x0 | x1 | x2 | x3)) {
    output[0] = output[1] = output[2] = output[3] = 0;
    return;
  }

  s0 = sinpi_1_9 * x0;
  s1 = sinpi_4_9 * x0;
  s2 = sinpi_2_9 * x1;
  s3 = sinpi_1_9 * x1;
  s4 = sinpi_3_9 * x2;
  s5 = sinpi_4_9 * x3;
  s6 = sinpi_2_9 * x3;
  s7 = x0 + x1 - x3;

  x0 = s0 + s2 + s5;
  x1 = sinpi_3_9 * s7;
  x2 = s1 - s3 + s6;
  x3 = s4;

  s0 = x0 + x3;
  s1 = x1;
  s2 = x2 - x3;
  s3 = x2 - x0 + x3;

  // 1-D transform scaling factor is sqrt(2).
  output[0] = fdct_round_shift(s0);
  output[1] = fdct_round_shift(s1);
  output[2] = fdct_round_shift(s2);
  output[3] = fdct_round_shift(s3);
}

static const transform_2d FHT_4[] = {
  { fdct4,  fdct4  },  // DCT_DCT  = 0
  { fadst4, fdct4  },  // ADST_DCT = 1
  { fdct4,  fadst4 },  // DCT_ADST = 2
  { fadst4, fadst4 }   // ADST_ADST = 3
};

void vp9_short_fht4x4_c(const int16_t *input, int16_t *output,
                        int stride, int tx_type) {
  int16_t out[4 * 4];
  int16_t *outptr = &out[0];
  int i, j;
  int16_t temp_in[4], temp_out[4];
  const transform_2d ht = FHT_4[tx_type];

  // Columns
  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 4; ++j)
      temp_in[j] = input[j * stride + i] * 16;
    if (i == 0 && temp_in[0])
      temp_in[0] += 1;
    ht.cols(temp_in, temp_out);
    for (j = 0; j < 4; ++j)
      outptr[j * 4 + i] = temp_out[j];
  }

  // Rows
  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 4; ++j)
      temp_in[j] = out[j + i * 4];
    ht.rows(temp_in, temp_out);
    for (j = 0; j < 4; ++j)
      output[j + i * 4] = (temp_out[j] + 1) >> 2;
  }
}

static void fdct8(const int16_t *input, int16_t *output) {
  /*canbe16*/ int s0, s1, s2, s3, s4, s5, s6, s7;
  /*needs32*/ int t0, t1, t2, t3;
  /*canbe16*/ int x0, x1, x2, x3;

  // stage 1
  s0 = input[0] + input[7];
  s1 = input[1] + input[6];
  s2 = input[2] + input[5];
  s3 = input[3] + input[4];
  s4 = input[3] - input[4];
  s5 = input[2] - input[5];
  s6 = input[1] - input[6];
  s7 = input[0] - input[7];

  // fdct4(step, step);
  x0 = s0 + s3;
  x1 = s1 + s2;
  x2 = s1 - s2;
  x3 = s0 - s3;
  t0 = (x0 + x1) * cospi_16_64;
  t1 = (x0 - x1) * cospi_16_64;
  t2 =  x2 * cospi_24_64 + x3 *  cospi_8_64;
  t3 = -x2 * cospi_8_64  + x3 * cospi_24_64;
  output[0] = fdct_round_shift(t0);
  output[2] = fdct_round_shift(t2);
  output[4] = fdct_round_shift(t1);
  output[6] = fdct_round_shift(t3);

  // Stage 2
  t0 = (s6 - s5) * cospi_16_64;
  t1 = (s6 + s5) * cospi_16_64;
  t2 = fdct_round_shift(t0);
  t3 = fdct_round_shift(t1);

  // Stage 3
  x0 = s4 + t2;
  x1 = s4 - t2;
  x2 = s7 - t3;
  x3 = s7 + t3;

  // Stage 4
  t0 = x0 * cospi_28_64 + x3 *   cospi_4_64;
  t1 = x1 * cospi_12_64 + x2 *  cospi_20_64;
  t2 = x2 * cospi_12_64 + x1 * -cospi_20_64;
  t3 = x3 * cospi_28_64 + x0 *  -cospi_4_64;
  output[1] = fdct_round_shift(t0);
  output[3] = fdct_round_shift(t2);
  output[5] = fdct_round_shift(t1);
  output[7] = fdct_round_shift(t3);
}

void vp9_fdct8x8_c(const int16_t *input, int16_t *final_output, int stride) {
  int i, j;
  int16_t intermediate[64];

  // Transform columns
  {
    int16_t *output = intermediate;
    /*canbe16*/ int s0, s1, s2, s3, s4, s5, s6, s7;
    /*needs32*/ int t0, t1, t2, t3;
    /*canbe16*/ int x0, x1, x2, x3;

    int i;
    for (i = 0; i < 8; i++) {
      // stage 1
      s0 = (input[0 * stride] + input[7 * stride]) * 4;
      s1 = (input[1 * stride] + input[6 * stride]) * 4;
      s2 = (input[2 * stride] + input[5 * stride]) * 4;
      s3 = (input[3 * stride] + input[4 * stride]) * 4;
      s4 = (input[3 * stride] - input[4 * stride]) * 4;
      s5 = (input[2 * stride] - input[5 * stride]) * 4;
      s6 = (input[1 * stride] - input[6 * stride]) * 4;
      s7 = (input[0 * stride] - input[7 * stride]) * 4;

      // fdct4(step, step);
      x0 = s0 + s3;
      x1 = s1 + s2;
      x2 = s1 - s2;
      x3 = s0 - s3;
      t0 = (x0 + x1) * cospi_16_64;
      t1 = (x0 - x1) * cospi_16_64;
      t2 =  x2 * cospi_24_64 + x3 *  cospi_8_64;
      t3 = -x2 * cospi_8_64  + x3 * cospi_24_64;
      output[0 * 8] = fdct_round_shift(t0);
      output[2 * 8] = fdct_round_shift(t2);
      output[4 * 8] = fdct_round_shift(t1);
      output[6 * 8] = fdct_round_shift(t3);

      // Stage 2
      t0 = (s6 - s5) * cospi_16_64;
      t1 = (s6 + s5) * cospi_16_64;
      t2 = fdct_round_shift(t0);
      t3 = fdct_round_shift(t1);

      // Stage 3
      x0 = s4 + t2;
      x1 = s4 - t2;
      x2 = s7 - t3;
      x3 = s7 + t3;

      // Stage 4
      t0 = x0 * cospi_28_64 + x3 *   cospi_4_64;
      t1 = x1 * cospi_12_64 + x2 *  cospi_20_64;
      t2 = x2 * cospi_12_64 + x1 * -cospi_20_64;
      t3 = x3 * cospi_28_64 + x0 *  -cospi_4_64;
      output[1 * 8] = fdct_round_shift(t0);
      output[3 * 8] = fdct_round_shift(t2);
      output[5 * 8] = fdct_round_shift(t1);
      output[7 * 8] = fdct_round_shift(t3);
      input++;
      output++;
    }
  }

  // Rows
  for (i = 0; i < 8; ++i) {
    fdct8(&intermediate[i * 8], &final_output[i * 8]);
    for (j = 0; j < 8; ++j)
      final_output[j + i * 8] /= 2;
  }
}

void vp9_fdct16x16_c(const int16_t *input, int16_t *output, int stride) {
  // The 2D transform is done with two passes which are actually pretty
  // similar. In the first one, we transform the columns and transpose
  // the results. In the second one, we transform the rows. To achieve that,
  // as the first pass results are transposed, we tranpose the columns (that
  // is the transposed rows) and transpose the results (so that it goes back
  // in normal/row positions).
  int pass;
  // We need an intermediate buffer between passes.
  int16_t intermediate[256];
  const int16_t *in = input;
  int16_t *out = intermediate;
  // Do the two transform/transpose passes
  for (pass = 0; pass < 2; ++pass) {
    /*canbe16*/ int step1[8];
    /*canbe16*/ int step2[8];
    /*canbe16*/ int step3[8];
    /*canbe16*/ int input[8];
    /*needs32*/ int temp1, temp2;
    int i;
    for (i = 0; i < 16; i++) {
      if (0 == pass) {
        // Calculate input for the first 8 results.
        input[0] = (in[0 * stride] + in[15 * stride]) * 4;
        input[1] = (in[1 * stride] + in[14 * stride]) * 4;
        input[2] = (in[2 * stride] + in[13 * stride]) * 4;
        input[3] = (in[3 * stride] + in[12 * stride]) * 4;
        input[4] = (in[4 * stride] + in[11 * stride]) * 4;
        input[5] = (in[5 * stride] + in[10 * stride]) * 4;
        input[6] = (in[6 * stride] + in[ 9 * stride]) * 4;
        input[7] = (in[7 * stride] + in[ 8 * stride]) * 4;
        // Calculate input for the next 8 results.
        step1[0] = (in[7 * stride] - in[ 8 * stride]) * 4;
        step1[1] = (in[6 * stride] - in[ 9 * stride]) * 4;
        step1[2] = (in[5 * stride] - in[10 * stride]) * 4;
        step1[3] = (in[4 * stride] - in[11 * stride]) * 4;
        step1[4] = (in[3 * stride] - in[12 * stride]) * 4;
        step1[5] = (in[2 * stride] - in[13 * stride]) * 4;
        step1[6] = (in[1 * stride] - in[14 * stride]) * 4;
        step1[7] = (in[0 * stride] - in[15 * stride]) * 4;
      } else {
        // Calculate input for the first 8 results.
        input[0] = ((in[0 * 16] + 1) >> 2) + ((in[15 * 16] + 1) >> 2);
        input[1] = ((in[1 * 16] + 1) >> 2) + ((in[14 * 16] + 1) >> 2);
        input[2] = ((in[2 * 16] + 1) >> 2) + ((in[13 * 16] + 1) >> 2);
        input[3] = ((in[3 * 16] + 1) >> 2) + ((in[12 * 16] + 1) >> 2);
        input[4] = ((in[4 * 16] + 1) >> 2) + ((in[11 * 16] + 1) >> 2);
        input[5] = ((in[5 * 16] + 1) >> 2) + ((in[10 * 16] + 1) >> 2);
        input[6] = ((in[6 * 16] + 1) >> 2) + ((in[ 9 * 16] + 1) >> 2);
        input[7] = ((in[7 * 16] + 1) >> 2) + ((in[ 8 * 16] + 1) >> 2);
        // Calculate input for the next 8 results.
        step1[0] = ((in[7 * 16] + 1) >> 2) - ((in[ 8 * 16] + 1) >> 2);
        step1[1] = ((in[6 * 16] + 1) >> 2) - ((in[ 9 * 16] + 1) >> 2);
        step1[2] = ((in[5 * 16] + 1) >> 2) - ((in[10 * 16] + 1) >> 2);
        step1[3] = ((in[4 * 16] + 1) >> 2) - ((in[11 * 16] + 1) >> 2);
        step1[4] = ((in[3 * 16] + 1) >> 2) - ((in[12 * 16] + 1) >> 2);
        step1[5] = ((in[2 * 16] + 1) >> 2) - ((in[13 * 16] + 1) >> 2);
        step1[6] = ((in[1 * 16] + 1) >> 2) - ((in[14 * 16] + 1) >> 2);
        step1[7] = ((in[0 * 16] + 1) >> 2) - ((in[15 * 16] + 1) >> 2);
      }
      // Work on the first eight values; fdct8(input, even_results);
      {
        /*canbe16*/ int s0, s1, s2, s3, s4, s5, s6, s7;
        /*needs32*/ int t0, t1, t2, t3;
        /*canbe16*/ int x0, x1, x2, x3;

        // stage 1
        s0 = input[0] + input[7];
        s1 = input[1] + input[6];
        s2 = input[2] + input[5];
        s3 = input[3] + input[4];
        s4 = input[3] - input[4];
        s5 = input[2] - input[5];
        s6 = input[1] - input[6];
        s7 = input[0] - input[7];

        // fdct4(step, step);
        x0 = s0 + s3;
        x1 = s1 + s2;
        x2 = s1 - s2;
        x3 = s0 - s3;
        t0 = (x0 + x1) * cospi_16_64;
        t1 = (x0 - x1) * cospi_16_64;
        t2 = x3 * cospi_8_64  + x2 * cospi_24_64;
        t3 = x3 * cospi_24_64 - x2 * cospi_8_64;
        out[0] = fdct_round_shift(t0);
        out[4] = fdct_round_shift(t2);
        out[8] = fdct_round_shift(t1);
        out[12] = fdct_round_shift(t3);

        // Stage 2
        t0 = (s6 - s5) * cospi_16_64;
        t1 = (s6 + s5) * cospi_16_64;
        t2 = fdct_round_shift(t0);
        t3 = fdct_round_shift(t1);

        // Stage 3
        x0 = s4 + t2;
        x1 = s4 - t2;
        x2 = s7 - t3;
        x3 = s7 + t3;

        // Stage 4
        t0 = x0 * cospi_28_64 + x3 *   cospi_4_64;
        t1 = x1 * cospi_12_64 + x2 *  cospi_20_64;
        t2 = x2 * cospi_12_64 + x1 * -cospi_20_64;
        t3 = x3 * cospi_28_64 + x0 *  -cospi_4_64;
        out[2] = fdct_round_shift(t0);
        out[6] = fdct_round_shift(t2);
        out[10] = fdct_round_shift(t1);
        out[14] = fdct_round_shift(t3);
      }
      // Work on the next eight values; step1 -> odd_results
      {
        // step 2
        temp1 = (step1[5] - step1[2]) * cospi_16_64;
        temp2 = (step1[4] - step1[3]) * cospi_16_64;
        step2[2] = fdct_round_shift(temp1);
        step2[3] = fdct_round_shift(temp2);
        temp1 = (step1[4] + step1[3]) * cospi_16_64;
        temp2 = (step1[5] + step1[2]) * cospi_16_64;
        step2[4] = fdct_round_shift(temp1);
        step2[5] = fdct_round_shift(temp2);
        // step 3
        step3[0] = step1[0] + step2[3];
        step3[1] = step1[1] + step2[2];
        step3[2] = step1[1] - step2[2];
        step3[3] = step1[0] - step2[3];
        step3[4] = step1[7] - step2[4];
        step3[5] = step1[6] - step2[5];
        step3[6] = step1[6] + step2[5];
        step3[7] = step1[7] + step2[4];
        // step 4
        temp1 = step3[1] *  -cospi_8_64 + step3[6] * cospi_24_64;
        temp2 = step3[2] * -cospi_24_64 - step3[5] *  cospi_8_64;
        step2[1] = fdct_round_shift(temp1);
        step2[2] = fdct_round_shift(temp2);
        temp1 = step3[2] * -cospi_8_64 + step3[5] * cospi_24_64;
        temp2 = step3[1] * cospi_24_64 + step3[6] *  cospi_8_64;
        step2[5] = fdct_round_shift(temp1);
        step2[6] = fdct_round_shift(temp2);
        // step 5
        step1[0] = step3[0] + step2[1];
        step1[1] = step3[0] - step2[1];
        step1[2] = step3[3] - step2[2];
        step1[3] = step3[3] + step2[2];
        step1[4] = step3[4] + step2[5];
        step1[5] = step3[4] - step2[5];
        step1[6] = step3[7] - step2[6];
        step1[7] = step3[7] + step2[6];
        // step 6
        temp1 = step1[0] * cospi_30_64 + step1[7] *  cospi_2_64;
        temp2 = step1[1] * cospi_14_64 + step1[6] * cospi_18_64;
        out[1] = fdct_round_shift(temp1);
        out[9] = fdct_round_shift(temp2);
        temp1 = step1[2] * cospi_22_64 + step1[5] * cospi_10_64;
        temp2 = step1[3] *  cospi_6_64 + step1[4] * cospi_26_64;
        out[5] = fdct_round_shift(temp1);
        out[13] = fdct_round_shift(temp2);
        temp1 = step1[3] * -cospi_26_64 + step1[4] *  cospi_6_64;
        temp2 = step1[2] * -cospi_10_64 + step1[5] * cospi_22_64;
        out[3] = fdct_round_shift(temp1);
        out[11] = fdct_round_shift(temp2);
        temp1 = step1[1] * -cospi_18_64 + step1[6] * cospi_14_64;
        temp2 = step1[0] *  -cospi_2_64 + step1[7] * cospi_30_64;
        out[7] = fdct_round_shift(temp1);
        out[15] = fdct_round_shift(temp2);
      }
      // Do next column (which is a transposed row in second/horizontal pass)
      in++;
      out += 16;
    }
    // Setup in/out for next pass.
    in = intermediate;
    out = output;
  }
}

static void fadst8(const int16_t *input, int16_t *output) {
  int s0, s1, s2, s3, s4, s5, s6, s7;

  int x0 = input[7];
  int x1 = input[0];
  int x2 = input[5];
  int x3 = input[2];
  int x4 = input[3];
  int x5 = input[4];
  int x6 = input[1];
  int x7 = input[6];

  // stage 1
  s0 = cospi_2_64  * x0 + cospi_30_64 * x1;
  s1 = cospi_30_64 * x0 - cospi_2_64  * x1;
  s2 = cospi_10_64 * x2 + cospi_22_64 * x3;
  s3 = cospi_22_64 * x2 - cospi_10_64 * x3;
  s4 = cospi_18_64 * x4 + cospi_14_64 * x5;
  s5 = cospi_14_64 * x4 - cospi_18_64 * x5;
  s6 = cospi_26_64 * x6 + cospi_6_64  * x7;
  s7 = cospi_6_64  * x6 - cospi_26_64 * x7;

  x0 = fdct_round_shift(s0 + s4);
  x1 = fdct_round_shift(s1 + s5);
  x2 = fdct_round_shift(s2 + s6);
  x3 = fdct_round_shift(s3 + s7);
  x4 = fdct_round_shift(s0 - s4);
  x5 = fdct_round_shift(s1 - s5);
  x6 = fdct_round_shift(s2 - s6);
  x7 = fdct_round_shift(s3 - s7);

  // stage 2
  s0 = x0;
  s1 = x1;
  s2 = x2;
  s3 = x3;
  s4 = cospi_8_64  * x4 + cospi_24_64 * x5;
  s5 = cospi_24_64 * x4 - cospi_8_64  * x5;
  s6 = - cospi_24_64 * x6 + cospi_8_64  * x7;
  s7 =   cospi_8_64  * x6 + cospi_24_64 * x7;

  x0 = s0 + s2;
  x1 = s1 + s3;
  x2 = s0 - s2;
  x3 = s1 - s3;
  x4 = fdct_round_shift(s4 + s6);
  x5 = fdct_round_shift(s5 + s7);
  x6 = fdct_round_shift(s4 - s6);
  x7 = fdct_round_shift(s5 - s7);

  // stage 3
  s2 = cospi_16_64 * (x2 + x3);
  s3 = cospi_16_64 * (x2 - x3);
  s6 = cospi_16_64 * (x6 + x7);
  s7 = cospi_16_64 * (x6 - x7);

  x2 = fdct_round_shift(s2);
  x3 = fdct_round_shift(s3);
  x6 = fdct_round_shift(s6);
  x7 = fdct_round_shift(s7);

  output[0] =   x0;
  output[1] = - x4;
  output[2] =   x6;
  output[3] = - x2;
  output[4] =   x3;
  output[5] = - x7;
  output[6] =   x5;
  output[7] = - x1;
}

static const transform_2d FHT_8[] = {
  { fdct8,  fdct8  },  // DCT_DCT  = 0
  { fadst8, fdct8  },  // ADST_DCT = 1
  { fdct8,  fadst8 },  // DCT_ADST = 2
  { fadst8, fadst8 }   // ADST_ADST = 3
};

void vp9_short_fht8x8_c(const int16_t *input, int16_t *output,
                        int stride, int tx_type) {
  int16_t out[64];
  int16_t *outptr = &out[0];
  int i, j;
  int16_t temp_in[8], temp_out[8];
  const transform_2d ht = FHT_8[tx_type];

  // Columns
  for (i = 0; i < 8; ++i) {
    for (j = 0; j < 8; ++j)
      temp_in[j] = input[j * stride + i] * 4;
    ht.cols(temp_in, temp_out);
    for (j = 0; j < 8; ++j)
      outptr[j * 8 + i] = temp_out[j];
  }

  // Rows
  for (i = 0; i < 8; ++i) {
    for (j = 0; j < 8; ++j)
      temp_in[j] = out[j + i * 8];
    ht.rows(temp_in, temp_out);
    for (j = 0; j < 8; ++j)
      output[j + i * 8] = (temp_out[j] + (temp_out[j] < 0)) >> 1;
  }
}

/* 4-point reversible, orthonormal Walsh-Hadamard in 3.5 adds, 0.5 shifts per
   pixel. */
void vp9_fwht4x4_c(const int16_t *input, int16_t *output, int stride) {
  int i;
  int a1, b1, c1, d1, e1;
  const int16_t *ip = input;
  int16_t *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0 * stride];
    b1 = ip[1 * stride];
    c1 = ip[2 * stride];
    d1 = ip[3 * stride];

    a1 += b1;
    d1 = d1 - c1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= c1;
    d1 += b1;
    op[0] = a1;
    op[4] = c1;
    op[8] = d1;
    op[12] = b1;

    ip++;
    op++;
  }
  ip = output;
  op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0];
    b1 = ip[1];
    c1 = ip[2];
    d1 = ip[3];

    a1 += b1;
    d1 -= c1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= c1;
    d1 += b1;
    op[0] = a1 * UNIT_QUANT_FACTOR;
    op[1] = c1 * UNIT_QUANT_FACTOR;
    op[2] = d1 * UNIT_QUANT_FACTOR;
    op[3] = b1 * UNIT_QUANT_FACTOR;

    ip += 4;
    op += 4;
  }
}

// Rewrote to use same algorithm as others.
static void fdct16(const int16_t in[16], int16_t out[16]) {
  /*canbe16*/ int step1[8];
  /*canbe16*/ int step2[8];
  /*canbe16*/ int step3[8];
  /*canbe16*/ int input[8];
  /*needs32*/ int temp1, temp2;

  // step 1
  input[0] = in[0] + in[15];
  input[1] = in[1] + in[14];
  input[2] = in[2] + in[13];
  input[3] = in[3] + in[12];
  input[4] = in[4] + in[11];
  input[5] = in[5] + in[10];
  input[6] = in[6] + in[ 9];
  input[7] = in[7] + in[ 8];

  step1[0] = in[7] - in[ 8];
  step1[1] = in[6] - in[ 9];
  step1[2] = in[5] - in[10];
  step1[3] = in[4] - in[11];
  step1[4] = in[3] - in[12];
  step1[5] = in[2] - in[13];
  step1[6] = in[1] - in[14];
  step1[7] = in[0] - in[15];

  // fdct8(step, step);
  {
    /*canbe16*/ int s0, s1, s2, s3, s4, s5, s6, s7;
    /*needs32*/ int t0, t1, t2, t3;
    /*canbe16*/ int x0, x1, x2, x3;

    // stage 1
    s0 = input[0] + input[7];
    s1 = input[1] + input[6];
    s2 = input[2] + input[5];
    s3 = input[3] + input[4];
    s4 = input[3] - input[4];
    s5 = input[2] - input[5];
    s6 = input[1] - input[6];
    s7 = input[0] - input[7];

    // fdct4(step, step);
    x0 = s0 + s3;
    x1 = s1 + s2;
    x2 = s1 - s2;
    x3 = s0 - s3;
    t0 = (x0 + x1) * cospi_16_64;
    t1 = (x0 - x1) * cospi_16_64;
    t2 = x3 * cospi_8_64  + x2 * cospi_24_64;
    t3 = x3 * cospi_24_64 - x2 * cospi_8_64;
    out[0] = fdct_round_shift(t0);
    out[4] = fdct_round_shift(t2);
    out[8] = fdct_round_shift(t1);
    out[12] = fdct_round_shift(t3);

    // Stage 2
    t0 = (s6 - s5) * cospi_16_64;
    t1 = (s6 + s5) * cospi_16_64;
    t2 = fdct_round_shift(t0);
    t3 = fdct_round_shift(t1);

    // Stage 3
    x0 = s4 + t2;
    x1 = s4 - t2;
    x2 = s7 - t3;
    x3 = s7 + t3;

    // Stage 4
    t0 = x0 * cospi_28_64 + x3 *   cospi_4_64;
    t1 = x1 * cospi_12_64 + x2 *  cospi_20_64;
    t2 = x2 * cospi_12_64 + x1 * -cospi_20_64;
    t3 = x3 * cospi_28_64 + x0 *  -cospi_4_64;
    out[2] = fdct_round_shift(t0);
    out[6] = fdct_round_shift(t2);
    out[10] = fdct_round_shift(t1);
    out[14] = fdct_round_shift(t3);
  }

  // step 2
  temp1 = (step1[5] - step1[2]) * cospi_16_64;
  temp2 = (step1[4] - step1[3]) * cospi_16_64;
  step2[2] = fdct_round_shift(temp1);
  step2[3] = fdct_round_shift(temp2);
  temp1 = (step1[4] + step1[3]) * cospi_16_64;
  temp2 = (step1[5] + step1[2]) * cospi_16_64;
  step2[4] = fdct_round_shift(temp1);
  step2[5] = fdct_round_shift(temp2);

  // step 3
  step3[0] = step1[0] + step2[3];
  step3[1] = step1[1] + step2[2];
  step3[2] = step1[1] - step2[2];
  step3[3] = step1[0] - step2[3];
  step3[4] = step1[7] - step2[4];
  step3[5] = step1[6] - step2[5];
  step3[6] = step1[6] + step2[5];
  step3[7] = step1[7] + step2[4];

  // step 4
  temp1 = step3[1] *  -cospi_8_64 + step3[6] * cospi_24_64;
  temp2 = step3[2] * -cospi_24_64 - step3[5] *  cospi_8_64;
  step2[1] = fdct_round_shift(temp1);
  step2[2] = fdct_round_shift(temp2);
  temp1 = step3[2] * -cospi_8_64 + step3[5] * cospi_24_64;
  temp2 = step3[1] * cospi_24_64 + step3[6] *  cospi_8_64;
  step2[5] = fdct_round_shift(temp1);
  step2[6] = fdct_round_shift(temp2);

  // step 5
  step1[0] = step3[0] + step2[1];
  step1[1] = step3[0] - step2[1];
  step1[2] = step3[3] - step2[2];
  step1[3] = step3[3] + step2[2];
  step1[4] = step3[4] + step2[5];
  step1[5] = step3[4] - step2[5];
  step1[6] = step3[7] - step2[6];
  step1[7] = step3[7] + step2[6];

  // step 6
  temp1 = step1[0] * cospi_30_64 + step1[7] *  cospi_2_64;
  temp2 = step1[1] * cospi_14_64 + step1[6] * cospi_18_64;
  out[1] = fdct_round_shift(temp1);
  out[9] = fdct_round_shift(temp2);

  temp1 = step1[2] * cospi_22_64 + step1[5] * cospi_10_64;
  temp2 = step1[3] *  cospi_6_64 + step1[4] * cospi_26_64;
  out[5] = fdct_round_shift(temp1);
  out[13] = fdct_round_shift(temp2);

  temp1 = step1[3] * -cospi_26_64 + step1[4] *  cospi_6_64;
  temp2 = step1[2] * -cospi_10_64 + step1[5] * cospi_22_64;
  out[3] = fdct_round_shift(temp1);
  out[11] = fdct_round_shift(temp2);

  temp1 = step1[1] * -cospi_18_64 + step1[6] * cospi_14_64;
  temp2 = step1[0] *  -cospi_2_64 + step1[7] * cospi_30_64;
  out[7] = fdct_round_shift(temp1);
  out[15] = fdct_round_shift(temp2);
}

static void fadst16(const int16_t *input, int16_t *output) {
  int s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15;

  int x0 = input[15];
  int x1 = input[0];
  int x2 = input[13];
  int x3 = input[2];
  int x4 = input[11];
  int x5 = input[4];
  int x6 = input[9];
  int x7 = input[6];
  int x8 = input[7];
  int x9 = input[8];
  int x10 = input[5];
  int x11 = input[10];
  int x12 = input[3];
  int x13 = input[12];
  int x14 = input[1];
  int x15 = input[14];

  // stage 1
  s0 = x0 * cospi_1_64  + x1 * cospi_31_64;
  s1 = x0 * cospi_31_64 - x1 * cospi_1_64;
  s2 = x2 * cospi_5_64  + x3 * cospi_27_64;
  s3 = x2 * cospi_27_64 - x3 * cospi_5_64;
  s4 = x4 * cospi_9_64  + x5 * cospi_23_64;
  s5 = x4 * cospi_23_64 - x5 * cospi_9_64;
  s6 = x6 * cospi_13_64 + x7 * cospi_19_64;
  s7 = x6 * cospi_19_64 - x7 * cospi_13_64;
  s8 = x8 * cospi_17_64 + x9 * cospi_15_64;
  s9 = x8 * cospi_15_64 - x9 * cospi_17_64;
  s10 = x10 * cospi_21_64 + x11 * cospi_11_64;
  s11 = x10 * cospi_11_64 - x11 * cospi_21_64;
  s12 = x12 * cospi_25_64 + x13 * cospi_7_64;
  s13 = x12 * cospi_7_64  - x13 * cospi_25_64;
  s14 = x14 * cospi_29_64 + x15 * cospi_3_64;
  s15 = x14 * cospi_3_64  - x15 * cospi_29_64;

  x0 = fdct_round_shift(s0 + s8);
  x1 = fdct_round_shift(s1 + s9);
  x2 = fdct_round_shift(s2 + s10);
  x3 = fdct_round_shift(s3 + s11);
  x4 = fdct_round_shift(s4 + s12);
  x5 = fdct_round_shift(s5 + s13);
  x6 = fdct_round_shift(s6 + s14);
  x7 = fdct_round_shift(s7 + s15);
  x8  = fdct_round_shift(s0 - s8);
  x9  = fdct_round_shift(s1 - s9);
  x10 = fdct_round_shift(s2 - s10);
  x11 = fdct_round_shift(s3 - s11);
  x12 = fdct_round_shift(s4 - s12);
  x13 = fdct_round_shift(s5 - s13);
  x14 = fdct_round_shift(s6 - s14);
  x15 = fdct_round_shift(s7 - s15);

  // stage 2
  s0 = x0;
  s1 = x1;
  s2 = x2;
  s3 = x3;
  s4 = x4;
  s5 = x5;
  s6 = x6;
  s7 = x7;
  s8 =    x8 * cospi_4_64   + x9 * cospi_28_64;
  s9 =    x8 * cospi_28_64  - x9 * cospi_4_64;
  s10 =   x10 * cospi_20_64 + x11 * cospi_12_64;
  s11 =   x10 * cospi_12_64 - x11 * cospi_20_64;
  s12 = - x12 * cospi_28_64 + x13 * cospi_4_64;
  s13 =   x12 * cospi_4_64  + x13 * cospi_28_64;
  s14 = - x14 * cospi_12_64 + x15 * cospi_20_64;
  s15 =   x14 * cospi_20_64 + x15 * cospi_12_64;

  x0 = s0 + s4;
  x1 = s1 + s5;
  x2 = s2 + s6;
  x3 = s3 + s7;
  x4 = s0 - s4;
  x5 = s1 - s5;
  x6 = s2 - s6;
  x7 = s3 - s7;
  x8 = fdct_round_shift(s8 + s12);
  x9 = fdct_round_shift(s9 + s13);
  x10 = fdct_round_shift(s10 + s14);
  x11 = fdct_round_shift(s11 + s15);
  x12 = fdct_round_shift(s8 - s12);
  x13 = fdct_round_shift(s9 - s13);
  x14 = fdct_round_shift(s10 - s14);
  x15 = fdct_round_shift(s11 - s15);

  // stage 3
  s0 = x0;
  s1 = x1;
  s2 = x2;
  s3 = x3;
  s4 = x4 * cospi_8_64  + x5 * cospi_24_64;
  s5 = x4 * cospi_24_64 - x5 * cospi_8_64;
  s6 = - x6 * cospi_24_64 + x7 * cospi_8_64;
  s7 =   x6 * cospi_8_64  + x7 * cospi_24_64;
  s8 = x8;
  s9 = x9;
  s10 = x10;
  s11 = x11;
  s12 = x12 * cospi_8_64  + x13 * cospi_24_64;
  s13 = x12 * cospi_24_64 - x13 * cospi_8_64;
  s14 = - x14 * cospi_24_64 + x15 * cospi_8_64;
  s15 =   x14 * cospi_8_64  + x15 * cospi_24_64;

  x0 = s0 + s2;
  x1 = s1 + s3;
  x2 = s0 - s2;
  x3 = s1 - s3;
  x4 = fdct_round_shift(s4 + s6);
  x5 = fdct_round_shift(s5 + s7);
  x6 = fdct_round_shift(s4 - s6);
  x7 = fdct_round_shift(s5 - s7);
  x8 = s8 + s10;
  x9 = s9 + s11;
  x10 = s8 - s10;
  x11 = s9 - s11;
  x12 = fdct_round_shift(s12 + s14);
  x13 = fdct_round_shift(s13 + s15);
  x14 = fdct_round_shift(s12 - s14);
  x15 = fdct_round_shift(s13 - s15);

  // stage 4
  s2 = (- cospi_16_64) * (x2 + x3);
  s3 = cospi_16_64 * (x2 - x3);
  s6 = cospi_16_64 * (x6 + x7);
  s7 = cospi_16_64 * (- x6 + x7);
  s10 = cospi_16_64 * (x10 + x11);
  s11 = cospi_16_64 * (- x10 + x11);
  s14 = (- cospi_16_64) * (x14 + x15);
  s15 = cospi_16_64 * (x14 - x15);

  x2 = fdct_round_shift(s2);
  x3 = fdct_round_shift(s3);
  x6 = fdct_round_shift(s6);
  x7 = fdct_round_shift(s7);
  x10 = fdct_round_shift(s10);
  x11 = fdct_round_shift(s11);
  x14 = fdct_round_shift(s14);
  x15 = fdct_round_shift(s15);

  output[0] = x0;
  output[1] = - x8;
  output[2] = x12;
  output[3] = - x4;
  output[4] = x6;
  output[5] = x14;
  output[6] = x10;
  output[7] = x2;
  output[8] = x3;
  output[9] =  x11;
  output[10] = x15;
  output[11] = x7;
  output[12] = x5;
  output[13] = - x13;
  output[14] = x9;
  output[15] = - x1;
}

static const transform_2d FHT_16[] = {
  { fdct16,  fdct16  },  // DCT_DCT  = 0
  { fadst16, fdct16  },  // ADST_DCT = 1
  { fdct16,  fadst16 },  // DCT_ADST = 2
  { fadst16, fadst16 }   // ADST_ADST = 3
};

void vp9_short_fht16x16_c(const int16_t *input, int16_t *output,
                          int stride, int tx_type) {
  int16_t out[256];
  int16_t *outptr = &out[0];
  int i, j;
  int16_t temp_in[16], temp_out[16];
  const transform_2d ht = FHT_16[tx_type];

  // Columns
  for (i = 0; i < 16; ++i) {
    for (j = 0; j < 16; ++j)
      temp_in[j] = input[j * stride + i] * 4;
    ht.cols(temp_in, temp_out);
    for (j = 0; j < 16; ++j)
      outptr[j * 16 + i] = (temp_out[j] + 1 + (temp_out[j] < 0)) >> 2;
//      outptr[j * 16 + i] = (temp_out[j] + 1 + (temp_out[j] > 0)) >> 2;
  }

  // Rows
  for (i = 0; i < 16; ++i) {
    for (j = 0; j < 16; ++j)
      temp_in[j] = out[j + i * 16];
    ht.rows(temp_in, temp_out);
    for (j = 0; j < 16; ++j)
      output[j + i * 16] = temp_out[j];
  }
}

static INLINE int dct_32_round(int input) {
  int rv = ROUND_POWER_OF_TWO(input, DCT_CONST_BITS);
  assert(-131072 <= rv && rv <= 131071);
  return rv;
}

static INLINE int half_round_shift(int input) {
  int rv = (input + 1 + (input < 0)) >> 2;
  return rv;
}

static void dct32_1d(const int *input, int *output, int round) {
  int step[32];
  // Stage 1
  step[0] = input[0] + input[(32 - 1)];
  step[1] = input[1] + input[(32 - 2)];
  step[2] = input[2] + input[(32 - 3)];
  step[3] = input[3] + input[(32 - 4)];
  step[4] = input[4] + input[(32 - 5)];
  step[5] = input[5] + input[(32 - 6)];
  step[6] = input[6] + input[(32 - 7)];
  step[7] = input[7] + input[(32 - 8)];
  step[8] = input[8] + input[(32 - 9)];
  step[9] = input[9] + input[(32 - 10)];
  step[10] = input[10] + input[(32 - 11)];
  step[11] = input[11] + input[(32 - 12)];
  step[12] = input[12] + input[(32 - 13)];
  step[13] = input[13] + input[(32 - 14)];
  step[14] = input[14] + input[(32 - 15)];
  step[15] = input[15] + input[(32 - 16)];
  step[16] = -input[16] + input[(32 - 17)];
  step[17] = -input[17] + input[(32 - 18)];
  step[18] = -input[18] + input[(32 - 19)];
  step[19] = -input[19] + input[(32 - 20)];
  step[20] = -input[20] + input[(32 - 21)];
  step[21] = -input[21] + input[(32 - 22)];
  step[22] = -input[22] + input[(32 - 23)];
  step[23] = -input[23] + input[(32 - 24)];
  step[24] = -input[24] + input[(32 - 25)];
  step[25] = -input[25] + input[(32 - 26)];
  step[26] = -input[26] + input[(32 - 27)];
  step[27] = -input[27] + input[(32 - 28)];
  step[28] = -input[28] + input[(32 - 29)];
  step[29] = -input[29] + input[(32 - 30)];
  step[30] = -input[30] + input[(32 - 31)];
  step[31] = -input[31] + input[(32 - 32)];

  // Stage 2
  output[0] = step[0] + step[16 - 1];
  output[1] = step[1] + step[16 - 2];
  output[2] = step[2] + step[16 - 3];
  output[3] = step[3] + step[16 - 4];
  output[4] = step[4] + step[16 - 5];
  output[5] = step[5] + step[16 - 6];
  output[6] = step[6] + step[16 - 7];
  output[7] = step[7] + step[16 - 8];
  output[8] = -step[8] + step[16 - 9];
  output[9] = -step[9] + step[16 - 10];
  output[10] = -step[10] + step[16 - 11];
  output[11] = -step[11] + step[16 - 12];
  output[12] = -step[12] + step[16 - 13];
  output[13] = -step[13] + step[16 - 14];
  output[14] = -step[14] + step[16 - 15];
  output[15] = -step[15] + step[16 - 16];

  output[16] = step[16];
  output[17] = step[17];
  output[18] = step[18];
  output[19] = step[19];

  output[20] = dct_32_round((-step[20] + step[27]) * cospi_16_64);
  output[21] = dct_32_round((-step[21] + step[26]) * cospi_16_64);
  output[22] = dct_32_round((-step[22] + step[25]) * cospi_16_64);
  output[23] = dct_32_round((-step[23] + step[24]) * cospi_16_64);

  output[24] = dct_32_round((step[24] + step[23]) * cospi_16_64);
  output[25] = dct_32_round((step[25] + step[22]) * cospi_16_64);
  output[26] = dct_32_round((step[26] + step[21]) * cospi_16_64);
  output[27] = dct_32_round((step[27] + step[20]) * cospi_16_64);

  output[28] = step[28];
  output[29] = step[29];
  output[30] = step[30];
  output[31] = step[31];

  // dump the magnitude by 4, hence the intermediate values are within
  // the range of 16 bits.
  if (round) {
    output[0] = half_round_shift(output[0]);
    output[1] = half_round_shift(output[1]);
    output[2] = half_round_shift(output[2]);
    output[3] = half_round_shift(output[3]);
    output[4] = half_round_shift(output[4]);
    output[5] = half_round_shift(output[5]);
    output[6] = half_round_shift(output[6]);
    output[7] = half_round_shift(output[7]);
    output[8] = half_round_shift(output[8]);
    output[9] = half_round_shift(output[9]);
    output[10] = half_round_shift(output[10]);
    output[11] = half_round_shift(output[11]);
    output[12] = half_round_shift(output[12]);
    output[13] = half_round_shift(output[13]);
    output[14] = half_round_shift(output[14]);
    output[15] = half_round_shift(output[15]);

    output[16] = half_round_shift(output[16]);
    output[17] = half_round_shift(output[17]);
    output[18] = half_round_shift(output[18]);
    output[19] = half_round_shift(output[19]);
    output[20] = half_round_shift(output[20]);
    output[21] = half_round_shift(output[21]);
    output[22] = half_round_shift(output[22]);
    output[23] = half_round_shift(output[23]);
    output[24] = half_round_shift(output[24]);
    output[25] = half_round_shift(output[25]);
    output[26] = half_round_shift(output[26]);
    output[27] = half_round_shift(output[27]);
    output[28] = half_round_shift(output[28]);
    output[29] = half_round_shift(output[29]);
    output[30] = half_round_shift(output[30]);
    output[31] = half_round_shift(output[31]);
  }

  // Stage 3
  step[0] = output[0] + output[(8 - 1)];
  step[1] = output[1] + output[(8 - 2)];
  step[2] = output[2] + output[(8 - 3)];
  step[3] = output[3] + output[(8 - 4)];
  step[4] = -output[4] + output[(8 - 5)];
  step[5] = -output[5] + output[(8 - 6)];
  step[6] = -output[6] + output[(8 - 7)];
  step[7] = -output[7] + output[(8 - 8)];
  step[8] = output[8];
  step[9] = output[9];
  step[10] = dct_32_round((-output[10] + output[13]) * cospi_16_64);
  step[11] = dct_32_round((-output[11] + output[12]) * cospi_16_64);
  step[12] = dct_32_round((output[12] + output[11]) * cospi_16_64);
  step[13] = dct_32_round((output[13] + output[10]) * cospi_16_64);
  step[14] = output[14];
  step[15] = output[15];

  step[16] = output[16] + output[23];
  step[17] = output[17] + output[22];
  step[18] = output[18] + output[21];
  step[19] = output[19] + output[20];
  step[20] = -output[20] + output[19];
  step[21] = -output[21] + output[18];
  step[22] = -output[22] + output[17];
  step[23] = -output[23] + output[16];
  step[24] = -output[24] + output[31];
  step[25] = -output[25] + output[30];
  step[26] = -output[26] + output[29];
  step[27] = -output[27] + output[28];
  step[28] = output[28] + output[27];
  step[29] = output[29] + output[26];
  step[30] = output[30] + output[25];
  step[31] = output[31] + output[24];

  // Stage 4
  output[0] = step[0] + step[3];
  output[1] = step[1] + step[2];
  output[2] = -step[2] + step[1];
  output[3] = -step[3] + step[0];
  output[4] = step[4];
  output[5] = dct_32_round((-step[5] + step[6]) * cospi_16_64);
  output[6] = dct_32_round((step[6] + step[5]) * cospi_16_64);
  output[7] = step[7];
  output[8] = step[8] + step[11];
  output[9] = step[9] + step[10];
  output[10] = -step[10] + step[9];
  output[11] = -step[11] + step[8];
  output[12] = -step[12] + step[15];
  output[13] = -step[13] + step[14];
  output[14] = step[14] + step[13];
  output[15] = step[15] + step[12];

  output[16] = step[16];
  output[17] = step[17];
  output[18] = dct_32_round(step[18] * -cospi_8_64 + step[29] * cospi_24_64);
  output[19] = dct_32_round(step[19] * -cospi_8_64 + step[28] * cospi_24_64);
  output[20] = dct_32_round(step[20] * -cospi_24_64 + step[27] * -cospi_8_64);
  output[21] = dct_32_round(step[21] * -cospi_24_64 + step[26] * -cospi_8_64);
  output[22] = step[22];
  output[23] = step[23];
  output[24] = step[24];
  output[25] = step[25];
  output[26] = dct_32_round(step[26] * cospi_24_64 + step[21] * -cospi_8_64);
  output[27] = dct_32_round(step[27] * cospi_24_64 + step[20] * -cospi_8_64);
  output[28] = dct_32_round(step[28] * cospi_8_64 + step[19] * cospi_24_64);
  output[29] = dct_32_round(step[29] * cospi_8_64 + step[18] * cospi_24_64);
  output[30] = step[30];
  output[31] = step[31];

  // Stage 5
  step[0] = dct_32_round((output[0] + output[1]) * cospi_16_64);
  step[1] = dct_32_round((-output[1] + output[0]) * cospi_16_64);
  step[2] = dct_32_round(output[2] * cospi_24_64 + output[3] * cospi_8_64);
  step[3] = dct_32_round(output[3] * cospi_24_64 - output[2] * cospi_8_64);
  step[4] = output[4] + output[5];
  step[5] = -output[5] + output[4];
  step[6] = -output[6] + output[7];
  step[7] = output[7] + output[6];
  step[8] = output[8];
  step[9] = dct_32_round(output[9] * -cospi_8_64 + output[14] * cospi_24_64);
  step[10] = dct_32_round(output[10] * -cospi_24_64 + output[13] * -cospi_8_64);
  step[11] = output[11];
  step[12] = output[12];
  step[13] = dct_32_round(output[13] * cospi_24_64 + output[10] * -cospi_8_64);
  step[14] = dct_32_round(output[14] * cospi_8_64 + output[9] * cospi_24_64);
  step[15] = output[15];

  step[16] = output[16] + output[19];
  step[17] = output[17] + output[18];
  step[18] = -output[18] + output[17];
  step[19] = -output[19] + output[16];
  step[20] = -output[20] + output[23];
  step[21] = -output[21] + output[22];
  step[22] = output[22] + output[21];
  step[23] = output[23] + output[20];
  step[24] = output[24] + output[27];
  step[25] = output[25] + output[26];
  step[26] = -output[26] + output[25];
  step[27] = -output[27] + output[24];
  step[28] = -output[28] + output[31];
  step[29] = -output[29] + output[30];
  step[30] = output[30] + output[29];
  step[31] = output[31] + output[28];

  // Stage 6
  output[0] = step[0];
  output[1] = step[1];
  output[2] = step[2];
  output[3] = step[3];
  output[4] = dct_32_round(step[4] * cospi_28_64 + step[7] * cospi_4_64);
  output[5] = dct_32_round(step[5] * cospi_12_64 + step[6] * cospi_20_64);
  output[6] = dct_32_round(step[6] * cospi_12_64 + step[5] * -cospi_20_64);
  output[7] = dct_32_round(step[7] * cospi_28_64 + step[4] * -cospi_4_64);
  output[8] = step[8] + step[9];
  output[9] = -step[9] + step[8];
  output[10] = -step[10] + step[11];
  output[11] = step[11] + step[10];
  output[12] = step[12] + step[13];
  output[13] = -step[13] + step[12];
  output[14] = -step[14] + step[15];
  output[15] = step[15] + step[14];

  output[16] = step[16];
  output[17] = dct_32_round(step[17] * -cospi_4_64 + step[30] * cospi_28_64);
  output[18] = dct_32_round(step[18] * -cospi_28_64 + step[29] * -cospi_4_64);
  output[19] = step[19];
  output[20] = step[20];
  output[21] = dct_32_round(step[21] * -cospi_20_64 + step[26] * cospi_12_64);
  output[22] = dct_32_round(step[22] * -cospi_12_64 + step[25] * -cospi_20_64);
  output[23] = step[23];
  output[24] = step[24];
  output[25] = dct_32_round(step[25] * cospi_12_64 + step[22] * -cospi_20_64);
  output[26] = dct_32_round(step[26] * cospi_20_64 + step[21] * cospi_12_64);
  output[27] = step[27];
  output[28] = step[28];
  output[29] = dct_32_round(step[29] * cospi_28_64 + step[18] * -cospi_4_64);
  output[30] = dct_32_round(step[30] * cospi_4_64 + step[17] * cospi_28_64);
  output[31] = step[31];

  // Stage 7
  step[0] = output[0];
  step[1] = output[1];
  step[2] = output[2];
  step[3] = output[3];
  step[4] = output[4];
  step[5] = output[5];
  step[6] = output[6];
  step[7] = output[7];
  step[8] = dct_32_round(output[8] * cospi_30_64 + output[15] * cospi_2_64);
  step[9] = dct_32_round(output[9] * cospi_14_64 + output[14] * cospi_18_64);
  step[10] = dct_32_round(output[10] * cospi_22_64 + output[13] * cospi_10_64);
  step[11] = dct_32_round(output[11] * cospi_6_64 + output[12] * cospi_26_64);
  step[12] = dct_32_round(output[12] * cospi_6_64 + output[11] * -cospi_26_64);
  step[13] = dct_32_round(output[13] * cospi_22_64 + output[10] * -cospi_10_64);
  step[14] = dct_32_round(output[14] * cospi_14_64 + output[9] * -cospi_18_64);
  step[15] = dct_32_round(output[15] * cospi_30_64 + output[8] * -cospi_2_64);

  step[16] = output[16] + output[17];
  step[17] = -output[17] + output[16];
  step[18] = -output[18] + output[19];
  step[19] = output[19] + output[18];
  step[20] = output[20] + output[21];
  step[21] = -output[21] + output[20];
  step[22] = -output[22] + output[23];
  step[23] = output[23] + output[22];
  step[24] = output[24] + output[25];
  step[25] = -output[25] + output[24];
  step[26] = -output[26] + output[27];
  step[27] = output[27] + output[26];
  step[28] = output[28] + output[29];
  step[29] = -output[29] + output[28];
  step[30] = -output[30] + output[31];
  step[31] = output[31] + output[30];

  // Final stage --- outputs indices are bit-reversed.
  output[0]  = step[0];
  output[16] = step[1];
  output[8]  = step[2];
  output[24] = step[3];
  output[4]  = step[4];
  output[20] = step[5];
  output[12] = step[6];
  output[28] = step[7];
  output[2]  = step[8];
  output[18] = step[9];
  output[10] = step[10];
  output[26] = step[11];
  output[6]  = step[12];
  output[22] = step[13];
  output[14] = step[14];
  output[30] = step[15];

  output[1]  = dct_32_round(step[16] * cospi_31_64 + step[31] * cospi_1_64);
  output[17] = dct_32_round(step[17] * cospi_15_64 + step[30] * cospi_17_64);
  output[9]  = dct_32_round(step[18] * cospi_23_64 + step[29] * cospi_9_64);
  output[25] = dct_32_round(step[19] * cospi_7_64 + step[28] * cospi_25_64);
  output[5]  = dct_32_round(step[20] * cospi_27_64 + step[27] * cospi_5_64);
  output[21] = dct_32_round(step[21] * cospi_11_64 + step[26] * cospi_21_64);
  output[13] = dct_32_round(step[22] * cospi_19_64 + step[25] * cospi_13_64);
  output[29] = dct_32_round(step[23] * cospi_3_64 + step[24] * cospi_29_64);
  output[3]  = dct_32_round(step[24] * cospi_3_64 + step[23] * -cospi_29_64);
  output[19] = dct_32_round(step[25] * cospi_19_64 + step[22] * -cospi_13_64);
  output[11] = dct_32_round(step[26] * cospi_11_64 + step[21] * -cospi_21_64);
  output[27] = dct_32_round(step[27] * cospi_27_64 + step[20] * -cospi_5_64);
  output[7]  = dct_32_round(step[28] * cospi_7_64 + step[19] * -cospi_25_64);
  output[23] = dct_32_round(step[29] * cospi_23_64 + step[18] * -cospi_9_64);
  output[15] = dct_32_round(step[30] * cospi_15_64 + step[17] * -cospi_17_64);
  output[31] = dct_32_round(step[31] * cospi_31_64 + step[16] * -cospi_1_64);
}

void vp9_fdct32x32_c(const int16_t *input, int16_t *out, int stride) {
  int i, j;
  int output[32 * 32];

  // Columns
  for (i = 0; i < 32; ++i) {
    int temp_in[32], temp_out[32];
    for (j = 0; j < 32; ++j)
      temp_in[j] = input[j * stride + i] * 4;
    dct32_1d(temp_in, temp_out, 0);
    for (j = 0; j < 32; ++j)
      output[j * 32 + i] = (temp_out[j] + 1 + (temp_out[j] > 0)) >> 2;
  }

  // Rows
  for (i = 0; i < 32; ++i) {
    int temp_in[32], temp_out[32];
    for (j = 0; j < 32; ++j)
      temp_in[j] = output[j + i * 32];
    dct32_1d(temp_in, temp_out, 0);
    for (j = 0; j < 32; ++j)
      out[j + i * 32] = (temp_out[j] + 1 + (temp_out[j] < 0)) >> 2;
  }
}

#if CONFIG_EXT_TX
static double tmp[32*32];
static double tmp2[32*32];

double dstmtx32[32*32] = {
    0.0122669185818545, 0.0245042850823902, 0.0366826186138404,
    0.0487725805040321, 0.0607450449758160, 0.0725711693136156,
    0.0842224633480550, 0.0956708580912724, 0.1068887733575705,
    0.1178491842064994, 0.1285256860483054, 0.1388925582549005,
    0.1489248261231083, 0.1585983210409114, 0.1678897387117546,
    0.1767766952966369, 0.1852377813387398, 0.1932526133406842,
    0.2008018828701612, 0.2078674030756363, 0.2144321525000680,
    0.2204803160870887, 0.2259973232808608, 0.2309698831278217,
    0.2353860162957552, 0.2392350839330522, 0.2425078132986360,
    0.2451963201008076, 0.2472941274911953, 0.2487961816680492,
    0.2496988640512931, 0.1767766952966369,
    0.0366826186138404, 0.0725711693136156, 0.1068887733575705,
    0.1388925582549005, 0.1678897387117546, 0.1932526133406842,
    0.2144321525000680, 0.2309698831278217, 0.2425078132986360,
    0.2487961816680492, 0.2496988640512931, 0.2451963201008076,
    0.2353860162957552, 0.2204803160870888, 0.2008018828701612,
    0.1767766952966369, 0.1489248261231084, 0.1178491842064995,
    0.0842224633480551, 0.0487725805040322, 0.0122669185818545,
    -0.0245042850823901, -0.0607450449758160, -0.0956708580912724,
    -0.1285256860483054, -0.1585983210409113, -0.1852377813387397,
    -0.2078674030756363, -0.2259973232808608, -0.2392350839330522,
    -0.2472941274911952, -0.1767766952966369,
    0.0607450449758160, 0.1178491842064994, 0.1678897387117546,
    0.2078674030756363, 0.2353860162957552, 0.2487961816680492,
    0.2472941274911953, 0.2309698831278217, 0.2008018828701612,
    0.1585983210409114, 0.1068887733575705, 0.0487725805040322,
    -0.0122669185818544, -0.0725711693136155, -0.1285256860483054,
    -0.1767766952966369, -0.2144321525000680, -0.2392350839330522,
    -0.2496988640512931, -0.2451963201008076, -0.2259973232808608,
    -0.1932526133406842, -0.1489248261231083, -0.0956708580912726,
    -0.0366826186138406, 0.0245042850823900, 0.0842224633480549,
    0.1388925582549005, 0.1852377813387397, 0.2204803160870887,
    0.2425078132986360, 0.1767766952966369,
    0.0842224633480550, 0.1585983210409114, 0.2144321525000680,
    0.2451963201008076, 0.2472941274911953, 0.2204803160870888,
    0.1678897387117546, 0.0956708580912725, 0.0122669185818545,
    -0.0725711693136155, -0.1489248261231083, -0.2078674030756363,
    -0.2425078132986360, -0.2487961816680492, -0.2259973232808608,
    -0.1767766952966369, -0.1068887733575706, -0.0245042850823901,
    0.0607450449758159, 0.1388925582549005, 0.2008018828701612,
    0.2392350839330522, 0.2496988640512931, 0.2309698831278217,
    0.1852377813387399, 0.1178491842064995, 0.0366826186138404,
    -0.0487725805040318, -0.1285256860483053, -0.1932526133406843,
    -0.2353860162957551, -0.1767766952966369,
    0.1068887733575705, 0.1932526133406842, 0.2425078132986360,
    0.2451963201008076, 0.2008018828701612, 0.1178491842064995,
    0.0122669185818545, -0.0956708580912724, -0.1852377813387397,
    -0.2392350839330522, -0.2472941274911952, -0.2078674030756364,
    -0.1285256860483055, -0.0245042850823901, 0.0842224633480549,
    0.1767766952966368, 0.2353860162957552, 0.2487961816680492,
    0.2144321525000681, 0.1388925582549006, 0.0366826186138404,
    -0.0725711693136157, -0.1678897387117544, -0.2309698831278216,
    -0.2496988640512931, -0.2204803160870888, -0.1489248261231083,
    -0.0487725805040320, 0.0607450449758156, 0.1585983210409112,
    0.2259973232808608, 0.1767766952966369,
    0.1285256860483054, 0.2204803160870887, 0.2496988640512931,
    0.2078674030756363, 0.1068887733575706, -0.0245042850823900,
    -0.1489248261231082, -0.2309698831278216, -0.2472941274911953,
    -0.1932526133406844, -0.0842224633480552, 0.0487725805040318,
    0.1678897387117544, 0.2392350839330522, 0.2425078132986361,
    0.1767766952966371, 0.0607450449758163, -0.0725711693136153,
    -0.1852377813387395, -0.2451963201008076, -0.2353860162957553,
    -0.1585983210409117, -0.0366826186138409, 0.0956708580912720,
    0.2008018828701609, 0.2487961816680492, 0.2259973232808611,
    0.1388925582549010, 0.0122669185818551, -0.1178491842064989,
    -0.2144321525000677, -0.1767766952966369,
    0.1489248261231083, 0.2392350839330522, 0.2353860162957552,
    0.1388925582549005, -0.0122669185818545, -0.1585983210409114,
    -0.2425078132986360, -0.2309698831278217, -0.1285256860483055,
    0.0245042850823902, 0.1678897387117547, 0.2451963201008076,
    0.2259973232808608, 0.1178491842064991, -0.0366826186138407,
    -0.1767766952966370, -0.2472941274911953, -0.2204803160870888,
    -0.1068887733575703, 0.0487725805040322, 0.1852377813387398,
    0.2487961816680493, 0.2144321525000679, 0.0956708580912723,
    -0.0607450449758161, -0.1932526133406842, -0.2496988640512931,
    -0.2078674030756359, -0.0842224633480545, 0.0725711693136160,
    0.2008018828701614, 0.1767766952966369,
    0.1678897387117546, 0.2487961816680492, 0.2008018828701613,
    0.0487725805040322, -0.1285256860483054, -0.2392350839330522,
    -0.2259973232808609, -0.0956708580912726, 0.0842224633480549,
    0.2204803160870887, 0.2425078132986361, 0.1388925582549010,
    -0.0366826186138402, -0.1932526133406840, -0.2496988640512931,
    -0.1767766952966371, -0.0122669185818550, 0.1585983210409112,
    0.2472941274911952, 0.2078674030756364, 0.0607450449758163,
    -0.1178491842064989, -0.2353860162957550, -0.2309698831278220,
    -0.1068887733575707, 0.0725711693136152, 0.2144321525000677,
    0.2451963201008078, 0.1489248261231091, -0.0245042850823899,
    -0.1852377813387394, -0.1767766952966369,
    0.1852377813387398, 0.2487961816680492, 0.1489248261231083,
    -0.0487725805040321, -0.2144321525000680, -0.2392350839330522,
    -0.1068887733575704, 0.0956708580912725, 0.2353860162957552,
    0.2204803160870888, 0.0607450449758159, -0.1388925582549008,
    -0.2472941274911953, -0.1932526133406841, -0.0122669185818546,
    0.1767766952966369, 0.2496988640512931, 0.1585983210409114,
    -0.0366826186138406, -0.2078674030756363, -0.2425078132986359,
    -0.1178491842064992, 0.0842224633480550, 0.2309698831278219,
    0.2259973232808607, 0.0725711693136156, -0.1285256860483052,
    -0.2451963201008077, -0.2008018828701612, -0.0245042850823903,
    0.1678897387117549, 0.1767766952966369,
    0.2008018828701612, 0.2392350839330522, 0.0842224633480551,
    -0.1388925582549005, -0.2496988640512931, -0.1585983210409115,
    0.0607450449758157, 0.2309698831278216, 0.2144321525000681,
    0.0245042850823902, -0.1852377813387395, -0.2451963201008077,
    -0.1068887733575707, 0.1178491842064989, 0.2472941274911952,
    0.1767766952966371, -0.0366826186138402, -0.2204803160870887,
    -0.2259973232808611, -0.0487725805040321, 0.1678897387117543,
    0.2487961816680493, 0.1285256860483056, -0.0956708580912719,
    -0.2425078132986358, -0.1932526133406845, 0.0122669185818538,
    0.2078674030756357, 0.2353860162957554, 0.0725711693136165,
    -0.1489248261231080, -0.1767766952966369,
    0.2144321525000680, 0.2204803160870888, 0.0122669185818545,
    -0.2078674030756363, -0.2259973232808608, -0.0245042850823901,
    0.2008018828701612, 0.2309698831278217, 0.0366826186138404,
    -0.1932526133406843, -0.2353860162957552, -0.0487725805040320,
    0.1852377813387398, 0.2392350839330522, 0.0607450449758159,
    -0.1767766952966369, -0.2425078132986359, -0.0725711693136155,
    0.1678897387117543, 0.2451963201008076, 0.0842224633480545,
    -0.1585983210409114, -0.2472941274911953, -0.0956708580912724,
    0.1489248261231088, 0.2487961816680492, 0.1068887733575708,
    -0.1388925582549007, -0.2496988640512931, -0.1178491842064993,
    0.1285256860483052, 0.1767766952966369,
    0.2259973232808608, 0.1932526133406843, -0.0607450449758160,
    -0.2451963201008076, -0.1489248261231085, 0.1178491842064994,
    0.2496988640512931, 0.0956708580912726, -0.1678897387117544,
    -0.2392350839330523, -0.0366826186138409, 0.2078674030756363,
    0.2144321525000681, -0.0245042850823899, -0.2353860162957550,
    -0.1767766952966372, 0.0842224633480550, 0.2487961816680492,
    0.1285256860483056, -0.1388925582549000, -0.2472941274911953,
    -0.0725711693136164, 0.1852377813387394, 0.2309698831278217,
    0.0122669185818552, -0.2204803160870886, -0.2008018828701617,
    0.0487725805040316, 0.2425078132986357, 0.1585983210409125,
    -0.1068887733575703, -0.1767766952966369,
    0.2353860162957552, 0.1585983210409114, -0.1285256860483054,
    -0.2451963201008076, -0.0366826186138404, 0.2204803160870887,
    0.1852377813387396, -0.0956708580912725, -0.2496988640512931,
    -0.0725711693136155, 0.2008018828701612, 0.2078674030756364,
    -0.0607450449758161, -0.2487961816680493, -0.1068887733575707,
    0.1767766952966369, 0.2259973232808607, -0.0245042850823899,
    -0.2425078132986360, -0.1388925582549004, 0.1489248261231081,
    0.2392350839330522, 0.0122669185818543, -0.2309698831278215,
    -0.1678897387117547, 0.1178491842064995, 0.2472941274911952,
    0.0487725805040314, -0.2144321525000676, -0.1932526133406846,
    0.0842224633480548, 0.1767766952966369,
    0.2425078132986360, 0.1178491842064995, -0.1852377813387397,
    -0.2078674030756364, 0.0842224633480549, 0.2487961816680492,
    0.0366826186138408, -0.2309698831278216, -0.1489248261231083,
    0.1585983210409112, 0.2259973232808611, -0.0487725805040317,
    -0.2496988640512931, -0.0725711693136164, 0.2144321525000677,
    0.1767766952966372, -0.1285256860483052, -0.2392350839330522,
    0.0122669185818538, 0.2451963201008075, 0.1068887733575708,
    -0.1932526133406836, -0.2008018828701617, 0.0956708580912718,
    0.2472941274911954, 0.0245042850823904, -0.2353860162957549,
    -0.1388925582549019, 0.1678897387117542, 0.2204803160870893,
    -0.0607450449758158, -0.1767766952966369,
    0.2472941274911953, 0.0725711693136156, -0.2259973232808608,
    -0.1388925582549005, 0.1852377813387399, 0.1932526133406844,
    -0.1285256860483053, -0.2309698831278217, 0.0607450449758161,
    0.2487961816680492, 0.0122669185818546, -0.2451963201008075,
    -0.0842224633480553, 0.2204803160870887, 0.1489248261231084,
    -0.1767766952966369, -0.2008018828701612, 0.1178491842064996,
    0.2353860162957551, -0.0487725805040325, -0.2496988640512931,
    -0.0245042850823904, 0.2425078132986362, 0.0956708580912733,
    -0.2144321525000676, -0.1585983210409119, 0.1678897387117542,
    0.2078674030756366, -0.1068887733575702, -0.2392350839330523,
    0.0366826186138403, 0.1767766952966369,
    0.2496988640512931, 0.0245042850823902, -0.2472941274911952,
    -0.0487725805040322, 0.2425078132986360, 0.0725711693136159,
    -0.2353860162957551, -0.0956708580912727, 0.2259973232808608,
    0.1178491842064996, -0.2144321525000677, -0.1388925582549010,
    0.2008018828701609, 0.1585983210409118, -0.1852377813387394,
    -0.1767766952966372, 0.1678897387117543, 0.1932526133406845,
    -0.1489248261231080, -0.2078674030756365, 0.1285256860483052,
    0.2204803160870893, -0.1068887733575703, -0.2309698831278221,
    0.0842224633480548, 0.2392350839330525, -0.0607450449758158,
    -0.2451963201008078, 0.0366826186138403, 0.2487961816680493,
    -0.0122669185818544, -0.1767766952966369,
    0.2496988640512931, -0.0245042850823901, -0.2472941274911952,
    0.0487725805040321, 0.2425078132986360, -0.0725711693136157,
    -0.2353860162957552, 0.0956708580912724, 0.2259973232808609,
    -0.1178491842064997, -0.2144321525000679, 0.1388925582549007,
    0.2008018828701612, -0.1585983210409114, -0.1852377813387397,
    0.1767766952966369, 0.1678897387117547, -0.1932526133406841,
    -0.1489248261231085, 0.2078674030756367, 0.1285256860483049,
    -0.2204803160870890, -0.1068887733575701, 0.2309698831278219,
    0.0842224633480547, -0.2392350839330523, -0.0607450449758158,
    0.2451963201008076, 0.0366826186138403, -0.2487961816680492,
    -0.0122669185818545, 0.1767766952966369,
    0.2472941274911953, -0.0725711693136155, -0.2259973232808608,
    0.1388925582549005, 0.1852377813387399, -0.1932526133406843,
    -0.1285256860483055, 0.2309698831278216, 0.0607450449758163,
    -0.2487961816680492, 0.0122669185818538, 0.2451963201008076,
    -0.0842224633480550, -0.2204803160870888, 0.1489248261231081,
    0.1767766952966372, -0.2008018828701608, -0.1178491842065001,
    0.2353860162957552, 0.0487725805040331, -0.2496988640512931,
    0.0245042850823888, 0.2425078132986361, -0.0956708580912726,
    -0.2144321525000685, 0.1585983210409113, 0.1678897387117555,
    -0.2078674030756361, -0.1068887733575718, 0.2392350839330520,
    0.0366826186138421, -0.1767766952966369,
    0.2425078132986360, -0.1178491842064994, -0.1852377813387398,
    0.2078674030756363, 0.0842224633480552, -0.2487961816680492,
    0.0366826186138406, 0.2309698831278217, -0.1489248261231081,
    -0.1585983210409118, 0.2259973232808609, 0.0487725805040321,
    -0.2496988640512931, 0.0725711693136160, 0.2144321525000679,
    -0.1767766952966368, -0.1285256860483057, 0.2392350839330521,
    0.0122669185818552, -0.2451963201008078, 0.1068887733575710,
    0.1932526133406840, -0.2008018828701613, -0.0956708580912725,
    0.2472941274911952, -0.0245042850823896, -0.2353860162957555,
    0.1388925582549012, 0.1678897387117542, -0.2204803160870889,
    -0.0607450449758159, 0.1767766952966369,
    0.2353860162957552, -0.1585983210409113, -0.1285256860483055,
    0.2451963201008076, -0.0366826186138402, -0.2204803160870888,
    0.1852377813387395, 0.0956708580912727, -0.2496988640512931,
    0.0725711693136152, 0.2008018828701617, -0.2078674030756362,
    -0.0607450449758164, 0.2487961816680493, -0.1068887733575703,
    -0.1767766952966373, 0.2259973232808605, 0.0245042850823904,
    -0.2425078132986364, 0.1388925582548998, 0.1489248261231086,
    -0.2392350839330518, 0.0122669185818536, 0.2309698831278218,
    -0.1678897387117535, -0.1178491842065003, 0.2472941274911952,
    -0.0487725805040305, -0.2144321525000685, 0.1932526133406840,
    0.0842224633480565, -0.1767766952966369,
    0.2259973232808609, -0.1932526133406842, -0.0607450449758163,
    0.2451963201008077, -0.1489248261231082, -0.1178491842064999,
    0.2496988640512931, -0.0956708580912720, -0.1678897387117552,
    0.2392350839330521, -0.0366826186138397, -0.2078674030756370,
    0.2144321525000677, 0.0245042850823912, -0.2353860162957554,
    0.1767766952966362, 0.0842224633480563, -0.2487961816680494,
    0.1285256860483051, 0.1388925582549012, -0.2472941274911951,
    0.0725711693136141, 0.1852377813387411, -0.2309698831278208,
    0.0122669185818535, 0.2204803160870894, -0.2008018828701602,
    -0.0487725805040342, 0.2425078132986366, -0.1585983210409105,
    -0.1068887733575719, 0.1767766952966369,
    0.2144321525000680, -0.2204803160870887, 0.0122669185818544,
    0.2078674030756364, -0.2259973232808608, 0.0245042850823899,
    0.2008018828701614, -0.2309698831278216, 0.0366826186138406,
    0.1932526133406845, -0.2353860162957549, 0.0487725805040317,
    0.1852377813387398, -0.2392350839330521, 0.0607450449758150,
    0.1767766952966373, -0.2425078132986360, 0.0725711693136158,
    0.1678897387117554, -0.2451963201008075, 0.0842224633480547,
    0.1585983210409126, -0.2472941274911951, 0.0956708580912717,
    0.1489248261231087, -0.2487961816680492, 0.1068887733575693,
    0.1388925582549013, -0.2496988640512931, 0.1178491842064977,
    0.1285256860483052, -0.1767766952966369,
    0.2008018828701612, -0.2392350839330522, 0.0842224633480551,
    0.1388925582549006, -0.2496988640512931, 0.1585983210409115,
    0.0607450449758159, -0.2309698831278217, 0.2144321525000682,
    -0.0245042850823899, -0.1852377813387397, 0.2451963201008077,
    -0.1068887733575703, -0.1178491842064993, 0.2472941274911952,
    -0.1767766952966368, -0.0366826186138411, 0.2204803160870885,
    -0.2259973232808608, 0.0487725805040315, 0.1678897387117541,
    -0.2487961816680492, 0.1285256860483050, 0.0956708580912718,
    -0.2425078132986360, 0.1932526133406840, 0.0122669185818555,
    -0.2078674030756362, 0.2353860162957551, -0.0725711693136164,
    -0.1489248261231095, 0.1767766952966369,
    0.1852377813387397, -0.2487961816680492, 0.1489248261231084,
    0.0487725805040320, -0.2144321525000679, 0.2392350839330523,
    -0.1068887733575704, -0.0956708580912723, 0.2353860162957551,
    -0.2204803160870891, 0.0607450449758160, 0.1388925582549004,
    -0.2472941274911952, 0.1932526133406841, -0.0122669185818546,
    -0.1767766952966367, 0.2496988640512931, -0.1585983210409120,
    -0.0366826186138394, 0.2078674030756356, -0.2425078132986359,
    0.1178491842064994, 0.0842224633480548, -0.2309698831278215,
    0.2259973232808611, -0.0725711693136165, -0.1285256860483044,
    0.2451963201008077, -0.2008018828701612, 0.0245042850823903,
    0.1678897387117543, -0.1767766952966369,
    0.1678897387117546, -0.2487961816680492, 0.2008018828701611,
    -0.0487725805040318, -0.1285256860483055, 0.2392350839330523,
    -0.2259973232808606, 0.0956708580912720, 0.0842224633480554,
    -0.2204803160870888, 0.2425078132986358, -0.1388925582548999,
    -0.0366826186138411, 0.1932526133406851, -0.2496988640512931,
    0.1767766952966361, -0.0122669185818527, -0.1585983210409119,
    0.2472941274911954, -0.2078674030756361, 0.0607450449758149,
    0.1178491842065011, -0.2353860162957555, 0.2309698831278211,
    -0.1068887733575685, -0.0725711693136168, 0.2144321525000690,
    -0.2451963201008071, 0.1489248261231056, 0.0245042850823908,
    -0.1852377813387407, 0.1767766952966369,
    0.1489248261231084, -0.2392350839330522, 0.2353860162957551,
    -0.1388925582549004, -0.0122669185818546, 0.1585983210409117,
    -0.2425078132986361, 0.2309698831278216, -0.1285256860483052,
    -0.0245042850823903, 0.1678897387117547, -0.2451963201008078,
    0.2259973232808605, -0.1178491842064987, -0.0366826186138411,
    0.1767766952966373, -0.2472941274911953, 0.2204803160870885,
    -0.1068887733575702, -0.0487725805040324, 0.1852377813387399,
    -0.2487961816680492, 0.2144321525000680, -0.0956708580912708,
    -0.0607450449758177, 0.1932526133406853, -0.2496988640512931,
    0.2078674030756355, -0.0842224633480553, -0.0725711693136169,
    0.2008018828701609, -0.1767766952966369,
    0.1285256860483054, -0.2204803160870888, 0.2496988640512931,
    -0.2078674030756363, 0.1068887733575705, 0.0245042850823902,
    -0.1489248261231084, 0.2309698831278217, -0.2472941274911952,
    0.1932526133406842, -0.0842224633480549, -0.0487725805040322,
    0.1678897387117541, -0.2392350839330523, 0.2425078132986357,
    -0.1767766952966368, 0.0607450449758166, 0.0725711693136158,
    -0.1852377813387405, 0.2451963201008077, -0.2353860162957554,
    0.1585983210409112, -0.0366826186138393, -0.0956708580912727,
    0.2008018828701609, -0.2487961816680491, 0.2259973232808603,
    -0.1388925582549003, 0.0122669185818551, 0.1178491842065013,
    -0.2144321525000687, 0.1767766952966369,
    0.1068887733575705, -0.1932526133406842, 0.2425078132986360,
    -0.2451963201008076, 0.2008018828701612, -0.1178491842064997,
    0.0122669185818547, 0.0956708580912723, -0.1852377813387397,
    0.2392350839330522, -0.2472941274911952, 0.2078674030756367,
    -0.1285256860483059, 0.0245042850823906, 0.0842224633480547,
    -0.1767766952966367, 0.2353860162957551, -0.2487961816680492,
    0.2144321525000680, -0.1388925582549004, 0.0366826186138402,
    0.0725711693136159, -0.1678897387117549, 0.2309698831278212,
    -0.2496988640512932, 0.2204803160870893, -0.1489248261231091,
    0.0487725805040329, 0.0607450449758152, -0.1585983210409108,
    0.2259973232808606, -0.1767766952966369,
    0.0842224633480551, -0.1585983210409115, 0.2144321525000681,
    -0.2451963201008077, 0.2472941274911952, -0.2204803160870887,
    0.1678897387117543, -0.0956708580912719, 0.0122669185818538,
    0.0725711693136165, -0.1489248261231092, 0.2078674030756365,
    -0.2425078132986364, 0.2487961816680491, -0.2259973232808601,
    0.1767766952966361, -0.1068887733575702, 0.0245042850823887,
    0.0607450449758167, -0.1388925582549021, 0.2008018828701619,
    -0.2392350839330528, 0.2496988640512929, -0.2309698831278214,
    0.1852377813387385, -0.1178491842064969, 0.0366826186138400,
    0.0487725805040335, -0.1285256860483076, 0.1932526133406866,
    -0.2353860162957556, 0.1767766952966369,
    0.0607450449758160, -0.1178491842064995, 0.1678897387117545,
    -0.2078674030756364, 0.2353860162957553, -0.2487961816680492,
    0.2472941274911952, -0.2309698831278216, 0.2008018828701609,
    -0.1585983210409107, 0.1068887733575703, -0.0487725805040324,
    -0.0122669185818544, 0.0725711693136157, -0.1285256860483058,
    0.1767766952966374, -0.2144321525000685, 0.2392350839330526,
    -0.2496988640512932, 0.2451963201008073, -0.2259973232808608,
    0.1932526133406839, -0.1489248261231077, 0.0956708580912732,
    -0.0366826186138392, -0.0245042850823899, 0.0842224633480567,
    -0.1388925582549008, 0.1852377813387413, -0.2204803160870892,
    0.2425078132986367, -0.1767766952966369,
    0.0366826186138405, -0.0725711693136156, 0.1068887733575707,
    -0.1388925582549006, 0.1678897387117546, -0.1932526133406844,
    0.2144321525000679, -0.2309698831278217, 0.2425078132986361,
    -0.2487961816680492, 0.2496988640512931, -0.2451963201008075,
    0.2353860162957552, -0.2204803160870890, 0.2008018828701608,
    -0.1767766952966367, 0.1489248261231086, -0.1178491842064986,
    0.0842224633480546, -0.0487725805040322, 0.0122669185818534,
    0.0245042850823889, -0.0607450449758177, 0.0956708580912736,
    -0.1285256860483061, 0.1585983210409115, -0.1852377813387395,
    0.2078674030756358, -0.2259973232808617, 0.2392350839330527,
    -0.2472941274911954, 0.1767766952966369,
    0.0122669185818545, -0.0245042850823901, 0.0366826186138404,
    -0.0487725805040320, 0.0607450449758159, -0.0725711693136155,
    0.0842224633480545, -0.0956708580912724, 0.1068887733575708,
    -0.1178491842064993, 0.1285256860483049, -0.1388925582549004,
    0.1489248261231086, -0.1585983210409106, 0.1678897387117541,
    -0.1767766952966368, 0.1852377813387399, -0.1932526133406847,
    0.2008018828701608, -0.2078674030756362, 0.2144321525000672,
    -0.2204803160870882, 0.2259973232808605, -0.2309698831278216,
    0.2353860162957553, -0.2392350839330524, 0.2425078132986362,
    -0.2451963201008072, 0.2472941274911950, -0.2487961816680491,
    0.2496988640512930, -0.1767766952966369,
};

double sltmtx16[16*16] = {
    0.2500000000000000, 0.4067446084099804, -0.4164756589659124, -0.1235860659014093, -0.5061927093909617, -0.1646276838589751, 0.0000000000000000, 0.0000000000000000, -0.5116672736016927, -0.1954395075848548, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.3525119939553163, -0.2723777912844086, -0.1133805836208212, -0.0874058970512761, -0.1362009006987028, 0.0000000000000000, 0.0000000000000000, 0.8278950396185307, -0.1207882584319831, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2982793795006523, -0.1282799236029048, -0.1031751013402331, 0.3313809152884095, -0.1077741175384305, 0.0000000000000000, 0.0000000000000000, -0.1207882584319831, 0.8278950396185307, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2440467650459883, 0.0158179440785990, -0.0929696190596450, 0.7501677276280951, -0.0793473343781581, 0.0000000000000000, 0.0000000000000000, -0.1954395075848548, -0.5116672736016927, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.1898141505913242, 0.1599158117601028, -0.0827641367790568, -0.0793473343781581, 0.7501677276280951, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.5116672736016927, -0.1954395075848548, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.1355815361366602, 0.3040136794416066, -0.0725586544984687, -0.1077741175384305, 0.3313809152884095, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.8278950396185307, -0.1207882584319831, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.0813489216819961, 0.4481115471231104, -0.0623531722178806, -0.1362009006987028, -0.0874058970512761, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.1207882584319831, 0.8278950396185307, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.0271163072273320, 0.5922094148046140, -0.0521476899372925, -0.1646276838589751, -0.5061927093909617, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.1954395075848548, -0.5116672736016927, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.0271163072273320, -0.0521476899372925, 0.5922094148046140, 0.0000000000000000, 0.0000000000000000, -0.5061927093909617, -0.1646276838589751, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.5116672736016927, -0.1954395075848548, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.0813489216819960, -0.0623531722178806, 0.4481115471231104, 0.0000000000000000, 0.0000000000000000, -0.0874058970512761, -0.1362009006987028, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.8278950396185307, -0.1207882584319831, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.1355815361366601, -0.0725586544984687, 0.3040136794416066, 0.0000000000000000, 0.0000000000000000, 0.3313809152884095, -0.1077741175384305, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.1207882584319831, 0.8278950396185307, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.1898141505913241, -0.0827641367790568, 0.1599158117601028, 0.0000000000000000, 0.0000000000000000, 0.7501677276280951, -0.0793473343781581, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.1954395075848548, -0.5116672736016927, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.2440467650459882, -0.0929696190596450, 0.0158179440785990, 0.0000000000000000, 0.0000000000000000, -0.0793473343781581, 0.7501677276280951, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.5116672736016927, -0.1954395075848548,
    0.2500000000000000, -0.2982793795006522, -0.1031751013402331, -0.1282799236029048, 0.0000000000000000, 0.0000000000000000, -0.1077741175384305, 0.3313809152884095, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.8278950396185307, -0.1207882584319831,
    0.2500000000000000, -0.3525119939553163, -0.1133805836208212, -0.2723777912844086, 0.0000000000000000, 0.0000000000000000, -0.1362009006987028, -0.0874058970512761, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.1207882584319831, 0.8278950396185307,
    0.2500000000000000, -0.4067446084099803, -0.1235860659014093, -0.4164756589659124, 0.0000000000000000, 0.0000000000000000, -0.1646276838589751, -0.5061927093909617, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.1954395075848548, -0.5116672736016927,
};

double sltmtx8[8*8] = {
    0.3535533905932737, 0.5400617248673215, -0.5061927093909617, -0.1646276838589751, -0.5116672736016927, -0.1954395075848548, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, 0.3857583749052296, -0.0874058970512761, -0.1362009006987028, 0.8278950396185307, -0.1207882584319831, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, 0.2314550249431377, 0.3313809152884095, -0.1077741175384305, -0.1207882584319831, 0.8278950396185307, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, 0.0771516749810458, 0.7501677276280951, -0.0793473343781581, -0.1954395075848548, -0.5116672736016927, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, -0.0771516749810461, -0.0793473343781581, 0.7501677276280951, 0.0000000000000000, 0.0000000000000000, -0.5116672736016927, -0.1954395075848548,
    0.3535533905932737, -0.2314550249431379, -0.1077741175384305, 0.3313809152884095, 0.0000000000000000, 0.0000000000000000, 0.8278950396185307, -0.1207882584319831,
    0.3535533905932737, -0.3857583749052299, -0.1362009006987028, -0.0874058970512761, 0.0000000000000000, 0.0000000000000000, -0.1207882584319831, 0.8278950396185307,
    0.3535533905932737, -0.5400617248673217, -0.1646276838589751, -0.5061927093909617, 0.0000000000000000, 0.0000000000000000, -0.1954395075848548, -0.5116672736016927,
};

double sltmtx4[4*4] = {
    0.5000000000000000, 0.6708203932499369, -0.5116672736016927, -0.1954395075848548,
    0.5000000000000000, 0.2236067977499789, 0.8278950396185307, -0.1207882584319831,
    0.5000000000000000, -0.2236067977499790, -0.1207882584319831, 0.8278950396185307,
    0.5000000000000000, -0.6708203932499368, -0.1954395075848548, -0.5116672736016927,
};

double haarmtx16[16*16] = {
    0.2500000000000000, 0.2500000000000000, 0.3535533905932738, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, 0.3535533905932738, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, 0.3535533905932738, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, 0.3535533905932738, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, 0.2500000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, 0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, 0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, 0.3535533905932738, 0.0000000000000000, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, 0.3535533905932738, 0.0000000000000000, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475,
    0.2500000000000000, -0.2500000000000000, 0.0000000000000000, -0.3535533905932738, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475,
};

double haarmtx8[8*8] = {
    0.3535533905932737, 0.3535533905932737, 0.5000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, 0.3535533905932737, 0.5000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, 0.3535533905932737, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, 0.3535533905932737, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000, 0.0000000000000000,
    0.3535533905932737, -0.3535533905932737, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475, 0.0000000000000000,
    0.3535533905932737, -0.3535533905932737, 0.0000000000000000, 0.5000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475, 0.0000000000000000,
    0.3535533905932737, -0.3535533905932737, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.7071067811865475,
    0.3535533905932737, -0.3535533905932737, 0.0000000000000000, -0.5000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, -0.7071067811865475,
};

double haarmtx4[4*4] = {
    0.5000000000000000, 0.5000000000000000, 0.7071067811865476, 0.0000000000000000,
    0.5000000000000000, 0.5000000000000000, -0.7071067811865476, 0.0000000000000000,
    0.5000000000000000, -0.5000000000000000, 0.0000000000000000, 0.7071067811865476,
    0.5000000000000000, -0.5000000000000000, 0.0000000000000000, -0.7071067811865476,
};

void vp9_fdst(const int16_t *input, int16_t *output, int stride, int size) {
  int i, j, k;
  double *basis;
  int factor = (size == 32) ? 4 : 8;

  switch (size) {
    case 32:
      basis = dstmtx32;
      break;
    default:
      assert(0);
      break;
  }

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      tmp[i*size+j] = 0;
      for (k = 0; k < size; k++) {
        tmp[i*size+j] += input[i*stride+k] * basis[k*size+j];  // row
      }
    }
  }

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      tmp2[i*size+j] = 0;
      for (k = 0; k < size; k++) {
        tmp2[i*size+j] += basis[k*size+i] * tmp[k*size+j];  // col
      }
      if (tmp2[i*size+j] >= 0)
        output[i*size+j] = (int16_t)(tmp2[i*size+j] * factor + 0.5);
      else
        output[i*size+j] = (int16_t)(tmp2[i*size+j] * factor - 0.5);
    }
  }
}

void vp9_fslt(const int16_t *input, int16_t *output, int stride, int size) {
  int i, j, k;
  double *basis;
  int factor = (size == 32) ? 4 : 8;

  switch (size) {
    case 16:
      basis = sltmtx16;
      break;
    case 8:
      basis = sltmtx8;
      break;
    case 4:
      basis = sltmtx4;
      break;
    default:
      assert(0);
      break;
  }

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      tmp[i*size+j] = 0;
      for (k = 0; k < size; k++) {
        tmp[i*size+j] += input[i*stride+k] * basis[k*size+j];  // row
      }
    }
  }

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      tmp2[i*size+j] = 0;
      for (k = 0; k < size; k++) {
        tmp2[i*size+j] += basis[k*size+i] * tmp[k*size+j];  // col
      }
      if (tmp2[i*size+j] >= 0)
        output[i*size+j] = (int16_t)(tmp2[i*size+j] * factor + 0.5);
      else
        output[i*size+j] = (int16_t)(tmp2[i*size+j] * factor - 0.5);
    }
  }
}

void vp9_fhaar(const int16_t *input, int16_t *output, int stride, int size) {
  int i, j, k;
  double *basis;
  int factor = (size == 32) ? 4 : 8;

  switch (size) {
    case 16:
      basis = haarmtx16;
      break;
    case 8:
      basis = haarmtx8;
      break;
    case 4:
      basis = haarmtx4;
      break;
    default:
      assert(0);
      break;
  }

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      tmp[i*size+j] = 0;
      for (k = 0; k < size; k++) {
        tmp[i*size+j] += input[i*stride+k] * basis[k*size+j];  // row
      }
    }
  }

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      tmp2[i*size+j] = 0;
      for (k = 0; k < size; k++) {
        tmp2[i*size+j] += basis[k*size+i] * tmp[k*size+j];  // col
      }
      if (tmp2[i*size+j] >= 0)
        output[i*size+j] = (int16_t)(tmp2[i*size+j] * factor + 0.5);
      else
        output[i*size+j] = (int16_t)(tmp2[i*size+j] * factor - 0.5);
    }
  }
}

void vp9_fnt(const int16_t *input, int16_t *output, int stride, int size) {
  int i, j, k;
  int factor = (size == 32) ? 4 : 8;

  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      if (input[i*stride+j] >= 0)
        output[i*size+j] = (int16_t)(input[i*stride+j] * factor + 0.5);
      else
        output[i*size+j] = (int16_t)(input[i*stride+j] * factor - 0.5);
    }
  }
}
#endif

// Note that although we use dct_32_round in dct32_1d computation flow,
// this 2d fdct32x32 for rate-distortion optimization loop is operating
// within 16 bits precision.
void vp9_fdct32x32_rd_c(const int16_t *input, int16_t *out, int stride) {
  int i, j;
  int output[32 * 32];

  // Columns
  for (i = 0; i < 32; ++i) {
    int temp_in[32], temp_out[32];
    for (j = 0; j < 32; ++j)
      temp_in[j] = input[j * stride + i] * 4;
    dct32_1d(temp_in, temp_out, 0);
    for (j = 0; j < 32; ++j)
      // TODO(cd): see quality impact of only doing
      //           output[j * 32 + i] = (temp_out[j] + 1) >> 2;
      //           PS: also change code in vp9/encoder/x86/vp9_dct_sse2.c
      output[j * 32 + i] = (temp_out[j] + 1 + (temp_out[j] > 0)) >> 2;
  }

  // Rows
  for (i = 0; i < 32; ++i) {
    int temp_in[32], temp_out[32];
    for (j = 0; j < 32; ++j)
      temp_in[j] = output[j + i * 32];
    dct32_1d(temp_in, temp_out, 1);
    for (j = 0; j < 32; ++j)
      out[j + i * 32] = temp_out[j];
  }
}

void vp9_fht4x4(TX_TYPE tx_type, const int16_t *input, int16_t *output,
                int stride) {
  if (tx_type == DCT_DCT)
    vp9_fdct4x4(input, output, stride);
  else
    vp9_short_fht4x4(input, output, stride, tx_type);
}

void vp9_fht8x8(TX_TYPE tx_type, const int16_t *input, int16_t *output,
                int stride) {
  if (tx_type == DCT_DCT)
    vp9_fdct8x8(input, output, stride);
  else
    vp9_short_fht8x8(input, output, stride, tx_type);
}

void vp9_fht16x16(TX_TYPE tx_type, const int16_t *input, int16_t *output,
                  int stride) {
  if (tx_type == DCT_DCT)
    vp9_fdct16x16(input, output, stride);
  else
    vp9_short_fht16x16(input, output, stride, tx_type);
}
