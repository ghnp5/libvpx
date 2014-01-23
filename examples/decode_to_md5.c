/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Frame-by-frame MD5 Checksum
// ===========================
//
// This example builds upon the simple decoder loop to show how checksums
// of the decoded output can be generated. These are used for validating
// decoder implementations against the reference implementation, for example.
//
// MD5 algorithm
// -------------
// The Message-Digest 5 (MD5) is a well known hash function. We have provided
// an implementation derived from the RSA Data Security, Inc. MD5 Message-Digest
// Algorithm for your use. Our implmentation only changes the interface of this
// reference code. You must include the `md5_utils.h` header for access to these
// functions.
//
// Processing The Decoded Data
// ---------------------------
// Each row of the image is passed to the MD5 accumulator. First the Y plane
// is processed, then U, then V. It is important to honor the image's `stride`
// values.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VPX_CODEC_DISABLE_COMPAT 1

#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

#include "./ivfdec.h"
#include "./md5_utils.h"
#include "./vpx_config.h"

#define VP8_FOURCC 0x30385056
#define VP9_FOURCC 0x30395056

static vpx_codec_iface_t *get_codec_interface(unsigned int fourcc) {
  switch (fourcc) {
    case VP8_FOURCC:
      return vpx_codec_vp8_dx();
    case VP9_FOURCC:
      return vpx_codec_vp9_dx();
  }
  return NULL;
}

void usage_exit() {
  exit(EXIT_FAILURE);
}

static void die_args(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  if (fmt[strlen(fmt) - 1] != '\n')
    printf("\n");
  exit(EXIT_FAILURE);
}

static void die_codec(vpx_codec_ctx_t *ctx, const char *s) {
  const char *detail = vpx_codec_error_detail(ctx);

  printf("%s: %s\n", s, vpx_codec_error(ctx));
  if(detail)
    printf("    %s\n",detail);
  exit(EXIT_FAILURE);
}

static void get_image_md5(const vpx_image_t *img, unsigned char digest[16]) {
  int plane, y;
  MD5Context md5;

  MD5Init(&md5);

  for (plane = 0; plane < 3; ++plane) {
    const unsigned char *buf = img->planes[plane];
    const int stride = img->stride[plane];
    const int w = plane ? (img->d_w + 1) >> 1 : img->d_w;
    const int h = plane ? (img->d_h + 1) >> 1 : img->d_h;

    for (y = 0; y < h; ++y) {
      MD5Update(&md5, buf, w);
      buf += stride;
    }
  }

  MD5Final(digest, &md5);
}

static void print_md5(FILE *stream, unsigned char digest[16]) {
  int i;

  for (i = 0; i < 16; ++i)
    fprintf(stream, "%02x", digest[i]);
}

int main(int argc, char **argv) {
  FILE *infile, *outfile;
  vpx_codec_ctx_t codec;
  vpx_codec_iface_t *iface;
  int flags = 0, frame_cnt = 0;
  vpx_video_t *video;

  if (argc != 3)
    die_args("Usage: %s <infile> <outfile>\n", argv[0]);

  if (!(infile = fopen(argv[1], "rb")))
    die_args("Failed to open %s for reading", argv[1]);

  if (!(outfile = fopen(argv[2], "wb")))
    die_args("Failed to open %s for writing", argv[2]);

  video = vpx_video_open_file(infile);
  if (!video)
    die_args("%s is not an IVF file.", argv[1]);

  iface = get_codec_interface(vpx_video_get_fourcc(video));
  if (!iface)
    die_args("Unknown FOURCC code.");

  printf("Using %s\n", vpx_codec_iface_name(iface));

  if (vpx_codec_dec_init(&codec, iface, NULL, flags))
    die_codec(&codec, "Failed to initialize decoder");

  while (vpx_video_read_frame(video)) {
    vpx_codec_iter_t iter = NULL;
    vpx_image_t *img;
    size_t frame_size;
    const unsigned char *frame = vpx_video_get_frame(video, &frame_size);
    if (vpx_codec_decode(&codec, frame, frame_size, NULL, 0))
      die_codec(&codec, "Failed to decode frame");

    while ((img = vpx_codec_get_frame(&codec, &iter)) != NULL) {
      unsigned char digest[16];

      get_image_md5(img, digest);
      print_md5(outfile, digest);
      fprintf(outfile, "  img-%dx%d-%04d.i420\n",
              img->d_w, img->d_h, ++frame_cnt);
    }
  }

  printf("Processed %d frames.\n", frame_cnt);
  if (vpx_codec_destroy(&codec))
    die_codec(&codec, "Failed to destroy codec");

  vpx_video_close(video);

  fclose(outfile);
  fclose(infile);
  return EXIT_SUCCESS;
}
