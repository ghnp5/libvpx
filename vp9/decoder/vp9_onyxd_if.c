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
#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "vp9/common/vp9_onyxc_int.h"
#if CONFIG_VP9_POSTPROC
#include "vp9/common/vp9_postproc.h"
#endif
#include "vp9/decoder/vp9_onyxd.h"
#include "vp9/decoder/vp9_onyxd_int.h"
#include "vpx_mem/vpx_mem.h"
#include "vp9/common/vp9_alloccommon.h"
#include "vp9/common/vp9_loopfilter.h"
#include "vp9/common/vp9_quant_common.h"
#include "vp9/common/kernel/vp9_inter_pred_rs.h"
#include "vp9/common/kernel/vp9_intra_pred_rs.h"
#include "vp9/common/kernel/vp9_loopfilter_rs.h"
#include "vpx_scale/vpx_scale.h"
#include "vp9/common/vp9_systemdependent.h"
#include "vpx_ports/vpx_timer.h"
#include "vp9/decoder/vp9_decodeframe.h"
#include "vp9/decoder/vp9_detokenize.h"
#include "./vpx_scale_rtcd.h"

#include "vp9/decoder/vp9_append.h"
#include "vp9/decoder/vp9_decodeframe_recon.h"
#include "vp9/decoder/vp9_step.h"
#include "vp9/decoder/vp9_loopfilter_step.h"
#include "vp9/decoder/vp9_device.h"
#include "vp9/decoder/vp9_loopfilter_recon.h"

#include "vp9/ppa.h"

#define WRITE_RECON_BUFFER 0
#if WRITE_RECON_BUFFER == 1
static void recon_write_yuv_frame(const char *name,
                                  const YV12_BUFFER_CONFIG *s,
                                  int w, int _h) {
  FILE *yuv_file = fopen(name, "ab");
  const uint8_t *src = s->y_buffer;
  int h = _h;

  do {
    fwrite(src, w, 1,  yuv_file);
    src += s->y_stride;
  } while (--h);

  src = s->u_buffer;
  h = (_h + 1) >> 1;
  w = (w + 1) >> 1;

  do {
    fwrite(src, w, 1,  yuv_file);
    src += s->uv_stride;
  } while (--h);

  src = s->v_buffer;
  h = (_h + 1) >> 1;

  do {
    fwrite(src, w, 1, yuv_file);
    src += s->uv_stride;
  } while (--h);

  fclose(yuv_file);
}
#endif
#if WRITE_RECON_BUFFER == 2
void write_dx_frame_to_file(YV12_BUFFER_CONFIG *frame, int this_frame) {
  // write the frame
  FILE *yframe;
  int i;
  char filename[255];

  snprintf(filename, sizeof(filename)-1, "dx\\y%04d.raw", this_frame);
  yframe = fopen(filename, "wb");

  for (i = 0; i < frame->y_height; i++)
    fwrite(frame->y_buffer + i * frame->y_stride,
           frame->y_width, 1, yframe);

  fclose(yframe);
  snprintf(filename, sizeof(filename)-1, "dx\\u%04d.raw", this_frame);
  yframe = fopen(filename, "wb");

  for (i = 0; i < frame->uv_height; i++)
    fwrite(frame->u_buffer + i * frame->uv_stride,
           frame->uv_width, 1, yframe);

  fclose(yframe);
  snprintf(filename, sizeof(filename)-1, "dx\\v%04d.raw", this_frame);
  yframe = fopen(filename, "wb");

  for (i = 0; i < frame->uv_height; i++)
    fwrite(frame->v_buffer + i * frame->uv_stride,
           frame->uv_width, 1, yframe);

  fclose(yframe);
}
#endif

void vp9_initialize_dec() {
  static int init_done = 0;

  if (!init_done) {
    vp9_initialize_common();
    vp9_init_quant_tables();
    init_done = 1;
  }
}

static void init_macroblockd(VP9D_COMP *const pbi) {
  MACROBLOCKD *xd = &pbi->mb;
  struct macroblockd_plane *const pd = xd->plane;
  int i;

  for (i = 0; i < MAX_MB_PLANE; ++i) {
    pd[i].qcoeff  = pbi->qcoeff[i];
    pd[i].dqcoeff = pbi->dqcoeff[i];
    pd[i].eobs    = pbi->eobs[i];
  }
}

#define MAX_TASKS 16

static void vp9_sched_init(VP9D_COMP *const pbi) {
#if CONFIG_MULTITHREAD
  pbi->sched = scheduler_create();
  assert(pbi->sched);

  pbi->steps_pool = steps_pool_get();
  assert(pbi->steps_pool);
  pbi->tsk_cache = task_cache_create(MAX_TASKS, pbi->steps_pool);
  assert(pbi->tsk_cache);
  scheduler_set_strategy(pbi->sched, SCHED_PERF_FIRST);

  if (pbi->rs_enabled) {
    pbi->lf_steps_pool = lf_steps_pool_setup_masks_get();
  } else {
    pbi->lf_steps_pool = lf_steps_pool_get();
  }
  assert(pbi->steps_pool);
  pbi->lf_tsk_cache = task_cache_create(MAX_TASKS, pbi->lf_steps_pool);
  assert(pbi->lf_tsk_cache);

  vp9_register_devices(pbi->sched);
#endif
}

static void vp9_sched_fini(VP9D_COMP *const pbi) {
#if CONFIG_MULTITHREAD
  scheduler_delete(pbi->sched);
  task_cache_delete(pbi->tsk_cache);
  task_cache_delete(pbi->lf_tsk_cache);
  task_steps_pool_delete(pbi->steps_pool);
  task_steps_pool_delete(pbi->lf_steps_pool);
#endif
}


VP9D_PTR vp9_create_decompressor(VP9D_CONFIG *oxcf) {
  VP9D_COMP *const pbi = vpx_memalign(32, sizeof(VP9D_COMP));
  VP9_COMMON *const cm = pbi ? &pbi->common : NULL;
#if CONFIG_MULTITHREAD
  const char *rs_enable_val = NULL;
#endif

  if (!cm)
    return NULL;

  vp9_zero(*pbi);
  PPA_INIT();

  // Initialize the references to not point to any frame buffers.
  memset(&cm->ref_frame_map, -1, sizeof(cm->ref_frame_map));

  if (setjmp(cm->error.jmp)) {
    cm->error.setjmp = 0;
    vp9_remove_decompressor(pbi);
    return NULL;
  }

  cm->error.setjmp = 1;
  vp9_initialize_dec();

  vp9_create_common(cm);

  pbi->oxcf = *oxcf;
  pbi->ready_for_new_data = 1;
  cm->current_video_frame = 0;

  pbi->rs_enabled = 0;

#if CONFIG_MULTITHREAD
  rs_enable_val = getenv("RSENABLE");
  if (rs_enable_val) {
    if (*rs_enable_val == '1') {
      if (vp9_loop_filter_rs_init(&pbi->loop_filter_rs_handle) == 0) {
        pbi->rs_enabled = 1;
      } else {
        fprintf(stderr, "vp9 loopfilter renderscript init failed!\n");
      }
    }
  }
#endif

  // vp9_init_dequantizer() is first called here. Add check in
  // frame_init_dequantizer() to avoid unnecessary calling of
  // vp9_init_dequantizer() for every frame.
  vp9_init_dequantizer(cm);

  vp9_loop_filter_init_wpp(cm);

  cm->error.setjmp = 0;
  pbi->decoded_key_frame = 0;

  init_macroblockd(pbi);

  vp9_worker_init(&pbi->lf_worker);

  vp9_sched_init(pbi);

  return pbi;
}

void vp9_remove_decompressor(VP9D_PTR ptr) {
  int i;
  VP9D_COMP *const pbi = (VP9D_COMP *)ptr;
  VP9_DECODER_RECON *decoder_recon;

  if (!pbi)
    return;

  PPA_END();

  vp9_sched_fini(pbi);

#if CONFIG_MULTITHREAD
  if (pbi->loop_filter_rs_handle)
    vp9_loop_filter_rs_fini(pbi->loop_filter_rs_handle);
#endif

  vp9_remove_common(&pbi->common);
  vp9_worker_end(&pbi->lf_worker);
  vpx_free(pbi->lf_worker.data1);

  for (i = 0; i < MAX_TILES; i++) {
    decoder_recon = &pbi->decoder_recon[i];
    free_buffers_recon(decoder_recon);
  }

  for (i = 0; i < pbi->num_tile_workers; ++i) {
    VP9Worker *const worker = &pbi->tile_workers[i];
    vp9_worker_end(worker);
    vpx_free(worker->data1);
    vpx_free(worker->data2);
  }
  vpx_free(pbi->tile_workers);
  vpx_free(pbi->mi_streams);
  vpx_free(pbi->above_context[0]);
  vpx_free(pbi->above_seg_context);
  vpx_free(pbi);
  vp9_release_inter_rs();
  vp9_release_intra_rs();
}

static int equal_dimensions(YV12_BUFFER_CONFIG *a, YV12_BUFFER_CONFIG *b) {
    return a->y_height == b->y_height && a->y_width == b->y_width &&
           a->uv_height == b->uv_height && a->uv_width == b->uv_width;
}

vpx_codec_err_t vp9_copy_reference_dec(VP9D_PTR ptr,
                                       VP9_REFFRAME ref_frame_flag,
                                       YV12_BUFFER_CONFIG *sd) {
  VP9D_COMP *pbi = (VP9D_COMP *) ptr;
  VP9_COMMON *cm = &pbi->common;

  /* TODO(jkoleszar): The decoder doesn't have any real knowledge of what the
   * encoder is using the frame buffers for. This is just a stub to keep the
   * vpxenc --test-decode functionality working, and will be replaced in a
   * later commit that adds VP9-specific controls for this functionality.
   */
  if (ref_frame_flag == VP9_LAST_FLAG) {
    YV12_BUFFER_CONFIG *cfg = &cm->yv12_fb[cm->ref_frame_map[0]];
    if (!equal_dimensions(cfg, sd))
      vpx_internal_error(&cm->error, VPX_CODEC_ERROR,
                         "Incorrect buffer dimensions");
    else
      vp8_yv12_copy_frame(cfg, sd);
  } else {
    vpx_internal_error(&cm->error, VPX_CODEC_ERROR,
                       "Invalid reference frame");
  }

  return cm->error.error_code;
}


vpx_codec_err_t vp9_set_reference_dec(VP9D_PTR ptr, VP9_REFFRAME ref_frame_flag,
                                      YV12_BUFFER_CONFIG *sd) {
  VP9D_COMP *pbi = (VP9D_COMP *) ptr;
  VP9_COMMON *cm = &pbi->common;
  RefBuffer *ref_buf = NULL;

  /* TODO(jkoleszar): The decoder doesn't have any real knowledge of what the
   * encoder is using the frame buffers for. This is just a stub to keep the
   * vpxenc --test-decode functionality working, and will be replaced in a
   * later commit that adds VP9-specific controls for this functionality.
   */
  if (ref_frame_flag == VP9_LAST_FLAG) {
    ref_buf = &cm->frame_refs[0];
  } else if (ref_frame_flag == VP9_GOLD_FLAG) {
    ref_buf = &cm->frame_refs[1];
  } else if (ref_frame_flag == VP9_ALT_FLAG) {
    ref_buf = &cm->frame_refs[2];
  } else {
    vpx_internal_error(&pbi->common.error, VPX_CODEC_ERROR,
                       "Invalid reference frame");
    return pbi->common.error.error_code;
  }

  if (!equal_dimensions(ref_buf->buf, sd)) {
    vpx_internal_error(&pbi->common.error, VPX_CODEC_ERROR,
                       "Incorrect buffer dimensions");
  } else {
    int *ref_fb_ptr = &ref_buf->idx;

    // Find an empty frame buffer.
    const int free_fb = get_free_fb(cm);
    // Decrease fb_idx_ref_cnt since it will be increased again in
    // ref_cnt_fb() below.
    cm->fb_idx_ref_cnt[free_fb]--;

    // Manage the reference counters and copy image.
    ref_cnt_fb(cm->fb_idx_ref_cnt, ref_fb_ptr, free_fb);
    ref_buf->buf = &cm->yv12_fb[*ref_fb_ptr];
    vp8_yv12_copy_frame(sd, ref_buf->buf);
  }

  return pbi->common.error.error_code;
}


int vp9_get_reference_dec(VP9D_PTR ptr, int index, YV12_BUFFER_CONFIG **fb) {
  VP9D_COMP *pbi = (VP9D_COMP *) ptr;
  VP9_COMMON *cm = &pbi->common;

  if (index < 0 || index >= REF_FRAMES)
    return -1;

  *fb = &cm->yv12_fb[cm->ref_frame_map[index]];
  return 0;
}

/* If any buffer updating is signaled it should be done here. */
static void swap_frame_buffers(VP9D_COMP *pbi) {
  int ref_index = 0, mask;
  VP9_COMMON *const cm = &pbi->common;

  for (mask = pbi->refresh_frame_flags; mask; mask >>= 1) {
    if (mask & 1)
      ref_cnt_fb(cm->fb_idx_ref_cnt, &cm->ref_frame_map[ref_index],
                 cm->new_fb_idx);
    ++ref_index;
  }

  cm->frame_to_show = get_frame_new_buffer(cm);
  cm->fb_idx_ref_cnt[cm->new_fb_idx]--;

  // Invalidate these references until the next frame starts.
  for (ref_index = 0; ref_index < 3; ref_index++)
    cm->frame_refs[ref_index].idx = INT_MAX;
}

int vp9_receive_compressed_data(VP9D_PTR ptr,
                                size_t size, const uint8_t **psource,
                                int64_t time_stamp) {
  VP9D_COMP *pbi = (VP9D_COMP *) ptr;
  VP9_COMMON *cm = &pbi->common;
  const uint8_t *source = *psource;
  int retcode = 0;

  PPAStartCpuEventFunc(vp9_decode_frame_time);
  /*if(pbi->ready_for_new_data == 0)
      return -1;*/

  if (ptr == 0)
    return -1;

  cm->error.error_code = VPX_CODEC_OK;

  pbi->source = source;
  pbi->source_sz = size;

  if (pbi->source_sz == 0) {
    /* This is used to signal that we are missing frames.
     * We do not know if the missing frame(s) was supposed to update
     * any of the reference buffers, but we act conservative and
     * mark only the last buffer as corrupted.
     *
     * TODO(jkoleszar): Error concealment is undefined and non-normative
     * at this point, but if it becomes so, [0] may not always be the correct
     * thing to do here.
     */
    if (cm->frame_refs[0].idx != INT_MAX)
      cm->frame_refs[0].buf->corrupted = 1;
  }

  cm->new_fb_idx = get_free_fb(cm);

  if (setjmp(cm->error.jmp)) {
    cm->error.setjmp = 0;

    /* We do not know if the missing frame(s) was supposed to update
     * any of the reference buffers, but we act conservative and
     * mark only the last buffer as corrupted.
     *
     * TODO(jkoleszar): Error concealment is undefined and non-normative
     * at this point, but if it becomes so, [0] may not always be the correct
     * thing to do here.
     */
    if (cm->frame_refs[0].idx != INT_MAX)
      cm->frame_refs[0].buf->corrupted = 1;

    if (cm->fb_idx_ref_cnt[cm->new_fb_idx] > 0)
      cm->fb_idx_ref_cnt[cm->new_fb_idx]--;

    return -1;
  }

  cm->error.setjmp = 1;

   //mcw mt
   retcode = vp9_decode_frame_mt(pbi, psource);
   //retcode = vp9_decode_frame_recon(pbi, psource);

  if (retcode < 0) {
    cm->error.error_code = VPX_CODEC_ERROR;
    cm->error.setjmp = 0;
    if (cm->fb_idx_ref_cnt[cm->new_fb_idx] > 0)
      cm->fb_idx_ref_cnt[cm->new_fb_idx]--;
    return retcode;
  }

  swap_frame_buffers(pbi);

#if WRITE_RECON_BUFFER == 2
  if (cm->show_frame)
    write_dx_frame_to_file(cm->frame_to_show,
                           cm->current_video_frame);
  else
    write_dx_frame_to_file(cm->frame_to_show,
                           cm->current_video_frame + 1000);
#endif

  if (!pbi->do_loopfilter_inline) {
    PPAStartCpuEventFunc(loopfilter_time);
#if CONFIG_MULTITHREAD
    if (pbi->rs_enabled) {
      vp9_loop_filter_frame_rs(pbi, cm, &pbi->mb,
                               pbi->common.lf.filter_level, 0, 0);
    } else {
      vp9_loop_filter_frame_wpp(pbi, cm, &pbi->mb,
                                pbi->common.lf.filter_level, 0, 0);
    }
#else
    vp9_loop_filter_frame_wpp(pbi, cm, &pbi->mb,
                              pbi->common.lf.filter_level, 0, 0);
#endif
    //vp9_loop_filter_frame(cm, &pbi->mb, pbi->common.lf.filter_level, 0, 0);
    PPAStopCpuEventFunc(loopfilter_time);
  }

#if WRITE_RECON_BUFFER == 2
  if (cm->show_frame)
    write_dx_frame_to_file(cm->frame_to_show,
                           cm->current_video_frame + 2000);
  else
    write_dx_frame_to_file(cm->frame_to_show,
                           cm->current_video_frame + 3000);
#endif

#if WRITE_RECON_BUFFER == 1
  if (cm->show_frame)
    recon_write_yuv_frame("recon.yuv", cm->frame_to_show,
                          cm->width, cm->height);
#endif

  vp9_clear_system_state();

  cm->last_show_frame = cm->show_frame;
  if (cm->show_frame) {
    if (!cm->show_existing_frame) {
      // current mip will be the prev_mip for the next frame
      MODE_INFO *temp = cm->prev_mip;
      MODE_INFO **temp2 = cm->prev_mi_grid_base;
      cm->prev_mip = cm->mip;
      cm->mip = temp;
      cm->prev_mi_grid_base = cm->mi_grid_base;
      cm->mi_grid_base = temp2;

      // update the upper left visible macroblock ptrs
      cm->mi = cm->mip + cm->mode_info_stride + 1;
      cm->prev_mi = cm->prev_mip + cm->mode_info_stride + 1;
      cm->mi_grid_visible = cm->mi_grid_base + cm->mode_info_stride + 1;
      cm->prev_mi_grid_visible = cm->prev_mi_grid_base +
                                 cm->mode_info_stride + 1;

      pbi->mb.mi_8x8 = cm->mi_grid_visible;
      pbi->mb.mi_8x8[0] = cm->mi;
    }
    cm->current_video_frame++;
  }

  pbi->ready_for_new_data = 0;
  pbi->last_time_stamp = time_stamp;
  pbi->source_sz = 0;

  cm->error.setjmp = 0;
  PPAStopCpuEventFunc(vp9_decode_frame_time);

  return retcode;
}

int vp9_get_raw_frame(VP9D_PTR ptr, YV12_BUFFER_CONFIG *sd,
                      int64_t *time_stamp, int64_t *time_end_stamp,
                      vp9_ppflags_t *flags) {
  int ret = -1;
  VP9D_COMP *pbi = (VP9D_COMP *) ptr;

  if (pbi->ready_for_new_data == 1)
    return ret;

  /* ie no raw frame to show!!! */
  if (pbi->common.show_frame == 0)
    return ret;

  pbi->ready_for_new_data = 1;
  *time_stamp = pbi->last_time_stamp;
  *time_end_stamp = 0;

#if CONFIG_VP9_POSTPROC
  ret = vp9_post_proc_frame(&pbi->common, sd, flags);
#else

  if (pbi->common.frame_to_show) {
    *sd = *pbi->common.frame_to_show;
    sd->y_width = pbi->common.width;
    sd->y_height = pbi->common.height;
    sd->uv_width = sd->y_width >> pbi->common.subsampling_x;
    sd->uv_height = sd->y_height >> pbi->common.subsampling_y;

    ret = 0;
  } else {
    ret = -1;
  }

#endif /*!CONFIG_POSTPROC*/
  vp9_clear_system_state();
  return ret;
}
