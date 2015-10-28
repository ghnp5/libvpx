/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <emmintrin.h>
#include <smmintrin.h>

#include "vpx_dsp/vpx_dsp_common.h"
#include "vp9/encoder/vp9_encoder.h"

#if !ARCH_X86 && !ARCH_X86_64
#  error "ARCH_X86 or ARCH_X86_64 is required"
#endif

#ifdef __GNUC__
# define __likely__(v)    __builtin_expect(v, 1)
# define __unlikely__(v)  __builtin_expect(v, 0)
#else
# define __likely__(v)    (v)
# define __unlikely__(v)  (v)
#endif

typedef union mvu {
  int32_t pair;
  MV      comp;
} MVU;

#define ROUND_POWER_OF_TWO(value, n) \
    (((value) + (1 << ((n) - 1))) >> (n))


// #define NEW_DIAMOND_SEARCH

static INLINE int is_mv_in(const MACROBLOCK *x, const MVU *mv) {
  return (mv->comp.col >= x->mv_col_min) && (mv->comp.col <= x->mv_col_max) &&
         (mv->comp.row >= x->mv_row_min) && (mv->comp.row <= x->mv_row_max);
}


static INLINE MV_JOINT_TYPE get_mv_joint(const MVU mv) {
  // This is simplified from the C implementation to utilise that
  //  x->nmvjointsadcost[1] == x->nmvjointsadcost[2]  and
  //  x->nmvjointsadcost[1] == x->nmvjointsadcost[3]
  return mv.pair == 0 ? 0 : 1;
}

static INLINE int mv_cost(const MVU mv,
                          const int *joint_cost, int *const comp_cost[2]) {
  return joint_cost[get_mv_joint(mv)] +
             comp_cost[0][mv.comp.row] + comp_cost[1][mv.comp.col];
}

static int mvsad_err_cost(const MACROBLOCK *x, const MVU mv, const MV *ref,
                          int error_per_bit) {
  const MVU diff = { .comp={  mv.comp.row - ref->row,
                              mv.comp.col - ref->col } };
  return ROUND_POWER_OF_TWO(mv_cost(diff, x->nmvjointsadcost,
                                    x->nmvsadcost) * error_per_bit, 8);
}


// This function utilises 3 properties of the cost function lookup table,
// constructed in 'cal_nmvjointsadcost' and 'cal_nmvsadcosts'.
// For the joint cost:
//   - mvjointsadcost[1] == mvjointsadcost[2] == mvjointsadcost[3]
// For the component costs:
//   - For all i: mvsadcost[0][i] == mvsadcost[1][i]
//         (Equal per component cost)
//   - For all i: mvsadcost[0][i] == mvsadcost[0][-i]
//         (Cost function is even)
// If these do not hold, then this function cannot be used without modification
int vp9_diamond_search_sad_avx(const MACROBLOCK *x,
                             const search_site_config *cfg,
                             MV *ref_mv, MV *best_mv, int search_param,
                             int sad_per_bit, int *num00,
                             const vp9_variance_fn_ptr_t *fn_ptr,
                             const MV *center_mv) {
  const MVU maxmv = { .comp={x->mv_row_max, x->mv_col_max} };
  const __m128i v_max_mv_w = _mm_set1_epi32(maxmv.pair);
  const MVU minmv = { .comp={x->mv_row_min, x->mv_col_min} };
  const __m128i v_min_mv_w = _mm_set1_epi32(minmv.pair);

  const __m128i v_spb_d = _mm_set1_epi32(sad_per_bit);

  const __m128i v_joint_cost_0_d = _mm_set1_epi32(x->nmvjointsadcost[0]);
  const __m128i v_joint_cost_1_d = _mm_set1_epi32(x->nmvjointsadcost[1]);

  // search_param determines the length of the initial step and hence the number
  // of iterations.
  // 0 = initial step (MAX_FIRST_STEP) pel
  // 1 = (MAX_FIRST_STEP/2) pel,
  // 2 = (MAX_FIRST_STEP/4) pel...
  const       MV *ss_mv = &cfg->ss_mv[cfg->searches_per_step * search_param];
  const intptr_t *ss_os = &cfg->ss_os[cfg->searches_per_step * search_param];
  const int tot_steps = cfg->total_steps - search_param;

  const MVU fcenter_mv = { .comp={center_mv->row >> 3, center_mv->col >> 3} };
  const __m128i vfcmv = _mm_set1_epi32(fcenter_mv.pair);

  const int ref_row = clamp(ref_mv->row, minmv.comp.row, maxmv.comp.row);
  const int ref_col = clamp(ref_mv->col, minmv.comp.col, maxmv.comp.col);

  MVU bmv = { .comp={ ref_row, ref_col} };
  MVU new_bmv = bmv;
  __m128i v_bmv_w = _mm_set1_epi32(bmv.pair);

  const     int         what_stride = x->plane[0].src.stride;
  const uint8_t * const what = x->plane[0].src.buf;
  const     int         in_what_stride = x->e_mbd.plane[0].pre[0].stride;
  const uint8_t * const in_what = x->e_mbd.plane[0].pre[0].buf +
                                  ref_row * in_what_stride + ref_col;

  // Work out the start point for the search
  const uint8_t *best_address = in_what;
  const uint8_t *new_best_address = best_address;
#if ARCH_X86_64
  __m128i v_ba_q = _mm_set1_epi64x((intptr_t)best_address);
#else
  __m128i v_ba_d = _mm_set1_epi32((intptr_t)best_address);
#endif

  unsigned int best_sad;
#ifdef NEW_DIAMOND_SEARCH
  int best_site = -1;
  int last_site = -1;
#endif

  int i;
  int j;
  int step;

  // The code is simplified to utilise this fact about the cost
  // table, so we assert it
  assert(x->nmvjointsadcost[1] == x->nmvjointsadcost[2]);
  assert(x->nmvjointsadcost[1] == x->nmvjointsadcost[3]);

  // Check the starting position
  best_sad = fn_ptr->sdf(what, what_stride, in_what, in_what_stride);
  best_sad += mvsad_err_cost(x, bmv, &fcenter_mv.comp, sad_per_bit);

  *num00 = 0;

  for (i = 0, step = 0; step < tot_steps; step++) {
    for (j = 0; j < cfg->searches_per_step; j += 4, i += 4) {
      __m128i v_sad_d;
      __m128i v_cost_d;
      __m128i v_outside_d;
      __m128i v_inside_d;
      __m128i v_diff_mv_w;
#if ARCH_X86_64
      __m128i v_blocka[2];
#else
      __m128i v_blocka[1];
#endif

      // Compute the candidate motion vectors
      const __m128i v_ss_mv_w = _mm_load_si128((const __m128i*)&ss_mv[i]);
      const __m128i v_these_mv_w = _mm_add_epi16(v_bmv_w, v_ss_mv_w);
      // Clamp them to the search bounds
      __m128i v_these_mv_clamp_w = v_these_mv_w;
      v_these_mv_clamp_w = _mm_min_epi16(v_these_mv_clamp_w, v_max_mv_w);
      v_these_mv_clamp_w = _mm_max_epi16(v_these_mv_clamp_w, v_min_mv_w);
      // The ones that did not change are inside the search area
      v_inside_d = _mm_cmpeq_epi32(v_these_mv_clamp_w, v_these_mv_w);

      // If none of them are inside, then move on
      if (__likely__(_mm_test_all_zeros(v_inside_d, v_inside_d))) {
        continue;
      }

      // The inverse mask indicates which of the MVs are outside
      v_outside_d = _mm_xor_si128(v_inside_d, _mm_set1_epi8(0xff));
      // Shift right to keep the sign bit clear, we will use this later
      // to set the cost to the maximum value.
      v_outside_d = _mm_srli_epi32(v_outside_d, 1);

      // Compute the difference MV
      v_diff_mv_w = _mm_sub_epi16(v_these_mv_clamp_w, vfcmv);
      // We utilise the fact that the cost function is even, and use the
      // absolute difference. This allows us to use unsigned indexes later
      // and reduces cache pressure somewhat as only a half of the table
      // is ever referenced.
      v_diff_mv_w = _mm_abs_epi16(v_diff_mv_w);

      // Compute the SIMD pointer offsets.
      {
#if ARCH_X86_64  //  sizeof(intptr_t) == 8
        // Load the offsets (could use _mm_maskload_ps here, instead of the
        // extra 'and' but it's slower that way)
        __m128i v_bo10_q = _mm_load_si128((const __m128i*)&ss_os[i+0]);
        __m128i v_bo32_q = _mm_load_si128((const __m128i*)&ss_os[i+2]);
        // Set the ones falling outside to zero
        v_bo10_q = _mm_and_si128(v_bo10_q,
                                _mm_cvtepi32_epi64(v_inside_d));
        v_bo32_q = _mm_and_si128(v_bo32_q,
                                _mm_unpackhi_epi32(v_inside_d, v_inside_d));
        // Compute the candidate addresses
        v_blocka[0] = _mm_add_epi64(v_ba_q, v_bo10_q);
        v_blocka[1] = _mm_add_epi64(v_ba_q, v_bo32_q);
#else  // ARCH_X86 //  sizeof(intptr_t) == 4
        __m128i v_bo_d = _mm_load_si128((const __m128i*)&ss_os[i]);
        v_bo_d = _mm_and_si128(v_bo_d, v_inside_d);
        v_blocka[0] = _mm_add_epi32(v_ba_d, v_bo_d);
#endif
      }

      fn_ptr->sdx4df(what, what_stride,
                    (const uint8_t **)&v_blocka[0], in_what_stride,
                    (uint32_t*)&v_sad_d);

      // Look up the component cost of the residual motion vector
      {
        const int32_t   row0 = _mm_extract_epi16(v_diff_mv_w, 0);
        const int32_t   col0 = _mm_extract_epi16(v_diff_mv_w, 1);
        const int32_t   row1 = _mm_extract_epi16(v_diff_mv_w, 2);
        const int32_t   col1 = _mm_extract_epi16(v_diff_mv_w, 3);
        const int32_t   row2 = _mm_extract_epi16(v_diff_mv_w, 4);
        const int32_t   col2 = _mm_extract_epi16(v_diff_mv_w, 5);
        const int32_t   row3 = _mm_extract_epi16(v_diff_mv_w, 6);
        const int32_t   col3 = _mm_extract_epi16(v_diff_mv_w, 7);

        // Note: This is a use case for vpgather in AVX2
        const uint32_t cost0 = x->nmvsadcost[0][row0] + x->nmvsadcost[0][col0];
        const uint32_t cost1 = x->nmvsadcost[0][row1] + x->nmvsadcost[0][col1];
        const uint32_t cost2 = x->nmvsadcost[0][row2] + x->nmvsadcost[0][col2];
        const uint32_t cost3 = x->nmvsadcost[0][row3] + x->nmvsadcost[0][col3];

        __m128i v_cost_10_d;
        __m128i v_cost_32_d;

        v_cost_10_d = _mm_cvtsi32_si128(cost0);
        v_cost_10_d = _mm_insert_epi32(v_cost_10_d, cost1, 1);

        v_cost_32_d = _mm_cvtsi32_si128(cost2);
        v_cost_32_d = _mm_insert_epi32(v_cost_32_d, cost3, 1);

        v_cost_d = _mm_unpacklo_epi64(v_cost_10_d, v_cost_32_d);
      }

      // Now add in the joint cost
      {
        const __m128i v_sel_d = _mm_cmpeq_epi32(v_diff_mv_w,
                                                _mm_setzero_si128());
        const __m128i v_joint_cost_d = _mm_blendv_epi8(v_joint_cost_1_d,
                                                       v_joint_cost_0_d,
                                                      v_sel_d);
        v_cost_d = _mm_add_epi32(v_cost_d, v_joint_cost_d);
      }

      // Multiply by sad_per_bit
      v_cost_d = _mm_mullo_epi32(v_cost_d, v_spb_d);
      // ROUND_POWER_OF_TWO(v_cost_d, 8)
      v_cost_d = _mm_add_epi32(v_cost_d, _mm_set1_epi32(0x80));
      v_cost_d = _mm_srai_epi32(v_cost_d, 8);
      // Add the cost to the sad
      v_sad_d = _mm_add_epi32(v_sad_d, v_cost_d);

      // Make the motion vectors outside the search area have max cost
      // by or'ing in the comparison mask, this way the minimum search won't
      // pick them.
      v_sad_d = _mm_or_si128(v_sad_d, v_outside_d);

      // Find the minimum value and index horizontally in v_sad_d
      {
        // Try speculatively on 16 bits, so we can use the minpos intrinsic
        const __m128i v_sad_w = _mm_packus_epi32(v_sad_d, v_sad_d);
        const __m128i v_minp_w = _mm_minpos_epu16(v_sad_w);

        uint32_t local_best_sad = _mm_extract_epi16(v_minp_w, 0);
        uint32_t local_best_idx = _mm_extract_epi16(v_minp_w, 1);

        // If the local best value is not saturated, just use it, otherwise
        // find the horizontal minimum again the hard way on 32 bits.
        // This is executed rarely.
        if (__unlikely__(local_best_sad == 0xffff)) {
          __m128i v_loval_d, v_hival_d, v_loidx_d, v_hiidx_d, v_sel_d;

          v_loval_d = v_sad_d;
          v_loidx_d = _mm_set_epi32(3, 2, 1, 0);
          v_hival_d = _mm_srli_si128(v_loval_d, 8);
          v_hiidx_d = _mm_srli_si128(v_loidx_d, 8);

          v_sel_d = _mm_cmplt_epi32(v_hival_d, v_loval_d);

          v_loval_d = _mm_blendv_epi8(v_loval_d, v_hival_d, v_sel_d);
          v_loidx_d = _mm_blendv_epi8(v_loidx_d, v_hiidx_d, v_sel_d);
          v_hival_d = _mm_srli_si128(v_loval_d, 4);
          v_hiidx_d = _mm_srli_si128(v_loidx_d, 4);

          v_sel_d = _mm_cmplt_epi32(v_hival_d, v_loval_d);

          v_loval_d = _mm_blendv_epi8(v_loval_d, v_hival_d, v_sel_d);
          v_loidx_d = _mm_blendv_epi8(v_loidx_d, v_hiidx_d, v_sel_d);

          local_best_sad = _mm_extract_epi32(v_loval_d, 0);
          local_best_idx = _mm_extract_epi32(v_loidx_d, 0);
        }

        // Update the global minimum if the local minimum is smaller
        if (__likely__(local_best_sad < best_sad)) {
          new_bmv = ((const MVU *)&v_these_mv_w)[local_best_idx];
          new_best_address  = ((const uint8_t **)v_blocka)[local_best_idx];

          best_sad = local_best_sad;

#ifdef NEW_DIAMOND_SEARCH
          best_site = i+local_best_idx;
#endif
        }
      }
    }

    bmv = new_bmv;
    best_address = new_best_address;

#ifdef NEW_DIAMOND_SEARCH
    if (__likely__(best_site != last_site)) {
      last_site = best_site;
      while (1) {
        const MVU this_mv = { .comp={ bmv.comp.row + ss_mv[best_site].row,
                               bmv.comp.col + ss_mv[best_site].col }};
        if (is_mv_in(x, &this_mv)) {
          const uint8_t *const check_here = ss_os[best_site] + best_address;
          unsigned int thissad = fn_ptr->sdf(what, what_stride, check_here,
                                        in_what_stride);
          if (thissad < best_sad) {
            thissad += mvsad_err_cost(x, this_mv,
                                      &fcenter_mv.comp, sad_per_bit);
            if (thissad < best_sad) {
              best_sad = thissad;
              bmv.comp.row += ss_mv[best_site].row;
              bmv.comp.col += ss_mv[best_site].col;
              best_address += ss_os[best_site];
              continue;
            }
          }
        }
        break;
      }
    }
#endif

    v_bmv_w = _mm_set1_epi32(bmv.pair);
#if ARCH_X86_64
    v_ba_q = _mm_set1_epi64x((intptr_t)best_address);
#else
    v_ba_d = _mm_set1_epi32((intptr_t)best_address);
#endif

    if (__unlikely__(best_address == in_what)) {
      (*num00)++;
    }
  }

  *best_mv = bmv.comp;
  return best_sad;
}
