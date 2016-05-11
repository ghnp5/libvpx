/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "test/acm_random.h"
#include "test/vp10_txfm_test.h"
#include "vp10/common/vp10_txfm.h"
#include "./vp10_rtcd.h"

using libvpx_test::ACMRandom;
using libvpx_test::input_base;
using libvpx_test::bd;
using libvpx_test::compute_avg_abs_error;
using libvpx_test::Fwd_Txfm2d_Func;
using libvpx_test::TYPE_TXFM;
using libvpx_test::TYPE_DCT;
using libvpx_test::TYPE_ADST;

namespace {

#if CONFIG_VP9_HIGHBITDEPTH
const Fwd_Txfm2d_Func fwd_txfm_func_ls[TX_SIZES] = {
    vp10_fwd_txfm2d_4x4_c, vp10_fwd_txfm2d_8x8_c, vp10_fwd_txfm2d_16x16_c,
    vp10_fwd_txfm2d_32x32_c};

const TYPE_TXFM type_ls_0[TX_SIZES] = {TYPE_DCT, TYPE_ADST, TYPE_DCT, TYPE_ADST};
const TYPE_TXFM type_ls_1[TX_SIZES] = {TYPE_DCT, TYPE_DCT, TYPE_ADST, TYPE_ADST};

TEST(vp10_fwd_txfm2d, accuracy) {
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    int txfm_size = 1 << (tx_size + 2);
    int sqr_txfm_size = txfm_size * txfm_size;
    int16_t* input = new int16_t[sqr_txfm_size];
    int32_t* output = new int32_t[sqr_txfm_size];
    double* ref_input = new double[sqr_txfm_size];
    double* ref_output = new double[sqr_txfm_size];

    for (int tx_type = 0; tx_type < TX_TYPES; ++tx_type) {
      TXFM_2D_FLIP_CFG fwd_txfm_flip_cfg = vp10_get_fwd_txfm_cfg(tx_type, tx_size);
      const TXFM_2D_CFG *fwd_txfm_cfg = fwd_txfm_flip_cfg.cfg;
      if (fwd_txfm_cfg != NULL) {
        Fwd_Txfm2d_Func fwd_txfm_func = fwd_txfm_func_ls[tx_size];
        TYPE_TXFM type0 = type_ls_0[tx_type];
        TYPE_TXFM type1 = type_ls_1[tx_type];
        int amplify_bit = fwd_txfm_cfg->shift[0] + fwd_txfm_cfg->shift[1] +
                          fwd_txfm_cfg->shift[2];
        double amplify_factor =
            amplify_bit >= 0 ? (1 << amplify_bit) : (1.0 / (1 << -amplify_bit));

        ACMRandom rnd(ACMRandom::DeterministicSeed());
        int count = 500;
        double avg_abs_error = 0;
        for (int ci = 0; ci < count; ci++) {
          for (int ni = 0; ni < sqr_txfm_size; ++ni) {
            input[ni] = rnd.Rand16() % input_base;
            ref_input[ni] = static_cast<double>(input[ni]);
            output[ni] = 0;
            ref_output[ni] = 0;
          }

          fwd_txfm_func(input, output, txfm_size, tx_type, bd);
          reference_hybrid_2d(ref_input, ref_output, txfm_size, type0, type1);

          for (int ni = 0; ni < sqr_txfm_size; ++ni) {
            ref_output[ni] = round(ref_output[ni] * amplify_factor);
            EXPECT_LE(fabs(output[ni] - ref_output[ni]) / amplify_factor, 70);
          }
          avg_abs_error += compute_avg_abs_error<int32_t, double>(
              output, ref_output, sqr_txfm_size);
        }

        avg_abs_error /= amplify_factor;
        avg_abs_error /= count;
        // max_abs_avg_error comes from upper bound of avg_abs_error
        // printf("type0: %d type1: %d txfm_size: %d accuracy_avg_abs_error:
        // %f\n",
        // type0, type1, txfm_size, avg_abs_error);
        double max_abs_avg_error = 7;
        EXPECT_LE(avg_abs_error, max_abs_avg_error);
      }
    }

    delete[] input;
    delete[] output;
    delete[] ref_input;
    delete[] ref_output;
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

}  // namespace
