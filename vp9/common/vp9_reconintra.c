/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>

#include "./vpx_config.h"
#include "vp9_rtcd.h"
#include "vp9/common/vp9_reconintra.h"
#include "vpx_mem/vpx_mem.h"

static void d27_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  // first column
  for (r = 0; r < bh - 1; ++r) {
      ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r] +
                                                   yleft_col[r + 1], 1);
  }
  ypred_ptr[(bh - 1) * y_stride] = yleft_col[bh-1];
  ypred_ptr++;
  // second column
  for (r = 0; r < bh - 2; ++r) {
      ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r] +
                                                   yleft_col[r + 1] * 2 +
                                                   yleft_col[r + 2], 2);
  }
  ypred_ptr[(bh - 2) * y_stride] = ROUND_POWER_OF_TWO(yleft_col[bh - 2] +
                                                      yleft_col[bh - 1] * 3,
                                                      2);
  ypred_ptr[(bh - 1) * y_stride] = yleft_col[bh-1];
  ypred_ptr++;

  // rest of last row
  for (c = 0; c < bw - 2; ++c) {
    ypred_ptr[(bh - 1) * y_stride + c] = yleft_col[bh-1];
  }

  for (r = bh - 2; r >= 0; --r) {
    for (c = 0; c < bw - 2; ++c) {
      ypred_ptr[r * y_stride + c] = ypred_ptr[(r + 1) * y_stride + c - 2];
    }
  }
}

static void d63_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  for (r = 0; r < bh; ++r) {
    for (c = 0; c < bw; ++c) {
      if (r & 1) {
        ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[r/2 + c] +
                                          yabove_row[r/2 + c + 1] * 2 +
                                          yabove_row[r/2 + c + 2], 2);
      } else {
        ypred_ptr[c] =ROUND_POWER_OF_TWO(yabove_row[r/2 + c] +
                                         yabove_row[r/2+ c + 1], 1);
      }
    }
    ypred_ptr += y_stride;
  }
}

static void d45_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  for (r = 0; r < bh; ++r) {
    for (c = 0; c < bw; ++c) {
      if (r + c + 2 < bw * 2)
        ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[r + c] +
                                          yabove_row[r + c + 1] * 2 +
                                          yabove_row[r + c + 2], 2);
      else
        ypred_ptr[c] = yabove_row[bw * 2 - 1];
    }
    ypred_ptr += y_stride;
  }
}

static void d117_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  // first row
  for (c = 0; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 1] + yabove_row[c], 1);
  ypred_ptr += y_stride;

  // second row
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yleft_col[0] +
                                    yabove_row[-1] * 2 +
                                    yabove_row[0], 2);
  for (c = 1; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 2] +
                                      yabove_row[c - 1] * 2 +
                                      yabove_row[c], 2);
  ypred_ptr += y_stride;

  // the rest of first col
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yabove_row[-1] +
                                    yleft_col[0] * 2 +
                                    yleft_col[1], 2);
  for (r = 3; r < bh; ++r)
    ypred_ptr[(r-2) * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r - 3] +
                                                     yleft_col[r - 2] * 2 +
                                                     yleft_col[r - 1], 2);
  // the rest of the block
  for (r = 2; r < bh; ++r) {
    for (c = 1; c < bw; c++)
      ypred_ptr[c] = ypred_ptr[-2 * y_stride + c - 1];
    ypred_ptr += y_stride;
  }
}


static void d135_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yleft_col[0] +
                                    yabove_row[-1] * 2 +
                                    yabove_row[0], 2);
  for (c = 1; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 2] +
                                      yabove_row[c - 1] * 2 +
                                      yabove_row[c], 2);

  ypred_ptr[y_stride] = ROUND_POWER_OF_TWO(yabove_row[-1] +
                                           yleft_col[0] * 2 +
                                           yleft_col[1], 2);
  for (r = 2; r < bh - 1; ++r)
    ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r - 2] +
                                                 yleft_col[r - 1] * 2 +
                                                 yleft_col[r + 1], 2);

  ypred_ptr[(bh - 1) * y_stride] = ROUND_POWER_OF_TWO(yleft_col[bh - 2] +
                                                      yleft_col[bh - 1] * 3,
                                                      2);

  ypred_ptr += y_stride;
  for (r = 1; r < bh; ++r) {
    for (c = 1; c < bw; c++)
      ypred_ptr[c] = ypred_ptr[-y_stride + c - 1];
    ypred_ptr += y_stride;
  }
}

static void d153_predictor(uint8_t *ypred_ptr,
                           int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row,
                           uint8_t *yleft_col) {
  int r, c;
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yabove_row[-1] + yleft_col[0], 1);
  for (r = 1; r < bh; r++)
    ypred_ptr[r * y_stride] =
        ROUND_POWER_OF_TWO(yleft_col[r - 1] + yleft_col[r], 1);
  ypred_ptr++;

  ypred_ptr[0] = ROUND_POWER_OF_TWO(yleft_col[0] +
                                    yabove_row[-1] * 2 +
                                    yabove_row[0], 2);
  ypred_ptr[y_stride] = ROUND_POWER_OF_TWO(yabove_row[-1] +
                                           yleft_col[0] * 2 +
                                           yleft_col[1], 2);
  for (r = 2; r < bh; r++)
    ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r - 2] +
                                                 yleft_col[r - 1] * 2 +
                                                 yleft_col[r], 2);
  ypred_ptr++;

  for (c = 0; c < bw - 2; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 1] +
                                      yabove_row[c] * 2 +
                                      yabove_row[c + 1], 2);
  ypred_ptr += y_stride;
  for (r = 1; r < bh; ++r) {
    for (c = 0; c < bw - 2; c++)
      ypred_ptr[c] = ypred_ptr[-y_stride + c - 2];
    ypred_ptr += y_stride;
  }
}

void vp9_build_intra_predictors(uint8_t *src, int src_stride,
                                uint8_t *ypred_ptr,
                                int y_stride, int mode,
                                int bw, int bh,
                                int up_available, int left_available,
                                int right_available) {
  int r, c, i;
  uint8_t yleft_col[64], yabove_data[129], ytop_left;
  uint8_t *yabove_row = yabove_data + 1;

  // 127 127 127 .. 127 127 127 127 127 127
  // 129  A   B  ..  Y   Z
  // 129  C   D  ..  W   X
  // 129  E   F  ..  U   V
  // 129  G   H  ..  S   T   T   T   T   T
  // ..

  if (left_available) {
    for (i = 0; i < bh; i++)
      yleft_col[i] = src[i * src_stride - 1];
  } else {
    vpx_memset(yleft_col, 129, bh);
  }

  if (up_available) {
    uint8_t *yabove_ptr = src - src_stride;
    vpx_memcpy(yabove_row, yabove_ptr, bw);
    if (bw == 4 && right_available)
      vpx_memcpy(yabove_row + bw, yabove_ptr + bw, bw);
    else
      vpx_memset(yabove_row + bw, yabove_row[bw -1], bw);
    ytop_left = left_available ? yabove_ptr[-1] : 129;
  } else {
    vpx_memset(yabove_row, 127, bw * 2);
    ytop_left = 127;
  }
  yabove_row[-1] = ytop_left;

  switch (mode) {
    case DC_PRED: {
      int i;
      int expected_dc = 128;
      int average = 0;
      int count = 0;

      if (up_available || left_available) {
        if (up_available) {
          for (i = 0; i < bw; i++)
            average += yabove_row[i];
          count += bw;
        }
        if (left_available) {
          for (i = 0; i < bh; i++)
            average += yleft_col[i];
          count += bh;
        }
        expected_dc = (average + (count >> 1)) / count;
      }
      for (r = 0; r < bh; r++) {
        vpx_memset(ypred_ptr, expected_dc, bw);
        ypred_ptr += y_stride;
      }
    }
    break;
    case V_PRED:
      for (r = 0; r < bh; r++) {
        vpx_memcpy(ypred_ptr, yabove_row, bw);
        ypred_ptr += y_stride;
      }
      break;
    case H_PRED:
      for (r = 0; r < bh; r++) {
        vpx_memset(ypred_ptr, yleft_col[r], bw);
        ypred_ptr += y_stride;
      }
      break;
    case TM_PRED:
      for (r = 0; r < bh; r++) {
        for (c = 0; c < bw; c++)
          ypred_ptr[c] = clip_pixel(yleft_col[r] + yabove_row[c] - ytop_left);
        ypred_ptr += y_stride;
      }
      break;
    case D45_PRED:
    case D135_PRED:
    case D117_PRED:
    case D153_PRED:
    case D27_PRED:
    case D63_PRED:
      if (bw == bh) {
        switch (mode) {
          case D45_PRED:
            d45_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
      } else if (bw > bh) {
        uint8_t pred[64*64];
        vpx_memset(yleft_col + bh, yleft_col[bh - 1], bw - bh);
        switch (mode) {
          case D45_PRED:
            d45_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
        for (i = 0; i < bh; i++)
          vpx_memcpy(ypred_ptr + y_stride * i, pred + i * 64, bw);
      } else {
        uint8_t pred[64 * 64];
        vpx_memset(yabove_row + bw * 2, yabove_row[bw * 2 - 1], (bh - bw) * 2);
        switch (mode) {
          case D45_PRED:
            d45_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
        for (i = 0; i < bh; i++)
          vpx_memcpy(ypred_ptr + y_stride * i, pred + i * 64, bw);
      }
      break;
    default:
      break;
  }
}

void vp9_build_intra_predictors_sby_s(MACROBLOCKD *xd,
                                      BLOCK_SIZE_TYPE bsize) {
  const int bwl = b_width_log2(bsize),  bw = 4 << bwl;
  const int bhl = b_height_log2(bsize), bh = 4 << bhl;

  vp9_build_intra_predictors(xd->plane[0].dst.buf, xd->plane[0].dst.stride,
                             xd->plane[0].dst.buf, xd->plane[0].dst.stride,
                             xd->mode_info_context->mbmi.mode,
                             bw, bh,
                             xd->up_available, xd->left_available,
                             0 /*xd->right_available*/);
}

void vp9_build_intra_predictors_sbuv_s(MACROBLOCKD *xd,
                                       BLOCK_SIZE_TYPE bsize) {
  int p;

  for (p = 1; p < MAX_MB_PLANE; p++) {
    const struct macroblockd_plane* const pd = &xd->plane[p];
    const int bwl = b_width_log2(bsize) - pd->subsampling_x;
    const int bw = 4 << bwl;
    const int bhl = b_height_log2(bsize) - pd->subsampling_y;
    const int bh = 4 << bhl;

    vp9_build_intra_predictors(pd->dst.buf, pd->dst.stride,
                               pd->dst.buf, pd->dst.stride,
                               xd->mode_info_context->mbmi.uv_mode,
                               bw, bh, xd->up_available,
                               xd->left_available, 0 /*xd->right_available*/);
  }
}

void vp9_intra4x4_predict(MACROBLOCKD *xd,
                          int block_idx,
                          BLOCK_SIZE_TYPE bsize,
                          int mode,
                          uint8_t *predictor, int pre_stride) {
  const int bwl = b_width_log2(bsize);
  const int wmask = (1 << bwl) - 1;
  const int have_top =
      (block_idx >> bwl) || xd->up_available;
  const int have_left =
      (block_idx & wmask) || xd->left_available;
  const int have_right = ((block_idx & wmask) != wmask);

  vp9_build_intra_predictors(predictor, pre_stride,
                             predictor, pre_stride,
                             mode, 4, 4, have_top, have_left,
                             have_right);
}

#if CONFIG_FILTERINTRA
void vp9_filter_intra4x4_predict(MACROBLOCKD *xd,
                          int block_idx,
                          BLOCK_SIZE_TYPE bsize,
                          int mode,
                          uint8_t *predictor, int pre_stride) {
  const int bwl = b_width_log2(bsize);
  const int wmask = (1 << bwl) - 1;
  const int have_top =
	  (block_idx >> bwl) || xd->up_available;
  const int have_left =
	  (block_idx & wmask) || xd->left_available;
  const int have_right = ((block_idx & wmask) != wmask);

  switch (mode) {
	  case B_DC_PRED  :
	  case B_D45_PRED :
	  case B_D63_PRED :
	  case B_D27_PRED :
		  vp9_build_intra_predictors(predictor, pre_stride,
									 predictor, pre_stride,
									 mode, 4, 4, have_top, have_left,
									 have_right);
		  break;
	  default: {
		  int r, c, i;
		  uint8_t yleft_col[4], yabove_data[9], ytop_left;
		  uint8_t *yabove_row = yabove_data + 1;
		  uint8_t *yabove_ptr;

		  if (have_top) {
		    yabove_ptr = predictor - pre_stride;
		    vpx_memcpy(yabove_row, yabove_ptr, 4);
		    if (have_right)
		      vpx_memcpy(yabove_row + 4, yabove_ptr + 4, 4);
		    else
		      vpx_memset(yabove_row + 4, yabove_row[3], 4);
		    ytop_left = have_left ? yabove_ptr[-1] : 129;
		  } else {
		    vpx_memset(yabove_row, 127, 8);
		    ytop_left = 127;
		  }
		  yabove_row[-1] = ytop_left;

		  if (have_left) {
		    for (i = 0; i < 4; i++)
		      yleft_col[i] = predictor[i * pre_stride - 1];
		  } else {
		    vpx_memset(yleft_col, 129, 4);
		  }

		  static const int prec_bits = 10;
		  static const int round_val = 511;

		  static const int taps[10][3] = {
		    {0, 0, 0},			// DC
		    {1014, 565, -559},	// V
		    {312, 1017, -312},  // H
		    {0, 0, 0},  		// D45
		    {478, 483, 153},	// D135
		    {699, 470, -122},   // D117
		    {356, 707, 35},   	// D153
		    {0, 0, 0},			// D27
		    {0, 0, 0},    		// D63
		    {877, 896, -812},   // TM
		  };
		  int predictor5[5][5];
		  int mean, ipred;
		  const int c1 = taps[mode][0];
		  const int c2 = taps[mode][1];
		  const int c3 = taps[mode][2];

		  switch (mode) {
			case B_V_PRED :
			  mean = ((int)yabove_row[0] + (int)yabove_row[1] +
			          (int)yabove_row[2] + (int)yabove_row[3] + 2) >> 2;
			  break;
			case B_H_PRED :
			  mean = ((int)yleft_col[0] + (int)yleft_col[1] +
					  (int)yleft_col[2] + (int)yleft_col[3] + 2) >> 2;
			  break;
			default :
			  mean = ((int)yabove_row[0] + (int)yabove_row[1] +
					  (int)yabove_row[2] + (int)yabove_row[3] +
					  (int)yleft_col[0] + (int)yleft_col[1] +
					  (int)yleft_col[2] + (int)yleft_col[3] + 4) >> 3;
			  break;
		  }

		  for ( c = 0; c < 5; c++)
			  predictor5[0][c] = (int)yabove_row[c-1]-mean;
		  for ( r = 1; r < 5; r++)
			  predictor5[r][0] = (int)yleft_col[r-1]-mean;

		  for (r = 1; r < 5; r++)
			  for (c = 1; c < 5; c++) {
				  ipred = c1 * predictor5[r-1][c] +
						  c2 * predictor5[r][c-1] + c3 * predictor5[r-1][c-1];
				  predictor5[r][c] = ipred < 0 ?
	                         -((-ipred + round_val) >> prec_bits) :
	                         ((ipred + round_val) >> prec_bits);
			  }

		  for (r = 0; r < 4; r++) {
			  for (c = 0; c < 4; c++) {
				     ipred = predictor5[r + 1][c + 1] + mean;
				     predictor[c] = clip_pixel(ipred);
			  }
			  predictor += pre_stride;
		  }
	  }
	  break;
  }
}
#endif
