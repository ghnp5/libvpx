/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "denoising.h"

#include "vp8/common/reconinter.h"
#include "vpx/vpx_integer.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx_rtcd.h"


static const unsigned int NOISE_MOTION_THRESHOLD = 20 * 20;
// SSE_DIFF_THRESHOLD is selected as ~95% confidence assuming var(noise) ~= 100.
static const unsigned int SSE_DIFF_THRESHOLD = 16 * 16 * 20;
static const unsigned int SSE_THRESHOLD = 16 * 16 * 40;


static unsigned int denoiser_motion_compensate(YV12_BUFFER_CONFIG *src,
        YV12_BUFFER_CONFIG *dst,
        MACROBLOCK *x,
        unsigned int best_sse,
        unsigned int zero_mv_sse,
        int recon_yoffset,
        int recon_uvoffset)
{
    MACROBLOCKD filter_xd = x->e_mbd;
    int mv_col;
    int mv_row;
    int sse_diff = zero_mv_sse - best_sse;
    // Compensate the running average.
    filter_xd.pre.y_buffer = src->y_buffer + recon_yoffset;
    filter_xd.pre.u_buffer = src->u_buffer + recon_uvoffset;
    filter_xd.pre.v_buffer = src->v_buffer + recon_uvoffset;
    // Write the compensated running average to the destination buffer.
    filter_xd.dst.y_buffer = dst->y_buffer + recon_yoffset;
    filter_xd.dst.u_buffer = dst->u_buffer + recon_uvoffset;
    filter_xd.dst.v_buffer = dst->v_buffer + recon_uvoffset;
    // Use the best MV for the compensation.
    filter_xd.mode_info_context->mbmi.ref_frame = LAST_FRAME;
    filter_xd.mode_info_context->mbmi.mode = filter_xd.best_sse_inter_mode;
    filter_xd.mode_info_context->mbmi.mv = filter_xd.best_sse_mv;
    filter_xd.mode_info_context->mbmi.need_to_clamp_mvs =
        filter_xd.need_to_clamp_best_mvs;
    mv_col = filter_xd.best_sse_mv.as_mv.col;
    mv_row = filter_xd.best_sse_mv.as_mv.row;

    if (filter_xd.mode_info_context->mbmi.mode <= B_PRED ||
        (mv_row *mv_row + mv_col *mv_col <= NOISE_MOTION_THRESHOLD &&
         sse_diff < SSE_DIFF_THRESHOLD))
    {
        // Handle intra blocks as referring to last frame with zero motion and
        // let the absolute pixel difference affect the filter factor.
        // Also consider small amount of motion as being random walk due to
        // noise, if it doesn't mean that we get a much bigger error.
        // Note that any changes to the mode info only affects the denoising.
        filter_xd.mode_info_context->mbmi.ref_frame = LAST_FRAME;
        filter_xd.mode_info_context->mbmi.mode = ZEROMV;
        filter_xd.mode_info_context->mbmi.mv.as_int = 0;
        x->e_mbd.best_sse_inter_mode = ZEROMV;
        x->e_mbd.best_sse_mv.as_int = 0;
        best_sse = zero_mv_sse;
    }

    if (!x->skip)
    {
        vp8_build_inter_predictors_mb(&filter_xd);
    }
    else
    {
        vp8_build_inter16x16_predictors_mb(&filter_xd,
                                           filter_xd.dst.y_buffer,
                                           filter_xd.dst.u_buffer,
                                           filter_xd.dst.v_buffer,
                                           filter_xd.dst.y_stride,
                                           filter_xd.dst.uv_stride);
    }

    return best_sse;
}

// The filtering coefficients used for denoizing are adjusted for static
// blocks, or blocks with very small motion vectors. This is done through
// the motion magnitude parameter.
//
// There are currently 2048 possible mapping from absolute difference to
// filter coefficient depending on the motion magnitude. Each mapping is
// in a LUT table. All these tables are staticly allocated but they are only
// filled on their first use.
//
// Each entry is a pair of 16b values, the coefficient and its complement
// to 256. Each of these value should only be 8b but they are 16b wide to
// avoid slow partial register manipulations.
enum {num_motion_magnitude_adjustments = 2048};

static union coeff_pair filter_coeff_LUT[num_motion_magnitude_adjustments][256];
static uint8_t filter_coeff_LUT_initialized[num_motion_magnitude_adjustments] =
    { 0 };


union coeff_pair *vp8_get_filter_coeff_LUT(unsigned int motion_magnitude)
{
    union coeff_pair *LUT;
    unsigned int motion_magnitude_adjustment = motion_magnitude >> 3;

    if (motion_magnitude_adjustment >= num_motion_magnitude_adjustments)
    {
        motion_magnitude_adjustment = num_motion_magnitude_adjustments - 1;
    }

    LUT = filter_coeff_LUT[motion_magnitude_adjustment];

    if (!filter_coeff_LUT_initialized[motion_magnitude_adjustment])
    {
        int absdiff;

        for (absdiff = 0; absdiff < 256; ++absdiff)
        {
            unsigned int filter_coefficient;
            filter_coefficient = (255 << 8) / (256 + ((absdiff * 330) >> 3));
            filter_coefficient += filter_coefficient /
                                  (3 + motion_magnitude_adjustment);

            if (filter_coefficient > 255)
            {
                filter_coefficient = 255;
            }

            LUT[absdiff].as_short[0] = filter_coefficient ;
            LUT[absdiff].as_short[1] = 256 - filter_coefficient;
        }

        filter_coeff_LUT_initialized[motion_magnitude_adjustment] = 1;
    }

    return LUT;
}



void vp8_denoiser_filter_c(YV12_BUFFER_CONFIG *mc_running_avg,
                           YV12_BUFFER_CONFIG *running_avg, MACROBLOCK *signal,
                           unsigned int motion_magnitude, int y_offset,
                           int uv_offset)
{
    unsigned char *sig = signal->thismb;
    int sig_stride = 16;
    unsigned char *mc_running_avg_y = mc_running_avg->y_buffer + y_offset;
    int mc_avg_y_stride = mc_running_avg->y_stride;
    unsigned char *running_avg_y = running_avg->y_buffer + y_offset;
    int avg_y_stride = running_avg->y_stride;
    const union coeff_pair *LUT = vp8_get_filter_coeff_LUT(motion_magnitude);
    int r, c;

    for (r = 0; r < 16; ++r)
    {
        // Calculate absolute differences
        unsigned char abs_diff[16];

        union coeff_pair filter_coefficient[16];

        for (c = 0; c < 16; ++c)
        {
            int absdiff = sig[c] - mc_running_avg_y[c];
            absdiff = absdiff > 0 ? absdiff : -absdiff;
            abs_diff[c] = absdiff;
        }

        // Use LUT to get filter coefficients (two 16b value; f and 256-f)
        for (c = 0; c < 16; ++c)
        {
            filter_coefficient[c] = LUT[abs_diff[c]];
        }

        // Filtering...
        for (c = 0; c < 16; ++c)
        {
            const uint16_t state = (uint16_t)(mc_running_avg_y[c]);
            const uint16_t sample = (uint16_t)(sig[c]);

            running_avg_y[c] = (filter_coefficient[c].as_short[0] * state +
                    filter_coefficient[c].as_short[1] * sample + 128) >> 8;
        }

        // Depending on the magnitude of the difference between the signal and
        // filtered version, either replace the signal by the filtered one or
        // update the filter state with the signal when the change in a pixel
        // isn't classified as noise.
        for (c = 0; c < 16; ++c)
        {
            const int diff = sig[c] - running_avg_y[c];

            if (diff * diff < NOISE_DIFF2_THRESHOLD)
            {
                sig[c] = running_avg_y[c];
            }
            else
            {
                running_avg_y[c] = sig[c];
            }
        }

        // Update pointers for next iteration.
        sig += sig_stride;
        mc_running_avg_y += mc_avg_y_stride;
        running_avg_y += avg_y_stride;
    }
}


int vp8_denoiser_allocate(VP8_DENOISER *denoiser, int width, int height)
{
    assert(denoiser);
    denoiser->yv12_running_avg.flags = 0;

    if (vp8_yv12_alloc_frame_buffer(&(denoiser->yv12_running_avg), width,
                                    height, VP8BORDERINPIXELS) < 0)
    {
        vp8_denoiser_free(denoiser);
        return 1;
    }

    denoiser->yv12_mc_running_avg.flags = 0;

    if (vp8_yv12_alloc_frame_buffer(&(denoiser->yv12_mc_running_avg), width,
                                    height, VP8BORDERINPIXELS) < 0)
    {
        vp8_denoiser_free(denoiser);
        return 1;
    }

    vpx_memset(denoiser->yv12_running_avg.buffer_alloc, 0,
               denoiser->yv12_running_avg.frame_size);
    vpx_memset(denoiser->yv12_mc_running_avg.buffer_alloc, 0,
               denoiser->yv12_mc_running_avg.frame_size);
    return 0;
}

void vp8_denoiser_free(VP8_DENOISER *denoiser)
{
    assert(denoiser);
    vp8_yv12_de_alloc_frame_buffer(&denoiser->yv12_running_avg);
    vp8_yv12_de_alloc_frame_buffer(&denoiser->yv12_mc_running_avg);
}

void vp8_denoiser_denoise_mb(VP8_DENOISER *denoiser,
                             MACROBLOCK *x,
                             unsigned int best_sse,
                             unsigned int zero_mv_sse,
                             int recon_yoffset,
                             int recon_uvoffset)
{
    int mv_row;
    int mv_col;
    unsigned int motion_magnitude2;
    // Motion compensate the running average.
    best_sse = denoiser_motion_compensate(&denoiser->yv12_running_avg,
                                          &denoiser->yv12_mc_running_avg,
                                          x,
                                          best_sse,
                                          zero_mv_sse,
                                          recon_yoffset,
                                          recon_uvoffset);

    mv_row = x->e_mbd.best_sse_mv.as_mv.row;
    mv_col = x->e_mbd.best_sse_mv.as_mv.col;
    motion_magnitude2 = mv_row * mv_row + mv_col * mv_col;

    if (best_sse > SSE_THRESHOLD ||
        motion_magnitude2 > 8 * NOISE_MOTION_THRESHOLD)
    {
        // No filtering of this block since it differs too much from the
        // predictor, or the motion vector magnitude is considered too big.
        vp8_copy_mem16x16(x->thismb, 16,
                          denoiser->yv12_running_avg.y_buffer + recon_yoffset,
                          denoiser->yv12_running_avg.y_stride);
        return;
    }

    // Filter.
    vp8_denoiser_filter(&denoiser->yv12_mc_running_avg,
                        &denoiser->yv12_running_avg, x, motion_magnitude2,
                        recon_yoffset, recon_uvoffset);
}
