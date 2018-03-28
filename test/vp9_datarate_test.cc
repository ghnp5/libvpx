/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "./vpx_config.h"
#include "third_party/googletest/src/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "vpx/vpx_codec.h"

namespace {

class DatarateTestVP9Large
    : public ::libvpx_test::EncoderTest,
      public ::libvpx_test::CodecTestWith2Params<libvpx_test::TestMode, int> {
 public:
  DatarateTestVP9Large() : EncoderTest(GET_PARAM(0)) {}

 protected:
  virtual ~DatarateTestVP9Large() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = GET_PARAM(2);
    ResetModel();
  }

  virtual void ResetModel() {
    last_pts_ = 0;
    bits_in_buffer_model_ = cfg_.rc_target_bitrate * cfg_.rc_buf_initial_sz;
    frame_number_ = 0;
    tot_frame_number_ = 0;
    first_drop_ = 0;
    num_drops_ = 0;
    // Denoiser is off by default.
    denoiser_on_ = 0;
    // For testing up to 3 layers.
    for (int i = 0; i < 3; ++i) {
      bits_total_[i] = 0;
    }
    denoiser_offon_test_ = 0;
    denoiser_offon_period_ = -1;
    frame_parallel_decoding_mode_ = 1;
    use_roi_ = false;
  }

  //
  // Frame flags and layer id for temporal layers.
  //

  // For two layers, test pattern is:
  //   1     3
  // 0    2     .....
  // For three layers, test pattern is:
  //   1      3    5      7
  //      2           6
  // 0          4            ....
  // LAST is always update on base/layer 0, GOLDEN is updated on layer 1.
  // For this 3 layer example, the 2nd enhancement layer (layer 2) updates
  // the altref frame.
  int SetFrameFlags(int frame_num, int num_temp_layers) {
    int frame_flags = 0;
    if (num_temp_layers == 2) {
      if (frame_num % 2 == 0) {
        // Layer 0: predict from L and ARF, update L.
        frame_flags =
            VP8_EFLAG_NO_REF_GF | VP8_EFLAG_NO_UPD_GF | VP8_EFLAG_NO_UPD_ARF;
      } else {
        // Layer 1: predict from L, G and ARF, and update G.
        frame_flags = VP8_EFLAG_NO_UPD_ARF | VP8_EFLAG_NO_UPD_LAST |
                      VP8_EFLAG_NO_UPD_ENTROPY;
      }
    } else if (num_temp_layers == 3) {
      if (frame_num % 4 == 0) {
        // Layer 0: predict from L and ARF; update L.
        frame_flags =
            VP8_EFLAG_NO_UPD_GF | VP8_EFLAG_NO_UPD_ARF | VP8_EFLAG_NO_REF_GF;
      } else if ((frame_num - 2) % 4 == 0) {
        // Layer 1: predict from L, G, ARF; update G.
        frame_flags = VP8_EFLAG_NO_UPD_ARF | VP8_EFLAG_NO_UPD_LAST;
      } else if ((frame_num - 1) % 2 == 0) {
        // Layer 2: predict from L, G, ARF; update ARF.
        frame_flags = VP8_EFLAG_NO_UPD_GF | VP8_EFLAG_NO_UPD_LAST;
      }
    }
    return frame_flags;
  }

  int SetLayerId(int frame_num, int num_temp_layers) {
    int layer_id = 0;
    if (num_temp_layers == 2) {
      if (frame_num % 2 == 0) {
        layer_id = 0;
      } else {
        layer_id = 1;
      }
    } else if (num_temp_layers == 3) {
      if (frame_num % 4 == 0) {
        layer_id = 0;
      } else if ((frame_num - 2) % 4 == 0) {
        layer_id = 1;
      } else if ((frame_num - 1) % 2 == 0) {
        layer_id = 2;
      }
    }
    return layer_id;
  }

  virtual void PreEncodeFrameHook(::libvpx_test::VideoSource *video,
                                  ::libvpx_test::Encoder *encoder) {
    if (video->frame() == 0) encoder->Control(VP8E_SET_CPUUSED, set_cpu_used_);

    if (denoiser_offon_test_) {
      ASSERT_GT(denoiser_offon_period_, 0)
          << "denoiser_offon_period_ is not positive.";
      if ((video->frame() + 1) % denoiser_offon_period_ == 0) {
        // Flip denoiser_on_ periodically
        denoiser_on_ ^= 1;
      }
    }

    encoder->Control(VP9E_SET_NOISE_SENSITIVITY, denoiser_on_);
    encoder->Control(VP9E_SET_TILE_COLUMNS, (cfg_.g_threads >> 1));
    encoder->Control(VP9E_SET_FRAME_PARALLEL_DECODING,
                     frame_parallel_decoding_mode_);

    if (use_roi_) {
      encoder->Control(VP9E_SET_ROI_MAP, &roi_);
    }

    if (cfg_.ts_number_layers > 1) {
      if (video->frame() == 0) {
        encoder->Control(VP9E_SET_SVC, 1);
      }
      vpx_svc_layer_id_t layer_id;
      layer_id.spatial_layer_id = 0;
      frame_flags_ = SetFrameFlags(video->frame(), cfg_.ts_number_layers);
      layer_id.temporal_layer_id =
          SetLayerId(video->frame(), cfg_.ts_number_layers);
      encoder->Control(VP9E_SET_SVC_LAYER_ID, &layer_id);
    }
    const vpx_rational_t tb = video->timebase();
    timebase_ = static_cast<double>(tb.num) / tb.den;
    duration_ = 0;
  }

  virtual void FramePktHook(const vpx_codec_cx_pkt_t *pkt) {
    // Time since last timestamp = duration.
    vpx_codec_pts_t duration = pkt->data.frame.pts - last_pts_;

    if (duration > 1) {
      // If first drop not set and we have a drop set it to this time.
      if (!first_drop_) first_drop_ = last_pts_ + 1;
      // Update the number of frame drops.
      num_drops_ += static_cast<int>(duration - 1);
      // Update counter for total number of frames (#frames input to encoder).
      // Needed for setting the proper layer_id below.
      tot_frame_number_ += static_cast<int>(duration - 1);
    }

    int layer = SetLayerId(tot_frame_number_, cfg_.ts_number_layers);

    // Add to the buffer the bits we'd expect from a constant bitrate server.
    bits_in_buffer_model_ += static_cast<int64_t>(
        duration * timebase_ * cfg_.rc_target_bitrate * 1000);

    // Buffer should not go negative.
    ASSERT_GE(bits_in_buffer_model_, 0)
        << "Buffer Underrun at frame " << pkt->data.frame.pts;

    const size_t frame_size_in_bits = pkt->data.frame.sz * 8;

    // Update the total encoded bits. For temporal layers, update the cumulative
    // encoded bits per layer.
    for (int i = layer; i < static_cast<int>(cfg_.ts_number_layers); ++i) {
      bits_total_[i] += frame_size_in_bits;
    }

    // Update the most recent pts.
    last_pts_ = pkt->data.frame.pts;
    ++frame_number_;
    ++tot_frame_number_;
  }

  virtual void EndPassHook(void) {
    for (int layer = 0; layer < static_cast<int>(cfg_.ts_number_layers);
         ++layer) {
      duration_ = (last_pts_ + 1) * timebase_;
      if (bits_total_[layer]) {
        // Effective file datarate:
        effective_datarate_[layer] = (bits_total_[layer] / 1000.0) / duration_;
      }
    }
  }

  vpx_codec_pts_t last_pts_;
  double timebase_;
  int frame_number_;      // Counter for number of non-dropped/encoded frames.
  int tot_frame_number_;  // Counter for total number of input frames.
  int64_t bits_total_[3];
  double duration_;
  double effective_datarate_[3];
  int set_cpu_used_;
  int64_t bits_in_buffer_model_;
  vpx_codec_pts_t first_drop_;
  int num_drops_;
  int denoiser_on_;
  int denoiser_offon_test_;
  int denoiser_offon_period_;
  int frame_parallel_decoding_mode_;
  bool use_roi_;
  vpx_roi_map_t roi_;
};

// Check basic rate targeting for VBR mode with 0 lag.
TEST_P(DatarateTestVP9Large, BasicRateTargetingVBRLagZero) {
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.g_error_resilient = 0;
  cfg_.rc_end_usage = VPX_VBR;
  cfg_.g_lag_in_frames = 0;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 300);
  for (int i = 400; i <= 800; i += 400) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.75)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.30)
        << " The datarate for the file is greater than target by too much!";
  }
}

// Check basic rate targeting for VBR mode with non-zero lag.
TEST_P(DatarateTestVP9Large, BasicRateTargetingVBRLagNonZero) {
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.g_error_resilient = 0;
  cfg_.rc_end_usage = VPX_VBR;
  // For non-zero lag, rate control will work (be within bounds) for
  // real-time mode.
  if (deadline_ == VPX_DL_REALTIME) {
    cfg_.g_lag_in_frames = 15;
  } else {
    cfg_.g_lag_in_frames = 0;
  }

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 300);
  for (int i = 400; i <= 800; i += 400) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.75)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.30)
        << " The datarate for the file is greater than target by too much!";
  }
}

// Check basic rate targeting for VBR mode with non-zero lag, with
// frame_parallel_decoding_mode off. This enables the adapt_coeff/mode/mv probs
// since error_resilience is off.
TEST_P(DatarateTestVP9Large, BasicRateTargetingVBRLagNonZeroFrameParDecOff) {
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.g_error_resilient = 0;
  cfg_.rc_end_usage = VPX_VBR;
  // For non-zero lag, rate control will work (be within bounds) for
  // real-time mode.
  if (deadline_ == VPX_DL_REALTIME) {
    cfg_.g_lag_in_frames = 15;
  } else {
    cfg_.g_lag_in_frames = 0;
  }

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 300);
  for (int i = 400; i <= 800; i += 400) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    frame_parallel_decoding_mode_ = 0;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.75)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.30)
        << " The datarate for the file is greater than target by too much!";
  }
}

// Check basic rate targeting for CBR mode.
TEST_P(DatarateTestVP9Large, BasicRateTargeting) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 140);
  for (int i = 150; i < 800; i += 200) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.15)
        << " The datarate for the file is greater than target by too much!";
  }
}

// Check basic rate targeting for CBR mode, with frame_parallel_decoding_mode
// off( and error_resilience off).
TEST_P(DatarateTestVP9Large, BasicRateTargetingFrameParDecOff) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;
  cfg_.g_error_resilient = 0;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 140);
  for (int i = 150; i < 800; i += 200) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    frame_parallel_decoding_mode_ = 0;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.15)
        << " The datarate for the file is greater than target by too much!";
  }
}

// Check basic rate targeting for CBR mode, with 2 threads and dropped frames.
TEST_P(DatarateTestVP9Large, BasicRateTargetingDropFramesMultiThreads) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 30;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;
  // Encode using multiple threads.
  cfg_.g_threads = 2;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 140);
  cfg_.rc_target_bitrate = 200;
  ResetModel();
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
      << " The datarate for the file is lower than target by too much!";
  ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.15)
      << " The datarate for the file is greater than target by too much!";
}

// Check basic rate targeting for CBR.
TEST_P(DatarateTestVP9Large, BasicRateTargeting444) {
  ::libvpx_test::Y4mVideoSource video("rush_hour_444.y4m", 0, 140);

  cfg_.g_profile = 1;
  cfg_.g_timebase = video.timebase();

  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;

  for (int i = 250; i < 900; i += 200) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_[0] * 0.80)
        << " The datarate for the file exceeds the target by too much!";
    ASSERT_LE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_[0] * 1.15)
        << " The datarate for the file missed the target!"
        << cfg_.rc_target_bitrate << " " << effective_datarate_;
  }
}

// Check that (1) the first dropped frame gets earlier and earlier
// as the drop frame threshold is increased, and (2) that the total number of
// frame drops does not decrease as we increase frame drop threshold.
// Use a lower qp-max to force some frame drops.
TEST_P(DatarateTestVP9Large, ChangingDropFrameThresh) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_undershoot_pct = 20;
  cfg_.rc_undershoot_pct = 20;
  cfg_.rc_dropframe_thresh = 10;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 50;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.rc_target_bitrate = 200;
  cfg_.g_lag_in_frames = 0;
  // TODO(marpan): Investigate datarate target failures with a smaller keyframe
  // interval (128).
  cfg_.kf_max_dist = 9999;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 140);

  const int kDropFrameThreshTestStep = 30;
  for (int j = 50; j <= 150; j += 100) {
    cfg_.rc_target_bitrate = j;
    vpx_codec_pts_t last_drop = 140;
    int last_num_drops = 0;
    for (int i = 10; i < 100; i += kDropFrameThreshTestStep) {
      cfg_.rc_dropframe_thresh = i;
      ResetModel();
      ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
      ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.25)
          << " The datarate for the file is greater than target by too much!";
      ASSERT_LE(first_drop_, last_drop)
          << " The first dropped frame for drop_thresh " << i
          << " > first dropped frame for drop_thresh "
          << i - kDropFrameThreshTestStep;
      ASSERT_GE(num_drops_, last_num_drops * 0.85)
          << " The number of dropped frames for drop_thresh " << i
          << " < number of dropped frames for drop_thresh "
          << i - kDropFrameThreshTestStep;
      last_drop = first_drop_;
      last_num_drops = num_drops_;
    }
  }
}

// Check basic rate targeting for 2 temporal layers.
TEST_P(DatarateTestVP9Large, BasicRateTargeting2TemporalLayers) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  // 2 Temporal layers, no spatial layers: Framerate decimation (2, 1).
  cfg_.ss_number_layers = 1;
  cfg_.ts_number_layers = 2;
  cfg_.ts_rate_decimator[0] = 2;
  cfg_.ts_rate_decimator[1] = 1;

  cfg_.temporal_layering_mode = VP9E_TEMPORAL_LAYERING_MODE_BYPASS;

  if (deadline_ == VPX_DL_REALTIME) cfg_.g_error_resilient = 1;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 200);
  for (int i = 200; i <= 800; i += 200) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    // 60-40 bitrate allocation for 2 temporal layers.
    cfg_.layer_target_bitrate[0] = 60 * cfg_.rc_target_bitrate / 100;
    cfg_.layer_target_bitrate[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int j = 0; j < static_cast<int>(cfg_.ts_number_layers); ++j) {
      ASSERT_GE(effective_datarate_[j], cfg_.layer_target_bitrate[j] * 0.85)
          << " The datarate for the file is lower than target by too much, "
             "for layer: "
          << j;
      ASSERT_LE(effective_datarate_[j], cfg_.layer_target_bitrate[j] * 1.15)
          << " The datarate for the file is greater than target by too much, "
             "for layer: "
          << j;
    }
  }
}

// Check basic rate targeting for 3 temporal layers.
TEST_P(DatarateTestVP9Large, BasicRateTargeting3TemporalLayers) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  // 3 Temporal layers, no spatial layers: Framerate decimation (4, 2, 1).
  cfg_.ss_number_layers = 1;
  cfg_.ts_number_layers = 3;
  cfg_.ts_rate_decimator[0] = 4;
  cfg_.ts_rate_decimator[1] = 2;
  cfg_.ts_rate_decimator[2] = 1;

  cfg_.temporal_layering_mode = VP9E_TEMPORAL_LAYERING_MODE_BYPASS;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 200);
  for (int i = 200; i <= 800; i += 200) {
    cfg_.rc_target_bitrate = i;
    ResetModel();
    // 40-20-40 bitrate allocation for 3 temporal layers.
    cfg_.layer_target_bitrate[0] = 40 * cfg_.rc_target_bitrate / 100;
    cfg_.layer_target_bitrate[1] = 60 * cfg_.rc_target_bitrate / 100;
    cfg_.layer_target_bitrate[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int j = 0; j < static_cast<int>(cfg_.ts_number_layers); ++j) {
      // TODO(yaowu): Work out more stable rc control strategy and
      //              Adjust the thresholds to be tighter than .75.
      ASSERT_GE(effective_datarate_[j], cfg_.layer_target_bitrate[j] * 0.75)
          << " The datarate for the file is lower than target by too much, "
             "for layer: "
          << j;
      // TODO(yaowu): Work out more stable rc control strategy and
      //              Adjust the thresholds to be tighter than 1.25.
      ASSERT_LE(effective_datarate_[j], cfg_.layer_target_bitrate[j] * 1.25)
          << " The datarate for the file is greater than target by too much, "
             "for layer: "
          << j;
    }
  }
}

// Check basic rate targeting for 3 temporal layers, with frame dropping.
// Only for one (low) bitrate with lower max_quantizer, and somewhat higher
// frame drop threshold, to force frame dropping.
TEST_P(DatarateTestVP9Large, BasicRateTargeting3TemporalLayersFrameDropping) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  // Set frame drop threshold and rc_max_quantizer to force some frame drops.
  cfg_.rc_dropframe_thresh = 20;
  cfg_.rc_max_quantizer = 45;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  // 3 Temporal layers, no spatial layers: Framerate decimation (4, 2, 1).
  cfg_.ss_number_layers = 1;
  cfg_.ts_number_layers = 3;
  cfg_.ts_rate_decimator[0] = 4;
  cfg_.ts_rate_decimator[1] = 2;
  cfg_.ts_rate_decimator[2] = 1;

  cfg_.temporal_layering_mode = VP9E_TEMPORAL_LAYERING_MODE_BYPASS;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 200);
  cfg_.rc_target_bitrate = 200;
  ResetModel();
  // 40-20-40 bitrate allocation for 3 temporal layers.
  cfg_.layer_target_bitrate[0] = 40 * cfg_.rc_target_bitrate / 100;
  cfg_.layer_target_bitrate[1] = 60 * cfg_.rc_target_bitrate / 100;
  cfg_.layer_target_bitrate[2] = cfg_.rc_target_bitrate;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  for (int j = 0; j < static_cast<int>(cfg_.ts_number_layers); ++j) {
    ASSERT_GE(effective_datarate_[j], cfg_.layer_target_bitrate[j] * 0.85)
        << " The datarate for the file is lower than target by too much, "
           "for layer: "
        << j;
    ASSERT_LE(effective_datarate_[j], cfg_.layer_target_bitrate[j] * 1.15)
        << " The datarate for the file is greater than target by too much, "
           "for layer: "
        << j;
    // Expect some frame drops in this test: for this 200 frames test,
    // expect at least 10% and not more than 60% drops.
    ASSERT_GE(num_drops_, 20);
    ASSERT_LE(num_drops_, 130);
  }
}

class DatarateTestVP9RealTime : public DatarateTestVP9Large {
 public:
  virtual ~DatarateTestVP9RealTime() {}
};

// Check VP9 region of interest feature.
TEST_P(DatarateTestVP9RealTime, RegionOfInterest) {
  if (deadline_ != VPX_DL_REALTIME || set_cpu_used_ < 5) return;
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 0;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 63;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 300);

  cfg_.rc_target_bitrate = 450;
  cfg_.g_w = 352;
  cfg_.g_h = 288;

  ResetModel();

  // Set ROI parameters
  use_roi_ = true;
  memset(&roi_, 0, sizeof(roi_));

  roi_.rows = (cfg_.g_h + 7) / 8;
  roi_.cols = (cfg_.g_w + 7) / 8;

  roi_.delta_q[1] = -20;
  roi_.delta_lf[1] = -20;
  memset(roi_.ref_frame, -1, sizeof(roi_.ref_frame));
  roi_.ref_frame[1] = 1;

  // Use 2 states: 1 is center square, 0 is the rest.
  roi_.roi_map = reinterpret_cast<uint8_t *>(
      calloc(roi_.rows * roi_.cols, sizeof(*roi_.roi_map)));
  ASSERT_TRUE(roi_.roi_map != NULL);

  for (unsigned int i = 0; i < roi_.rows; ++i) {
    for (unsigned int j = 0; j < roi_.cols; ++j) {
      if (i > (roi_.rows >> 2) && i < ((roi_.rows * 3) >> 2) &&
          j > (roi_.cols >> 2) && j < ((roi_.cols * 3) >> 2)) {
        roi_.roi_map[i * roi_.cols + j] = 1;
      }
    }
  }

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_GE(cfg_.rc_target_bitrate, effective_datarate_[0] * 0.90)
      << " The datarate for the file exceeds the target!";

  ASSERT_LE(cfg_.rc_target_bitrate, effective_datarate_[0] * 1.4)
      << " The datarate for the file missed the target!";

  free(roi_.roi_map);
}

#if CONFIG_VP9_TEMPORAL_DENOISING
class DatarateTestVP9LargeDenoiser : public DatarateTestVP9Large {
 public:
  virtual ~DatarateTestVP9LargeDenoiser() {}
};

// Check basic datarate targeting, for a single bitrate, when denoiser is on.
TEST_P(DatarateTestVP9LargeDenoiser, LowNoise) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 2;
  cfg_.rc_max_quantizer = 56;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 140);

  // For the temporal denoiser (#if CONFIG_VP9_TEMPORAL_DENOISING),
  // there is only one denoiser mode: denoiserYonly(which is 1),
  // but may add more modes in the future.
  cfg_.rc_target_bitrate = 300;
  ResetModel();
  // Turn on the denoiser.
  denoiser_on_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
      << " The datarate for the file is lower than target by too much!";
  ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.15)
      << " The datarate for the file is greater than target by too much!";
}

// Check basic datarate targeting, for a single bitrate, when denoiser is on,
// for clip with high noise level. Use 2 threads.
TEST_P(DatarateTestVP9LargeDenoiser, HighNoise) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 2;
  cfg_.rc_max_quantizer = 56;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;
  cfg_.g_threads = 2;

  ::libvpx_test::Y4mVideoSource video("noisy_clip_640_360.y4m", 0, 200);

  // For the temporal denoiser (#if CONFIG_VP9_TEMPORAL_DENOISING),
  // there is only one denoiser mode: kDenoiserOnYOnly(which is 1),
  // but may add more modes in the future.
  cfg_.rc_target_bitrate = 1000;
  ResetModel();
  // Turn on the denoiser.
  denoiser_on_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
      << " The datarate for the file is lower than target by too much!";
  ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.15)
      << " The datarate for the file is greater than target by too much!";
}

// Check basic datarate targeting, for a single bitrate, when denoiser is on,
// for 1280x720 clip with 4 threads.
TEST_P(DatarateTestVP9LargeDenoiser, 4threads) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 2;
  cfg_.rc_max_quantizer = 56;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;
  cfg_.g_threads = 4;

  ::libvpx_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 300);

  // For the temporal denoiser (#if CONFIG_VP9_TEMPORAL_DENOISING),
  // there is only one denoiser mode: denoiserYonly(which is 1),
  // but may add more modes in the future.
  cfg_.rc_target_bitrate = 1000;
  ResetModel();
  // Turn on the denoiser.
  denoiser_on_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
      << " The datarate for the file is lower than target by too much!";
  ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.29)
      << " The datarate for the file is greater than target by too much!";
}

// Check basic datarate targeting, for a single bitrate, when denoiser is off
// and on.
TEST_P(DatarateTestVP9LargeDenoiser, DenoiserOffOn) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_dropframe_thresh = 1;
  cfg_.rc_min_quantizer = 2;
  cfg_.rc_max_quantizer = 56;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 299);

  // For the temporal denoiser (#if CONFIG_VP9_TEMPORAL_DENOISING),
  // there is only one denoiser mode: denoiserYonly(which is 1),
  // but may add more modes in the future.
  cfg_.rc_target_bitrate = 300;
  ResetModel();
  // The denoiser is off by default.
  denoiser_on_ = 0;
  // Set the offon test flag.
  denoiser_offon_test_ = 1;
  denoiser_offon_period_ = 100;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_GE(effective_datarate_[0], cfg_.rc_target_bitrate * 0.85)
      << " The datarate for the file is lower than target by too much!";
  ASSERT_LE(effective_datarate_[0], cfg_.rc_target_bitrate * 1.15)
      << " The datarate for the file is greater than target by too much!";
}
#endif  // CONFIG_VP9_TEMPORAL_DENOISING

VP9_INSTANTIATE_TEST_CASE(DatarateTestVP9Large,
                          ::testing::Values(::libvpx_test::kOnePassGood,
                                            ::libvpx_test::kRealTime),
                          ::testing::Range(2, 9));
VP9_INSTANTIATE_TEST_CASE(DatarateTestVP9RealTime,
                          ::testing::Values(::libvpx_test::kRealTime),
                          ::testing::Range(5, 9));
#if CONFIG_VP9_TEMPORAL_DENOISING
VP9_INSTANTIATE_TEST_CASE(DatarateTestVP9LargeDenoiser,
                          ::testing::Values(::libvpx_test::kRealTime),
                          ::testing::Range(5, 9));
#endif
}  // namespace