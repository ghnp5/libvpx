/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_ENCODEMV_H
#define __INC_ENCODEMV_H

#include "onyx_int.h"

void vp8_write_mvprobs(VP8_COMP *);
void vp8_encode_motion_vector(vp8_writer *, const MV *, const MV_CONTEXT *);
void vp8_build_component_cost_table(int *mvcost[2], const MV_CONTEXT *mvc, int mvc_flag[2]);
#if CONFIG_HIGH_PRECISION_MV
void vp8_write_mvprobs_hp(VP8_COMP *);
void vp8_encode_motion_vector_hp(vp8_writer *, const MV *, const MV_CONTEXT_HP *);
void vp8_build_component_cost_table_hp(int *mvcost[2], const MV_CONTEXT_HP *mvc, int mvc_flag[2]);
#endif  /* CONFIG_HIGH_PRECISION_MV */

#endif
