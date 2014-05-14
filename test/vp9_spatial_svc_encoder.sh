#!/bin/sh
##
##  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
##
##  This file tests the libvpx vp9_spatial_svc_encoder example. To add new
##  tests to to this file, do the following:
##    1. Write a shell function (this is your test).
##    2. Add the function to vp9_spatial_svc_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

# Environment check: $YUV_RAW_INPUT is required.
vp9_spatial_svc_encoder_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    echo "Libvpx test data must exist in LIBVPX_TEST_DATA_PATH."
    return 1
  fi
}

# Runs vp9_spatial_svc_encoder. $1 is the test name.
vp9_spatial_svc_encoder() {
  local encoder="${LIBVPX_BIN_PATH}/vp9_spatial_svc_encoder"
  encoder="${encoder}${VPX_TEST_EXE_SUFFIX}"
  local test_name="$1"
  local output_file="${VPX_TEST_OUTPUT_DIR}/vp9_ssvc_encoder${test_name}.ivf"
  local frames_to_encode="10"
  local max_kf="9999"

  shift

  [ -x "${encoder}" ] || return 1

  eval "${encoder}" -w "${YUV_RAW_INPUT_WIDTH}" -h "${YUV_RAW_INPUT_HEIGHT}" \
      -k "${max_kf}" -f "${frames_to_encode}" "$@" "${YUV_RAW_INPUT}" \
      "${output_file}" \
      ${devnull}

  [ -e "${output_file}" ] || return 1
}

vp9_spatial_svc_mode_i_layers_1() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m i -l 1
  fi
}

vp9_spatial_svc_mode_i_layers_2() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m i -l 2
  fi
}

vp9_spatial_svc_mode_i_layers_3() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m i -l 3
  fi
}

vp9_spatial_svc_mode_altip_layers_1() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m "alt-ip" -l 1
  fi
}

vp9_spatial_svc_mode_altip_layers_2() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m "alt-ip" -l 2
  fi
}

vp9_spatial_svc_mode_altip_layers_3() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m "alt-ip" -l 3
  fi
}

vp9_spatial_svc_mode_ip_layers_1() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m ip -l 1
  fi
}

vp9_spatial_svc_mode_ip_layers_2() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m ip -l 2
  fi
}

vp9_spatial_svc_mode_ip_layers_3() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m ip -l 3
  fi
}

vp9_spatial_svc_mode_gf_layers_1() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m gf -l 1
  fi
}

vp9_spatial_svc_mode_gf_layers_2() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m gf -l 2
  fi
}

vp9_spatial_svc_mode_gf_layers_3() {
  if [ "$(vp9_encode_available)" = "yes" ]; then
    local test_name="${FUNCNAME}"
    vp9_spatial_svc_encoder "${test_name}" -m gf -l 3
  fi
}

vp9_spatial_svc_tests="vp9_spatial_svc_mode_i_layers_1
                       vp9_spatial_svc_mode_i_layers_2
                       vp9_spatial_svc_mode_i_layers_3
                       vp9_spatial_svc_mode_altip_layers_1
                       vp9_spatial_svc_mode_altip_layers_2
                       vp9_spatial_svc_mode_altip_layers_3
                       vp9_spatial_svc_mode_ip_layers_1
                       vp9_spatial_svc_mode_ip_layers_2
                       vp9_spatial_svc_mode_ip_layers_3
                       vp9_spatial_svc_mode_gf_layers_1
                       vp9_spatial_svc_mode_gf_layers_2
                       vp9_spatial_svc_mode_gf_layers_3"

run_tests vp9_spatial_svc_encoder_verify_environment "${vp9_spatial_svc_tests}"
