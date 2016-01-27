/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP10_COMMON_LOOPFILTER_H_
#define VP10_COMMON_LOOPFILTER_H_

#include "vpx_ports/mem.h"
#include "./vpx_config.h"

#include "vp10/common/blockd.h"
#include "vp10/common/seg_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LOOP_FILTER 63
#define MAX_SHARPNESS 7

#define SIMD_WIDTH 16

#define MAX_MODE_LF_DELTAS      2

#if CONFIG_LOOP_RESTORATION
#define RESTORATION_LEVEL_BITS_KF 4
#define RESTORATION_LEVELS_KF     (1 << RESTORATION_LEVEL_BITS_KF)
#define RESTORATION_LEVEL_BITS    3
#define RESTORATION_LEVELS        (1 << RESTORATION_LEVEL_BITS)
#define DEF_RESTORATION_LEVEL     2

#define RESTORATION_PRECISION     16
#define RESTORATION_HALFWIN       3
#define RESTORATION_WIN           (2 * RESTORATION_HALFWIN + 1)

typedef struct restoration_params {
  int sigma_x;  // spatial variance x
  int sigma_y;  // spatial variance y
  int sigma_r;  // range variance
} restoration_params_t;

static restoration_params_t
    restoration_level_to_params_arr[RESTORATION_LEVELS + 1] = {
  // Values are rounded to 1/16 th precision
  {0, 0, 0},    // 0 - default
  {8, 9, 30},
  {9, 8, 30},
  {9, 11, 32},
  {11, 9, 32},
  {14, 14, 32},
  {18, 18, 36},
  {24, 24, 40},
  {32, 32, 40},
};

static restoration_params_t
    restoration_level_to_params_arr_kf[RESTORATION_LEVELS_KF + 1] = {
  // Values are rounded to 1/16 th precision
  {0, 0, 0},    // 0 - default
  {8, 8, 30},
  {9, 9, 32},
  {10, 10, 32},
  {12, 12, 32},
  {14, 14, 32},
  {18, 18, 36},
  {24, 24, 40},
  {30, 30, 44},
  {36, 36, 48},
  {42, 42, 48},
  {48, 48, 48},
  {48, 48, 56},
  {56, 56, 48},
  {56, 56, 56},
  {56, 56, 64},
  {64, 64, 48},
};

int vp10_restoration_level_bits(const struct VP10Common *const cm);
int vp10_loop_restoration_used(int level, int kf);

static INLINE restoration_params_t vp10_restoration_level_to_params(
    int index, int kf) {
  return kf ? restoration_level_to_params_arr_kf[index] :
              restoration_level_to_params_arr[index];
}
#endif  // CONFIG_LOOP_RESTORATION

enum lf_path {
  LF_PATH_420,
  LF_PATH_444,
  LF_PATH_SLOW,
};

struct loopfilter {
  int filter_level;

  int sharpness_level;
  int last_sharpness_level;

  uint8_t mode_ref_delta_enabled;
  uint8_t mode_ref_delta_update;

  // 0 = Intra, Last, Last2+Last3+LAST4(CONFIG_EXT_REFS),
  // GF, ARF
  signed char ref_deltas[MAX_REF_FRAMES];
  signed char last_ref_deltas[MAX_REF_FRAMES];

  // 0 = ZERO_MV, MV
  signed char mode_deltas[MAX_MODE_LF_DELTAS];
  signed char last_mode_deltas[MAX_MODE_LF_DELTAS];

#if CONFIG_LOOP_RESTORATION
  int restoration_level;
  int last_restoration_level;
#endif  // CONFIG_LOOP_RESTORATION
};

// Need to align this structure so when it is declared and
// passed it can be loaded into vector registers.
typedef struct {
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, mblim[SIMD_WIDTH]);
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, lim[SIMD_WIDTH]);
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, hev_thr[SIMD_WIDTH]);
} loop_filter_thresh;

typedef struct {
  loop_filter_thresh lfthr[MAX_LOOP_FILTER + 1];
  uint8_t lvl[MAX_SEGMENTS][MAX_REF_FRAMES][MAX_MODE_LF_DELTAS];
#if CONFIG_LOOP_RESTORATION
  double *wx_lut[RESTORATION_WIN];
  double *wr_lut;
  int restoration_sigma_x_set;
  int restoration_sigma_y_set;
  int restoration_sigma_r_set;
  int restoration_used;
#endif  // CONFIG_LOOP_RESTORATION
} loop_filter_info_n;

// This structure holds bit masks for all 8x8 blocks in a 64x64 region.
// Each 1 bit represents a position in which we want to apply the loop filter.
// Left_ entries refer to whether we apply a filter on the border to the
// left of the block.   Above_ entries refer to whether or not to apply a
// filter on the above border.   Int_ entries refer to whether or not to
// apply borders on the 4x4 edges within the 8x8 block that each bit
// represents.
// Since each transform is accompanied by a potentially different type of
// loop filter there is a different entry in the array for each transform size.
typedef struct {
  uint64_t left_y[TX_SIZES];
  uint64_t above_y[TX_SIZES];
  uint64_t int_4x4_y;
  uint16_t left_uv[TX_SIZES];
  uint16_t above_uv[TX_SIZES];
  uint16_t left_int_4x4_uv;
  uint16_t above_int_4x4_uv;
  uint8_t lfl_y[64];
  uint8_t lfl_uv[16];
} LOOP_FILTER_MASK;

/* assorted loopfilter functions which get used elsewhere */
struct VP10Common;
struct macroblockd;
struct VP9LfSyncData;

// This function sets up the bit masks for the entire 64x64 region represented
// by mi_row, mi_col.
void vp10_setup_mask(struct VP10Common *const cm,
                    const int mi_row, const int mi_col,
                    MODE_INFO **mi_8x8, const int mode_info_stride,
                    LOOP_FILTER_MASK *lfm);

void vp10_filter_block_plane_ss00(struct VP10Common *const cm,
                                 struct macroblockd_plane *const plane,
                                 int mi_row,
                                 LOOP_FILTER_MASK *lfm);

void vp10_filter_block_plane_ss11(struct VP10Common *const cm,
                                 struct macroblockd_plane *const plane,
                                 int mi_row,
                                 LOOP_FILTER_MASK *lfm);

void vp10_filter_block_plane_non420(struct VP10Common *cm,
                                   struct macroblockd_plane *plane,
                                   MODE_INFO **mi_8x8,
                                   int mi_row, int mi_col);

void vp10_loop_filter_init(struct VP10Common *cm);

// Update the loop filter for the current frame.
// This should be called before vp10_loop_filter_rows(), vp10_loop_filter_frame()
// calls this function directly.
void vp10_loop_filter_frame_init(struct VP10Common *cm, int default_filt_lvl);

void vp10_loop_filter_frame(YV12_BUFFER_CONFIG *frame,
                            struct VP10Common *cm,
                            struct macroblockd *mbd,
                            int filter_level,
                            int y_only, int partial_frame);

// Apply the loop filter to [start, stop) macro block rows in frame_buffer.
void vp10_loop_filter_rows(YV12_BUFFER_CONFIG *frame_buffer,
                           struct VP10Common *cm,
                           struct macroblockd_plane planes[MAX_MB_PLANE],
                           int start, int stop, int y_only);

#if CONFIG_LOOP_RESTORATION
void vp10_loop_restoration_frame(YV12_BUFFER_CONFIG *frame,
                                 struct VP10Common *cm,
                                 int restoration_level,
                                 int y_only, int partial_frame);
void vp10_loop_filter_restoration_frame(YV12_BUFFER_CONFIG *frame,
                                        struct VP10Common *cm,
                                        struct macroblockd *mbd,
                                        int frame_filter_level,
                                        int restoration_level,
                                        int y_only, int partial_frame);
void vp10_loop_restoration_init(loop_filter_info_n *lfi, int T, int kf);
void vp10_loop_restoration_rows(YV12_BUFFER_CONFIG *frame,
                                struct VP10Common *cm,
                                int start_mi_row, int end_mi_row,
                                int y_only);
#endif  // CONFIG_LOOP_RESTORATION

typedef struct LoopFilterWorkerData {
  YV12_BUFFER_CONFIG *frame_buffer;
  struct VP10Common *cm;
  struct macroblockd_plane planes[MAX_MB_PLANE];

  int start;
  int stop;
  int y_only;
} LFWorkerData;

void vp10_loop_filter_data_reset(
    LFWorkerData *lf_data, YV12_BUFFER_CONFIG *frame_buffer,
    struct VP10Common *cm, const struct macroblockd_plane planes[MAX_MB_PLANE]);

// Operates on the rows described by 'lf_data'.
int vp10_loop_filter_worker(LFWorkerData *const lf_data, void *unused);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP10_COMMON_LOOPFILTER_H_
