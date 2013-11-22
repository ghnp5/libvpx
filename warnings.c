/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./warnings.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpx/vpx_encoder.h"

#include "./tools_common.h"
#include "./vpxenc.h"

static const char quantizer_warning_string[] =
    "Bad quantizer values. Quantizer values should not be equal, and should "
    "differ by at least 8.";
static const char lag_in_frames_with_realtime[] =
    "Lag in frames is ignored when deadline is set to realtime.";

struct WarningListNode {
  const char *warning_string;
  struct WarningListNode *next_warning;
};

struct WarningList {
  struct WarningListNode *warning_node;
};

void AddWarning(const char *warning_string, struct WarningList *warning_list) {
  struct WarningListNode **node = &warning_list->warning_node;

  struct WarningListNode *new_node = malloc(sizeof(struct WarningListNode));
  if (new_node == NULL) {
    fatal("Unable to allocate warning node.");
  }

  new_node->warning_string = warning_string;
  new_node->next_warning = NULL;

  while (*node != NULL)
    node = &(*node)->next_warning;

  *node = new_node;
}

void FreeWarningList(struct WarningList *warning_list) {
  struct WarningListNode *node = warning_list->warning_node;
  while (warning_list->warning_node != NULL) {
    node = warning_list->warning_node->next_warning;
    free(warning_list->warning_node);
    warning_list->warning_node = node;
  }
}

int continue_prompt(int num_warnings) {
  int c;
  fprintf(stderr,
          "%d encoder configuration warning(s). Continue? (y to continue) ",
          num_warnings);
  c = getchar();
  return c == 'y';
}

void check_lag_in_frames_realtime_deadline(int lag_in_frames,
                                           int deadline,
                                           struct WarningList *warning_list) {
  if (deadline == VPX_DL_REALTIME && lag_in_frames != 0)
    AddWarning(lag_in_frames_with_realtime, warning_list);
}

void check_quantizer(int min_q, int max_q, struct WarningList *warning_list) {
  if (min_q == max_q || abs(max_q - min_q) < 8)
    AddWarning(quantizer_warning_string, warning_list);
}

void check_encoder_config(int disable_prompt,
                          const struct VpxEncoderConfig *global_config,
                          const struct vpx_codec_enc_cfg *stream_config) {
  int num_warnings = 0;
  struct WarningListNode *warning = NULL;
  struct WarningList warning_list = {0};

  check_quantizer(stream_config->rc_min_quantizer,
                  stream_config->rc_max_quantizer,
                  &warning_list);
  check_lag_in_frames_realtime_deadline(stream_config->g_lag_in_frames,
                                        global_config->deadline,
                                        &warning_list);

  /* Count and print warnings. */
  warning = warning_list.warning_node;
  while (warning != NULL) {
    ++num_warnings;
    warn(warning->warning_string);
    warning = warning->next_warning;
  }

  FreeWarningList(&warning_list);

  if (num_warnings) {
    if (!disable_prompt && !continue_prompt(num_warnings))
      exit(EXIT_FAILURE);
  }
}
