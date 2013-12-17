/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 * ARMv8 support by rewrite NEON assembly to NEON intrinsics.
 * Enable with -mfpu=neon compile option.
 *
 * This file includes below assembly code.
 * dequantizeb_neon.asm
 */

#include <arm_neon.h>

void vp8_dequantize_b_loop_neon(
        int16_t *Q,
        int16_t *DQC,
        int16_t *DQ) {
    int16x8x2_t qQ, qDQC, qDQ;

    qQ   = vld2q_s16(Q);
    qDQC = vld2q_s16(DQC);

    qDQ.val[0] = vmulq_s16(qQ.val[0], qDQC.val[0]);
    qDQ.val[1] = vmulq_s16(qQ.val[1], qDQC.val[1]);

    vst2q_s16(DQ, qDQ);
    return;
}
