/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vp9/common/vp9_blockd.h"
#include "vp9/decoder/vp9_onyxd_int.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"
#include "vp9/decoder/vp9_detokenize.h"
#include "vp9/common/vp9_seg_common.h"

#define EOB_CONTEXT_NODE            0
#define ZERO_CONTEXT_NODE           1
#define ONE_CONTEXT_NODE            2
#define LOW_VAL_CONTEXT_NODE        3
#define TWO_CONTEXT_NODE            4
#define THREE_CONTEXT_NODE          5
#define HIGH_LOW_CONTEXT_NODE       6
#define CAT_ONE_CONTEXT_NODE        7
#define CAT_THREEFOUR_CONTEXT_NODE  8
#define CAT_THREE_CONTEXT_NODE      9
#define CAT_FIVE_CONTEXT_NODE       10

#define CAT1_MIN_VAL    5
#define CAT2_MIN_VAL    7
#define CAT3_MIN_VAL   11
#define CAT4_MIN_VAL   19
#define CAT5_MIN_VAL   35
#define CAT6_MIN_VAL   67
#define CAT1_PROB0    159
#define CAT2_PROB0    145
#define CAT2_PROB1    165

#define CAT3_PROB0 140
#define CAT3_PROB1 148
#define CAT3_PROB2 173

#define CAT4_PROB0 135
#define CAT4_PROB1 140
#define CAT4_PROB2 155
#define CAT4_PROB3 176

#define CAT5_PROB0 130
#define CAT5_PROB1 134
#define CAT5_PROB2 141
#define CAT5_PROB3 157
#define CAT5_PROB4 180

static const vp9_prob cat6_prob[15] = {
  254, 254, 254, 252, 249, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0
};

DECLARE_ALIGNED(16, extern const uint8_t, vp9_norm[256]);

static int get_signed(BOOL_DECODER *br, int value_to_sign) {
  return decode_bool(br, 128) ? -value_to_sign : value_to_sign;
}

#define INCREMENT_COUNT(token)               \
  do {                                       \
    coef_counts[type][ref][get_coef_band(txfm_size, c)][pt][token]++;     \
    pt = vp9_get_coef_context(&recent_energy, token);         \
  } while (0)

#define WRITE_COEF_CONTINUE(val, token)                       \
  {                                                           \
    qcoeff_ptr[scan[c]] = (int16_t) get_signed(br, val);        \
    INCREMENT_COUNT(token);                                   \
    c++;                                                      \
    continue;                                                 \
  }

#define ADJUST_COEF(prob, bits_count)  \
  do {                                 \
    if (vp9_read(br, prob))            \
      val += (uint16_t)(1 << bits_count);\
  } while (0);

static int decode_coefs(VP9D_COMP *dx, const MACROBLOCKD *xd,
                        BOOL_DECODER* const br, int block_idx,
                        PLANE_TYPE type, TX_TYPE tx_type,
                        int seg_eob, int16_t *qcoeff_ptr,
                        const int *const scan, TX_SIZE txfm_size) {
  ENTROPY_CONTEXT* const A0 = (ENTROPY_CONTEXT *) xd->above_context;
  ENTROPY_CONTEXT* const L0 = (ENTROPY_CONTEXT *) xd->left_context;
  int aidx, lidx;
  ENTROPY_CONTEXT above_ec, left_ec;
  FRAME_CONTEXT *const fc = &dx->common.fc;
  int recent_energy = 0;
  int pt, c = 0;
  vp9_coeff_probs *coef_probs;
  vp9_prob *prob;
  vp9_coeff_count *coef_counts;
  const int ref = xd->mode_info_context->mbmi.ref_frame != INTRA_FRAME;

  if (xd->mode_info_context->mbmi.sb_type == BLOCK_SIZE_SB64X64) {
    aidx = vp9_block2above_sb64[txfm_size][block_idx];
    lidx = vp9_block2left_sb64[txfm_size][block_idx];
  } else if (xd->mode_info_context->mbmi.sb_type == BLOCK_SIZE_SB32X32) {
    aidx = vp9_block2above_sb[txfm_size][block_idx];
    lidx = vp9_block2left_sb[txfm_size][block_idx];
  } else {
    aidx = vp9_block2above[txfm_size][block_idx];
    lidx = vp9_block2left[txfm_size][block_idx];
  }

  switch (txfm_size) {
    default:
    case TX_4X4:
#if CONFIG_DCTOKPRED
      above_ec = A0[aidx] >> 3;
      left_ec = L0[lidx] >> 3;
#else
      above_ec = A0[aidx] != 0;
      left_ec = L0[lidx] != 0;
#endif
      coef_probs  = fc->coef_probs_4x4;
      coef_counts = fc->coef_counts_4x4;
      break;
    case TX_8X8:
      coef_probs  = fc->coef_probs_8x8;
      coef_counts = fc->coef_counts_8x8;
#if CONFIG_DCTOKPRED
      above_ec = (A0[aidx] + A0[aidx + 1]) >> 3;
      left_ec  = (L0[lidx] + L0[lidx + 1]) >> 3;
#else
      above_ec = (A0[aidx] + A0[aidx + 1]) != 0;
      left_ec  = (L0[lidx] + L0[lidx + 1]) != 0;
#endif
      break;
    case TX_16X16:
      coef_probs  = fc->coef_probs_16x16;
      coef_counts = fc->coef_counts_16x16;
      if (type == PLANE_TYPE_UV) {
        ENTROPY_CONTEXT *A1 = (ENTROPY_CONTEXT *) (xd->above_context + 1);
        ENTROPY_CONTEXT *L1 = (ENTROPY_CONTEXT *) (xd->left_context + 1);
#if CONFIG_DCTOKPRED
        above_ec = (A0[aidx] + A0[aidx + 1] + A1[aidx] + A1[aidx + 1]) >> 3;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L1[lidx] + L1[lidx + 1]) >> 3;
      } else {
        above_ec = (A0[aidx] + A0[aidx + 1] + A0[aidx + 2] + A0[aidx + 3]) >> 3;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L0[lidx + 2] + L0[lidx + 3]) >> 3;
#else
        above_ec = (A0[aidx] + A0[aidx + 1] + A1[aidx] + A1[aidx + 1]) != 0;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L1[lidx] + L1[lidx + 1]) != 0;
      } else {
        above_ec = (A0[aidx] + A0[aidx + 1] + A0[aidx + 2] + A0[aidx + 3]) != 0;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L0[lidx + 2] + L0[lidx + 3]) != 0;
#endif
      }
      break;
    case TX_32X32:
      coef_probs = fc->coef_probs_32x32;
      coef_counts = fc->coef_counts_32x32;
      if (type == PLANE_TYPE_UV) {
        ENTROPY_CONTEXT *A1 = (ENTROPY_CONTEXT *) (xd->above_context + 1);
        ENTROPY_CONTEXT *L1 = (ENTROPY_CONTEXT *) (xd->left_context + 1);
        ENTROPY_CONTEXT *A2 = (ENTROPY_CONTEXT *) (xd->above_context + 2);
        ENTROPY_CONTEXT *L2 = (ENTROPY_CONTEXT *) (xd->left_context + 2);
        ENTROPY_CONTEXT *A3 = (ENTROPY_CONTEXT *) (xd->above_context + 3);
        ENTROPY_CONTEXT *L3 = (ENTROPY_CONTEXT *) (xd->left_context + 3);
#if CONFIG_DCTOKPRED
        above_ec = (A0[aidx] + A0[aidx + 1] + A1[aidx] + A1[aidx + 1] +
                    A2[aidx] + A2[aidx + 1] + A3[aidx] + A3[aidx + 1]) >> 3;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L1[lidx] + L1[lidx + 1] +
                    L2[lidx] + L2[lidx + 1] + L3[lidx] + L3[lidx + 1]) >> 3;
#else
        above_ec = (A0[aidx] + A0[aidx + 1] + A1[aidx] + A1[aidx + 1] +
                    A2[aidx] + A2[aidx + 1] + A3[aidx] + A3[aidx + 1]) != 0;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L1[lidx] + L1[lidx + 1] +
                    L2[lidx] + L2[lidx + 1] + L3[lidx] + L3[lidx + 1]) != 0;
#endif
      } else {
        ENTROPY_CONTEXT *A1 = (ENTROPY_CONTEXT *) (xd->above_context + 1);
        ENTROPY_CONTEXT *L1 = (ENTROPY_CONTEXT *) (xd->left_context + 1);
#if CONFIG_DCTOKPRED
        above_ec = (A0[aidx] + A0[aidx + 1] + A0[aidx + 2] + A0[aidx + 3] +
                    A1[aidx] + A1[aidx + 1] + A1[aidx + 2] + A1[aidx + 3]) >> 3;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L0[lidx + 2] + L0[lidx + 3] +
                    L1[lidx] + L1[lidx + 1] + L1[lidx + 2] + L1[lidx + 3]) >> 3;
#else
        above_ec = (A0[aidx] + A0[aidx + 1] + A0[aidx + 2] + A0[aidx + 3] +
                    A1[aidx] + A1[aidx + 1] + A1[aidx + 2] + A1[aidx + 3]) != 0;
        left_ec  = (L0[lidx] + L0[lidx + 1] + L0[lidx + 2] + L0[lidx + 3] +
                    L1[lidx] + L1[lidx + 1] + L1[lidx + 2] + L1[lidx + 3]) != 0;
#endif
      }
      break;
  }

  VP9_COMBINEENTROPYCONTEXTS(pt, above_ec, left_ec);
  while (1) {
    int val;
    const uint8_t *cat6 = cat6_prob;

    if (c >= seg_eob)
      break;
    prob = coef_probs[type][ref][get_coef_band(txfm_size, c)][pt];
    if (!vp9_read(br, prob[EOB_CONTEXT_NODE]))
      break;
SKIP_START:
    if (c >= seg_eob)
      break;
    if (!vp9_read(br, prob[ZERO_CONTEXT_NODE])) {
      INCREMENT_COUNT(ZERO_TOKEN);
      ++c;
      prob = coef_probs[type][ref][get_coef_band(txfm_size, c)][pt];
      goto SKIP_START;
    }
    // ONE_CONTEXT_NODE_0_
    if (!vp9_read(br, prob[ONE_CONTEXT_NODE])) {
      WRITE_COEF_CONTINUE(1, ONE_TOKEN);
    }
    // LOW_VAL_CONTEXT_NODE_0_
    if (!vp9_read(br, prob[LOW_VAL_CONTEXT_NODE])) {
      if (!vp9_read(br, prob[TWO_CONTEXT_NODE])) {
        WRITE_COEF_CONTINUE(2, TWO_TOKEN);
      }
      if (!vp9_read(br, prob[THREE_CONTEXT_NODE])) {
        WRITE_COEF_CONTINUE(3, THREE_TOKEN);
      }
      WRITE_COEF_CONTINUE(4, FOUR_TOKEN);
    }
    // HIGH_LOW_CONTEXT_NODE_0_
    if (!vp9_read(br, prob[HIGH_LOW_CONTEXT_NODE])) {
      if (!vp9_read(br, prob[CAT_ONE_CONTEXT_NODE])) {
        val = CAT1_MIN_VAL;
        ADJUST_COEF(CAT1_PROB0, 0);
        WRITE_COEF_CONTINUE(val, DCT_VAL_CATEGORY1);
      }
      val = CAT2_MIN_VAL;
      ADJUST_COEF(CAT2_PROB1, 1);
      ADJUST_COEF(CAT2_PROB0, 0);
      WRITE_COEF_CONTINUE(val, DCT_VAL_CATEGORY2);
    }
    // CAT_THREEFOUR_CONTEXT_NODE_0_
    if (!vp9_read(br, prob[CAT_THREEFOUR_CONTEXT_NODE])) {
      if (!vp9_read(br, prob[CAT_THREE_CONTEXT_NODE])) {
        val = CAT3_MIN_VAL;
        ADJUST_COEF(CAT3_PROB2, 2);
        ADJUST_COEF(CAT3_PROB1, 1);
        ADJUST_COEF(CAT3_PROB0, 0);
        WRITE_COEF_CONTINUE(val, DCT_VAL_CATEGORY3);
      }
      val = CAT4_MIN_VAL;
      ADJUST_COEF(CAT4_PROB3, 3);
      ADJUST_COEF(CAT4_PROB2, 2);
      ADJUST_COEF(CAT4_PROB1, 1);
      ADJUST_COEF(CAT4_PROB0, 0);
      WRITE_COEF_CONTINUE(val, DCT_VAL_CATEGORY4);
    }
    // CAT_FIVE_CONTEXT_NODE_0_:
    if (!vp9_read(br, prob[CAT_FIVE_CONTEXT_NODE])) {
      val = CAT5_MIN_VAL;
      ADJUST_COEF(CAT5_PROB4, 4);
      ADJUST_COEF(CAT5_PROB3, 3);
      ADJUST_COEF(CAT5_PROB2, 2);
      ADJUST_COEF(CAT5_PROB1, 1);
      ADJUST_COEF(CAT5_PROB0, 0);
      WRITE_COEF_CONTINUE(val, DCT_VAL_CATEGORY5);
    }
    val = 0;
    while (*cat6) {
      val = (val << 1) | vp9_read(br, *cat6++);
    }
    val += CAT6_MIN_VAL;
    WRITE_COEF_CONTINUE(val, DCT_VAL_CATEGORY6);
  }

  if (c < seg_eob)
    coef_counts[type][ref][get_coef_band(txfm_size, c)][pt][DCT_EOB_TOKEN]++;

#if CONFIG_DCTOKPRED
  pt = abs(qcoeff_ptr[scan[0]]);
  pt <<= 3 - txfm_size; // uniform scale
  A0[aidx] = L0[lidx] = pt;
#else
  A0[aidx] = L0[lidx] = c > 0;
#endif
  if (txfm_size >= TX_8X8) {
    A0[aidx + 1] = L0[lidx + 1] = A0[aidx];
    if (txfm_size >= TX_16X16) {
      if (type == PLANE_TYPE_UV) {
        ENTROPY_CONTEXT *A1 = (ENTROPY_CONTEXT *) (xd->above_context + 1);
        ENTROPY_CONTEXT *L1 = (ENTROPY_CONTEXT *) (xd->left_context + 1);
        A1[aidx] = A1[aidx + 1] = L1[lidx] = L1[lidx + 1] = A0[aidx];
        if (txfm_size >= TX_32X32) {
          ENTROPY_CONTEXT *A2 = (ENTROPY_CONTEXT *) (xd->above_context + 2);
          ENTROPY_CONTEXT *L2 = (ENTROPY_CONTEXT *) (xd->left_context + 2);
          ENTROPY_CONTEXT *A3 = (ENTROPY_CONTEXT *) (xd->above_context + 3);
          ENTROPY_CONTEXT *L3 = (ENTROPY_CONTEXT *) (xd->left_context + 3);
          A2[aidx] = A2[aidx + 1] = A3[aidx] = A3[aidx + 1] = A0[aidx];
          L2[lidx] = L2[lidx + 1] = L3[lidx] = L3[lidx + 1] = A0[aidx];
        }
      } else {
        A0[aidx + 2] = A0[aidx + 3] = L0[lidx + 2] = L0[lidx + 3] = A0[aidx];
        if (txfm_size >= TX_32X32) {
          ENTROPY_CONTEXT *A1 = (ENTROPY_CONTEXT *) (xd->above_context + 1);
          ENTROPY_CONTEXT *L1 = (ENTROPY_CONTEXT *) (xd->left_context + 1);
          A1[aidx] = A1[aidx + 1] = A1[aidx + 2] = A1[aidx + 3] = A0[aidx];
          L1[lidx] = L1[lidx + 1] = L1[lidx + 2] = L1[lidx + 3] = A0[aidx];
        }
      }
    }
  }

  return c;
}

static int get_eob(MACROBLOCKD* const xd, int segment_id, int eob_max) {
  return vp9_get_segdata(xd, segment_id, SEG_LVL_SKIP) ? 0 : eob_max;
}

int vp9_decode_sb_tokens(VP9D_COMP* const pbi,
                         MACROBLOCKD* const xd,
                         BOOL_DECODER* const bc) {
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  int i, eobtotal = 0, seg_eob, c;

  switch (xd->mode_info_context->mbmi.txfm_size) {
    case TX_32X32:
      // Luma block
      c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                       DCT_DCT, get_eob(xd, segment_id, 1024),
                       xd->qcoeff, vp9_default_zig_zag1d_32x32, TX_32X32);
      xd->eobs[0] = c;
      eobtotal += c;

      // 16x16 chroma blocks
      seg_eob = get_eob(xd, segment_id, 256);
      for (i = 64; i < 96; i += 16) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_16x16, TX_16X16);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    case TX_16X16:
      // 16x16 luma blocks
      seg_eob = get_eob(xd, segment_id, 256);
      for (i = 0; i < 64; i += 16) {
        c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_16x16, TX_16X16);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 16x16 chroma blocks
      for (i = 64; i < 96; i += 16) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_16x16, TX_16X16);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    case TX_8X8:
      // 8x8 luma blocks
      seg_eob = get_eob(xd, segment_id, 64);
      for (i = 0; i < 64; i += 4) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_8x8, TX_8X8);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 8x8 chroma blocks
      for (i = 64; i < 96; i += 4) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_8x8, TX_8X8);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    case TX_4X4:
      // 4x4 luma blocks
      seg_eob = get_eob(xd, segment_id, 16);
      for (i = 0; i < 64; i++) {
        c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_4x4, TX_4X4);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 4x4 chroma blocks
      for (i = 64; i < 96; i++) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_4x4, TX_4X4);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    default: assert(0);
  }

  return eobtotal;
}

int vp9_decode_sb64_tokens(VP9D_COMP* const pbi,
                           MACROBLOCKD* const xd,
                           BOOL_DECODER* const bc) {
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  int i, eobtotal = 0, seg_eob, c;

  switch (xd->mode_info_context->mbmi.txfm_size) {
    case TX_32X32:
      // Luma block
      seg_eob = get_eob(xd, segment_id, 1024);
      for (i = 0; i < 256; i += 64) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_32x32, TX_32X32);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 32x32 chroma blocks
      for (i = 256; i < 384; i += 64) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_32x32, TX_32X32);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    case TX_16X16:
      // 16x16 luma blocks
      seg_eob = get_eob(xd, segment_id, 256);
      for (i = 0; i < 256; i += 16) {
        c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_16x16, TX_16X16);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 16x16 chroma blocks
      for (i = 256; i < 384; i += 16) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_16x16, TX_16X16);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    case TX_8X8:
      // 8x8 luma blocks
      seg_eob = get_eob(xd, segment_id, 64);
      for (i = 0; i < 256; i += 4) {
        c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_8x8, TX_8X8);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 8x8 chroma blocks
      for (i = 256; i < 384; i += 4) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_8x8, TX_8X8);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    case TX_4X4:
      // 4x4 luma blocks
      seg_eob = get_eob(xd, segment_id, 16);
      for (i = 0; i < 256; i++) {
        c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                         DCT_DCT, seg_eob, xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_4x4, TX_4X4);
        xd->eobs[i] = c;
        eobtotal += c;
      }

      // 4x4 chroma blocks
      for (i = 256; i < 384; i++) {
        c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV, DCT_DCT, seg_eob,
                         xd->qcoeff + i * 16,
                         vp9_default_zig_zag1d_4x4, TX_4X4);
        xd->eobs[i] = c;
        eobtotal += c;
      }
      break;
    default: assert(0);
  }

  return eobtotal;
}

static int vp9_decode_mb_tokens_16x16(VP9D_COMP* const pbi,
                                      MACROBLOCKD* const xd,
                                      BOOL_DECODER* const bc) {
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  int i, eobtotal = 0, seg_eob;

  // Luma block
  int c = decode_coefs(pbi, xd, bc, 0, PLANE_TYPE_Y_WITH_DC,
                       get_tx_type(xd, &xd->block[0]),
                       get_eob(xd, segment_id, 256),
                       xd->qcoeff, vp9_default_zig_zag1d_16x16, TX_16X16);
  xd->eobs[0] = c;
  eobtotal += c;

  // 8x8 chroma blocks
  seg_eob = get_eob(xd, segment_id, 64);
  for (i = 16; i < 24; i += 4) {
    c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV,
                     DCT_DCT, seg_eob, xd->block[i].qcoeff,
                     vp9_default_zig_zag1d_8x8, TX_8X8);
    xd->eobs[i] = c;
    eobtotal += c;
  }
  return eobtotal;
}

static int vp9_decode_mb_tokens_8x8(VP9D_COMP* const pbi,
                                    MACROBLOCKD* const xd,
                                    BOOL_DECODER* const bc) {
  int c, i, eobtotal = 0, seg_eob;
  const int segment_id = xd->mode_info_context->mbmi.segment_id;

  // luma blocks
  seg_eob = get_eob(xd, segment_id, 64);
  for (i = 0; i < 16; i += 4) {
    c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_Y_WITH_DC,
                     get_tx_type(xd, xd->block + i),
                     seg_eob, xd->block[i].qcoeff,
                     vp9_default_zig_zag1d_8x8, TX_8X8);
    xd->eobs[i] = c;
    eobtotal += c;
  }

  // chroma blocks
  if (xd->mode_info_context->mbmi.mode == I8X8_PRED ||
      xd->mode_info_context->mbmi.mode == SPLITMV) {
    // use 4x4 transform for U, V components in I8X8/splitmv prediction mode
    seg_eob = get_eob(xd, segment_id, 16);
    for (i = 16; i < 24; i++) {
      c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV,
                       DCT_DCT, seg_eob, xd->block[i].qcoeff,
                       vp9_default_zig_zag1d_4x4, TX_4X4);
      xd->eobs[i] = c;
      eobtotal += c;
    }
  } else {
    for (i = 16; i < 24; i += 4) {
      c = decode_coefs(pbi, xd, bc, i, PLANE_TYPE_UV,
                       DCT_DCT, seg_eob, xd->block[i].qcoeff,
                       vp9_default_zig_zag1d_8x8, TX_8X8);
      xd->eobs[i] = c;
      eobtotal += c;
    }
  }

  return eobtotal;
}

static int decode_coefs_4x4(VP9D_COMP *dx, MACROBLOCKD *xd,
                            BOOL_DECODER* const bc,
                            PLANE_TYPE type, int i, int seg_eob,
                            TX_TYPE tx_type, const int *scan) {
  int c = decode_coefs(dx, xd, bc, i, type, tx_type, seg_eob,
                       xd->block[i].qcoeff, scan, TX_4X4);
  xd->eobs[i] = c;
  return c;
}

static int decode_coefs_4x4_y(VP9D_COMP *dx, MACROBLOCKD *xd,
                              BOOL_DECODER* const bc,
                              PLANE_TYPE type, int i, int seg_eob) {
  const TX_TYPE tx_type = (type == PLANE_TYPE_Y_WITH_DC) ?
                          get_tx_type(xd, &xd->block[i]) : DCT_DCT;
  const int *scan;

  switch (tx_type) {
    case ADST_DCT:
      scan = vp9_row_scan_4x4;
      break;
    case DCT_ADST:
      scan = vp9_col_scan_4x4;
      break;
    default:
      scan = vp9_default_zig_zag1d_4x4;
      break;
  }

  return decode_coefs_4x4(dx, xd, bc, type, i, seg_eob, tx_type, scan);
}

int vp9_decode_coefs_4x4(VP9D_COMP *dx, MACROBLOCKD *xd,
                         BOOL_DECODER* const bc,
                         PLANE_TYPE type, int i) {
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  const int seg_eob = get_eob(xd, segment_id, 16);

  return decode_coefs_4x4_y(dx, xd, bc, type, i, seg_eob);
}

static int decode_mb_tokens_4x4_uv(VP9D_COMP* const dx,
                                   MACROBLOCKD* const xd,
                                   BOOL_DECODER* const bc,
                                   int seg_eob) {
  int eobtotal = 0, i;

  // chroma blocks
  for (i = 16; i < 24; i++) {
    eobtotal += decode_coefs_4x4(dx, xd, bc, PLANE_TYPE_UV, i, seg_eob,
                                 DCT_DCT, vp9_default_zig_zag1d_4x4);
  }

  return eobtotal;
}

int vp9_decode_mb_tokens_4x4_uv(VP9D_COMP* const dx,
                                MACROBLOCKD* const xd,
                                BOOL_DECODER* const bc) {
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  const int seg_eob = get_eob(xd, segment_id, 16);

  return decode_mb_tokens_4x4_uv(dx, xd, bc, seg_eob);
}

static int vp9_decode_mb_tokens_4x4(VP9D_COMP* const dx,
                                    MACROBLOCKD* const xd,
                                    BOOL_DECODER* const bc) {
  int i, eobtotal = 0;
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  const int seg_eob = get_eob(xd, segment_id, 16);

  // luma blocks
  for (i = 0; i < 16; ++i) {
    eobtotal += decode_coefs_4x4_y(dx, xd, bc,
                                   PLANE_TYPE_Y_WITH_DC, i, seg_eob);
  }

  // chroma blocks
  eobtotal += decode_mb_tokens_4x4_uv(dx, xd, bc, seg_eob);

  return eobtotal;
}

int vp9_decode_mb_tokens(VP9D_COMP* const dx,
                         MACROBLOCKD* const xd,
                         BOOL_DECODER* const bc) {
  const TX_SIZE tx_size = xd->mode_info_context->mbmi.txfm_size;
  switch (tx_size) {
    case TX_16X16:
      return vp9_decode_mb_tokens_16x16(dx, xd, bc);
    case TX_8X8:
      return vp9_decode_mb_tokens_8x8(dx, xd, bc);
    default:
      assert(tx_size == TX_4X4);
      return vp9_decode_mb_tokens_4x4(dx, xd, bc);
  }
}
