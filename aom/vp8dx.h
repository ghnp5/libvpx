/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*!\defgroup vp8_decoder WebM VP8/VP9 Decoder
 * \ingroup vp8
 *
 * @{
 */
/*!\file
 * \brief Provides definitions for using VP8 or VP9 within the vpx Decoder
 *        interface.
 */
#ifndef AOM_VP8DX_H_
#define AOM_VP8DX_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Include controls common to both the encoder and decoder */
#include "./aom.h"

/*!\name Algorithm interface for VP10
 *
 * This interface provides the capability to decode VP10 streams.
 * @{
 */
extern aom_codec_iface_t aom_codec_av1_dx_algo;
extern aom_codec_iface_t *aom_codec_av1_dx(void);
/*!@} - end algorithm interface member group*/

/*!\enum aom_dec_control_id
 * \brief VP8 decoder control functions
 *
 * This set of macros define the control functions available for the VP8
 * decoder interface.
 *
 * \sa #aom_codec_control
 */
enum aom_dec_control_id {
  /** control function to get info on which reference frames were updated
   *  by the last decode
   */
  AOMD_GET_LAST_REF_UPDATES = AOM_DECODER_CTRL_ID_START,

  /** check if the indicated frame is corrupted */
  AOMD_GET_FRAME_CORRUPTED,

  /** control function to get info on which reference frames were used
   *  by the last decode
   */
  AOMD_GET_LAST_REF_USED,

  /** decryption function to decrypt encoded buffer data immediately
   * before decoding. Takes a aom_decrypt_init, which contains
   * a callback function and opaque context pointer.
   */
  AOMD_SET_DECRYPTOR,
  AOMD_SET_DECRYPTOR = AOMD_SET_DECRYPTOR,

  /** control function to get the dimensions that the current frame is decoded
   * at. This may be different to the intended display size for the frame as
   * specified in the wrapper or frame header (see AV1D_GET_DISPLAY_SIZE). */
  AV1D_GET_FRAME_SIZE,

  /** control function to get the current frame's intended display dimensions
   * (as specified in the wrapper or frame header). This may be different to
   * the decoded dimensions of this frame (see AV1D_GET_FRAME_SIZE). */
  AV1D_GET_DISPLAY_SIZE,

  /** control function to get the bit depth of the stream. */
  AV1D_GET_BIT_DEPTH,

  /** control function to set the byte alignment of the planes in the reference
   * buffers. Valid values are power of 2, from 32 to 1024. A value of 0 sets
   * legacy alignment. I.e. Y plane is aligned to 32 bytes, U plane directly
   * follows Y plane, and V plane directly follows U plane. Default value is 0.
   */
  AV1_SET_BYTE_ALIGNMENT,

  /** control function to invert the decoding order to from right to left. The
   * function is used in a test to confirm the decoding independence of tile
   * columns. The function may be used in application where this order
   * of decoding is desired.
   *
   * TODO(yaowu): Rework the unit test that uses this control, and in a future
   *              release, this test-only control shall be removed.
   */
  AV1_INVERT_TILE_DECODE_ORDER,

  /** control function to set the skip loop filter flag. Valid values are
   * integers. The decoder will skip the loop filter when its value is set to
   * nonzero. If the loop filter is skipped the decoder may accumulate decode
   * artifacts. The default value is 0.
   */
  AV1_SET_SKIP_LOOP_FILTER,

  AOM_DECODER_CTRL_ID_MAX,

  /** control function to set the range of tile decoding. A value that is
   * greater and equal to zero indicates only the specific row/column is
   * decoded. A value that is -1 indicates the whole row/column is decoded.
   * A special case is both values are -1 that means the whole frame is
   * decoded.
   */
  AV1_SET_DECODE_TILE_ROW,
  AV1_SET_DECODE_TILE_COL
};

/** Decrypt n bytes of data from input -> output, using the decrypt_state
 *  passed in AOMD_SET_DECRYPTOR.
 */
typedef void (*aom_decrypt_cb)(void *decrypt_state, const unsigned char *input,
                               unsigned char *output, int count);

/*!\brief Structure to hold decryption state
 *
 * Defines a structure to hold the decryption state and access function.
 */
typedef struct aom_decrypt_init {
  /*! Decrypt callback. */
  aom_decrypt_cb decrypt_cb;

  /*! Decryption state. */
  void *decrypt_state;
} aom_decrypt_init;

/*!\brief A deprecated alias for aom_decrypt_init.
 */
typedef aom_decrypt_init aom_decrypt_init;

/*!\cond */
/*!\brief VP8 decoder control function parameter type
 *
 * Defines the data types that VP8D control functions take. Note that
 * additional common controls are defined in aom.h
 *
 */

AOM_CTRL_USE_TYPE(AOMD_GET_LAST_REF_UPDATES, int *)
#define AOM_CTRL_AOMD_GET_LAST_REF_UPDATES
AOM_CTRL_USE_TYPE(AOMD_GET_FRAME_CORRUPTED, int *)
#define AOM_CTRL_AOMD_GET_FRAME_CORRUPTED
AOM_CTRL_USE_TYPE(AOMD_GET_LAST_REF_USED, int *)
#define AOM_CTRL_AOMD_GET_LAST_REF_USED
AOM_CTRL_USE_TYPE(AOMD_SET_DECRYPTOR, aom_decrypt_init *)
#define AOM_CTRL_AOMD_SET_DECRYPTOR
AOM_CTRL_USE_TYPE(AOMD_SET_DECRYPTOR, aom_decrypt_init *)
#define AOM_CTRL_AOMD_SET_DECRYPTOR
AOM_CTRL_USE_TYPE(AV1D_GET_DISPLAY_SIZE, int *)
#define AOM_CTRL_AV1D_GET_DISPLAY_SIZE
AOM_CTRL_USE_TYPE(AV1D_GET_BIT_DEPTH, unsigned int *)
#define AOM_CTRL_AV1D_GET_BIT_DEPTH
AOM_CTRL_USE_TYPE(AV1D_GET_FRAME_SIZE, int *)
#define AOM_CTRL_AV1D_GET_FRAME_SIZE
AOM_CTRL_USE_TYPE(AV1_INVERT_TILE_DECODE_ORDER, int)
#define AOM_CTRL_AV1_INVERT_TILE_DECODE_ORDER
AOM_CTRL_USE_TYPE(AV1_SET_DECODE_TILE_ROW, int)
#define AOM_CTRL_AV1_SET_DECODE_TILE_ROW
AOM_CTRL_USE_TYPE(AV1_SET_DECODE_TILE_COL, int)
#define AOM_CTRL_AV1_SET_DECODE_TILE_COL
/*!\endcond */
/*! @} - end defgroup vp8_decoder */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_VP8DX_H_
