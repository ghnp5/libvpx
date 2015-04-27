/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_mem/vpx_mem.h"
#include "vp9/common/mips/msa/vp9_macros_msa.h"

static void copy_width8_msa(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height) {
  int32_t cnt;
  uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
  v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

  if (0 == height % 12) {
    for (cnt = (height / 12); cnt--;) {
      LOAD_8VECS_UB(src, src_stride,
                    src0, src1, src2, src3, src4, src5, src6, src7);
      src += (8 * src_stride);

      out0 = __msa_copy_u_d((v2i64)src0, 0);
      out1 = __msa_copy_u_d((v2i64)src1, 0);
      out2 = __msa_copy_u_d((v2i64)src2, 0);
      out3 = __msa_copy_u_d((v2i64)src3, 0);
      out4 = __msa_copy_u_d((v2i64)src4, 0);
      out5 = __msa_copy_u_d((v2i64)src5, 0);
      out6 = __msa_copy_u_d((v2i64)src6, 0);
      out7 = __msa_copy_u_d((v2i64)src7, 0);

      STORE_DWORD(dst, out0);
      dst += dst_stride;
      STORE_DWORD(dst, out1);
      dst += dst_stride;
      STORE_DWORD(dst, out2);
      dst += dst_stride;
      STORE_DWORD(dst, out3);
      dst += dst_stride;
      STORE_DWORD(dst, out4);
      dst += dst_stride;
      STORE_DWORD(dst, out5);
      dst += dst_stride;
      STORE_DWORD(dst, out6);
      dst += dst_stride;
      STORE_DWORD(dst, out7);
      dst += dst_stride;

      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      src += (4 * src_stride);

      out0 = __msa_copy_u_d((v2i64)src0, 0);
      out1 = __msa_copy_u_d((v2i64)src1, 0);
      out2 = __msa_copy_u_d((v2i64)src2, 0);
      out3 = __msa_copy_u_d((v2i64)src3, 0);

      STORE_DWORD(dst, out0);
      dst += dst_stride;
      STORE_DWORD(dst, out1);
      dst += dst_stride;
      STORE_DWORD(dst, out2);
      dst += dst_stride;
      STORE_DWORD(dst, out3);
      dst += dst_stride;
    }
  } else if (0 == height % 8) {
    for (cnt = height >> 3; cnt--;) {
      LOAD_8VECS_UB(src, src_stride,
                    src0, src1, src2, src3, src4, src5, src6, src7);
      src += (8 * src_stride);

      out0 = __msa_copy_u_d((v2i64)src0, 0);
      out1 = __msa_copy_u_d((v2i64)src1, 0);
      out2 = __msa_copy_u_d((v2i64)src2, 0);
      out3 = __msa_copy_u_d((v2i64)src3, 0);
      out4 = __msa_copy_u_d((v2i64)src4, 0);
      out5 = __msa_copy_u_d((v2i64)src5, 0);
      out6 = __msa_copy_u_d((v2i64)src6, 0);
      out7 = __msa_copy_u_d((v2i64)src7, 0);

      STORE_DWORD(dst, out0);
      dst += dst_stride;
      STORE_DWORD(dst, out1);
      dst += dst_stride;
      STORE_DWORD(dst, out2);
      dst += dst_stride;
      STORE_DWORD(dst, out3);
      dst += dst_stride;
      STORE_DWORD(dst, out4);
      dst += dst_stride;
      STORE_DWORD(dst, out5);
      dst += dst_stride;
      STORE_DWORD(dst, out6);
      dst += dst_stride;
      STORE_DWORD(dst, out7);
      dst += dst_stride;
    }
  } else if (0 == height % 4) {
    for (cnt = (height / 4); cnt--;) {
      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      src += (4 * src_stride);

      out0 = __msa_copy_u_d((v2i64)src0, 0);
      out1 = __msa_copy_u_d((v2i64)src1, 0);
      out2 = __msa_copy_u_d((v2i64)src2, 0);
      out3 = __msa_copy_u_d((v2i64)src3, 0);

      STORE_DWORD(dst, out0);
      dst += dst_stride;
      STORE_DWORD(dst, out1);
      dst += dst_stride;
      STORE_DWORD(dst, out2);
      dst += dst_stride;
      STORE_DWORD(dst, out3);
      dst += dst_stride;
    }
  } else if (0 == height % 2) {
    for (cnt = (height / 2); cnt--;) {
      LOAD_2VECS_UB(src, src_stride, src0, src1);
      src += (2 * src_stride);

      out0 = __msa_copy_u_d((v2i64)src0, 0);
      out1 = __msa_copy_u_d((v2i64)src1, 0);

      STORE_DWORD(dst, out0);
      dst += dst_stride;
      STORE_DWORD(dst, out1);
      dst += dst_stride;
    }
  }
}

static void copy_16multx8mult_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  int32_t height, int32_t width) {
  int32_t cnt, loop_cnt;
  const uint8_t *src_tmp;
  uint8_t *dst_tmp;
  v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

  for (cnt = (width >> 4); cnt--;) {
    src_tmp = src;
    dst_tmp = dst;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
      LOAD_8VECS_UB(src_tmp, src_stride,
                    src0, src1, src2, src3, src4, src5, src6, src7);
      src_tmp += (8 * src_stride);

      STORE_8VECS_UB(dst_tmp, dst_stride,
                     src0, src1, src2, src3, src4, src5, src6, src7);
      dst_tmp += (8 * dst_stride);
    }

    src += 16;
    dst += 16;
  }
}

static void copy_width16_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height) {
  int32_t cnt;
  v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

  if (0 == height % 12) {
    for (cnt = (height / 12); cnt--;) {
      LOAD_8VECS_UB(src, src_stride,
                    src0, src1, src2, src3, src4, src5, src6, src7);
      src += (8 * src_stride);

      STORE_8VECS_UB(dst, dst_stride,
                     src0, src1, src2, src3, src4, src5, src6, src7);
      dst += (8 * dst_stride);

      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      src += (4 * src_stride);

      STORE_4VECS_UB(dst, dst_stride, src0, src1, src2, src3);
      dst += (4 * dst_stride);
    }
  } else if (0 == height % 8) {
    copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 16);
  } else if (0 == height % 4) {
    for (cnt = (height >> 2); cnt--;) {
      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      src += (4 * src_stride);

      STORE_4VECS_UB(dst, dst_stride, src0, src1, src2, src3);
      dst += (4 * dst_stride);
    }
  }
}

static void copy_width32_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height) {
  int32_t cnt;
  v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

  if (0 == height % 12) {
    for (cnt = (height / 12); cnt--;) {
      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      LOAD_4VECS_UB(src + 16, src_stride, src4, src5, src6, src7);
      src += (4 * src_stride);

      STORE_4VECS_UB(dst, dst_stride, src0, src1, src2, src3);
      STORE_4VECS_UB(dst + 16, dst_stride, src4, src5, src6, src7);
      dst += (4 * dst_stride);

      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      LOAD_4VECS_UB(src + 16, src_stride, src4, src5, src6, src7);
      src += (4 * src_stride);

      STORE_4VECS_UB(dst, dst_stride, src0, src1, src2, src3);
      STORE_4VECS_UB(dst + 16, dst_stride, src4, src5, src6, src7);
      dst += (4 * dst_stride);

      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      LOAD_4VECS_UB(src + 16, src_stride, src4, src5, src6, src7);
      src += (4 * src_stride);

      STORE_4VECS_UB(dst, dst_stride, src0, src1, src2, src3);
      STORE_4VECS_UB(dst + 16, dst_stride, src4, src5, src6, src7);
      dst += (4 * dst_stride);
    }
  } else if (0 == height % 8) {
    copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 32);
  } else if (0 == height % 4) {
    for (cnt = (height >> 2); cnt--;) {
      LOAD_4VECS_UB(src, src_stride, src0, src1, src2, src3);
      LOAD_4VECS_UB(src + 16, src_stride, src4, src5, src6, src7);
      src += (4 * src_stride);

      STORE_4VECS_UB(dst, dst_stride, src0, src1, src2, src3);
      STORE_4VECS_UB(dst + 16, dst_stride, src4, src5, src6, src7);
      dst += (4 * dst_stride);
    }
  }
}

static void copy_width64_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height) {
  copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 64);
}

void vp9_convolve_copy_msa(const uint8_t *src, ptrdiff_t src_stride,
                           uint8_t *dst, ptrdiff_t dst_stride,
                           const int16_t *filter_x, int32_t filter_x_stride,
                           const int16_t *filter_y, int32_t filter_y_stride,
                           int32_t w, int32_t h) {
  (void)filter_x;  (void)filter_x_stride;
  (void)filter_y;  (void)filter_y_stride;

  switch (w) {
    case 4: {
      uint32_t cnt, tmp;
      /* 1 word storage */
      for (cnt = h; cnt--;) {
        tmp = LOAD_WORD(src);
        STORE_WORD(dst, tmp);
        src += src_stride;
        dst += dst_stride;
      }
    }
    break;
    case 8: {
      copy_width8_msa(src, src_stride, dst, dst_stride, h);
    }
    break;
    case 16: {
      copy_width16_msa(src, src_stride, dst, dst_stride, h);
    }
    break;
    case 32: {
      copy_width32_msa(src, src_stride, dst, dst_stride, h);
    }
    break;
    case 64: {
      copy_width64_msa(src, src_stride, dst, dst_stride, h);
    }
    break;
    default: {
      uint32_t cnt;
      for (cnt = h; cnt--;) {
        vpx_memcpy(dst, src, w);
        src += src_stride;
        dst += dst_stride;
      }
    }
    break;
  }
}
