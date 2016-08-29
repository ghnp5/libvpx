/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP10_COMMON_ALLOCCOMMON_H_
#define VP10_COMMON_ALLOCCOMMON_H_

#define INVALID_IDX -1  // Invalid buffer index.

#ifdef __cplusplus
extern "C" {
#endif

struct VP10Common;
struct BufferPool;

void av1_remove_common(struct VP10Common *cm);

int av1_alloc_context_buffers(struct VP10Common *cm, int width, int height);
void av1_init_context_buffers(struct VP10Common *cm);
void av1_free_context_buffers(struct VP10Common *cm);

void av1_free_ref_frame_buffers(struct BufferPool *pool);
#if CONFIG_LOOP_RESTORATION
void av1_free_restoration_buffers(struct VP10Common *cm);
#endif  // CONFIG_LOOP_RESTORATION

int av1_alloc_state_buffers(struct VP10Common *cm, int width, int height);
void av1_free_state_buffers(struct VP10Common *cm);

void av1_set_mb_mi(struct VP10Common *cm, int width, int height);

void av1_swap_current_and_last_seg_map(struct VP10Common *cm);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP10_COMMON_ALLOCCOMMON_H_
