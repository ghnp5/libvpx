/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_config.h"
#include "vpx_dsp_rtcd.h"
#include "vp8_rtcd.h"
#include "vpx_dsp/postproc.h"
#include "vpx_scale_rtcd.h"
#include "vpx_scale/yv12config.h"
#include "postproc.h"
#include "common.h"
#include "vpx_scale/vpx_scale.h"
#include "systemdependent.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define RGB_TO_YUV(t)                                                   \
  ((0.257 * (float)(t >> 16)) + (0.504 * (float)(t >> 8 & 0xff)) +      \
   (0.098 * (float)(t & 0xff)) + 16),                                   \
      (-(0.148 * (float)(t >> 16)) - (0.291 * (float)(t >> 8 & 0xff)) + \
       (0.439 * (float)(t & 0xff)) + 128),                              \
      ((0.439 * (float)(t >> 16)) - (0.368 * (float)(t >> 8 & 0xff)) -  \
       (0.071 * (float)(t & 0xff)) + 128)

/* global constants */
#if CONFIG_POSTPROC_VISUALIZER
static const unsigned char MB_PREDICTION_MODE_colors[MB_MODE_COUNT][3] = {
  { RGB_TO_YUV(0x98FB98) }, /* PaleGreen */
  { RGB_TO_YUV(0x00FF00) }, /* Green */
  { RGB_TO_YUV(0xADFF2F) }, /* GreenYellow */
  { RGB_TO_YUV(0x228B22) }, /* ForestGreen */
  { RGB_TO_YUV(0x006400) }, /* DarkGreen */
  { RGB_TO_YUV(0x98F5FF) }, /* Cadet Blue */
  { RGB_TO_YUV(0x6CA6CD) }, /* Sky Blue */
  { RGB_TO_YUV(0x00008B) }, /* Dark blue */
  { RGB_TO_YUV(0x551A8B) }, /* Purple */
  { RGB_TO_YUV(0xFF0000) }  /* Red */
};

static const unsigned char B_PREDICTION_MODE_colors[B_MODE_COUNT][3] = {
  { RGB_TO_YUV(0x6633ff) }, /* Purple */
  { RGB_TO_YUV(0xcc33ff) }, /* Magenta */
  { RGB_TO_YUV(0xff33cc) }, /* Pink */
  { RGB_TO_YUV(0xff3366) }, /* Coral */
  { RGB_TO_YUV(0x3366ff) }, /* Blue */
  { RGB_TO_YUV(0xed00f5) }, /* Dark Blue */
  { RGB_TO_YUV(0x2e00b8) }, /* Dark Purple */
  { RGB_TO_YUV(0xff6633) }, /* Orange */
  { RGB_TO_YUV(0x33ccff) }, /* Light Blue */
  { RGB_TO_YUV(0x8ab800) }, /* Green */
  { RGB_TO_YUV(0xffcc33) }, /* Light Orange */
  { RGB_TO_YUV(0x33ffcc) }, /* Aqua */
  { RGB_TO_YUV(0x66ff33) }, /* Light Green */
  { RGB_TO_YUV(0xccff33) }, /* Yellow */
};

static const unsigned char MV_REFERENCE_FRAME_colors[MAX_REF_FRAMES][3] = {
  { RGB_TO_YUV(0x00ff00) }, /* Blue */
  { RGB_TO_YUV(0x0000ff) }, /* Green */
  { RGB_TO_YUV(0xffff00) }, /* Yellow */
  { RGB_TO_YUV(0xff0000) }, /* Red */
};
#endif

extern void vp8_blit_text(const char *msg, unsigned char *address,
                          const int pitch);
extern void vp8_blit_line(int x0, int x1, int y0, int y1, unsigned char *image,
                          const int pitch);
/***********************************************************************************************************
 */
static int q2mbl(int x) {
  if (x < 20) x = 20;

  x = 50 + (x - 50) * 10 / 8;
  return x * x / 3;
}

#if CONFIG_POSTPROC
static void vp8_de_mblock(YV12_BUFFER_CONFIG *post, int q) {
  vpx_mbpost_proc_across_ip(post->y_buffer, post->y_stride, post->y_height,
                            post->y_width, q2mbl(q));
  vpx_mbpost_proc_down(post->y_buffer, post->y_stride, post->y_height,
                       post->y_width, q2mbl(q));
}

void vp8_deblock(VP8_COMMON *cm, YV12_BUFFER_CONFIG *source,
                 YV12_BUFFER_CONFIG *post, int q, int low_var_thresh,
                 int flag) {
  double level = 6.0e-05 * q * q * q - .0067 * q * q + .306 * q + .0065;
  int ppl = (int)(level + .5);

  const MODE_INFO *mode_info_context = cm->show_frame_mi;
  int mbr, mbc;

  /* The pixel thresholds are adjusted according to if or not the macroblock
   * is a skipped block.  */
  unsigned char *ylimits = cm->pp_limits_buffer;
  unsigned char *uvlimits = cm->pp_limits_buffer + 16 * cm->mb_cols;
  (void)low_var_thresh;
  (void)flag;

  if (ppl > 0) {
    for (mbr = 0; mbr < cm->mb_rows; ++mbr) {
      unsigned char *ylptr = ylimits;
      unsigned char *uvlptr = uvlimits;
      for (mbc = 0; mbc < cm->mb_cols; ++mbc) {
        unsigned char mb_ppl;

        if (mode_info_context->mbmi.mb_skip_coeff) {
          mb_ppl = (unsigned char)ppl >> 1;
        } else {
          mb_ppl = (unsigned char)ppl;
        }

        memset(ylptr, mb_ppl, 16);
        memset(uvlptr, mb_ppl, 8);

        ylptr += 16;
        uvlptr += 8;
        mode_info_context++;
      }
      mode_info_context++;

      vpx_post_proc_down_and_across_mb_row(
          source->y_buffer + 16 * mbr * source->y_stride,
          post->y_buffer + 16 * mbr * post->y_stride, source->y_stride,
          post->y_stride, source->y_width, ylimits, 16);

      vpx_post_proc_down_and_across_mb_row(
          source->u_buffer + 8 * mbr * source->uv_stride,
          post->u_buffer + 8 * mbr * post->uv_stride, source->uv_stride,
          post->uv_stride, source->uv_width, uvlimits, 8);
      vpx_post_proc_down_and_across_mb_row(
          source->v_buffer + 8 * mbr * source->uv_stride,
          post->v_buffer + 8 * mbr * post->uv_stride, source->uv_stride,
          post->uv_stride, source->uv_width, uvlimits, 8);
    }
  } else {
    vp8_yv12_copy_frame(source, post);
  }
}
#endif

void vp8_de_noise(VP8_COMMON *cm, YV12_BUFFER_CONFIG *source,
                  YV12_BUFFER_CONFIG *post, int q, int low_var_thresh, int flag,
                  int uvfilter) {
  int mbr;
  double level = 6.0e-05 * q * q * q - .0067 * q * q + .306 * q + .0065;
  int ppl = (int)(level + .5);
  int mb_rows = cm->mb_rows;
  int mb_cols = cm->mb_cols;
  unsigned char *limits = cm->pp_limits_buffer;
  ;
  (void)post;
  (void)low_var_thresh;
  (void)flag;

  memset(limits, (unsigned char)ppl, 16 * mb_cols);

  /* TODO: The original code don't filter the 2 outer rows and columns. */
  for (mbr = 0; mbr < mb_rows; ++mbr) {
    vpx_post_proc_down_and_across_mb_row(
        source->y_buffer + 16 * mbr * source->y_stride,
        source->y_buffer + 16 * mbr * source->y_stride, source->y_stride,
        source->y_stride, source->y_width, limits, 16);
    if (uvfilter == 1) {
      vpx_post_proc_down_and_across_mb_row(
          source->u_buffer + 8 * mbr * source->uv_stride,
          source->u_buffer + 8 * mbr * source->uv_stride, source->uv_stride,
          source->uv_stride, source->uv_width, limits, 8);
      vpx_post_proc_down_and_across_mb_row(
          source->v_buffer + 8 * mbr * source->uv_stride,
          source->v_buffer + 8 * mbr * source->uv_stride, source->uv_stride,
          source->uv_stride, source->uv_width, limits, 8);
    }
  }
}

/* Blend the macro block with a solid colored square.  Leave the
 * edges unblended to give distinction to macro blocks in areas
 * filled with the same color block.
 */
void vp8_blend_mb_inner_c(unsigned char *y, unsigned char *u, unsigned char *v,
                          int y_1, int u_1, int v_1, int alpha, int stride) {
  int i, j;
  int y1_const = y_1 * ((1 << 16) - alpha);
  int u1_const = u_1 * ((1 << 16) - alpha);
  int v1_const = v_1 * ((1 << 16) - alpha);

  y += 2 * stride + 2;
  for (i = 0; i < 12; ++i) {
    for (j = 0; j < 12; ++j) {
      y[j] = (y[j] * alpha + y1_const) >> 16;
    }
    y += stride;
  }

  stride >>= 1;

  u += stride + 1;
  v += stride + 1;

  for (i = 0; i < 6; ++i) {
    for (j = 0; j < 6; ++j) {
      u[j] = (u[j] * alpha + u1_const) >> 16;
      v[j] = (v[j] * alpha + v1_const) >> 16;
    }
    u += stride;
    v += stride;
  }
}

/* Blend only the edge of the macro block.  Leave center
 * unblended to allow for other visualizations to be layered.
 */
void vp8_blend_mb_outer_c(unsigned char *y, unsigned char *u, unsigned char *v,
                          int y_1, int u_1, int v_1, int alpha, int stride) {
  int i, j;
  int y1_const = y_1 * ((1 << 16) - alpha);
  int u1_const = u_1 * ((1 << 16) - alpha);
  int v1_const = v_1 * ((1 << 16) - alpha);

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 16; ++j) {
      y[j] = (y[j] * alpha + y1_const) >> 16;
    }
    y += stride;
  }

  for (i = 0; i < 12; ++i) {
    y[0] = (y[0] * alpha + y1_const) >> 16;
    y[1] = (y[1] * alpha + y1_const) >> 16;
    y[14] = (y[14] * alpha + y1_const) >> 16;
    y[15] = (y[15] * alpha + y1_const) >> 16;
    y += stride;
  }

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 16; ++j) {
      y[j] = (y[j] * alpha + y1_const) >> 16;
    }
    y += stride;
  }

  stride >>= 1;

  for (j = 0; j < 8; ++j) {
    u[j] = (u[j] * alpha + u1_const) >> 16;
    v[j] = (v[j] * alpha + v1_const) >> 16;
  }
  u += stride;
  v += stride;

  for (i = 0; i < 6; ++i) {
    u[0] = (u[0] * alpha + u1_const) >> 16;
    v[0] = (v[0] * alpha + v1_const) >> 16;

    u[7] = (u[7] * alpha + u1_const) >> 16;
    v[7] = (v[7] * alpha + v1_const) >> 16;

    u += stride;
    v += stride;
  }

  for (j = 0; j < 8; ++j) {
    u[j] = (u[j] * alpha + u1_const) >> 16;
    v[j] = (v[j] * alpha + v1_const) >> 16;
  }
}

void vp8_blend_b_c(unsigned char *y, unsigned char *u, unsigned char *v,
                   int y_1, int u_1, int v_1, int alpha, int stride) {
  int i, j;
  int y1_const = y_1 * ((1 << 16) - alpha);
  int u1_const = u_1 * ((1 << 16) - alpha);
  int v1_const = v_1 * ((1 << 16) - alpha);

  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 4; ++j) {
      y[j] = (y[j] * alpha + y1_const) >> 16;
    }
    y += stride;
  }

  stride >>= 1;

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 2; ++j) {
      u[j] = (u[j] * alpha + u1_const) >> 16;
      v[j] = (v[j] * alpha + v1_const) >> 16;
    }
    u += stride;
    v += stride;
  }
}

#if CONFIG_POSTPROC_VISUALIZER
static void constrain_line(int x_0, int *x_1, int y_0, int *y_1, int width,
                           int height) {
  int dx;
  int dy;

  if (*x_1 > width) {
    dx = *x_1 - x_0;
    dy = *y_1 - y_0;

    *x_1 = width;
    if (dx) *y_1 = ((width - x_0) * dy) / dx + y_0;
  }
  if (*x_1 < 0) {
    dx = *x_1 - x_0;
    dy = *y_1 - y_0;

    *x_1 = 0;
    if (dx) *y_1 = ((0 - x_0) * dy) / dx + y_0;
  }
  if (*y_1 > height) {
    dx = *x_1 - x_0;
    dy = *y_1 - y_0;

    *y_1 = height;
    if (dy) *x_1 = ((height - y_0) * dx) / dy + x_0;
  }
  if (*y_1 < 0) {
    dx = *x_1 - x_0;
    dy = *y_1 - y_0;

    *y_1 = 0;
    if (dy) *x_1 = ((0 - y_0) * dx) / dy + x_0;
  }
}
#endif  // CONFIG_POSTPROC_VISUALIZER

#if CONFIG_POSTPROC
int vp8_post_proc_frame(VP8_COMMON *oci, YV12_BUFFER_CONFIG *dest,
                        vp8_ppflags_t *ppflags) {
  int q = oci->filter_level * 10 / 6;
  int flags = ppflags->post_proc_flag;
  int deblock_level = ppflags->deblocking_level;
  int noise_level = ppflags->noise_level;

  if (!oci->frame_to_show) return -1;

  if (q > 63) q = 63;

  if (!flags) {
    *dest = *oci->frame_to_show;

    /* handle problem with extending borders */
    dest->y_width = oci->Width;
    dest->y_height = oci->Height;
    dest->uv_height = dest->y_height / 2;
    oci->postproc_state.last_base_qindex = oci->base_qindex;
    oci->postproc_state.last_frame_valid = 1;
    return 0;
  }
  if (flags & VP8D_ADDNOISE) {
    if (!oci->postproc_state.generated_noise) {
      oci->postproc_state.generated_noise = vpx_calloc(
          oci->Width + 256, sizeof(*oci->postproc_state.generated_noise));
      if (!oci->postproc_state.generated_noise) return 1;
    }
  }

  /* Allocate post_proc_buffer_int if needed */
  if ((flags & VP8D_MFQE) && !oci->post_proc_buffer_int_used) {
    if ((flags & VP8D_DEBLOCK) || (flags & VP8D_DEMACROBLOCK)) {
      int width = (oci->Width + 15) & ~15;
      int height = (oci->Height + 15) & ~15;

      if (vp8_yv12_alloc_frame_buffer(&oci->post_proc_buffer_int, width, height,
                                      VP8BORDERINPIXELS)) {
        vpx_internal_error(&oci->error, VPX_CODEC_MEM_ERROR,
                           "Failed to allocate MFQE framebuffer");
      }

      oci->post_proc_buffer_int_used = 1;

      /* insure that postproc is set to all 0's so that post proc
       * doesn't pull random data in from edge
       */
      memset((&oci->post_proc_buffer_int)->buffer_alloc, 128,
             (&oci->post_proc_buffer)->frame_size);
    }
  }

  vp8_clear_system_state();

  if ((flags & VP8D_MFQE) && oci->postproc_state.last_frame_valid &&
      oci->current_video_frame >= 2 &&
      oci->postproc_state.last_base_qindex < 60 &&
      oci->base_qindex - oci->postproc_state.last_base_qindex >= 20) {
    vp8_multiframe_quality_enhance(oci);
    if (((flags & VP8D_DEBLOCK) || (flags & VP8D_DEMACROBLOCK)) &&
        oci->post_proc_buffer_int_used) {
      vp8_yv12_copy_frame(&oci->post_proc_buffer, &oci->post_proc_buffer_int);
      if (flags & VP8D_DEMACROBLOCK) {
        vp8_deblock(oci, &oci->post_proc_buffer_int, &oci->post_proc_buffer,
                    q + (deblock_level - 5) * 10, 1, 0);
        vp8_de_mblock(&oci->post_proc_buffer, q + (deblock_level - 5) * 10);
      } else if (flags & VP8D_DEBLOCK) {
        vp8_deblock(oci, &oci->post_proc_buffer_int, &oci->post_proc_buffer, q,
                    1, 0);
      }
    }
    /* Move partially towards the base q of the previous frame */
    oci->postproc_state.last_base_qindex =
        (3 * oci->postproc_state.last_base_qindex + oci->base_qindex) >> 2;
  } else if (flags & VP8D_DEMACROBLOCK) {
    vp8_deblock(oci, oci->frame_to_show, &oci->post_proc_buffer,
                q + (deblock_level - 5) * 10, 1, 0);
    vp8_de_mblock(&oci->post_proc_buffer, q + (deblock_level - 5) * 10);

    oci->postproc_state.last_base_qindex = oci->base_qindex;
  } else if (flags & VP8D_DEBLOCK) {
    vp8_deblock(oci, oci->frame_to_show, &oci->post_proc_buffer, q, 1, 0);
    oci->postproc_state.last_base_qindex = oci->base_qindex;
  } else {
    vp8_yv12_copy_frame(oci->frame_to_show, &oci->post_proc_buffer);
    oci->postproc_state.last_base_qindex = oci->base_qindex;
  }
  oci->postproc_state.last_frame_valid = 1;

  if (flags & VP8D_ADDNOISE) {
    if (oci->postproc_state.last_q != q ||
        oci->postproc_state.last_noise != noise_level) {
      double sigma;
      struct postproc_state *ppstate = &oci->postproc_state;
      vp8_clear_system_state();
      sigma = noise_level + .5 + .6 * q / 63.0;
      ppstate->clamp =
          vpx_setup_noise(sigma, ppstate->generated_noise, oci->Width + 256);
      ppstate->last_q = q;
      ppstate->last_noise = noise_level;
    }

    vpx_plane_add_noise(
        oci->post_proc_buffer.y_buffer, oci->postproc_state.generated_noise,
        oci->postproc_state.clamp, oci->postproc_state.clamp,
        oci->post_proc_buffer.y_width, oci->post_proc_buffer.y_height,
        oci->post_proc_buffer.y_stride);
  }

#if CONFIG_POSTPROC_VISUALIZER
  if (flags & VP8D_DEBUG_TXT_FRAME_INFO) {
    char message[512];
    sprintf(message, "F%1dG%1dQ%3dF%3dP%d_s%dx%d",
            (oci->frame_type == KEY_FRAME), oci->refresh_golden_frame,
            oci->base_qindex, oci->filter_level, flags, oci->mb_cols,
            oci->mb_rows);
    vp8_blit_text(message, oci->post_proc_buffer.y_buffer,
                  oci->post_proc_buffer.y_stride);
  }

  if (flags & VP8D_DEBUG_TXT_MBLK_MODES) {
    int i, j;
    unsigned char *y_ptr;
    YV12_BUFFER_CONFIG *post = &oci->post_proc_buffer;
    int mb_rows = post->y_height >> 4;
    int mb_cols = post->y_width >> 4;
    int mb_index = 0;
    MODE_INFO *mi = oci->mi;

    y_ptr = post->y_buffer + 4 * post->y_stride + 4;

    /* vp8_filter each macro block */
    for (i = 0; i < mb_rows; ++i) {
      for (j = 0; j < mb_cols; ++j) {
        char zz[4];

        sprintf(zz, "%c", mi[mb_index].mbmi.mode + 'a');

        vp8_blit_text(zz, y_ptr, post->y_stride);
        mb_index++;
        y_ptr += 16;
      }

      mb_index++; /* border */
      y_ptr += post->y_stride * 16 - post->y_width;
    }
  }

  if (flags & VP8D_DEBUG_TXT_DC_DIFF) {
    int i, j;
    unsigned char *y_ptr;
    YV12_BUFFER_CONFIG *post = &oci->post_proc_buffer;
    int mb_rows = post->y_height >> 4;
    int mb_cols = post->y_width >> 4;
    int mb_index = 0;
    MODE_INFO *mi = oci->mi;

    y_ptr = post->y_buffer + 4 * post->y_stride + 4;

    /* vp8_filter each macro block */
    for (i = 0; i < mb_rows; ++i) {
      for (j = 0; j < mb_cols; ++j) {
        char zz[4];
        int dc_diff = !(mi[mb_index].mbmi.mode != B_PRED &&
                        mi[mb_index].mbmi.mode != SPLITMV &&
                        mi[mb_index].mbmi.mb_skip_coeff);

        if (oci->frame_type == KEY_FRAME)
          sprintf(zz, "a");
        else
          sprintf(zz, "%c", dc_diff + '0');

        vp8_blit_text(zz, y_ptr, post->y_stride);
        mb_index++;
        y_ptr += 16;
      }

      mb_index++; /* border */
      y_ptr += post->y_stride * 16 - post->y_width;
    }
  }

  if (flags & VP8D_DEBUG_TXT_RATE_INFO) {
    char message[512];
    sprintf(message, "Bitrate: %10.2f framerate: %10.2f ", oci->bitrate,
            oci->framerate);
    vp8_blit_text(message, oci->post_proc_buffer.y_buffer,
                  oci->post_proc_buffer.y_stride);
  }

  /* Draw motion vectors */
  if ((flags & VP8D_DEBUG_DRAW_MV) && ppflags->display_mv_flag) {
    YV12_BUFFER_CONFIG *post = &oci->post_proc_buffer;
    int width = post->y_width;
    int height = post->y_height;
    unsigned char *y_buffer = oci->post_proc_buffer.y_buffer;
    int y_stride = oci->post_proc_buffer.y_stride;
    MODE_INFO *mi = oci->mi;
    int x0, y0;

    for (y0 = 0; y0 < height; y0 += 16) {
      for (x0 = 0; x0 < width; x0 += 16) {
        int x1, y1;

        if (!(ppflags->display_mv_flag & (1 << mi->mbmi.mode))) {
          mi++;
          continue;
        }

        if (mi->mbmi.mode == SPLITMV) {
          switch (mi->mbmi.partitioning) {
            case 0: /* mv_top_bottom */
            {
              union b_mode_info *bmi = &mi->bmi[0];
              MV *mv = &bmi->mv.as_mv;

              x1 = x0 + 8 + (mv->col >> 3);
              y1 = y0 + 4 + (mv->row >> 3);

              constrain_line(x0 + 8, &x1, y0 + 4, &y1, width, height);
              vp8_blit_line(x0 + 8, x1, y0 + 4, y1, y_buffer, y_stride);

              bmi = &mi->bmi[8];

              x1 = x0 + 8 + (mv->col >> 3);
              y1 = y0 + 12 + (mv->row >> 3);

              constrain_line(x0 + 8, &x1, y0 + 12, &y1, width, height);
              vp8_blit_line(x0 + 8, x1, y0 + 12, y1, y_buffer, y_stride);

              break;
            }
            case 1: /* mv_left_right */
            {
              union b_mode_info *bmi = &mi->bmi[0];
              MV *mv = &bmi->mv.as_mv;

              x1 = x0 + 4 + (mv->col >> 3);
              y1 = y0 + 8 + (mv->row >> 3);

              constrain_line(x0 + 4, &x1, y0 + 8, &y1, width, height);
              vp8_blit_line(x0 + 4, x1, y0 + 8, y1, y_buffer, y_stride);

              bmi = &mi->bmi[2];

              x1 = x0 + 12 + (mv->col >> 3);
              y1 = y0 + 8 + (mv->row >> 3);

              constrain_line(x0 + 12, &x1, y0 + 8, &y1, width, height);
              vp8_blit_line(x0 + 12, x1, y0 + 8, y1, y_buffer, y_stride);

              break;
            }
            case 2: /* mv_quarters   */
            {
              union b_mode_info *bmi = &mi->bmi[0];
              MV *mv = &bmi->mv.as_mv;

              x1 = x0 + 4 + (mv->col >> 3);
              y1 = y0 + 4 + (mv->row >> 3);

              constrain_line(x0 + 4, &x1, y0 + 4, &y1, width, height);
              vp8_blit_line(x0 + 4, x1, y0 + 4, y1, y_buffer, y_stride);

              bmi = &mi->bmi[2];

              x1 = x0 + 12 + (mv->col >> 3);
              y1 = y0 + 4 + (mv->row >> 3);

              constrain_line(x0 + 12, &x1, y0 + 4, &y1, width, height);
              vp8_blit_line(x0 + 12, x1, y0 + 4, y1, y_buffer, y_stride);

              bmi = &mi->bmi[8];

              x1 = x0 + 4 + (mv->col >> 3);
              y1 = y0 + 12 + (mv->row >> 3);

              constrain_line(x0 + 4, &x1, y0 + 12, &y1, width, height);
              vp8_blit_line(x0 + 4, x1, y0 + 12, y1, y_buffer, y_stride);

              bmi = &mi->bmi[10];

              x1 = x0 + 12 + (mv->col >> 3);
              y1 = y0 + 12 + (mv->row >> 3);

              constrain_line(x0 + 12, &x1, y0 + 12, &y1, width, height);
              vp8_blit_line(x0 + 12, x1, y0 + 12, y1, y_buffer, y_stride);
              break;
            }
            default: {
              union b_mode_info *bmi = mi->bmi;
              int bx0, by0;

              for (by0 = y0; by0 < (y0 + 16); by0 += 4) {
                for (bx0 = x0; bx0 < (x0 + 16); bx0 += 4) {
                  MV *mv = &bmi->mv.as_mv;

                  x1 = bx0 + 2 + (mv->col >> 3);
                  y1 = by0 + 2 + (mv->row >> 3);

                  constrain_line(bx0 + 2, &x1, by0 + 2, &y1, width, height);
                  vp8_blit_line(bx0 + 2, x1, by0 + 2, y1, y_buffer, y_stride);

                  bmi++;
                }
              }
            }
          }
        } else if (mi->mbmi.mode >= NEARESTMV) {
          MV *mv = &mi->mbmi.mv.as_mv;
          const int lx0 = x0 + 8;
          const int ly0 = y0 + 8;

          x1 = lx0 + (mv->col >> 3);
          y1 = ly0 + (mv->row >> 3);

          if (x1 != lx0 && y1 != ly0) {
            constrain_line(lx0, &x1, ly0 - 1, &y1, width, height);
            vp8_blit_line(lx0, x1, ly0 - 1, y1, y_buffer, y_stride);

            constrain_line(lx0, &x1, ly0 + 1, &y1, width, height);
            vp8_blit_line(lx0, x1, ly0 + 1, y1, y_buffer, y_stride);
          } else
            vp8_blit_line(lx0, x1, ly0, y1, y_buffer, y_stride);
        }

        mi++;
      }
      mi++;
    }
  }

  /* Color in block modes */
  if ((flags & VP8D_DEBUG_CLR_BLK_MODES) &&
      (ppflags->display_mb_modes_flag || ppflags->display_b_modes_flag)) {
    int y, x;
    YV12_BUFFER_CONFIG *post = &oci->post_proc_buffer;
    int width = post->y_width;
    int height = post->y_height;
    unsigned char *y_ptr = oci->post_proc_buffer.y_buffer;
    unsigned char *u_ptr = oci->post_proc_buffer.u_buffer;
    unsigned char *v_ptr = oci->post_proc_buffer.v_buffer;
    int y_stride = oci->post_proc_buffer.y_stride;
    MODE_INFO *mi = oci->mi;

    for (y = 0; y < height; y += 16) {
      for (x = 0; x < width; x += 16) {
        int Y = 0, U = 0, V = 0;

        if (mi->mbmi.mode == B_PRED &&
            ((ppflags->display_mb_modes_flag & B_PRED) ||
             ppflags->display_b_modes_flag)) {
          int by, bx;
          unsigned char *yl, *ul, *vl;
          union b_mode_info *bmi = mi->bmi;

          yl = y_ptr + x;
          ul = u_ptr + (x >> 1);
          vl = v_ptr + (x >> 1);

          for (by = 0; by < 16; by += 4) {
            for (bx = 0; bx < 16; bx += 4) {
              if ((ppflags->display_b_modes_flag & (1 << mi->mbmi.mode)) ||
                  (ppflags->display_mb_modes_flag & B_PRED)) {
                Y = B_PREDICTION_MODE_colors[bmi->as_mode][0];
                U = B_PREDICTION_MODE_colors[bmi->as_mode][1];
                V = B_PREDICTION_MODE_colors[bmi->as_mode][2];

                vp8_blend_b(yl + bx, ul + (bx >> 1), vl + (bx >> 1), Y, U, V,
                            0xc000, y_stride);
              }
              bmi++;
            }

            yl += y_stride * 4;
            ul += y_stride * 1;
            vl += y_stride * 1;
          }
        } else if (ppflags->display_mb_modes_flag & (1 << mi->mbmi.mode)) {
          Y = MB_PREDICTION_MODE_colors[mi->mbmi.mode][0];
          U = MB_PREDICTION_MODE_colors[mi->mbmi.mode][1];
          V = MB_PREDICTION_MODE_colors[mi->mbmi.mode][2];

          vp8_blend_mb_inner(y_ptr + x, u_ptr + (x >> 1), v_ptr + (x >> 1), Y,
                             U, V, 0xc000, y_stride);
        }

        mi++;
      }
      y_ptr += y_stride * 16;
      u_ptr += y_stride * 4;
      v_ptr += y_stride * 4;

      mi++;
    }
  }

  /* Color in frame reference blocks */
  if ((flags & VP8D_DEBUG_CLR_FRM_REF_BLKS) &&
      ppflags->display_ref_frame_flag) {
    int y, x;
    YV12_BUFFER_CONFIG *post = &oci->post_proc_buffer;
    int width = post->y_width;
    int height = post->y_height;
    unsigned char *y_ptr = oci->post_proc_buffer.y_buffer;
    unsigned char *u_ptr = oci->post_proc_buffer.u_buffer;
    unsigned char *v_ptr = oci->post_proc_buffer.v_buffer;
    int y_stride = oci->post_proc_buffer.y_stride;
    MODE_INFO *mi = oci->mi;

    for (y = 0; y < height; y += 16) {
      for (x = 0; x < width; x += 16) {
        int Y = 0, U = 0, V = 0;

        if (ppflags->display_ref_frame_flag & (1 << mi->mbmi.ref_frame)) {
          Y = MV_REFERENCE_FRAME_colors[mi->mbmi.ref_frame][0];
          U = MV_REFERENCE_FRAME_colors[mi->mbmi.ref_frame][1];
          V = MV_REFERENCE_FRAME_colors[mi->mbmi.ref_frame][2];

          vp8_blend_mb_outer(y_ptr + x, u_ptr + (x >> 1), v_ptr + (x >> 1), Y,
                             U, V, 0xc000, y_stride);
        }

        mi++;
      }
      y_ptr += y_stride * 16;
      u_ptr += y_stride * 4;
      v_ptr += y_stride * 4;

      mi++;
    }
  }
#endif

  *dest = oci->post_proc_buffer;

  /* handle problem with extending borders */
  dest->y_width = oci->Width;
  dest->y_height = oci->Height;
  dest->uv_height = dest->y_height / 2;
  return 0;
}
#endif
