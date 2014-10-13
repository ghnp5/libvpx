/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpxenc.h"
#include "./vpx_config.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_LIBYUV
#include "third_party/libyuv/include/libyuv/scale.h"
#endif

#include "vpx/vpx_encoder.h"
#if CONFIG_DECODERS
#include "vpx/vpx_decoder.h"
#endif

#include "./args.h"
#include "./ivfenc.h"
#include "./tools_common.h"

#if CONFIG_VP8_ENCODER || CONFIG_VP9_ENCODER
#include "vpx/vp8cx.h"
#endif
#if CONFIG_VP8_DECODER || CONFIG_VP9_DECODER
#include "vpx/vp8dx.h"
#endif

#include "vpx/vpx_integer.h"
#include "vpx_ports/mem_ops.h"
#include "vpx_ports/vpx_timer.h"
#include "./rate_hist.h"
#include "./vpxstats.h"
#include "./warnings.h"
#if CONFIG_WEBM_IO
#include "./webmenc.h"
#endif
#include "./y4minput.h"

/* Swallow warnings about unused results of fread/fwrite */
static size_t wrap_fread(void *ptr, size_t size, size_t nmemb,
                         FILE *stream) {
  return fread(ptr, size, nmemb, stream);
}
#define fread wrap_fread

static size_t wrap_fwrite(const void *ptr, size_t size, size_t nmemb,
                          FILE *stream) {
  return fwrite(ptr, size, nmemb, stream);
}
#define fwrite wrap_fwrite


static const char *exec_name;

static void warn_or_exit_on_errorv(vpx_codec_ctx_t *ctx, int fatal,
                                   const char *s, va_list ap) {
  if (ctx->err) {
    const char *detail = vpx_codec_error_detail(ctx);

    vfprintf(stderr, s, ap);
    fprintf(stderr, ": %s\n", vpx_codec_error(ctx));

    if (detail)
      fprintf(stderr, "    %s\n", detail);

    if (fatal)
      exit(EXIT_FAILURE);
  }
}

static void ctx_exit_on_error(vpx_codec_ctx_t *ctx, const char *s, ...) {
  va_list ap;

  va_start(ap, s);
  warn_or_exit_on_errorv(ctx, 1, s, ap);
  va_end(ap);
}

static void warn_or_exit_on_error(vpx_codec_ctx_t *ctx, int fatal,
                                  const char *s, ...) {
  va_list ap;

  va_start(ap, s);
  warn_or_exit_on_errorv(ctx, fatal, s, ap);
  va_end(ap);
}

int read_frame(struct VpxInputContext *input_ctx, vpx_image_t *img) {
  FILE *f = input_ctx->file;
  y4m_input *y4m = &input_ctx->y4m;
  int shortread = 0;

  if (input_ctx->file_type == FILE_TYPE_Y4M) {
    if (y4m_input_fetch_frame(y4m, f, img) < 1)
      return 0;
  } else {
    shortread = read_yuv_frame(input_ctx, img);
  }

  return !shortread;
}

int file_is_y4m(const char detect[4]) {
  if (memcmp(detect, "YUV4", 4) == 0) {
    return 1;
  }
  return 0;
}

int fourcc_is_ivf(const char detect[4]) {
  if (memcmp(detect, "DKIF", 4) == 0) {
    return 1;
  }
  return 0;
}

static const arg_def_t debugmode = ARG_DEF("D", "debug", 0,
                                           "Debug mode (makes output deterministic)");
static const arg_def_t outputfile = ARG_DEF("o", "output", 1,
                                            "Output filename");
static const arg_def_t use_yv12 = ARG_DEF(NULL, "yv12", 0,
                                          "Input file is YV12 ");
static const arg_def_t use_i420 = ARG_DEF(NULL, "i420", 0,
                                          "Input file is I420 (default)");
static const arg_def_t use_i422 = ARG_DEF(NULL, "i422", 0,
                                          "Input file is I422");
static const arg_def_t use_i444 = ARG_DEF(NULL, "i444", 0,
                                          "Input file is I444");
static const arg_def_t use_i440 = ARG_DEF(NULL, "i440", 0,
                                          "Input file is I440");
static const arg_def_t codecarg = ARG_DEF(NULL, "codec", 1,
                                          "Codec to use");
static const arg_def_t passes           = ARG_DEF("p", "passes", 1,
                                                  "Number of passes (1/2)");
static const arg_def_t pass_arg         = ARG_DEF(NULL, "pass", 1,
                                                  "Pass to execute (1/2)");
static const arg_def_t fpf_name         = ARG_DEF(NULL, "fpf", 1,
                                                  "First pass statistics file name");
#if CONFIG_FP_MB_STATS
static const arg_def_t fpmbf_name         = ARG_DEF(NULL, "fpmbf", 1,
                                      "First pass block statistics file name");
#endif
static const arg_def_t limit = ARG_DEF(NULL, "limit", 1,
                                       "Stop encoding after n input frames");
static const arg_def_t skip = ARG_DEF(NULL, "skip", 1,
                                      "Skip the first n input frames");
static const arg_def_t deadline         = ARG_DEF("d", "deadline", 1,
                                                  "Deadline per frame (usec)");
static const arg_def_t best_dl          = ARG_DEF(NULL, "best", 0,
                                                  "Use Best Quality Deadline");
static const arg_def_t good_dl          = ARG_DEF(NULL, "good", 0,
                                                  "Use Good Quality Deadline");
static const arg_def_t rt_dl            = ARG_DEF(NULL, "rt", 0,
                                                  "Use Realtime Quality Deadline");
static const arg_def_t quietarg         = ARG_DEF("q", "quiet", 0,
                                                  "Do not print encode progress");
static const arg_def_t verbosearg       = ARG_DEF("v", "verbose", 0,
                                                  "Show encoder parameters");
static const arg_def_t psnrarg          = ARG_DEF(NULL, "psnr", 0,
                                                  "Show PSNR in status line");

static const struct arg_enum_list test_decode_enum[] = {
  {"off",   TEST_DECODE_OFF},
  {"fatal", TEST_DECODE_FATAL},
  {"warn",  TEST_DECODE_WARN},
  {NULL, 0}
};
static const arg_def_t recontest = ARG_DEF_ENUM(NULL, "test-decode", 1,
                                                "Test encode/decode mismatch",
                                                test_decode_enum);
static const arg_def_t framerate        = ARG_DEF(NULL, "fps", 1,
                                                  "Stream frame rate (rate/scale)");
static const arg_def_t use_ivf          = ARG_DEF(NULL, "ivf", 0,
                                                  "Output IVF (default is WebM if WebM IO is enabled)");
static const arg_def_t out_part = ARG_DEF("P", "output-partitions", 0,
                                          "Makes encoder output partitions. Requires IVF output!");
static const arg_def_t q_hist_n         = ARG_DEF(NULL, "q-hist", 1,
                                                  "Show quantizer histogram (n-buckets)");
static const arg_def_t rate_hist_n         = ARG_DEF(NULL, "rate-hist", 1,
                                                     "Show rate histogram (n-buckets)");
static const arg_def_t disable_warnings =
    ARG_DEF(NULL, "disable-warnings", 0,
            "Disable warnings about potentially incorrect encode settings.");
static const arg_def_t disable_warning_prompt =
    ARG_DEF("y", "disable-warning-prompt", 0,
            "Display warnings, but do not prompt user to continue.");

<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
static const arg_def_t test16bitinternalarg = ARG_DEF(
    NULL, "test-16bit-internal", 0, "Force use of 16 bit internal buffer");
#endif

static const arg_def_t *main_args[] = {
  &debugmode,
  &outputfile, &codecarg, &passes, &pass_arg, &fpf_name, &limit, &skip,
  &deadline, &best_dl, &good_dl, &rt_dl,
  &quietarg, &verbosearg, &psnrarg, &use_ivf, &out_part, &q_hist_n,
  &rate_hist_n, &disable_warnings, &disable_warning_prompt,
  NULL
};

static const arg_def_t usage            = ARG_DEF("u", "usage", 1,
                                                  "Usage profile number to use");
static const arg_def_t threads          = ARG_DEF("t", "threads", 1,
                                                  "Max number of threads to use");
static const arg_def_t profile          = ARG_DEF(NULL, "profile", 1,
                                                  "Bitstream profile number to use");
static const arg_def_t width            = ARG_DEF("w", "width", 1,
                                                  "Frame width");
static const arg_def_t height           = ARG_DEF("h", "height", 1,
                                                  "Frame height");

#if CONFIG_WEBM_IO
static const struct arg_enum_list stereo_mode_enum[] = {
  {"mono", STEREO_FORMAT_MONO},
  {"left-right", STEREO_FORMAT_LEFT_RIGHT},
  {"bottom-top", STEREO_FORMAT_BOTTOM_TOP},
  {"top-bottom", STEREO_FORMAT_TOP_BOTTOM},
  {"right-left", STEREO_FORMAT_RIGHT_LEFT},
  {NULL, 0}
};
static const arg_def_t stereo_mode      = ARG_DEF_ENUM(NULL, "stereo-mode", 1,
                                                       "Stereo 3D video format", stereo_mode_enum);
#endif
static const arg_def_t timebase         = ARG_DEF(NULL, "timebase", 1,
                                                  "Output timestamp precision (fractional seconds)");
static const arg_def_t error_resilient  = ARG_DEF(NULL, "error-resilient", 1,
                                                  "Enable error resiliency features");
static const arg_def_t lag_in_frames    = ARG_DEF(NULL, "lag-in-frames", 1,
                                                  "Max number of frames to lag");

static const arg_def_t *global_args[] = {
  &use_yv12, &use_i420, &use_i422, &use_i444, &use_i440,
  &usage, &threads, &profile,
  &width, &height,
#if CONFIG_WEBM_IO
  &stereo_mode,
#endif
  &timebase, &framerate,
  &error_resilient,
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  &test16bitinternalarg,
#endif
  &lag_in_frames, NULL
};

static const arg_def_t dropframe_thresh   = ARG_DEF(NULL, "drop-frame", 1,
                                                    "Temporal resampling threshold (buf %)");
static const arg_def_t resize_allowed     = ARG_DEF(NULL, "resize-allowed", 1,
                                                    "Spatial resampling enabled (bool)");
static const arg_def_t resize_width       = ARG_DEF(NULL, "resize-width", 1,
                                                    "Width of encoded frame");
static const arg_def_t resize_height      = ARG_DEF(NULL, "resize-height", 1,
                                                    "Height of encoded frame");
static const arg_def_t resize_up_thresh   = ARG_DEF(NULL, "resize-up", 1,
                                                    "Upscale threshold (buf %)");
static const arg_def_t resize_down_thresh = ARG_DEF(NULL, "resize-down", 1,
                                                    "Downscale threshold (buf %)");
static const struct arg_enum_list end_usage_enum[] = {
  {"vbr", VPX_VBR},
  {"cbr", VPX_CBR},
  {"cq",  VPX_CQ},
  {"q",   VPX_Q},
  {NULL, 0}
};
static const arg_def_t end_usage          = ARG_DEF_ENUM(NULL, "end-usage", 1,
                                                         "Rate control mode", end_usage_enum);
static const arg_def_t target_bitrate     = ARG_DEF(NULL, "target-bitrate", 1,
                                                    "Bitrate (kbps)");
static const arg_def_t min_quantizer      = ARG_DEF(NULL, "min-q", 1,
                                                    "Minimum (best) quantizer");
static const arg_def_t max_quantizer      = ARG_DEF(NULL, "max-q", 1,
                                                    "Maximum (worst) quantizer");
static const arg_def_t undershoot_pct     = ARG_DEF(NULL, "undershoot-pct", 1,
                                                    "Datarate undershoot (min) target (%)");
static const arg_def_t overshoot_pct      = ARG_DEF(NULL, "overshoot-pct", 1,
                                                    "Datarate overshoot (max) target (%)");
static const arg_def_t buf_sz             = ARG_DEF(NULL, "buf-sz", 1,
                                                    "Client buffer size (ms)");
static const arg_def_t buf_initial_sz     = ARG_DEF(NULL, "buf-initial-sz", 1,
                                                    "Client initial buffer size (ms)");
static const arg_def_t buf_optimal_sz     = ARG_DEF(NULL, "buf-optimal-sz", 1,
                                                    "Client optimal buffer size (ms)");
static const arg_def_t *rc_args[] = {
  &dropframe_thresh, &resize_allowed, &resize_width, &resize_height,
  &resize_up_thresh, &resize_down_thresh, &end_usage, &target_bitrate,
  &min_quantizer, &max_quantizer, &undershoot_pct, &overshoot_pct, &buf_sz,
  &buf_initial_sz, &buf_optimal_sz, NULL
};


static const arg_def_t bias_pct = ARG_DEF(NULL, "bias-pct", 1,
                                          "CBR/VBR bias (0=CBR, 100=VBR)");
static const arg_def_t minsection_pct = ARG_DEF(NULL, "minsection-pct", 1,
                                                "GOP min bitrate (% of target)");
static const arg_def_t maxsection_pct = ARG_DEF(NULL, "maxsection-pct", 1,
                                                "GOP max bitrate (% of target)");
static const arg_def_t *rc_twopass_args[] = {
  &bias_pct, &minsection_pct, &maxsection_pct, NULL
};


static const arg_def_t kf_min_dist = ARG_DEF(NULL, "kf-min-dist", 1,
                                             "Minimum keyframe interval (frames)");
static const arg_def_t kf_max_dist = ARG_DEF(NULL, "kf-max-dist", 1,
                                             "Maximum keyframe interval (frames)");
static const arg_def_t kf_disabled = ARG_DEF(NULL, "disable-kf", 0,
                                             "Disable keyframe placement");
static const arg_def_t *kf_args[] = {
  &kf_min_dist, &kf_max_dist, &kf_disabled, NULL
};


static const arg_def_t noise_sens = ARG_DEF(NULL, "noise-sensitivity", 1,
                                            "Noise sensitivity (frames to blur)");
static const arg_def_t sharpness = ARG_DEF(NULL, "sharpness", 1,
                                           "Loop filter sharpness (0..7)");
static const arg_def_t static_thresh = ARG_DEF(NULL, "static-thresh", 1,
                                               "Motion detection threshold");
static const arg_def_t cpu_used = ARG_DEF(NULL, "cpu-used", 1,
                                          "CPU Used (-16..16)");
static const arg_def_t auto_altref = ARG_DEF(NULL, "auto-alt-ref", 1,
                                             "Enable automatic alt reference frames");
static const arg_def_t arnr_maxframes = ARG_DEF(NULL, "arnr-maxframes", 1,
                                                "AltRef max frames (0..15)");
static const arg_def_t arnr_strength = ARG_DEF(NULL, "arnr-strength", 1,
                                               "AltRef filter strength (0..6)");
static const arg_def_t arnr_type = ARG_DEF(NULL, "arnr-type", 1,
                                           "AltRef type");
static const struct arg_enum_list tuning_enum[] = {
  {"psnr", VP8_TUNE_PSNR},
  {"ssim", VP8_TUNE_SSIM},
  {NULL, 0}
};
static const arg_def_t tune_ssim = ARG_DEF_ENUM(NULL, "tune", 1,
                                                "Material to favor", tuning_enum);
static const arg_def_t cq_level = ARG_DEF(NULL, "cq-level", 1,
                                          "Constant/Constrained Quality level");
static const arg_def_t max_intra_rate_pct = ARG_DEF(NULL, "max-intra-rate", 1,
                                                    "Max I-frame bitrate (pct)");

#if CONFIG_VP8_ENCODER
static const arg_def_t token_parts =
    ARG_DEF(NULL, "token-parts", 1, "Number of token partitions to use, log2");
static const arg_def_t *vp8_args[] = {
  &cpu_used, &auto_altref, &noise_sens, &sharpness, &static_thresh,
  &token_parts, &arnr_maxframes, &arnr_strength, &arnr_type,
  &tune_ssim, &cq_level, &max_intra_rate_pct,
  NULL
};
static const int vp8_arg_ctrl_map[] = {
  VP8E_SET_CPUUSED, VP8E_SET_ENABLEAUTOALTREF,
  VP8E_SET_NOISE_SENSITIVITY, VP8E_SET_SHARPNESS, VP8E_SET_STATIC_THRESHOLD,
  VP8E_SET_TOKEN_PARTITIONS,
  VP8E_SET_ARNR_MAXFRAMES, VP8E_SET_ARNR_STRENGTH, VP8E_SET_ARNR_TYPE,
  VP8E_SET_TUNING, VP8E_SET_CQ_LEVEL, VP8E_SET_MAX_INTRA_BITRATE_PCT,
  0
};
#endif

#if CONFIG_VP9_ENCODER
static const struct arg_enum_list aq_mode_enum[] = {
  {"off", 0},
  {"variance", 1},
  {"complexity", 2},
  {"cyclic", 3},
  {NULL, 0}
};
static const arg_def_t tile_cols =
    ARG_DEF(NULL, "tile-columns", 1, "Number of tile columns to use, log2");
static const arg_def_t tile_rows =
    ARG_DEF(NULL, "tile-rows", 1, "Number of tile rows to use, log2");
static const arg_def_t lossless = ARG_DEF(NULL, "lossless", 1, "Lossless mode");
static const arg_def_t frame_parallel_decoding = ARG_DEF(
    NULL, "frame-parallel", 1, "Enable frame parallel decodability features");
static const arg_def_t aq_mode = ARG_DEF_ENUM(
    NULL, "aq-mode", 1, "Adaptive quantization mode", aq_mode_enum);
static const arg_def_t frame_periodic_boost = ARG_DEF(
    NULL, "frame-boost", 1,
    "Enable frame periodic boost (0: off (default), 1: on)");

<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
static const struct arg_enum_list bitdepth_enum[] = {
  {"8",  VPX_BITS_8},
  {"10", VPX_BITS_10},
  {"12", VPX_BITS_12},
  {NULL, 0}
};

static const arg_def_t bitdeptharg   = ARG_DEF_ENUM("b", "bit-depth", 1,
                                                    "Bit depth for codec "
                                                    "(8 for version <=1, "
                                                    "10 or 12 for version 2)",
                                                    bitdepth_enum);
static const arg_def_t inbitdeptharg = ARG_DEF(NULL, "input-bit-depth", 1,
                                               "Bit depth of input");
#endif

static const struct arg_enum_list tune_content_enum[] = {
  {"default", VP9E_CONTENT_DEFAULT},
  {"screen", VP9E_CONTENT_SCREEN},
  {NULL, 0}
};

static const arg_def_t tune_content = ARG_DEF_ENUM(
    NULL, "tune-content", 1, "Tune content type", tune_content_enum);

static const arg_def_t *vp9_args[] = {
  &cpu_used, &auto_altref, &sharpness, &static_thresh,
  &tile_cols, &tile_rows, &arnr_maxframes, &arnr_strength, &arnr_type,
  &tune_ssim, &cq_level, &max_intra_rate_pct, &lossless,
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
  &frame_parallel_decoding, &aq_mode, &frame_periodic_boost, &tune_content,
#if CONFIG_VP9_HIGH
=======
  &frame_parallel_decoding, &aq_mode, &frame_periodic_boost,
  &noise_sens, &tune_content,
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  &bitdeptharg, &inbitdeptharg,
#endif
  NULL
};
static const int vp9_arg_ctrl_map[] = {
  VP8E_SET_CPUUSED, VP8E_SET_ENABLEAUTOALTREF,
  VP8E_SET_SHARPNESS, VP8E_SET_STATIC_THRESHOLD,
  VP9E_SET_TILE_COLUMNS, VP9E_SET_TILE_ROWS,
  VP8E_SET_ARNR_MAXFRAMES, VP8E_SET_ARNR_STRENGTH, VP8E_SET_ARNR_TYPE,
  VP8E_SET_TUNING, VP8E_SET_CQ_LEVEL, VP8E_SET_MAX_INTRA_BITRATE_PCT,
  VP9E_SET_LOSSLESS, VP9E_SET_FRAME_PARALLEL_DECODING, VP9E_SET_AQ_MODE,
  VP9E_SET_FRAME_PERIODIC_BOOST, VP9E_SET_NOISE_SENSITIVITY,
  VP9E_SET_TUNE_CONTENT,
  0
};
#endif

static const arg_def_t *no_args[] = { NULL };

void usage_exit() {
  int i;

  fprintf(stderr, "Usage: %s <options> -o dst_filename src_filename \n",
          exec_name);

  fprintf(stderr, "\nOptions:\n");
  arg_show_usage(stderr, main_args);
  fprintf(stderr, "\nEncoder Global Options:\n");
  arg_show_usage(stderr, global_args);
  fprintf(stderr, "\nRate Control Options:\n");
  arg_show_usage(stderr, rc_args);
  fprintf(stderr, "\nTwopass Rate Control Options:\n");
  arg_show_usage(stderr, rc_twopass_args);
  fprintf(stderr, "\nKeyframe Placement Options:\n");
  arg_show_usage(stderr, kf_args);
#if CONFIG_VP8_ENCODER
  fprintf(stderr, "\nVP8 Specific Options:\n");
  arg_show_usage(stderr, vp8_args);
#endif
#if CONFIG_VP9_ENCODER
  fprintf(stderr, "\nVP9 Specific Options:\n");
  arg_show_usage(stderr, vp9_args);
#endif
  fprintf(stderr, "\nStream timebase (--timebase):\n"
          "  The desired precision of timestamps in the output, expressed\n"
          "  in fractional seconds. Default is 1/1000.\n");
  fprintf(stderr, "\nIncluded encoders:\n\n");

  for (i = 0; i < get_vpx_encoder_count(); ++i) {
    const VpxInterface *const encoder = get_vpx_encoder_by_index(i);
    fprintf(stderr, "    %-6s - %s\n",
            encoder->name, vpx_codec_iface_name(encoder->codec_interface()));
  }

  exit(EXIT_FAILURE);
}

#define mmin(a, b)  ((a) < (b) ? (a) : (b))

<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
static void find_mismatch_high(const vpx_image_t *const img1,
                               const vpx_image_t *const img2,
                               int yloc[4], int uloc[4], int vloc[4]) {
  uint16_t *plane1, *plane2;
  uint32_t stride1, stride2;
  const uint32_t bsize = 64;
  const uint32_t bsizey = bsize >> img1->y_chroma_shift;
  const uint32_t bsizex = bsize >> img1->x_chroma_shift;
  const uint32_t c_w =
      (img1->d_w + img1->x_chroma_shift) >> img1->x_chroma_shift;
  const uint32_t c_h =
      (img1->d_h + img1->y_chroma_shift) >> img1->y_chroma_shift;
  int match = 1;
  uint32_t i, j;
  yloc[0] = yloc[1] = yloc[2] = yloc[3] = -1;
  plane1 = (uint16_t*)img1->planes[VPX_PLANE_Y];
  plane2 = (uint16_t*)img2->planes[VPX_PLANE_Y];
  stride1 = img1->stride[VPX_PLANE_Y]/2;
  stride2 = img2->stride[VPX_PLANE_Y]/2;
  for (i = 0, match = 1; match && i < img1->d_h; i += bsize) {
    for (j = 0; match && j < img1->d_w; j += bsize) {
      int k, l;
      const int si = mmin(i + bsize, img1->d_h) - i;
      const int sj = mmin(j + bsize, img1->d_w) - j;
      for (k = 0; match && k < si; ++k) {
        for (l = 0; match && l < sj; ++l) {
          if (*(plane1 + (i + k) * stride1 + j + l) !=
              *(plane2 + (i + k) * stride2 + j + l)) {
            yloc[0] = i + k;
            yloc[1] = j + l;
            yloc[2] = *(plane1 + (i + k) * stride1 + j + l);
            yloc[3] = *(plane2 + (i + k) * stride2 + j + l);
            match = 0;
            break;
          }
        }
      }
    }
  }

  uloc[0] = uloc[1] = uloc[2] = uloc[3] = -1;
  plane1 = (uint16_t*)img1->planes[VPX_PLANE_U];
  plane2 = (uint16_t*)img2->planes[VPX_PLANE_U];
  stride1 = img1->stride[VPX_PLANE_U]/2;
  stride2 = img2->stride[VPX_PLANE_U]/2;
  for (i = 0, match = 1; match && i < c_h; i += bsizey) {
    for (j = 0; match && j < c_w; j += bsizex) {
      int k, l;
      const int si = mmin(i + bsizey, c_h - i);
      const int sj = mmin(j + bsizex, c_w - j);
      for (k = 0; match && k < si; ++k) {
        for (l = 0; match && l < sj; ++l) {
          if (*(plane1 + (i + k) * stride1 + j + l) !=
              *(plane2 + (i + k) * stride2 + j + l)) {
            uloc[0] = i + k;
            uloc[1] = j + l;
            uloc[2] = *(plane1 + (i + k) * stride1 + j + l);
            uloc[3] = *(plane2 + (i + k) * stride2 + j + l);
            match = 0;
            break;
          }
        }
      }
    }
  }

  vloc[0] = vloc[1] = vloc[2] = vloc[3] = -1;
  plane1 = (uint16_t*)img1->planes[VPX_PLANE_V];
  plane2 = (uint16_t*)img2->planes[VPX_PLANE_V];
  stride1 = img1->stride[VPX_PLANE_V]/2;
  stride2 = img2->stride[VPX_PLANE_V]/2;
  for (i = 0, match = 1; match && i < c_h; i += bsizey) {
    for (j = 0; match && j < c_w; j += bsizex) {
      int k, l;
      const int si = mmin(i + bsizey, c_h - i);
      const int sj = mmin(j + bsizex, c_w - j);
      for (k = 0; match && k < si; ++k) {
        for (l = 0; match && l < sj; ++l) {
          if (*(plane1 + (i + k) * stride1 + j + l) !=
              *(plane2 + (i + k) * stride2 + j + l)) {
            vloc[0] = i + k;
            vloc[1] = j + l;
            vloc[2] = *(plane1 + (i + k) * stride1 + j + l);
            vloc[3] = *(plane2 + (i + k) * stride2 + j + l);
            match = 0;
            break;
          }
        }
      }
    }
  }
}
#endif

static void find_mismatch(const vpx_image_t *const img1,
                          const vpx_image_t *const img2,
                          int yloc[4], int uloc[4], int vloc[4]) {
  const uint32_t bsize = 64;
  const uint32_t bsizey = bsize >> img1->y_chroma_shift;
  const uint32_t bsizex = bsize >> img1->x_chroma_shift;
  const uint32_t c_w =
      (img1->d_w + img1->x_chroma_shift) >> img1->x_chroma_shift;
  const uint32_t c_h =
      (img1->d_h + img1->y_chroma_shift) >> img1->y_chroma_shift;
  int match = 1;
  uint32_t i, j;
  yloc[0] = yloc[1] = yloc[2] = yloc[3] = -1;
  for (i = 0, match = 1; match && i < img1->d_h; i += bsize) {
    for (j = 0; match && j < img1->d_w; j += bsize) {
      int k, l;
      const int si = mmin(i + bsize, img1->d_h) - i;
      const int sj = mmin(j + bsize, img1->d_w) - j;
      for (k = 0; match && k < si; ++k) {
        for (l = 0; match && l < sj; ++l) {
          if (*(img1->planes[VPX_PLANE_Y] +
                (i + k) * img1->stride[VPX_PLANE_Y] + j + l) !=
              *(img2->planes[VPX_PLANE_Y] +
                (i + k) * img2->stride[VPX_PLANE_Y] + j + l)) {
            yloc[0] = i + k;
            yloc[1] = j + l;
            yloc[2] = *(img1->planes[VPX_PLANE_Y] +
                        (i + k) * img1->stride[VPX_PLANE_Y] + j + l);
            yloc[3] = *(img2->planes[VPX_PLANE_Y] +
                        (i + k) * img2->stride[VPX_PLANE_Y] + j + l);
            match = 0;
            break;
          }
        }
      }
    }
  }

  uloc[0] = uloc[1] = uloc[2] = uloc[3] = -1;
  for (i = 0, match = 1; match && i < c_h; i += bsizey) {
    for (j = 0; match && j < c_w; j += bsizex) {
      int k, l;
      const int si = mmin(i + bsizey, c_h - i);
      const int sj = mmin(j + bsizex, c_w - j);
      for (k = 0; match && k < si; ++k) {
        for (l = 0; match && l < sj; ++l) {
          if (*(img1->planes[VPX_PLANE_U] +
                (i + k) * img1->stride[VPX_PLANE_U] + j + l) !=
              *(img2->planes[VPX_PLANE_U] +
                (i + k) * img2->stride[VPX_PLANE_U] + j + l)) {
            uloc[0] = i + k;
            uloc[1] = j + l;
            uloc[2] = *(img1->planes[VPX_PLANE_U] +
                        (i + k) * img1->stride[VPX_PLANE_U] + j + l);
            uloc[3] = *(img2->planes[VPX_PLANE_U] +
                        (i + k) * img2->stride[VPX_PLANE_U] + j + l);
            match = 0;
            break;
          }
        }
      }
    }
  }
  vloc[0] = vloc[1] = vloc[2] = vloc[3] = -1;
  for (i = 0, match = 1; match && i < c_h; i += bsizey) {
    for (j = 0; match && j < c_w; j += bsizex) {
      int k, l;
      const int si = mmin(i + bsizey, c_h - i);
      const int sj = mmin(j + bsizex, c_w - j);
      for (k = 0; match && k < si; ++k) {
        for (l = 0; match && l < sj; ++l) {
          if (*(img1->planes[VPX_PLANE_V] +
                (i + k) * img1->stride[VPX_PLANE_V] + j + l) !=
              *(img2->planes[VPX_PLANE_V] +
                (i + k) * img2->stride[VPX_PLANE_V] + j + l)) {
            vloc[0] = i + k;
            vloc[1] = j + l;
            vloc[2] = *(img1->planes[VPX_PLANE_V] +
                        (i + k) * img1->stride[VPX_PLANE_V] + j + l);
            vloc[3] = *(img2->planes[VPX_PLANE_V] +
                        (i + k) * img2->stride[VPX_PLANE_V] + j + l);
            match = 0;
            break;
          }
        }
      }
    }
  }
}

static int compare_img(const vpx_image_t *const img1,
                       const vpx_image_t *const img2) {
  uint32_t l_w = img1->d_w;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
  const uint32_t l_h = img1->d_h;
=======
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  uint32_t c_w =
      (img1->d_w + img1->x_chroma_shift) >> img1->x_chroma_shift;
  const uint32_t c_h =
      (img1->d_h + img1->y_chroma_shift) >> img1->y_chroma_shift;
  uint32_t i;
  int match = 1;

  match &= (img1->fmt == img2->fmt);
  match &= (img1->d_w == img2->d_w);
  match &= (img1->d_h == img2->d_h);
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  if (img1->fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
    l_w *= 2;
    c_w *= 2;
  }
#endif

  for (i = 0; i < l_h; ++i)
    match &= (memcmp(img1->planes[VPX_PLANE_Y] + i * img1->stride[VPX_PLANE_Y],
                     img2->planes[VPX_PLANE_Y] + i * img2->stride[VPX_PLANE_Y],
                     l_w) == 0);

  for (i = 0; i < c_h; ++i)
    match &= (memcmp(img1->planes[VPX_PLANE_U] + i * img1->stride[VPX_PLANE_U],
                     img2->planes[VPX_PLANE_U] + i * img2->stride[VPX_PLANE_U],
                     c_w) == 0);

  for (i = 0; i < c_h; ++i)
    match &= (memcmp(img1->planes[VPX_PLANE_V] + i * img1->stride[VPX_PLANE_V],
                     img2->planes[VPX_PLANE_V] + i * img2->stride[VPX_PLANE_V],
                     c_w) == 0);

  return match;
}


#define NELEMENTS(x) (sizeof(x)/sizeof(x[0]))
#define MAX(x,y) ((x)>(y)?(x):(y))
#if CONFIG_VP8_ENCODER && !CONFIG_VP9_ENCODER
#define ARG_CTRL_CNT_MAX NELEMENTS(vp8_arg_ctrl_map)
#elif !CONFIG_VP8_ENCODER && CONFIG_VP9_ENCODER
#define ARG_CTRL_CNT_MAX NELEMENTS(vp9_arg_ctrl_map)
#else
#define ARG_CTRL_CNT_MAX MAX(NELEMENTS(vp8_arg_ctrl_map), \
                             NELEMENTS(vp9_arg_ctrl_map))
#endif

#if !CONFIG_WEBM_IO
typedef int stereo_format_t;
struct EbmlGlobal { int debug; };
#endif

/* Per-stream configuration */
struct stream_config {
  struct vpx_codec_enc_cfg  cfg;
  const char               *out_fn;
  const char               *stats_fn;
#if CONFIG_FP_MB_STATS
  const char               *fpmb_stats_fn;
#endif
  stereo_format_t           stereo_fmt;
  int                       arg_ctrls[ARG_CTRL_CNT_MAX][2];
  int                       arg_ctrl_cnt;
  int                       write_webm;
  int                       have_kf_max_dist;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  // whether to use 16bit internal buffers
  int                       use_16bit_internal;
#endif
};

struct stream_state {
  int                       index;
  struct stream_state      *next;
  struct stream_config      config;
  FILE                     *file;
  struct rate_hist         *rate_hist;
  struct EbmlGlobal         ebml;
  uint64_t                  psnr_sse_total;
  uint64_t                  psnr_samples_total;
  double                    psnr_totals[4];
  int                       psnr_count;
  int                       counts[64];
  vpx_codec_ctx_t           encoder;
  unsigned int              frames_out;
  uint64_t                  cx_time;
  size_t                    nbytes;
  stats_io_t                stats;
#if CONFIG_FP_MB_STATS
  stats_io_t                fpmb_stats;
#endif
  struct vpx_image         *img;
  vpx_codec_ctx_t           decoder;
  int                       mismatch_seen;
};

void validate_positive_rational(const char          *msg,
                                struct vpx_rational *rat) {
  if (rat->den < 0) {
    rat->num *= -1;
    rat->den *= -1;
  }

  if (rat->num < 0)
    die("Error: %s must be positive\n", msg);

  if (!rat->den)
    die("Error: %s has zero denominator\n", msg);
}


static void parse_global_config(struct VpxEncoderConfig *global, char **argv) {
  char       **argi, **argj;
  struct arg   arg;

  /* Initialize default parameters */
  memset(global, 0, sizeof(*global));
  global->codec = get_vpx_encoder_by_index(0);
  global->passes = 0;
  global->color_type = I420;
  /* Assign default deadline to good quality */
  global->deadline = VPX_DL_GOOD_QUALITY;

  for (argi = argj = argv; (*argj = *argi); argi += arg.argv_step) {
    arg.argv_step = 1;

    if (arg_match(&arg, &codecarg, argi)) {
      global->codec = get_vpx_encoder_by_name(arg.val);
      if (!global->codec)
        die("Error: Unrecognized argument (%s) to --codec\n", arg.val);
    } else if (arg_match(&arg, &passes, argi)) {
      global->passes = arg_parse_uint(&arg);

      if (global->passes < 1 || global->passes > 2)
        die("Error: Invalid number of passes (%d)\n", global->passes);
    } else if (arg_match(&arg, &pass_arg, argi)) {
      global->pass = arg_parse_uint(&arg);

      if (global->pass < 1 || global->pass > 2)
        die("Error: Invalid pass selected (%d)\n",
            global->pass);
    } else if (arg_match(&arg, &usage, argi))
      global->usage = arg_parse_uint(&arg);
    else if (arg_match(&arg, &deadline, argi))
      global->deadline = arg_parse_uint(&arg);
    else if (arg_match(&arg, &best_dl, argi))
      global->deadline = VPX_DL_BEST_QUALITY;
    else if (arg_match(&arg, &good_dl, argi))
      global->deadline = VPX_DL_GOOD_QUALITY;
    else if (arg_match(&arg, &rt_dl, argi))
      global->deadline = VPX_DL_REALTIME;
    else if (arg_match(&arg, &use_yv12, argi))
      global->color_type = YV12;
    else if (arg_match(&arg, &use_i420, argi))
      global->color_type = I420;
    else if (arg_match(&arg, &use_i422, argi))
      global->color_type = I422;
    else if (arg_match(&arg, &use_i444, argi))
      global->color_type = I444;
    else if (arg_match(&arg, &use_i440, argi))
      global->color_type = I440;
    else if (arg_match(&arg, &quietarg, argi))
      global->quiet = 1;
    else if (arg_match(&arg, &verbosearg, argi))
      global->verbose = 1;
    else if (arg_match(&arg, &limit, argi))
      global->limit = arg_parse_uint(&arg);
    else if (arg_match(&arg, &skip, argi))
      global->skip_frames = arg_parse_uint(&arg);
    else if (arg_match(&arg, &psnrarg, argi))
      global->show_psnr = 1;
    else if (arg_match(&arg, &recontest, argi))
      global->test_decode = arg_parse_enum_or_int(&arg);
    else if (arg_match(&arg, &framerate, argi)) {
      global->framerate = arg_parse_rational(&arg);
      validate_positive_rational(arg.name, &global->framerate);
      global->have_framerate = 1;
    } else if (arg_match(&arg, &out_part, argi))
      global->out_part = 1;
    else if (arg_match(&arg, &debugmode, argi))
      global->debug = 1;
    else if (arg_match(&arg, &q_hist_n, argi))
      global->show_q_hist_buckets = arg_parse_uint(&arg);
    else if (arg_match(&arg, &rate_hist_n, argi))
      global->show_rate_hist_buckets = arg_parse_uint(&arg);
    else if (arg_match(&arg, &disable_warnings, argi))
      global->disable_warnings = 1;
    else if (arg_match(&arg, &disable_warning_prompt, argi))
      global->disable_warning_prompt = 1;
    else
      argj++;
  }

  if (global->pass) {
    /* DWIM: Assume the user meant passes=2 if pass=2 is specified */
    if (global->pass > global->passes) {
      warn("Assuming --pass=%d implies --passes=%d\n",
           global->pass, global->pass);
      global->passes = global->pass;
    }
  }
  /* Validate global config */
  if (global->passes == 0) {
#if CONFIG_VP9_ENCODER
    // Make default VP9 passes = 2 until there is a better quality 1-pass
    // encoder
    if (global->codec != NULL && global->codec->name != NULL)
      global->passes = (strcmp(global->codec->name, "vp9") == 0 &&
                        global->deadline != VPX_DL_REALTIME) ? 2 : 1;
#else
    global->passes = 1;
#endif
  }

  if (global->deadline == VPX_DL_REALTIME &&
      global->passes > 1) {
    warn("Enforcing one-pass encoding in realtime mode\n");
    global->passes = 1;
  }
}


void open_input_file(struct VpxInputContext *input) {
  /* Parse certain options from the input file, if possible */
  input->file = strcmp(input->filename, "-")
      ? fopen(input->filename, "rb") : set_binary_mode(stdin);

  if (!input->file)
    fatal("Failed to open input file");

  if (!fseeko(input->file, 0, SEEK_END)) {
    /* Input file is seekable. Figure out how long it is, so we can get
     * progress info.
     */
    input->length = ftello(input->file);
    rewind(input->file);
  }

  /* For RAW input sources, these bytes will applied on the first frame
   *  in read_frame().
   */
  input->detect.buf_read = fread(input->detect.buf, 1, 4, input->file);
  input->detect.position = 0;

  if (input->detect.buf_read == 4
      && file_is_y4m(input->detect.buf)) {
    if (y4m_input_open(&input->y4m, input->file, input->detect.buf, 4,
                       input->only_i420) >= 0) {
      input->file_type = FILE_TYPE_Y4M;
      input->width = input->y4m.pic_w;
      input->height = input->y4m.pic_h;
      input->framerate.numerator = input->y4m.fps_n;
      input->framerate.denominator = input->y4m.fps_d;
      input->fmt = input->y4m.vpx_fmt;
      input->bit_depth = input->y4m.bit_depth;
    } else
      fatal("Unsupported Y4M stream.");
  } else if (input->detect.buf_read == 4 && fourcc_is_ivf(input->detect.buf)) {
    fatal("IVF is not supported as input.");
  } else {
    input->file_type = FILE_TYPE_RAW;
  }
}


static void close_input_file(struct VpxInputContext *input) {
  fclose(input->file);
  if (input->file_type == FILE_TYPE_Y4M)
    y4m_input_close(&input->y4m);
}

static struct stream_state *new_stream(struct VpxEncoderConfig *global,
                                       struct stream_state *prev) {
  struct stream_state *stream;

  stream = calloc(1, sizeof(*stream));
  if (stream == NULL) {
    fatal("Failed to allocate new stream.");
  }

  if (prev) {
    memcpy(stream, prev, sizeof(*stream));
    stream->index++;
    prev->next = stream;
  } else {
    vpx_codec_err_t  res;

    /* Populate encoder configuration */
    res = vpx_codec_enc_config_default(global->codec->codec_interface(),
                                       &stream->config.cfg,
                                       global->usage);
    if (res)
      fatal("Failed to get config: %s\n", vpx_codec_err_to_string(res));

    /* Change the default timebase to a high enough value so that the
     * encoder will always create strictly increasing timestamps.
     */
    stream->config.cfg.g_timebase.den = 1000;

    /* Never use the library's default resolution, require it be parsed
     * from the file or set on the command line.
     */
    stream->config.cfg.g_w = 0;
    stream->config.cfg.g_h = 0;
    stream->config.cfg.g_in_bit_depth = 0;

    /* Initialize remaining stream parameters */
    stream->config.write_webm = 1;
#if CONFIG_WEBM_IO
    stream->config.stereo_fmt = STEREO_FORMAT_MONO;
    stream->ebml.last_pts_ns = -1;
    stream->ebml.writer = NULL;
    stream->ebml.segment = NULL;
#endif

    /* Allows removal of the application version from the EBML tags */
    stream->ebml.debug = global->debug;

    /* Default lag_in_frames is 0 in realtime mode */
    if (global->deadline == VPX_DL_REALTIME)
      stream->config.cfg.g_lag_in_frames = 0;
  }

  /* Output files must be specified for each stream */
  stream->config.out_fn = NULL;

  stream->next = NULL;
  return stream;
}


static int parse_stream_params(struct VpxEncoderConfig *global,
                               struct stream_state  *stream,
                               char **argv) {
  char                   **argi, **argj;
  struct arg               arg;
  static const arg_def_t **ctrl_args = no_args;
  static const int        *ctrl_args_map = NULL;
  struct stream_config    *config = &stream->config;
  int                      eos_mark_found = 0;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  int                      test_16bit_internal = 0;
#endif

  // Handle codec specific options
  if (0) {
#if CONFIG_VP8_ENCODER
  } else if (strcmp(global->codec->name, "vp8") == 0) {
    ctrl_args = vp8_args;
    ctrl_args_map = vp8_arg_ctrl_map;
#endif
#if CONFIG_VP9_ENCODER
  } else if (strcmp(global->codec->name, "vp9") == 0) {
    ctrl_args = vp9_args;
    ctrl_args_map = vp9_arg_ctrl_map;
#endif
  }

  for (argi = argj = argv; (*argj = *argi); argi += arg.argv_step) {
    arg.argv_step = 1;

    /* Once we've found an end-of-stream marker (--) we want to continue
     * shifting arguments but not consuming them.
     */
    if (eos_mark_found) {
      argj++;
      continue;
    } else if (!strcmp(*argj, "--")) {
      eos_mark_found = 1;
      continue;
    }

    if (0) {
    } else if (arg_match(&arg, &outputfile, argi)) {
      config->out_fn = arg.val;
    } else if (arg_match(&arg, &fpf_name, argi)) {
      config->stats_fn = arg.val;
#if CONFIG_FP_MB_STATS
    } else if (arg_match(&arg, &fpmbf_name, argi)) {
      config->fpmb_stats_fn = arg.val;
#endif
    } else if (arg_match(&arg, &use_ivf, argi)) {
      config->write_webm = 0;
    } else if (arg_match(&arg, &threads, argi)) {
      config->cfg.g_threads = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &profile, argi)) {
      config->cfg.g_profile = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &width, argi)) {
      config->cfg.g_w = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &height, argi)) {
      config->cfg.g_h = arg_parse_uint(&arg);
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
    } else if (arg_match(&arg, &bitdeptharg, argi)) {
      config->cfg.g_bit_depth = arg_parse_enum(&arg);
    } else if (arg_match(&arg, &inbitdeptharg, argi)) {
      config->cfg.g_in_bit_depth = arg_parse_uint(&arg);
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
    } else if (arg_match(&arg, &bitdeptharg, argi)) {
      config->cfg.g_bit_depth = arg_parse_enum_or_int(&arg);
    } else if (arg_match(&arg, &inbitdeptharg, argi)) {
      config->cfg.g_input_bit_depth = arg_parse_uint(&arg);
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
#endif
#if CONFIG_WEBM_IO
    } else if (arg_match(&arg, &stereo_mode, argi)) {
      config->stereo_fmt = arg_parse_enum_or_int(&arg);
#endif
    } else if (arg_match(&arg, &timebase, argi)) {
      config->cfg.g_timebase = arg_parse_rational(&arg);
      validate_positive_rational(arg.name, &config->cfg.g_timebase);
    } else if (arg_match(&arg, &error_resilient, argi)) {
      config->cfg.g_error_resilient = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &lag_in_frames, argi)) {
      config->cfg.g_lag_in_frames = arg_parse_uint(&arg);
      if (global->deadline == VPX_DL_REALTIME &&
          config->cfg.g_lag_in_frames != 0) {
        warn("non-zero %s option ignored in realtime mode.\n", arg.name);
        config->cfg.g_lag_in_frames = 0;
      }
    } else if (arg_match(&arg, &dropframe_thresh, argi)) {
      config->cfg.rc_dropframe_thresh = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &resize_allowed, argi)) {
      config->cfg.rc_resize_allowed = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &resize_width, argi)) {
      config->cfg.rc_scaled_width = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &resize_height, argi)) {
      config->cfg.rc_scaled_height = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &resize_up_thresh, argi)) {
      config->cfg.rc_resize_up_thresh = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &resize_down_thresh, argi)) {
      config->cfg.rc_resize_down_thresh = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &end_usage, argi)) {
      config->cfg.rc_end_usage = arg_parse_enum_or_int(&arg);
    } else if (arg_match(&arg, &target_bitrate, argi)) {
      config->cfg.rc_target_bitrate = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &min_quantizer, argi)) {
      config->cfg.rc_min_quantizer = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &max_quantizer, argi)) {
      config->cfg.rc_max_quantizer = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &undershoot_pct, argi)) {
      config->cfg.rc_undershoot_pct = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &overshoot_pct, argi)) {
      config->cfg.rc_overshoot_pct = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &buf_sz, argi)) {
      config->cfg.rc_buf_sz = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &buf_initial_sz, argi)) {
      config->cfg.rc_buf_initial_sz = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &buf_optimal_sz, argi)) {
      config->cfg.rc_buf_optimal_sz = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &bias_pct, argi)) {
        config->cfg.rc_2pass_vbr_bias_pct = arg_parse_uint(&arg);
      if (global->passes < 2)
        warn("option %s ignored in one-pass mode.\n", arg.name);
    } else if (arg_match(&arg, &minsection_pct, argi)) {
      config->cfg.rc_2pass_vbr_minsection_pct = arg_parse_uint(&arg);

      if (global->passes < 2)
        warn("option %s ignored in one-pass mode.\n", arg.name);
    } else if (arg_match(&arg, &maxsection_pct, argi)) {
      config->cfg.rc_2pass_vbr_maxsection_pct = arg_parse_uint(&arg);

      if (global->passes < 2)
        warn("option %s ignored in one-pass mode.\n", arg.name);
    } else if (arg_match(&arg, &kf_min_dist, argi)) {
      config->cfg.kf_min_dist = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &kf_max_dist, argi)) {
      config->cfg.kf_max_dist = arg_parse_uint(&arg);
      config->have_kf_max_dist = 1;
    } else if (arg_match(&arg, &kf_disabled, argi)) {
      config->cfg.kf_mode = VPX_KF_DISABLED;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
    } else if (arg_match(&arg, &test16bitinternalarg, argi)) {
      if (strcmp(global->codec->name, "vp9") == 0) {
        test_16bit_internal = 1;
      }
#endif
    } else {
      int i, match = 0;
      for (i = 0; ctrl_args[i]; i++) {
        if (arg_match(&arg, ctrl_args[i], argi)) {
          int j;
          match = 1;

          /* Point either to the next free element or the first
          * instance of this control.
          */
          for (j = 0; j < config->arg_ctrl_cnt; j++)
            if (ctrl_args_map != NULL &&
                config->arg_ctrls[j][0] == ctrl_args_map[i])
              break;

          /* Update/insert */
          assert(j < (int)ARG_CTRL_CNT_MAX);
          if (ctrl_args_map != NULL && j < (int)ARG_CTRL_CNT_MAX) {
            config->arg_ctrls[j][0] = ctrl_args_map[i];
            config->arg_ctrls[j][1] = arg_parse_enum_or_int(&arg);
            if (j == config->arg_ctrl_cnt)
              config->arg_ctrl_cnt++;
          }
        }
      }
      if (!match)
        argj++;
    }
  }
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  if (strcmp(global->codec->name, "vp9") == 0) {
    config->use_16bit_internal = test_16bit_internal |
                                 (config->cfg.g_profile > 1);
  }
#endif
  return eos_mark_found;
}


#define FOREACH_STREAM(func) \
  do { \
    struct stream_state *stream; \
    for (stream = streams; stream; stream = stream->next) { \
      func; \
    } \
  } while (0)


static void validate_stream_config(const struct stream_state *stream,
                                   const struct VpxEncoderConfig *global) {
  const struct stream_state *streami;

  if (!stream->config.cfg.g_w || !stream->config.cfg.g_h)
    fatal("Stream %d: Specify stream dimensions with --width (-w) "
          " and --height (-h)", stream->index);

  // Check that the codec bit depth is greater than the input bit depth.
  if (stream->config.cfg.g_input_bit_depth >
      (unsigned int)stream->config.cfg.g_bit_depth) {
    fatal("Stream %d: codec bit depth (%d) less than input bit depth (%d)",
          stream->index, (int)stream->config.cfg.g_bit_depth,
          stream->config.cfg.g_input_bit_depth);
  }

  // Check that the codec bit depth is greater than the input bit depth
  if (stream->config.cfg.g_in_bit_depth >
      (unsigned int)stream->config.cfg.g_bit_depth) {
    fatal("Stream %d: codec bit depth (%d) less than input bit depth (%d)",
          stream->index, (unsigned int)stream->config.cfg.g_bit_depth,
          stream->config.cfg.g_in_bit_depth);
  }

  for (streami = stream; streami; streami = streami->next) {
    /* All streams require output files */
    if (!streami->config.out_fn)
      fatal("Stream %d: Output file is required (specify with -o)",
            streami->index);

    /* Check for two streams outputting to the same file */
    if (streami != stream) {
      const char *a = stream->config.out_fn;
      const char *b = streami->config.out_fn;
      if (!strcmp(a, b) && strcmp(a, "/dev/null") && strcmp(a, ":nul"))
        fatal("Stream %d: duplicate output file (from stream %d)",
              streami->index, stream->index);
    }

    /* Check for two streams sharing a stats file. */
    if (streami != stream) {
      const char *a = stream->config.stats_fn;
      const char *b = streami->config.stats_fn;
      if (a && b && !strcmp(a, b))
        fatal("Stream %d: duplicate stats file (from stream %d)",
              streami->index, stream->index);
    }

#if CONFIG_FP_MB_STATS
    /* Check for two streams sharing a mb stats file. */
    if (streami != stream) {
      const char *a = stream->config.fpmb_stats_fn;
      const char *b = streami->config.fpmb_stats_fn;
      if (a && b && !strcmp(a, b))
        fatal("Stream %d: duplicate mb stats file (from stream %d)",
              streami->index, stream->index);
    }
#endif
  }
}


static void set_stream_dimensions(struct stream_state *stream,
                                  unsigned int w,
                                  unsigned int h) {
  if (!stream->config.cfg.g_w) {
    if (!stream->config.cfg.g_h)
      stream->config.cfg.g_w = w;
    else
      stream->config.cfg.g_w = w * stream->config.cfg.g_h / h;
  }
  if (!stream->config.cfg.g_h) {
    stream->config.cfg.g_h = h * stream->config.cfg.g_w / w;
  }
}

static void set_default_kf_interval(struct stream_state *stream,
                                    struct VpxEncoderConfig *global) {
  /* Use a max keyframe interval of 5 seconds, if none was
   * specified on the command line.
   */
  if (!stream->config.have_kf_max_dist) {
    double framerate = (double)global->framerate.num / global->framerate.den;
    if (framerate > 0.0)
      stream->config.cfg.kf_max_dist = (unsigned int)(5.0 * framerate);
  }
}

static const char* file_type_to_string(enum VideoFileType t) {
  switch (t) {
    case FILE_TYPE_RAW: return "RAW";
    case FILE_TYPE_Y4M: return "Y4M";
    default: return "Other";
  }
}

static const char* image_format_to_string(vpx_img_fmt_t f) {
  switch (f) {
    case VPX_IMG_FMT_I420: return "I420";
    case VPX_IMG_FMT_I422: return "I422";
    case VPX_IMG_FMT_I444: return "I444";
    case VPX_IMG_FMT_I440: return "I440";
    case VPX_IMG_FMT_YV12: return "YV12";
    case VPX_IMG_FMT_I42016: return "I42016";
    case VPX_IMG_FMT_I42216: return "I42216";
    case VPX_IMG_FMT_I44416: return "I44416";
    case VPX_IMG_FMT_I44016: return "I44016";
    default: return "Other";
  }
}

static void show_stream_config(struct stream_state *stream,
                               struct VpxEncoderConfig *global,
                               struct VpxInputContext *input) {

#define SHOW(field) \
  fprintf(stderr, "    %-28s = %d\n", #field, stream->config.cfg.field)

  if (stream->index == 0) {
    fprintf(stderr, "Codec: %s\n",
            vpx_codec_iface_name(global->codec->codec_interface()));
    fprintf(stderr, "Source file: %s File Type: %s Format: %s\n",
            input->filename,
            file_type_to_string(input->file_type),
            image_format_to_string(input->fmt));
  }
  if (stream->next || stream->index)
    fprintf(stderr, "\nStream Index: %d\n", stream->index);
  fprintf(stderr, "Destination file: %s\n", stream->config.out_fn);
  fprintf(stderr, "Encoder parameters:\n");

  SHOW(g_usage);
  SHOW(g_threads);
  SHOW(g_profile);
  SHOW(g_w);
  SHOW(g_h);
  SHOW(g_bit_depth);
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
  SHOW(g_in_bit_depth);
=======
  SHOW(g_input_bit_depth);
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  SHOW(g_timebase.num);
  SHOW(g_timebase.den);
  SHOW(g_error_resilient);
  SHOW(g_pass);
  SHOW(g_lag_in_frames);
  SHOW(rc_dropframe_thresh);
  SHOW(rc_resize_allowed);
  SHOW(rc_scaled_width);
  SHOW(rc_scaled_height);
  SHOW(rc_resize_up_thresh);
  SHOW(rc_resize_down_thresh);
  SHOW(rc_end_usage);
  SHOW(rc_target_bitrate);
  SHOW(rc_min_quantizer);
  SHOW(rc_max_quantizer);
  SHOW(rc_undershoot_pct);
  SHOW(rc_overshoot_pct);
  SHOW(rc_buf_sz);
  SHOW(rc_buf_initial_sz);
  SHOW(rc_buf_optimal_sz);
  SHOW(rc_2pass_vbr_bias_pct);
  SHOW(rc_2pass_vbr_minsection_pct);
  SHOW(rc_2pass_vbr_maxsection_pct);
  SHOW(kf_mode);
  SHOW(kf_min_dist);
  SHOW(kf_max_dist);
}


static void open_output_file(struct stream_state *stream,
                             struct VpxEncoderConfig *global) {
  const char *fn = stream->config.out_fn;
  const struct vpx_codec_enc_cfg *const cfg = &stream->config.cfg;

  if (cfg->g_pass == VPX_RC_FIRST_PASS)
    return;

  stream->file = strcmp(fn, "-") ? fopen(fn, "wb") : set_binary_mode(stdout);

  if (!stream->file)
    fatal("Failed to open output file");

  if (stream->config.write_webm && fseek(stream->file, 0, SEEK_CUR))
    fatal("WebM output to pipes not supported.");

#if CONFIG_WEBM_IO
  if (stream->config.write_webm) {
    stream->ebml.stream = stream->file;
    write_webm_file_header(&stream->ebml, cfg,
                           &global->framerate,
                           stream->config.stereo_fmt,
                           global->codec->fourcc);
  }
#endif

  if (!stream->config.write_webm) {
    ivf_write_file_header(stream->file, cfg, global->codec->fourcc, 0);
  }
}


static void close_output_file(struct stream_state *stream,
                              unsigned int fourcc) {
  const struct vpx_codec_enc_cfg *const cfg = &stream->config.cfg;

  if (cfg->g_pass == VPX_RC_FIRST_PASS)
    return;

#if CONFIG_WEBM_IO
  if (stream->config.write_webm) {
    write_webm_file_footer(&stream->ebml);
  }
#endif

  if (!stream->config.write_webm) {
    if (!fseek(stream->file, 0, SEEK_SET))
      ivf_write_file_header(stream->file, &stream->config.cfg,
                            fourcc,
                            stream->frames_out);
  }

  fclose(stream->file);
}


static void setup_pass(struct stream_state *stream,
                       struct VpxEncoderConfig *global,
                       int pass) {
  if (stream->config.stats_fn) {
    if (!stats_open_file(&stream->stats, stream->config.stats_fn,
                         pass))
      fatal("Failed to open statistics store");
  } else {
    if (!stats_open_mem(&stream->stats, pass))
      fatal("Failed to open statistics store");
  }

#if CONFIG_FP_MB_STATS
  if (stream->config.fpmb_stats_fn) {
    if (!stats_open_file(&stream->fpmb_stats,
                         stream->config.fpmb_stats_fn, pass))
      fatal("Failed to open mb statistics store");
  } else {
    if (!stats_open_mem(&stream->fpmb_stats, pass))
      fatal("Failed to open mb statistics store");
  }
#endif

  stream->config.cfg.g_pass = global->passes == 2
                              ? pass ? VPX_RC_LAST_PASS : VPX_RC_FIRST_PASS
                            : VPX_RC_ONE_PASS;
  if (pass) {
    stream->config.cfg.rc_twopass_stats_in = stats_get(&stream->stats);
#if CONFIG_FP_MB_STATS
    stream->config.cfg.rc_firstpass_mb_stats_in =
        stats_get(&stream->fpmb_stats);
#endif
  }

  stream->cx_time = 0;
  stream->nbytes = 0;
  stream->frames_out = 0;
}


static void initialize_encoder(struct stream_state *stream,
                               struct VpxEncoderConfig *global) {
  int i;
  int flags = 0;

  flags |= global->show_psnr ? VPX_CODEC_USE_PSNR : 0;
  flags |= global->out_part ? VPX_CODEC_USE_OUTPUT_PARTITION : 0;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
  flags |= stream->config.use_16bit_internal ? VPX_CODEC_USE_HIGH : 0;
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
  flags |= stream->config.use_16bit_internal ? VPX_CODEC_USE_HIGHBITDEPTH : 0;
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
#endif

  /* Construct Encoder Context */
  vpx_codec_enc_init(&stream->encoder, global->codec->codec_interface(),
                     &stream->config.cfg, flags);
  ctx_exit_on_error(&stream->encoder, "Failed to initialize encoder");

  /* Note that we bypass the vpx_codec_control wrapper macro because
   * we're being clever to store the control IDs in an array. Real
   * applications will want to make use of the enumerations directly
   */
  for (i = 0; i < stream->config.arg_ctrl_cnt; i++) {
    int ctrl = stream->config.arg_ctrls[i][0];
    int value = stream->config.arg_ctrls[i][1];
    if (vpx_codec_control_(&stream->encoder, ctrl, value))
      fprintf(stderr, "Error: Tried to set control %d = %d\n",
              ctrl, value);

    ctx_exit_on_error(&stream->encoder, "Failed to control codec");
  }

#if CONFIG_DECODERS
  if (global->test_decode != TEST_DECODE_OFF) {
    const VpxInterface *decoder = get_vpx_decoder_by_name(global->codec->name);
    vpx_codec_dec_init(&stream->decoder, decoder->codec_interface(), NULL, 0);
  }
#endif
}


static void encode_frame(struct stream_state *stream,
                         struct VpxEncoderConfig *global,
                         struct vpx_image *img,
                         unsigned int frames_in) {
  vpx_codec_pts_t frame_start, next_frame_start;
  struct vpx_codec_enc_cfg *cfg = &stream->config.cfg;
  struct vpx_usec_timer timer;

  frame_start = (cfg->g_timebase.den * (int64_t)(frames_in - 1)
                 * global->framerate.den)
                / cfg->g_timebase.num / global->framerate.num;
  next_frame_start = (cfg->g_timebase.den * (int64_t)(frames_in)
                      * global->framerate.den) /
                     cfg->g_timebase.num / global->framerate.num;

  /* Scale if necessary */
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
  if (img) {
    if ((img->fmt & VPX_IMG_FMT_HIGHBITDEPTH) &&
        (img->d_w != cfg->g_w || img->d_h != cfg->g_h)) {
      if (img->fmt != VPX_IMG_FMT_I42016) {
        fprintf(stderr, "%s can only scale 4:2:0 inputs\n", exec_name);
        exit(EXIT_FAILURE);
      }
#if CONFIG_LIBYUV
      if (!stream->img)
        stream->img = vpx_img_alloc(NULL, VPX_IMG_FMT_I42016,
                                    cfg->g_w, cfg->g_h, 16);
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
  if (img) {
    if ((img->fmt & VPX_IMG_FMT_HIGHBITDEPTH) &&
        (img->d_w != cfg->g_w || img->d_h != cfg->g_h)) {
      if (img->fmt != VPX_IMG_FMT_I42016) {
        fprintf(stderr, "%s can only scale 4:2:0 inputs\n", exec_name);
        exit(EXIT_FAILURE);
      }
#if CONFIG_LIBYUV
      if (!stream->img) {
        stream->img = vpx_img_alloc(NULL, VPX_IMG_FMT_I42016,
                                    cfg->g_w, cfg->g_h, 16);
      }
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
      I420Scale_16((uint16*)img->planes[VPX_PLANE_Y],
                   img->stride[VPX_PLANE_Y]/2,
                   (uint16*)img->planes[VPX_PLANE_U],
                   img->stride[VPX_PLANE_U]/2,
                   (uint16*)img->planes[VPX_PLANE_V],
                   img->stride[VPX_PLANE_V]/2,
                   img->d_w, img->d_h,
                   (uint16*)stream->img->planes[VPX_PLANE_Y],
                   stream->img->stride[VPX_PLANE_Y]/2,
                   (uint16*)stream->img->planes[VPX_PLANE_U],
                   stream->img->stride[VPX_PLANE_U]/2,
                   (uint16*)stream->img->planes[VPX_PLANE_V],
                   stream->img->stride[VPX_PLANE_V]/2,
                   stream->img->d_w, stream->img->d_h,
                   kFilterBox);
      img = stream->img;
#else
    stream->encoder.err = 1;
    ctx_exit_on_error(&stream->encoder,
                      "Stream %d: Failed to encode frame.\n"
                      "Scaling disabled in this configuration. \n"
                      "To enable, configure with --enable-libyuv\n",
                      stream->index);
#endif
    }
  }
#endif
  if (img && (img->d_w != cfg->g_w || img->d_h != cfg->g_h)) {
    if (img->fmt != VPX_IMG_FMT_I420 && img->fmt != VPX_IMG_FMT_YV12) {
      fprintf(stderr, "%s can only scale 4:2:0 8bpp inputs\n", exec_name);
      exit(EXIT_FAILURE);
    }
#if CONFIG_LIBYUV
    if (!stream->img)
      stream->img = vpx_img_alloc(NULL, VPX_IMG_FMT_I420,
                                  cfg->g_w, cfg->g_h, 16);
    I420Scale(img->planes[VPX_PLANE_Y], img->stride[VPX_PLANE_Y],
              img->planes[VPX_PLANE_U], img->stride[VPX_PLANE_U],
              img->planes[VPX_PLANE_V], img->stride[VPX_PLANE_V],
              img->d_w, img->d_h,
              stream->img->planes[VPX_PLANE_Y],
              stream->img->stride[VPX_PLANE_Y],
              stream->img->planes[VPX_PLANE_U],
              stream->img->stride[VPX_PLANE_U],
              stream->img->planes[VPX_PLANE_V],
              stream->img->stride[VPX_PLANE_V],
              stream->img->d_w, stream->img->d_h,
              kFilterBox);
    img = stream->img;
#else
    stream->encoder.err = 1;
    ctx_exit_on_error(&stream->encoder,
                      "Stream %d: Failed to encode frame.\n"
                      "Scaling disabled in this configuration. \n"
                      "To enable, configure with --enable-libyuv\n",
                      stream->index);
#endif
  }

  vpx_usec_timer_start(&timer);
  vpx_codec_encode(&stream->encoder, img, frame_start,
                   (unsigned long)(next_frame_start - frame_start),
                   0, global->deadline);
  vpx_usec_timer_mark(&timer);
  stream->cx_time += vpx_usec_timer_elapsed(&timer);
  ctx_exit_on_error(&stream->encoder, "Stream %d: Failed to encode frame",
                    stream->index);
}


static void update_quantizer_histogram(struct stream_state *stream) {
  if (stream->config.cfg.g_pass != VPX_RC_FIRST_PASS) {
    int q;

    vpx_codec_control(&stream->encoder, VP8E_GET_LAST_QUANTIZER_64, &q);
    ctx_exit_on_error(&stream->encoder, "Failed to read quantizer");
    stream->counts[q]++;
  }
}


static void get_cx_data(struct stream_state *stream,
                        struct VpxEncoderConfig *global,
                        int *got_data) {
  const vpx_codec_cx_pkt_t *pkt;
  const struct vpx_codec_enc_cfg *cfg = &stream->config.cfg;
  vpx_codec_iter_t iter = NULL;

  *got_data = 0;
  while ((pkt = vpx_codec_get_cx_data(&stream->encoder, &iter))) {
    static size_t fsize = 0;
    static int64_t ivf_header_pos = 0;

    switch (pkt->kind) {
      case VPX_CODEC_CX_FRAME_PKT:
        if (!(pkt->data.frame.flags & VPX_FRAME_IS_FRAGMENT)) {
          stream->frames_out++;
        }
        if (!global->quiet)
          fprintf(stderr, " %6luF", (unsigned long)pkt->data.frame.sz);

        update_rate_histogram(stream->rate_hist, cfg, pkt);
#if CONFIG_WEBM_IO
        if (stream->config.write_webm) {
          write_webm_block(&stream->ebml, cfg, pkt);
        }
#endif
        if (!stream->config.write_webm) {
          if (pkt->data.frame.partition_id <= 0) {
            ivf_header_pos = ftello(stream->file);
            fsize = pkt->data.frame.sz;

            ivf_write_frame_header(stream->file, pkt->data.frame.pts, fsize);
          } else {
            fsize += pkt->data.frame.sz;

            if (!(pkt->data.frame.flags & VPX_FRAME_IS_FRAGMENT)) {
              const int64_t currpos = ftello(stream->file);
              fseeko(stream->file, ivf_header_pos, SEEK_SET);
              ivf_write_frame_size(stream->file, fsize);
              fseeko(stream->file, currpos, SEEK_SET);
            }
          }

          (void) fwrite(pkt->data.frame.buf, 1, pkt->data.frame.sz,
                        stream->file);
        }
        stream->nbytes += pkt->data.raw.sz;

        *got_data = 1;
#if CONFIG_DECODERS
        if (global->test_decode != TEST_DECODE_OFF && !stream->mismatch_seen) {
          vpx_codec_decode(&stream->decoder, pkt->data.frame.buf,
                           (unsigned int)pkt->data.frame.sz, NULL, 0);
          if (stream->decoder.err) {
            warn_or_exit_on_error(&stream->decoder,
                                  global->test_decode == TEST_DECODE_FATAL,
                                  "Failed to decode frame %d in stream %d",
                                  stream->frames_out + 1, stream->index);
            stream->mismatch_seen = stream->frames_out + 1;
          }
        }
#endif
        break;
      case VPX_CODEC_STATS_PKT:
        stream->frames_out++;
        stats_write(&stream->stats,
                    pkt->data.twopass_stats.buf,
                    pkt->data.twopass_stats.sz);
        stream->nbytes += pkt->data.raw.sz;
        break;
#if CONFIG_FP_MB_STATS
      case VPX_CODEC_FPMB_STATS_PKT:
        stats_write(&stream->fpmb_stats,
                    pkt->data.firstpass_mb_stats.buf,
                    pkt->data.firstpass_mb_stats.sz);
        stream->nbytes += pkt->data.raw.sz;
        break;
#endif
      case VPX_CODEC_PSNR_PKT:

        if (global->show_psnr) {
          int i;

          stream->psnr_sse_total += pkt->data.psnr.sse[0];
          stream->psnr_samples_total += pkt->data.psnr.samples[0];
          for (i = 0; i < 4; i++) {
            if (!global->quiet)
              fprintf(stderr, "%.3f ", pkt->data.psnr.psnr[i]);
            stream->psnr_totals[i] += pkt->data.psnr.psnr[i];
          }
          stream->psnr_count++;
        }

        break;
      default:
        break;
    }
  }
}


<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
static void show_psnr(struct stream_state *stream, double peak) {
=======
static void show_psnr(struct stream_state  *stream, double peak) {
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  int i;
  double ovpsnr;

  if (!stream->psnr_count)
    return;

  fprintf(stderr, "Stream %d PSNR (Overall/Avg/Y/U/V)", stream->index);
  ovpsnr = sse_to_psnr((double)stream->psnr_samples_total, peak,
                       (double)stream->psnr_sse_total);
  fprintf(stderr, " %.3f", ovpsnr);

  for (i = 0; i < 4; i++) {
    fprintf(stderr, " %.3f", stream->psnr_totals[i] / stream->psnr_count);
  }
  fprintf(stderr, "\n");
}


static float usec_to_fps(uint64_t usec, unsigned int frames) {
  return (float)(usec > 0 ? frames * 1000000.0 / (float)usec : 0);
}

<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
static void high_img_upshift(vpx_image_t *dst, vpx_image_t *src,
                             int input_shift) {
  // Note the offset is 1 less than half
  const int offset = input_shift > 0 ? (1 << (input_shift - 1)) - 1 : 0;
  int plane;
  if (dst->w != src->w || dst->h != src->h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift ||
      dst->fmt != src->fmt || input_shift < 0) {
    fatal("Unsupported image conversion");
  }
  switch (src->fmt) {
    case VPX_IMG_FMT_I42016:
    case VPX_IMG_FMT_I42216:
    case VPX_IMG_FMT_I44416:
      break;
    default:
      fatal("Unsupported image conversion");
      break;
  }
  for (plane = 0; plane < 3; plane++) {
    int w = src->w;
    int h = src->h;
    int x, y;
    if (plane) {
      w >>= src->x_chroma_shift;
      h >>= src->y_chroma_shift;
    }
    for (y = 0; y < h; y++) {
      uint16_t *p_src = (uint16_t *)(src->planes[plane] +
                                     y * src->stride[plane]);
      uint16_t *p_dst = (uint16_t *)(dst->planes[plane] +
                                     y * dst->stride[plane]);
      for (x = 0; x < w; x++)
        *p_dst++ = (*p_src++ << input_shift) + offset;
    }
  }
}

static void low_img_upshift(vpx_image_t *dst, vpx_image_t *src,
                            int input_shift) {
  // Note the offset is 1 less than half
  const int offset = input_shift > 0 ? (1 << (input_shift - 1)) - 1 : 0;
  int plane;
  if (dst->w != src->w || dst->h != src->h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift ||
      dst->fmt != src->fmt + VPX_IMG_FMT_HIGHBITDEPTH ||
      input_shift < 0) {
    fatal("Unsupported image conversion");
  }
  switch (src->fmt) {
    case VPX_IMG_FMT_I420:
    case VPX_IMG_FMT_I422:
    case VPX_IMG_FMT_I444:
      break;
    default:
      fatal("Unsupported image conversion");
      break;
  }
  for (plane = 0; plane < 3; plane++) {
    int w = src->w;
    int h = src->h;
    int x, y;
    if (plane) {
      w >>= src->x_chroma_shift;
      h >>= src->y_chroma_shift;
    }
    for (y = 0; y < h; y++) {
      uint8_t *p_src = src->planes[plane] + y * src->stride[plane];
      uint16_t *p_dst = (uint16_t *)(dst->planes[plane] +
                                     y * dst->stride[plane]);
      for (x = 0; x < w; x++) {
        *p_dst++ = (*p_src++ << input_shift) + offset;
      }
    }
  }
}

static void img_upshift(vpx_image_t *dst, vpx_image_t *src,
                        int input_shift) {
  if (src->fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
    high_img_upshift(dst, src, input_shift);
  } else {
    low_img_upshift(dst, src, input_shift);
  }
}

static void img_cast_16_to_8(vpx_image_t *dst, vpx_image_t *src) {
  int plane;
  if (dst->fmt + VPX_IMG_FMT_HIGHBITDEPTH != src->fmt ||
      dst->d_w != src->d_w || dst->d_h != src->d_h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift) {
    fatal("Unsupported image conversion");
  }
  switch (dst->fmt) {
    case VPX_IMG_FMT_I420:
    case VPX_IMG_FMT_I422:
    case VPX_IMG_FMT_I444:
      break;
    default:
      fatal("Unsupported image conversion");
      break;
  }
  for (plane = 0; plane < 3; plane++) {
    int w = src->d_w;
    int h = src->d_h;
    int x, y;
    if (plane) {
      w >>= src->x_chroma_shift;
      h >>= src->y_chroma_shift;
    }
    for (y = 0; y < h; y++) {
      uint16_t *p_src = (uint16_t *)(src->planes[plane] +
                                     y * src->stride[plane]);
      uint8_t *p_dst = dst->planes[plane] + y * dst->stride[plane];
      for (x = 0; x < w; x++) {
        *p_dst++ = *p_src++;
      }
    }
  }
}
#endif

=======
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
static void test_decode(struct stream_state  *stream,
                        enum TestDecodeFatality fatal,
                        const VpxInterface *codec) {
  vpx_image_t enc_img, dec_img;

  if (stream->mismatch_seen)
    return;

  /* Get the internal reference frame */
  if (strcmp(codec->name, "vp8") == 0) {
    struct vpx_ref_frame ref_enc, ref_dec;
    int width, height;

    width = (stream->config.cfg.g_w + 15) & ~15;
    height = (stream->config.cfg.g_h + 15) & ~15;
    vpx_img_alloc(&ref_enc.img, VPX_IMG_FMT_I420, width, height, 1);
    enc_img = ref_enc.img;
    vpx_img_alloc(&ref_dec.img, VPX_IMG_FMT_I420, width, height, 1);
    dec_img = ref_dec.img;

    ref_enc.frame_type = VP8_LAST_FRAME;
    ref_dec.frame_type = VP8_LAST_FRAME;
    vpx_codec_control(&stream->encoder, VP8_COPY_REFERENCE, &ref_enc);
    vpx_codec_control(&stream->decoder, VP8_COPY_REFERENCE, &ref_dec);
  } else {
    struct vp9_ref_frame ref_enc, ref_dec;

    ref_enc.idx = 0;
    ref_dec.idx = 0;
    vpx_codec_control(&stream->encoder, VP9_GET_REFERENCE, &ref_enc);
    enc_img = ref_enc.img;
    vpx_codec_control(&stream->decoder, VP9_GET_REFERENCE, &ref_dec);
    dec_img = ref_dec.img;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
    if ((enc_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) != (dec_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH)) {
      if (enc_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
        vpx_img_alloc(&enc_img, enc_img.fmt - VPX_IMG_FMT_HIGHBITDEPTH, enc_img.d_w,
                      enc_img.d_h, 16);
        img_cast_16_to_8(&enc_img, &ref_enc.img);
      }
      if (dec_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
        vpx_img_alloc(&dec_img, dec_img.fmt - VPX_IMG_FMT_HIGHBITDEPTH, dec_img.d_w,
                      dec_img.d_h, 16);
        img_cast_16_to_8(&dec_img, &ref_dec.img);
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
    if ((enc_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) !=
        (dec_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH)) {
      if (enc_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
        vpx_img_alloc(&enc_img, enc_img.fmt - VPX_IMG_FMT_HIGHBITDEPTH,
                      enc_img.d_w, enc_img.d_h, 16);
        vpx_img_truncate_16_to_8(&enc_img, &ref_enc.img);
      }
      if (dec_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
        vpx_img_alloc(&dec_img, dec_img.fmt - VPX_IMG_FMT_HIGHBITDEPTH,
                      dec_img.d_w, dec_img.d_h, 16);
        vpx_img_truncate_16_to_8(&dec_img, &ref_dec.img);
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
      }
    }
#endif
  }
  ctx_exit_on_error(&stream->encoder, "Failed to get encoder reference frame");
  ctx_exit_on_error(&stream->decoder, "Failed to get decoder reference frame");

  if (!compare_img(&enc_img, &dec_img)) {
    int y[4], u[4], v[4];
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
    if (enc_img.fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
      find_mismatch_high(&enc_img, &dec_img, y, u, v);
    } else {
      find_mismatch(&enc_img, &dec_img, y, u, v);
    }
#else
    find_mismatch(&enc_img, &dec_img, y, u, v);
#endif
    stream->decoder.err = 1;
    warn_or_exit_on_error(&stream->decoder, fatal == TEST_DECODE_FATAL,
                          "Stream %d: Encode/decode mismatch on frame %d at"
                          " Y[%d, %d] {%d/%d},"
                          " U[%d, %d] {%d/%d},"
                          " V[%d, %d] {%d/%d}",
                          stream->index, stream->frames_out,
                          y[0], y[1], y[2], y[3],
                          u[0], u[1], u[2], u[3],
                          v[0], v[1], v[2], v[3]);
    stream->mismatch_seen = stream->frames_out;
  }

  vpx_img_free(&enc_img);
  vpx_img_free(&dec_img);
}


static void print_time(const char *label, int64_t etl) {
  int64_t hours;
  int64_t mins;
  int64_t secs;

  if (etl >= 0) {
    hours = etl / 3600;
    etl -= hours * 3600;
    mins = etl / 60;
    etl -= mins * 60;
    secs = etl;

    fprintf(stderr, "[%3s %2"PRId64":%02"PRId64":%02"PRId64"] ",
            label, hours, mins, secs);
  } else {
    fprintf(stderr, "[%3s  unknown] ", label);
  }
}

int main(int argc, const char **argv_) {
  int pass;
  vpx_image_t raw;
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  vpx_image_t raw_shift;
  int allocated_raw_shift = 0;
  int use_16bit_internal = 0;
  int input_shift = 0;
#endif
  int frame_avail, got_data;

  struct VpxInputContext input;
  struct VpxEncoderConfig global;
  struct stream_state *streams = NULL;
  char **argv, **argi;
  uint64_t cx_time = 0;
  int stream_cnt = 0;
  int res = 0;

  memset(&input, 0, sizeof(input));
  exec_name = argv_[0];

  if (argc < 3)
    usage_exit();

  /* Setup default input stream settings */
  input.framerate.numerator = 30;
  input.framerate.denominator = 1;
  input.only_i420 = 1;
  input.bit_depth = 0;

  /* First parse the global configuration values, because we want to apply
   * other parameters on top of the default configuration provided by the
   * codec.
   */
  argv = argv_dup(argc - 1, argv_ + 1);
  parse_global_config(&global, argv);

  switch (global.color_type) {
    case I420:
      input.fmt = VPX_IMG_FMT_I420;
      break;
    case I422:
      input.fmt = VPX_IMG_FMT_I422;
      break;
    case I444:
      input.fmt = VPX_IMG_FMT_I444;
      break;
    case I440:
      input.fmt = VPX_IMG_FMT_I440;
      break;
    case YV12:
      input.fmt = VPX_IMG_FMT_YV12;
      break;
  }

  {
    /* Now parse each stream's parameters. Using a local scope here
     * due to the use of 'stream' as loop variable in FOREACH_STREAM
     * loops
     */
    struct stream_state *stream = NULL;

    do {
      stream = new_stream(&global, stream);
      stream_cnt++;
      if (!streams)
        streams = stream;
    } while (parse_stream_params(&global, stream, argv));
  }

  /* Check for unrecognized options */
  for (argi = argv; *argi; argi++)
    if (argi[0][0] == '-' && argi[0][1])
      die("Error: Unrecognized option %s\n", *argi);

  FOREACH_STREAM(check_encoder_config(global.disable_warning_prompt,
                                      &global, &stream->config.cfg););

  /* Handle non-option arguments */
  input.filename = argv[0];

  if (!input.filename)
    usage_exit();

  /* Decide if other chroma subsamplings than 4:2:0 are supported */
  if (global.codec->fourcc == VP9_FOURCC)
    input.only_i420 = 0;

  for (pass = global.pass ? global.pass - 1 : 0; pass < global.passes; pass++) {
    int frames_in = 0, seen_frames = 0;
    int64_t estimated_time_left = -1;
    int64_t average_rate = -1;
    int64_t lagged_count = 0;

    open_input_file(&input);

    /* If the input file doesn't specify its w/h (raw files), try to get
     * the data from the first stream's configuration.
     */
    if (!input.width || !input.height)
      FOREACH_STREAM({
      if (stream->config.cfg.g_w && stream->config.cfg.g_h) {
        input.width = stream->config.cfg.g_w;
        input.height = stream->config.cfg.g_h;
        break;
      }
    });

    /* Update stream configurations from the input file's parameters */
    if (!input.width || !input.height)
      fatal("Specify stream dimensions with --width (-w) "
            " and --height (-h)");

    /* If input file does not specify bit-depth but input-bit-depth parameter
     * exists, assume that to be the input bit-depth. However, if the
     * input-bit-depth paramter does not exist, assume the input bit-depth
     * to be the same as the codec bit-depth.
     */
    if (!input.bit_depth) {
      FOREACH_STREAM({
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
        if (stream->config.cfg.g_in_bit_depth)
          input.bit_depth = stream->config.cfg.g_in_bit_depth;
        else
          input.bit_depth = stream->config.cfg.g_in_bit_depth =
              (unsigned int)stream->config.cfg.g_bit_depth;
      });
      if (input.bit_depth > 8) input.fmt |= VPX_IMG_FMT_HIGHBITDEPTH;
    } else {
      FOREACH_STREAM({
        stream->config.cfg.g_in_bit_depth = input.bit_depth;
=======
        if (stream->config.cfg.g_input_bit_depth)
          input.bit_depth = stream->config.cfg.g_input_bit_depth;
        else
          input.bit_depth = stream->config.cfg.g_input_bit_depth =
              (int)stream->config.cfg.g_bit_depth;
      });
      if (input.bit_depth > 8) input.fmt |= VPX_IMG_FMT_HIGHBITDEPTH;
    } else {
      FOREACH_STREAM({
        stream->config.cfg.g_input_bit_depth = input.bit_depth;
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
      });
    }

    FOREACH_STREAM(set_stream_dimensions(stream, input.width, input.height));
    FOREACH_STREAM(validate_stream_config(stream, &global));

    /* Ensure that --passes and --pass are consistent. If --pass is set and
     * --passes=2, ensure --fpf was set.
     */
    if (global.pass && global.passes == 2)
      FOREACH_STREAM( {
      if (!stream->config.stats_fn)
        die("Stream %d: Must specify --fpf when --pass=%d"
        " and --passes=2\n", stream->index, global.pass);
    });

#if !CONFIG_WEBM_IO
    FOREACH_STREAM({
      stream->config.write_webm = 0;
      warn("vpxenc was compiled without WebM container support."
           "Producing IVF output");
    });
#endif

    /* Use the frame rate from the file only if none was specified
     * on the command-line.
     */
    if (!global.have_framerate) {
      global.framerate.num = input.framerate.numerator;
      global.framerate.den = input.framerate.denominator;
    }

    FOREACH_STREAM(set_default_kf_interval(stream, &global));

    if (pass == (global.pass ? global.pass - 1 : 0)) {
      if (input.file_type == FILE_TYPE_Y4M)
        /*The Y4M reader does its own allocation.
          Just initialize this here to avoid problems if we never read any
           frames.*/
        memset(&raw, 0, sizeof(raw));
      else
        vpx_img_alloc(&raw, input.fmt, input.width, input.height, 32);

      FOREACH_STREAM(stream->rate_hist =
                     init_rate_histogram(&stream->config.cfg,
                                         &global.framerate));
    }

    FOREACH_STREAM(setup_pass(stream, &global, pass));
    FOREACH_STREAM(open_output_file(stream, &global));
    FOREACH_STREAM(initialize_encoder(stream, &global));

<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
    /* Show configuration */
    if (global.verbose && pass == 0)
      FOREACH_STREAM(show_stream_config(stream, &global, &input));

#if CONFIG_VP9_HIGH
    if (strcmp(global.codec->name, "vp9") == 0) {
      // Check to see if at least one stream uses 16 bit internal.
      // Currently assume that the bit_depths for all streams using
      // highbitdepth are the same.
      FOREACH_STREAM({
        if (stream->config.use_16bit_internal) {
          use_16bit_internal = 1;
        }
        if (stream->config.cfg.g_profile == 0) {
          input_shift = 0;
        } else {
          input_shift = (unsigned int)stream->config.cfg.g_bit_depth -
              stream->config.cfg.g_in_bit_depth;
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
    if (strcmp(global.codec->name, "vp9") == 0) {
      // Check to see if at least one stream uses 16 bit internal.
      // Currently assume that the bit_depths for all streams using
      // highbitdepth are the same.
      FOREACH_STREAM({
        if (stream->config.use_16bit_internal) {
          use_16bit_internal = 1;
        }
        if (stream->config.cfg.g_profile == 0) {
          input_shift = 0;
        } else {
          input_shift = (int)stream->config.cfg.g_bit_depth -
              stream->config.cfg.g_input_bit_depth;
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
        }
      });
    }
#endif

    frame_avail = 1;
    got_data = 0;

    while (frame_avail || got_data) {
      struct vpx_usec_timer timer;

      if (!global.limit || frames_in < global.limit) {
        frame_avail = read_frame(&input, &raw);

        if (frame_avail)
          frames_in++;
        seen_frames = frames_in > global.skip_frames ?
                          frames_in - global.skip_frames : 0;

        if (!global.quiet) {
          float fps = usec_to_fps(cx_time, seen_frames);
          fprintf(stderr, "\rPass %d/%d ", pass + 1, global.passes);

          if (stream_cnt == 1)
            fprintf(stderr,
                    "frame %4d/%-4d %7"PRId64"B ",
                    frames_in, streams->frames_out, (int64_t)streams->nbytes);
          else
            fprintf(stderr, "frame %4d ", frames_in);

          fprintf(stderr, "%7"PRId64" %s %.2f %s ",
                  cx_time > 9999999 ? cx_time / 1000 : cx_time,
                  cx_time > 9999999 ? "ms" : "us",
                  fps >= 1.0 ? fps : fps * 60,
                  fps >= 1.0 ? "fps" : "fpm");
          print_time("ETA", estimated_time_left);
        }

      } else
        frame_avail = 0;

      if (frames_in > global.skip_frames) {
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
        vpx_image_t *frame_to_encode;
        if (input_shift || (use_16bit_internal && input.bit_depth == 8)) {
          assert(use_16bit_internal);
          // Input bit depth and stream bit depth do not match, so up
          // shift frame to stream bit depth
          if (!allocated_raw_shift) {
            vpx_img_alloc(&raw_shift, raw.fmt | VPX_IMG_FMT_HIGHBITDEPTH,
                          input.width, input.height, 32);
            allocated_raw_shift = 1;
          }
          img_upshift(&raw_shift, &raw, input_shift);
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
        vpx_image_t *frame_to_encode;
        if (input_shift || (use_16bit_internal && input.bit_depth == 8)) {
          assert(use_16bit_internal);
          // Input bit depth and stream bit depth do not match, so up
          // shift frame to stream bit depth
          if (!allocated_raw_shift) {
            vpx_img_alloc(&raw_shift, raw.fmt | VPX_IMG_FMT_HIGHBITDEPTH,
                          input.width, input.height, 32);
            allocated_raw_shift = 1;
          }
          vpx_img_upshift(&raw_shift, &raw, input_shift);
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
          frame_to_encode = &raw_shift;
        } else {
          frame_to_encode = &raw;
        }
        vpx_usec_timer_start(&timer);
        if (use_16bit_internal) {
          assert(frame_to_encode->fmt & VPX_IMG_FMT_HIGHBITDEPTH);
          FOREACH_STREAM({
            if (stream->config.use_16bit_internal)
              encode_frame(stream, &global,
                           frame_avail ? frame_to_encode : NULL,
                           frames_in);
            else
              assert(0);
          });
        } else {
          assert((frame_to_encode->fmt & VPX_IMG_FMT_HIGHBITDEPTH) == 0);
          FOREACH_STREAM(encode_frame(stream, &global,
                                      frame_avail ? frame_to_encode : NULL,
                                      frames_in));
        }
#else
        vpx_usec_timer_start(&timer);
        FOREACH_STREAM(encode_frame(stream, &global,
                                    frame_avail ? &raw : NULL,
                                    frames_in));
#endif
        vpx_usec_timer_mark(&timer);
        cx_time += vpx_usec_timer_elapsed(&timer);

        FOREACH_STREAM(update_quantizer_histogram(stream));

        got_data = 0;
        FOREACH_STREAM(get_cx_data(stream, &global, &got_data));

        if (!got_data && input.length && streams != NULL &&
            !streams->frames_out) {
          lagged_count = global.limit ? seen_frames : ftello(input.file);
        } else if (input.length) {
          int64_t remaining;
          int64_t rate;

          if (global.limit) {
            const int64_t frame_in_lagged = (seen_frames - lagged_count) * 1000;
            rate = cx_time ? frame_in_lagged * (int64_t)1000000 / cx_time : 0;
            remaining = 1000 * (global.limit - global.skip_frames
                                - seen_frames + lagged_count);
          } else {
            const int64_t input_pos = ftello(input.file);
            const int64_t input_pos_lagged = input_pos - lagged_count;
            const int64_t limit = input.length;
            rate = cx_time ? input_pos_lagged * (int64_t)1000000 / cx_time : 0;
            remaining = limit - input_pos + lagged_count;
          }
          average_rate = (average_rate <= 0)
              ? rate
              : (average_rate * 7 + rate) / 8;
          estimated_time_left = average_rate ? remaining / average_rate : -1;
        }

        if (got_data && global.test_decode != TEST_DECODE_OFF)
          FOREACH_STREAM(test_decode(stream, global.test_decode, global.codec));
      }
      fflush(stdout);
      if (!global.quiet)
        fprintf(stderr, "\033[K");
    }

    if (stream_cnt > 1)
      fprintf(stderr, "\n");

    if (!global.quiet) {
      FOREACH_STREAM(fprintf(stderr,
          "\rPass %d/%d frame %4d/%-4d %7"PRId64"B %7"PRId64"b/f %7"PRId64"b/s"
          " %7"PRId64" %s (%.2f fps)\033[K\n",
          pass + 1,
          global.passes, frames_in, stream->frames_out, (int64_t)stream->nbytes,
          seen_frames ? (int64_t)(stream->nbytes * 8 / seen_frames) : 0,
          seen_frames ? (int64_t)stream->nbytes * 8 *
              (int64_t)global.framerate.num / global.framerate.den /
              seen_frames : 0,
          stream->cx_time > 9999999 ? stream->cx_time / 1000 : stream->cx_time,
          stream->cx_time > 9999999 ? "ms" : "us",
          usec_to_fps(stream->cx_time, seen_frames)));
    }

    if (global.show_psnr) {
      if (global.codec->fourcc == VP9_FOURCC) {
<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
        FOREACH_STREAM({
          const unsigned int bit_depth = stream->config.cfg.g_in_bit_depth;
          show_psnr(stream, (1 << bit_depth) - 1);
        });
=======
        FOREACH_STREAM(
            show_psnr(stream, (1 << stream->config.cfg.g_input_bit_depth) - 1));
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
      } else {
        FOREACH_STREAM(show_psnr(stream, 255.0));
      }
    }

    FOREACH_STREAM(vpx_codec_destroy(&stream->encoder));

    if (global.test_decode != TEST_DECODE_OFF) {
      FOREACH_STREAM(vpx_codec_destroy(&stream->decoder));
    }

    close_input_file(&input);

    if (global.test_decode == TEST_DECODE_FATAL) {
      FOREACH_STREAM(res |= stream->mismatch_seen);
    }
    FOREACH_STREAM(close_output_file(stream, global.codec->fourcc));

    FOREACH_STREAM(stats_close(&stream->stats, global.passes - 1));

#if CONFIG_FP_MB_STATS
    FOREACH_STREAM(stats_close(&stream->fpmb_stats, global.passes - 1));
#endif

    if (global.pass)
      break;
  }

  if (global.show_q_hist_buckets)
    FOREACH_STREAM(show_q_histogram(stream->counts,
                                    global.show_q_hist_buckets));

  if (global.show_rate_hist_buckets)
    FOREACH_STREAM(show_rate_histogram(stream->rate_hist,
                                       &stream->config.cfg,
                                       global.show_rate_hist_buckets));
  FOREACH_STREAM(destroy_rate_histogram(stream->rate_hist));

#if CONFIG_INTERNAL_STATS
  /* TODO(jkoleszar): This doesn't belong in this executable. Do it for now,
   * to match some existing utilities.
   */
  if (!(global.pass == 1 && global.passes == 2))
    FOREACH_STREAM({
      FILE *f = fopen("opsnr.stt", "a");
      if (stream->mismatch_seen) {
        fprintf(f, "First mismatch occurred in frame %d\n",
                stream->mismatch_seen);
      } else {
        fprintf(f, "No mismatch detected in recon buffers\n");
      }
      fclose(f);
    });
#endif

<<<<<<< HEAD   (93657e Merge "Add bit_depth to internal image structure" into highb)
#if CONFIG_VP9_HIGH
=======
#if CONFIG_VP9 && CONFIG_VP9_HIGHBITDEPTH
>>>>>>> BRANCH (9a29fd Merge "Rename highbitdepth functions to use highbd prefix")
  if (allocated_raw_shift)
    vpx_img_free(&raw_shift);
#endif
  vpx_img_free(&raw);
  free(argv);
  free(streams);
  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}
