/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_entropy.h"
#if CONFIG_COEFFICIENT_RANGE_CHECKING
#include "vp9/common/vp9_idct.h"
#endif
#include "vp9/decoder/vp9_detokenize.h"

#define EOB_CONTEXT_NODE            0
#define ZERO_CONTEXT_NODE           1
#define ONE_CONTEXT_NODE            2
#define LOW_VAL_CONTEXT_NODE        0
#define TWO_CONTEXT_NODE            1
#define THREE_CONTEXT_NODE          2
#define HIGH_LOW_CONTEXT_NODE       3
#define CAT_ONE_CONTEXT_NODE        4
#define CAT_THREEFOUR_CONTEXT_NODE  5
#define CAT_THREE_CONTEXT_NODE      6
#define CAT_FIVE_CONTEXT_NODE       7

#define INCREMENT_COUNT(token)                              \
  do {                                                      \
     if (!cm->frame_parallel_decoding_mode)                 \
       ++coef_counts[band][ctx][token];                     \
  } while (0)

static INLINE int read_coeff(const vp9_prob *probs, int n, vp9_reader *r) {
  int i, val = 0;
  for (i = 0; i < n; ++i)
    val = (val << 1) | vp9_read(r, probs[i]);
  return val;
}

static const vp9_tree_index coeff_subtree_high[TREE_SIZE(ENTROPY_TOKENS)] = {
  2, 6,                                         /* 0 = LOW_VAL */
  -TWO_TOKEN, 4,                                /* 1 = TWO */
  -THREE_TOKEN, -FOUR_TOKEN,                    /* 2 = THREE */
  8, 10,                                        /* 3 = HIGH_LOW */
  -CATEGORY1_TOKEN, -CATEGORY2_TOKEN,           /* 4 = CAT_ONE */
  12, 14,                                       /* 5 = CAT_THREEFOUR */
  -CATEGORY3_TOKEN, -CATEGORY4_TOKEN,           /* 6 = CAT_THREE */
  -CATEGORY5_TOKEN, -CATEGORY6_TOKEN            /* 7 = CAT_FIVE */
};

static int decode_coefs(VP9_COMMON *cm, const MACROBLOCKD *xd, PLANE_TYPE type,
                        tran_low_t *dqcoeff, TX_SIZE tx_size,
                        const int16_t *dq,
#if CONFIG_NEW_QUANT
                        const dequant_val_type_nuq *dq_val,
#endif  // CONFIG_NEW_QUANT
                        int ctx, const int16_t *scan, const int16_t *nb,
                        vp9_reader *r) {
  const int max_eob = 16 << (tx_size << 1);
  const FRAME_CONTEXT *const fc = &cm->fc;
  FRAME_COUNTS *const counts = &cm->counts;
  const int ref = is_inter_block(&xd->mi[0].src_mi->mbmi);
  int band, c = 0;
  const vp9_prob (*coef_probs)[COEFF_CONTEXTS][UNCONSTRAINED_NODES] =
      fc->coef_probs[tx_size][type][ref];
  const vp9_prob *prob;
  unsigned int (*coef_counts)[COEFF_CONTEXTS][UNCONSTRAINED_NODES + 1] =
      counts->coef[tx_size][type][ref];
  unsigned int (*eob_branch_count)[COEFF_CONTEXTS] =
      counts->eob_branch[tx_size][type][ref];
  uint8_t token_cache[MAX_NUM_COEFS];
  const uint8_t *band_translate = get_band_translate(tx_size);
  const int dq_shift = (tx_size > TX_16X16) ? tx_size - TX_16X16 : 0;
  int v, token;
  int16_t dqv = dq[0];
#if CONFIG_NEW_QUANT
#if CONFIG_TX_SKIP
  const int use_rect_quant = is_rect_quant_used(&xd->mi[0].src_mi->mbmi, type);
#endif
  const tran_low_t *dqv_val = &dq_val[0][0];
#endif  // CONFIG_NEW_QUANT
#if CONFIG_TX_SKIP
  int tx_skip = xd->mi[0].src_mi->mbmi.tx_skip[type];
#endif  // CONFIG_TX_SKIP
  const uint8_t *cat1_prob;
  const uint8_t *cat2_prob;
  const uint8_t *cat3_prob;
  const uint8_t *cat4_prob;
  const uint8_t *cat5_prob;
  const uint8_t *cat6_prob;

#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth) {
    if (cm->bit_depth == VPX_BITS_10) {
      cat1_prob = vp9_cat1_prob_high10;
      cat2_prob = vp9_cat2_prob_high10;
      cat3_prob = vp9_cat3_prob_high10;
      cat4_prob = vp9_cat4_prob_high10;
      cat5_prob = vp9_cat5_prob_high10;
      cat6_prob = vp9_cat6_prob_high10;
    } else {
      cat1_prob = vp9_cat1_prob_high12;
      cat2_prob = vp9_cat2_prob_high12;
      cat3_prob = vp9_cat3_prob_high12;
      cat4_prob = vp9_cat4_prob_high12;
      cat5_prob = vp9_cat5_prob_high12;
      cat6_prob = vp9_cat6_prob_high12;
    }
  } else {
    cat1_prob = vp9_cat1_prob;
    cat2_prob = vp9_cat2_prob;
    cat3_prob = vp9_cat3_prob;
    cat4_prob = vp9_cat4_prob;
    cat5_prob = vp9_cat5_prob;
    cat6_prob = vp9_cat6_prob;
  }
#else
  cat1_prob = vp9_cat1_prob;
  cat2_prob = vp9_cat2_prob;
  cat3_prob = vp9_cat3_prob;
  cat4_prob = vp9_cat4_prob;
  cat5_prob = vp9_cat5_prob;
  cat6_prob = vp9_cat6_prob;
#endif

#if CONFIG_TX_SKIP
    if (tx_skip)
      band_translate = vp9_coefband_tx_skip;
#endif  // CONFIG_TX_SKIP

  while (c < max_eob) {
    int val = -1;
    band = *band_translate++;
    prob = coef_probs[band][ctx];
    if (!cm->frame_parallel_decoding_mode)
      ++eob_branch_count[band][ctx];
    if (!vp9_read(r, prob[EOB_CONTEXT_NODE])) {
      INCREMENT_COUNT(EOB_MODEL_TOKEN);
      break;
    }
#if CONFIG_NEW_QUANT
    dqv_val = &dq_val[band][0];
#endif  // CONFIG_NEW_QUANT

    while (!vp9_read(r, prob[ZERO_CONTEXT_NODE])) {
      INCREMENT_COUNT(ZERO_TOKEN);
      dqv = dq[1];
      token_cache[scan[c]] = 0;
      ++c;
      if (c >= max_eob)
        return c;  // zero tokens at the end (no eob token)
      ctx = get_coef_context(nb, token_cache, c);
      band = *band_translate++;
      prob = coef_probs[band][ctx];
#if CONFIG_NEW_QUANT
      dqv_val = &dq_val[band][0];
#endif  // CONFIG_NEW_QUANT
    }

    if (!vp9_read(r, prob[ONE_CONTEXT_NODE])) {
      INCREMENT_COUNT(ONE_TOKEN);
      token = ONE_TOKEN;
      val = 1;
    } else {
      INCREMENT_COUNT(TWO_TOKEN);
      token = vp9_read_tree(r, coeff_subtree_high,
                            vp9_pareto8_full[prob[PIVOT_NODE] - 1]);
      switch (token) {
        case TWO_TOKEN:
        case THREE_TOKEN:
        case FOUR_TOKEN:
          val = token;
          break;
        case CATEGORY1_TOKEN:
          val = CAT1_MIN_VAL + read_coeff(cat1_prob, 1, r);
          break;
        case CATEGORY2_TOKEN:
          val = CAT2_MIN_VAL + read_coeff(cat2_prob, 2, r);
          break;
        case CATEGORY3_TOKEN:
          val = CAT3_MIN_VAL + read_coeff(cat3_prob, 3, r);
          break;
        case CATEGORY4_TOKEN:
          val = CAT4_MIN_VAL + read_coeff(cat4_prob, 4, r);
          break;
        case CATEGORY5_TOKEN:
          val = CAT5_MIN_VAL + read_coeff(cat5_prob, 5, r);
          break;
        case CATEGORY6_TOKEN:
#if CONFIG_VP9_HIGHBITDEPTH
          switch (cm->bit_depth) {
            case VPX_BITS_8:
              val = CAT6_MIN_VAL + read_coeff(cat6_prob, NUM_CAT6_BITS, r);
              break;
            case VPX_BITS_10:
              val = CAT6_MIN_VAL +
                    read_coeff(cat6_prob, NUM_CAT6_BITS_HIGH10, r);
              break;
            case VPX_BITS_12:
              val = CAT6_MIN_VAL +
                    read_coeff(cat6_prob, NUM_CAT6_BITS_HIGH12, r);
              break;
            default:
              assert(0);
              return -1;
          }
#else
          val = CAT6_MIN_VAL + read_coeff(cat6_prob, NUM_CAT6_BITS, r);
#endif
          break;
      }
    }
#if CONFIG_NEW_QUANT
#if CONFIG_TX_SKIP
    if (use_rect_quant) {
      v = (val * dqv) >> dq_shift;
    } else {
      v = vp9_dequant_abscoeff_nuq(val, dqv, dqv_val);
      v = dq_shift ? ROUND_POWER_OF_TWO(v, dq_shift) : v;
    }
#else
    v = vp9_dequant_abscoeff_nuq(val, dqv, dqv_val);
    v = dq_shift ? ROUND_POWER_OF_TWO(v, dq_shift) : v;
#endif  // CONFIG_TX_SKIP
#else   // CONFIG_NEW_QUANT
    v = (val * dqv) >> dq_shift;
#endif  // CONFIG_NEW_QUANT

#if CONFIG_COEFFICIENT_RANGE_CHECKING
    dqcoeff[scan[c]] = check_range(vp9_read_bit(r) ? -v : v);
#else
    dqcoeff[scan[c]] = vp9_read_bit(r) ? -v : v;
#endif
    token_cache[scan[c]] = vp9_pt_energy_class[token];
    ++c;
    ctx = get_coef_context(nb, token_cache, c);
    dqv = dq[1];
  }

  return c;
}

#if CONFIG_TX_SKIP
static int decode_coefs_pxd(VP9_COMMON *cm, const MACROBLOCKD *xd,
                            PLANE_TYPE type, tran_low_t *dqcoeff,
                            TX_SIZE tx_size, const int16_t *dq,
#if CONFIG_NEW_QUANT
                            const dequant_val_type_nuq *dq_val,
#endif  // CONFIG_NEW_QUANT
                            int ctx, const int16_t *scan, const int16_t *nb,
                            vp9_reader *r) {
  const int max_eob = 16 << (tx_size << 1);
  const FRAME_CONTEXT *const fc = &cm->fc;
  FRAME_COUNTS *const counts = &cm->counts;
  const int ref = is_inter_block(&xd->mi[0].src_mi->mbmi);
  int c = 0;
  const vp9_prob *prob;
  const vp9_prob (*coef_probs_pxd)[ENTROPY_TOKENS - 1] =
      fc->coef_probs_pxd[tx_size][type][ref];
  unsigned int (*coef_counts_pxd)[ENTROPY_TOKENS] =
      counts->coef_pxd[tx_size][type][ref];
  unsigned int *eob_branch_count_pxd =
      counts->eob_branch_pxd[tx_size][type][ref];

  uint8_t token_cache[MAX_NUM_COEFS];
  const int dq_shift = (tx_size > TX_16X16) ? tx_size - TX_16X16 : 0;
  int v, token;
  int16_t dqv = dq[0];
#if CONFIG_NEW_QUANT
#if CONFIG_TX_SKIP
  const int use_rect_quant = is_rect_quant_used(&xd->mi[0].src_mi->mbmi, type);
#endif
  const tran_low_t *dqv_val = &dq_val[0][0];
#endif  // CONFIG_NEW_QUANT
  const uint8_t *cat1_prob;
  const uint8_t *cat2_prob;
  const uint8_t *cat3_prob;
  const uint8_t *cat4_prob;
  const uint8_t *cat5_prob;
  const uint8_t *cat6_prob;

#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth) {
    if (cm->bit_depth == VPX_BITS_10) {
      cat1_prob = vp9_cat1_prob_high10;
      cat2_prob = vp9_cat2_prob_high10;
      cat3_prob = vp9_cat3_prob_high10;
      cat4_prob = vp9_cat4_prob_high10;
      cat5_prob = vp9_cat5_prob_high10;
      cat6_prob = vp9_cat6_prob_high10;
    } else {
      cat1_prob = vp9_cat1_prob_high12;
      cat2_prob = vp9_cat2_prob_high12;
      cat3_prob = vp9_cat3_prob_high12;
      cat4_prob = vp9_cat4_prob_high12;
      cat5_prob = vp9_cat5_prob_high12;
      cat6_prob = vp9_cat6_prob_high12;
    }
  } else {
    cat1_prob = vp9_cat1_prob;
    cat2_prob = vp9_cat2_prob;
    cat3_prob = vp9_cat3_prob;
    cat4_prob = vp9_cat4_prob;
    cat5_prob = vp9_cat5_prob;
    cat6_prob = vp9_cat6_prob;
  }
#else
  cat1_prob = vp9_cat1_prob;
  cat2_prob = vp9_cat2_prob;
  cat3_prob = vp9_cat3_prob;
  cat4_prob = vp9_cat4_prob;
  cat5_prob = vp9_cat5_prob;
  cat6_prob = vp9_cat6_prob;
#endif

  while (c < max_eob) {
    int val = -1;

    prob = coef_probs_pxd[ctx];
    if (!cm->frame_parallel_decoding_mode)
      ++eob_branch_count_pxd[ctx];
    if (!vp9_read(r, prob[EOB_CONTEXT_NODE])) {
      if (!cm->frame_parallel_decoding_mode)
        ++coef_counts_pxd[ctx][EOB_TOKEN];
      break;
    }

#if CONFIG_NEW_QUANT
    dqv_val = &dq_val[TX_SKIP_COEFF_BAND][0];
#endif  // CONFIG_NEW_QUANT


    while (!vp9_read(r, prob[ZERO_CONTEXT_NODE])) {
      if (!cm->frame_parallel_decoding_mode)
        ++coef_counts_pxd[ctx][ZERO_TOKEN];
      dqv = dq[1];
      token_cache[scan[c]] = 0;
      ++c;
      if (c >= max_eob)
        return c;  // zero tokens at the end (no eob token)
      ctx = get_coef_context(nb, token_cache, c);
      prob = coef_probs_pxd[ctx];
#if CONFIG_NEW_QUANT
      dqv_val = &dq_val[TX_SKIP_COEFF_BAND][0];
#endif  // CONFIG_NEW_QUANT
    }

    if (!vp9_read(r, prob[ONE_CONTEXT_NODE])) {
      if (!cm->frame_parallel_decoding_mode)
        ++coef_counts_pxd[ctx][ONE_TOKEN];
      token = ONE_TOKEN;
      val = 1;
    } else {
      token = vp9_read_tree(r, coeff_subtree_high, &prob[PIVOT_NODE + 1]);

      if (!cm->frame_parallel_decoding_mode)
        ++coef_counts_pxd[ctx][token];
      switch (token) {
        case TWO_TOKEN:
        case THREE_TOKEN:
        case FOUR_TOKEN:
          val = token;
          break;
        case CATEGORY1_TOKEN:
          val = CAT1_MIN_VAL + read_coeff(cat1_prob, 1, r);
          break;
        case CATEGORY2_TOKEN:
          val = CAT2_MIN_VAL + read_coeff(cat2_prob, 2, r);
          break;
        case CATEGORY3_TOKEN:
          val = CAT3_MIN_VAL + read_coeff(cat3_prob, 3, r);
          break;
        case CATEGORY4_TOKEN:
          val = CAT4_MIN_VAL + read_coeff(cat4_prob, 4, r);
          break;
        case CATEGORY5_TOKEN:
          val = CAT5_MIN_VAL + read_coeff(cat5_prob, 5, r);
          break;
        case CATEGORY6_TOKEN:
#if CONFIG_VP9_HIGHBITDEPTH
          switch (cm->bit_depth) {
            case VPX_BITS_8:
              val = CAT6_MIN_VAL + read_coeff(cat6_prob, NUM_CAT6_BITS, r);
              break;
            case VPX_BITS_10:
              val = CAT6_MIN_VAL +
              read_coeff(cat6_prob, NUM_CAT6_BITS_HIGH10, r);
              break;
            case VPX_BITS_12:
              val = CAT6_MIN_VAL +
              read_coeff(cat6_prob, NUM_CAT6_BITS_HIGH12, r);
              break;
            default:
              assert(0);
              return -1;
          }
#else
          val = CAT6_MIN_VAL + read_coeff(cat6_prob, NUM_CAT6_BITS, r);
#endif
          break;
      }
    }

#if CONFIG_NEW_QUANT
#if CONFIG_TX_SKIP
    if (use_rect_quant) {
      v = (val * dqv) >> dq_shift;
    } else {
      v = vp9_dequant_abscoeff_nuq(val, dqv, dqv_val);
      v = dq_shift ? ROUND_POWER_OF_TWO(v, dq_shift) : v;
    }
#else
    v = vp9_dequant_abscoeff_nuq(val, dqv, dqv_val);
    v = dq_shift ? ROUND_POWER_OF_TWO(v, dq_shift) : v;
#endif  // CONFIG_TX_SKIP
#else   // CONFIG_NEW_QUANT
    v = (val * dqv) >> dq_shift;
#endif  // CONFIG_NEW_QUANT

#if CONFIG_COEFFICIENT_RANGE_CHECKING
    dqcoeff[scan[c]] = check_range(vp9_read_bit(r) ? -v : v);
#else
    dqcoeff[scan[c]] = vp9_read_bit(r) ? -v : v;
#endif
    token_cache[scan[c]] = vp9_pt_energy_class[token];
    ++c;
    ctx = get_coef_context(nb, token_cache, c);
    dqv = dq[1];
  }

  return c;
}
#endif  // CONFIG_TX_SKIP

int vp9_decode_block_tokens(VP9_COMMON *cm, MACROBLOCKD *xd,
                            int plane, int block, BLOCK_SIZE plane_bsize,
                            int x, int y, TX_SIZE tx_size, vp9_reader *r) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ctx = get_entropy_context(tx_size, pd->above_context + x,
                                               pd->left_context + y);
  const scan_order *so = get_scan(xd, tx_size, pd->plane_type, block);
  int eob;

#if CONFIG_TX_SKIP
  if (xd->mi->src_mi->mbmi.tx_skip[plane != 0] && FOR_SCREEN_CONTENT)
    eob = decode_coefs_pxd(cm, xd, pd->plane_type,
                           BLOCK_OFFSET(pd->dqcoeff, block), tx_size,
                           pd->dequant_pxd,
#if CONFIG_NEW_QUANT
                           pd->dequant_val_nuq_pxd,
#endif  // CONFIG_NEW_QUANT
                           ctx, so->scan,
                           so->neighbors, r);
  else
#endif  // CONFIG_TX_SKIP
    eob = decode_coefs(cm, xd, pd->plane_type,
                       BLOCK_OFFSET(pd->dqcoeff, block), tx_size,
#if CONFIG_TX_SKIP
                       xd->mi->src_mi->mbmi.tx_skip[plane != 0] ?
                           pd->dequant_pxd : pd->dequant,
#else
                       pd->dequant,
#endif  // CONFIG_TX_SKIP
#if CONFIG_NEW_QUANT
#if CONFIG_TX_SKIP
                       xd->mi->src_mi->mbmi.tx_skip[plane != 0] ?
                           pd->dequant_val_nuq_pxd : pd->dequant_val_nuq,
#else
                       pd->dequant_val_nuq,
#endif  // CONFIG_TX_SKIP
#endif  // CONFIG_NEW_QUANT
                       ctx, so->scan,
                       so->neighbors, r);

#if CONFIG_TX64X64
  if (plane > 0) assert(tx_size != TX_64X64);
#endif  // CONFIG_TX64X64
  vp9_set_contexts(xd, pd, plane_bsize, tx_size, eob > 0, x, y);
  return eob;
}


