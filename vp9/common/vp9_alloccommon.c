/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpx_config.h"
#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/common/vp9_systemdependent.h"

// TODO(hkuang): Don't need to lock the whole pool after implementing atomic
// frame reference count.
void lock_buffer_pool(BufferPool *const pool) {
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&pool->pool_mutex);
#else
  (void)pool;
#endif
}

void unlock_buffer_pool(BufferPool *const pool) {
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(&pool->pool_mutex);
#else
  (void)pool;
#endif
}

void vp9_set_mb_mi(VP9_COMMON *cm, int width, int height) {
  const int aligned_width = ALIGN_POWER_OF_TWO(width, MI_SIZE_LOG2);
  const int aligned_height = ALIGN_POWER_OF_TWO(height, MI_SIZE_LOG2);

  cm->mi_cols = aligned_width >> MI_SIZE_LOG2;
  cm->mi_rows = aligned_height >> MI_SIZE_LOG2;
  cm->mi_stride = calc_mi_size(cm->mi_cols);

  cm->mb_cols = (cm->mi_cols + 1) >> 1;
  cm->mb_rows = (cm->mi_rows + 1) >> 1;
  cm->MBs = cm->mb_rows * cm->mb_cols;
}

static int alloc_seg_map(VP9_COMMON *cm, int seg_map_size) {
  int i;

  for (i = 0; i < NUM_PING_PONG_BUFFERS; ++i) {
    cm->seg_map_array[i] = (uint8_t *)vpx_calloc(seg_map_size, 1);
    if (cm->seg_map_array[i] == NULL)
      return 1;
  }

  // Init the index.
  cm->seg_map_idx = 0;
  cm->prev_seg_map_idx = 1;

  cm->current_frame_seg_map = cm->seg_map_array[cm->seg_map_idx];
  if (!cm->frame_parallel_decode)
    cm->last_frame_seg_map = cm->seg_map_array[cm->prev_seg_map_idx];

  return 0;
}

static void free_seg_map(VP9_COMMON *cm) {
  int i;

  for (i = 0; i < NUM_PING_PONG_BUFFERS; ++i) {
    vpx_free(cm->seg_map_array[i]);
    cm->seg_map_array[i] = NULL;
  }

  cm->current_frame_seg_map = NULL;

  if (!cm->frame_parallel_decode) {
    cm->last_frame_seg_map = NULL;
  }
}

void vp9_free_ref_frame_buffers(BufferPool *pool) {
  int i;

  for (i = 0; i < FRAME_BUFFERS; ++i) {
    if (pool->frame_bufs[i].ref_count > 0 &&
        pool->frame_bufs[i].raw_frame_buffer.data != NULL) {
      pool->release_fb_cb(pool->cb_priv, &pool->frame_bufs[i].raw_frame_buffer);
      pool->frame_bufs[i].ref_count = 0;
    }
    vpx_free(pool->frame_bufs[i].mvs);
    pool->frame_bufs[i].mvs = NULL;
    vp9_free_frame_buffer(&pool->frame_bufs[i].buf);
  }
}

void vp9_free_postproc_buffers(VP9_COMMON *cm) {
#if CONFIG_VP9_POSTPROC
  vp9_free_frame_buffer(&cm->post_proc_buffer);
  vp9_free_frame_buffer(&cm->post_proc_buffer_int);
#else
  (void)cm;
#endif
}

void vp9_free_context_buffers(VP9_COMMON *cm) {
  cm->free_mi(cm);
  free_seg_map(cm);
  vpx_free(cm->above_context);
  cm->above_context = NULL;
  vpx_free(cm->above_seg_context);
  cm->above_seg_context = NULL;
  vpx_free(cm->lf.lfm);
  cm->lf.lfm = NULL;
}

int vp9_alloc_context_buffers(VP9_COMMON *cm, int width, int height) {
  vp9_free_context_buffers(cm);

  vp9_set_mb_mi(cm, width, height);
  if (cm->alloc_mi(cm, cm->mi_stride * calc_mi_size(cm->mi_rows)))
    goto fail;

  // Create the segmentation map structure and set to 0.
  free_seg_map(cm);
  if (alloc_seg_map(cm, cm->mi_rows * cm->mi_cols))
    goto fail;

  cm->above_context = (ENTROPY_CONTEXT *)vpx_calloc(
      2 * mi_cols_aligned_to_sb(cm->mi_cols) * MAX_MB_PLANE,
      sizeof(*cm->above_context));
  if (!cm->above_context) goto fail;

  cm->above_seg_context = (PARTITION_CONTEXT *)vpx_calloc(
      mi_cols_aligned_to_sb(cm->mi_cols), sizeof(*cm->above_seg_context));
  if (!cm->above_seg_context) goto fail;

  cm->lf.lfm_stride = (cm->mi_cols + (MI_BLOCK_SIZE - 1)) >> 3;
  cm->lf.lfm = (LOOP_FILTER_MASK *)vpx_calloc(
      ((cm->mi_rows + (MI_BLOCK_SIZE - 1)) >> 3) * cm->lf.lfm_stride,
      sizeof(*cm->lf.lfm));
  if (!cm->lf.lfm) goto fail;

#if 0
  printf("cm->lf.lfm %x\n", cm->lf.lfm);
  printf("num sb cols %d\n", cm->lf.lfm_stride);
  printf("num sb rows %d\n", ((cm->mi_rows + (MI_BLOCK_SIZE - 1)) >> 3));
  printf("sizeof(*cm->lf.lfm) %d\n", sizeof(*cm->lf.lfm));
  printf("total %d\n", ((cm->mi_rows + (MI_BLOCK_SIZE - 1)) >> 3) *
      cm->lf.lfm_stride * sizeof(*cm->lf.lfm));
#endif

  return 0;

 fail:
  vp9_free_context_buffers(cm);
  return 1;
}

void vp9_remove_common(VP9_COMMON *cm) {
  vp9_free_context_buffers(cm);

  vpx_free(cm->fc);
  cm->fc = NULL;
  vpx_free(cm->frame_contexts);
  cm->frame_contexts = NULL;
}

void vp9_init_context_buffers(VP9_COMMON *cm) {
  cm->setup_mi(cm);
  if (cm->last_frame_seg_map && !cm->frame_parallel_decode)
    vpx_memset(cm->last_frame_seg_map, 0, cm->mi_rows * cm->mi_cols);
}

void vp9_swap_current_and_last_seg_map(VP9_COMMON *cm) {
  // Swap indices.
  const int tmp = cm->seg_map_idx;
  cm->seg_map_idx = cm->prev_seg_map_idx;
  cm->prev_seg_map_idx = tmp;

  cm->current_frame_seg_map = cm->seg_map_array[cm->seg_map_idx];
  cm->last_frame_seg_map = cm->seg_map_array[cm->prev_seg_map_idx];
}
