/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vp10/common/entropy.h"
#include "vp10/common/blockd.h"
#include "vp10/common/onyxc_int.h"
#include "vp10/common/entropymode.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx/vpx_integer.h"

// Unconstrained Node Tree
const vpx_tree_index vp10_coef_con_tree[TREE_SIZE(ENTROPY_TOKENS)] = {
  2, 6,                                // 0 = LOW_VAL
  -TWO_TOKEN, 4,                       // 1 = TWO
  -THREE_TOKEN, -FOUR_TOKEN,           // 2 = THREE
  8, 10,                               // 3 = HIGH_LOW
  -CATEGORY1_TOKEN, -CATEGORY2_TOKEN,  // 4 = CAT_ONE
  12, 14,                              // 5 = CAT_THREEFOUR
  -CATEGORY3_TOKEN, -CATEGORY4_TOKEN,  // 6 = CAT_THREE
  -CATEGORY5_TOKEN, -CATEGORY6_TOKEN   // 7 = CAT_FIVE
};

const vpx_prob vp10_cat1_prob[] = { 159 };
const vpx_prob vp10_cat2_prob[] = { 165, 145 };
const vpx_prob vp10_cat3_prob[] = { 173, 148, 140 };
const vpx_prob vp10_cat4_prob[] = { 176, 155, 140, 135 };
const vpx_prob vp10_cat5_prob[] = { 180, 157, 141, 134, 130 };
const vpx_prob vp10_cat6_prob[] = {
    254, 254, 254, 252, 249, 243, 230, 196, 177, 153, 140, 133, 130, 129
};
#if CONFIG_VP9_HIGHBITDEPTH
const vpx_prob vp10_cat1_prob_high10[] = { 159 };
const vpx_prob vp10_cat2_prob_high10[] = { 165, 145 };
const vpx_prob vp10_cat3_prob_high10[] = { 173, 148, 140 };
const vpx_prob vp10_cat4_prob_high10[] = { 176, 155, 140, 135 };
const vpx_prob vp10_cat5_prob_high10[] = { 180, 157, 141, 134, 130 };
const vpx_prob vp10_cat6_prob_high10[] = {
    255, 255, 254, 254, 254, 252, 249, 243,
    230, 196, 177, 153, 140, 133, 130, 129
};
const vpx_prob vp10_cat1_prob_high12[] = { 159 };
const vpx_prob vp10_cat2_prob_high12[] = { 165, 145 };
const vpx_prob vp10_cat3_prob_high12[] = { 173, 148, 140 };
const vpx_prob vp10_cat4_prob_high12[] = { 176, 155, 140, 135 };
const vpx_prob vp10_cat5_prob_high12[] = { 180, 157, 141, 134, 130 };
const vpx_prob vp10_cat6_prob_high12[] = {
    255, 255, 255, 255, 254, 254, 254, 252, 249,
    243, 230, 196, 177, 153, 140, 133, 130, 129
};
#endif

const uint8_t vp10_coefband_trans_8x8plus[1024] = {
  0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 5,
  // beyond MAXBAND_INDEX+1 all values are filled as 5
                    5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

const uint8_t vp10_coefband_trans_4x4[16] = {
  0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5,
};

const uint8_t vp10_pt_energy_class[ENTROPY_TOKENS] = {
  0, 1, 2, 3, 3, 4, 4, 5, 5, 5, 5, 5
};

// Model obtained from a 2-sided zero-centerd distribuition derived
// from a Pareto distribution. The cdf of the distribution is:
// cdf(x) = 0.5 + 0.5 * sgn(x) * [1 - {alpha/(alpha + |x|)} ^ beta]
//
// For a given beta and a given probablity of the 1-node, the alpha
// is first solved, and then the {alpha, beta} pair is used to generate
// the probabilities for the rest of the nodes.

// beta = 8

// Every odd line in this table can be generated from the even lines
// by averaging :
// vp10_pareto8_full[l][node] = (vp10_pareto8_full[l-1][node] +
//                              vp10_pareto8_full[l+1][node] ) >> 1;
const vpx_prob vp10_pareto8_full[COEFF_PROB_MODELS][MODEL_NODES] = {
  {  3,  86, 128,   6,  86,  23,  88,  29},
  {  6,  86, 128,  11,  87,  42,  91,  52},
  {  9,  86, 129,  17,  88,  61,  94,  76},
  { 12,  86, 129,  22,  88,  77,  97,  93},
  { 15,  87, 129,  28,  89,  93, 100, 110},
  { 17,  87, 129,  33,  90, 105, 103, 123},
  { 20,  88, 130,  38,  91, 118, 106, 136},
  { 23,  88, 130,  43,  91, 128, 108, 146},
  { 26,  89, 131,  48,  92, 139, 111, 156},
  { 28,  89, 131,  53,  93, 147, 114, 163},
  { 31,  90, 131,  58,  94, 156, 117, 171},
  { 34,  90, 131,  62,  94, 163, 119, 177},
  { 37,  90, 132,  66,  95, 171, 122, 184},
  { 39,  90, 132,  70,  96, 177, 124, 189},
  { 42,  91, 132,  75,  97, 183, 127, 194},
  { 44,  91, 132,  79,  97, 188, 129, 198},
  { 47,  92, 133,  83,  98, 193, 132, 202},
  { 49,  92, 133,  86,  99, 197, 134, 205},
  { 52,  93, 133,  90, 100, 201, 137, 208},
  { 54,  93, 133,  94, 100, 204, 139, 211},
  { 57,  94, 134,  98, 101, 208, 142, 214},
  { 59,  94, 134, 101, 102, 211, 144, 216},
  { 62,  94, 135, 105, 103, 214, 146, 218},
  { 64,  94, 135, 108, 103, 216, 148, 220},
  { 66,  95, 135, 111, 104, 219, 151, 222},
  { 68,  95, 135, 114, 105, 221, 153, 223},
  { 71,  96, 136, 117, 106, 224, 155, 225},
  { 73,  96, 136, 120, 106, 225, 157, 226},
  { 76,  97, 136, 123, 107, 227, 159, 228},
  { 78,  97, 136, 126, 108, 229, 160, 229},
  { 80,  98, 137, 129, 109, 231, 162, 231},
  { 82,  98, 137, 131, 109, 232, 164, 232},
  { 84,  98, 138, 134, 110, 234, 166, 233},
  { 86,  98, 138, 137, 111, 235, 168, 234},
  { 89,  99, 138, 140, 112, 236, 170, 235},
  { 91,  99, 138, 142, 112, 237, 171, 235},
  { 93, 100, 139, 145, 113, 238, 173, 236},
  { 95, 100, 139, 147, 114, 239, 174, 237},
  { 97, 101, 140, 149, 115, 240, 176, 238},
  { 99, 101, 140, 151, 115, 241, 177, 238},
  {101, 102, 140, 154, 116, 242, 179, 239},
  {103, 102, 140, 156, 117, 242, 180, 239},
  {105, 103, 141, 158, 118, 243, 182, 240},
  {107, 103, 141, 160, 118, 243, 183, 240},
  {109, 104, 141, 162, 119, 244, 185, 241},
  {111, 104, 141, 164, 119, 244, 186, 241},
  {113, 104, 142, 166, 120, 245, 187, 242},
  {114, 104, 142, 168, 121, 245, 188, 242},
  {116, 105, 143, 170, 122, 246, 190, 243},
  {118, 105, 143, 171, 122, 246, 191, 243},
  {120, 106, 143, 173, 123, 247, 192, 244},
  {121, 106, 143, 175, 124, 247, 193, 244},
  {123, 107, 144, 177, 125, 248, 195, 244},
  {125, 107, 144, 178, 125, 248, 196, 244},
  {127, 108, 145, 180, 126, 249, 197, 245},
  {128, 108, 145, 181, 127, 249, 198, 245},
  {130, 109, 145, 183, 128, 249, 199, 245},
  {132, 109, 145, 184, 128, 249, 200, 245},
  {134, 110, 146, 186, 129, 250, 201, 246},
  {135, 110, 146, 187, 130, 250, 202, 246},
  {137, 111, 147, 189, 131, 251, 203, 246},
  {138, 111, 147, 190, 131, 251, 204, 246},
  {140, 112, 147, 192, 132, 251, 205, 247},
  {141, 112, 147, 193, 132, 251, 206, 247},
  {143, 113, 148, 194, 133, 251, 207, 247},
  {144, 113, 148, 195, 134, 251, 207, 247},
  {146, 114, 149, 197, 135, 252, 208, 248},
  {147, 114, 149, 198, 135, 252, 209, 248},
  {149, 115, 149, 199, 136, 252, 210, 248},
  {150, 115, 149, 200, 137, 252, 210, 248},
  {152, 115, 150, 201, 138, 252, 211, 248},
  {153, 115, 150, 202, 138, 252, 212, 248},
  {155, 116, 151, 204, 139, 253, 213, 249},
  {156, 116, 151, 205, 139, 253, 213, 249},
  {158, 117, 151, 206, 140, 253, 214, 249},
  {159, 117, 151, 207, 141, 253, 215, 249},
  {161, 118, 152, 208, 142, 253, 216, 249},
  {162, 118, 152, 209, 142, 253, 216, 249},
  {163, 119, 153, 210, 143, 253, 217, 249},
  {164, 119, 153, 211, 143, 253, 217, 249},
  {166, 120, 153, 212, 144, 254, 218, 250},
  {167, 120, 153, 212, 145, 254, 219, 250},
  {168, 121, 154, 213, 146, 254, 220, 250},
  {169, 121, 154, 214, 146, 254, 220, 250},
  {171, 122, 155, 215, 147, 254, 221, 250},
  {172, 122, 155, 216, 147, 254, 221, 250},
  {173, 123, 155, 217, 148, 254, 222, 250},
  {174, 123, 155, 217, 149, 254, 222, 250},
  {176, 124, 156, 218, 150, 254, 223, 250},
  {177, 124, 156, 219, 150, 254, 223, 250},
  {178, 125, 157, 220, 151, 254, 224, 251},
  {179, 125, 157, 220, 151, 254, 224, 251},
  {180, 126, 157, 221, 152, 254, 225, 251},
  {181, 126, 157, 221, 152, 254, 225, 251},
  {183, 127, 158, 222, 153, 254, 226, 251},
  {184, 127, 158, 223, 154, 254, 226, 251},
  {185, 128, 159, 224, 155, 255, 227, 251},
  {186, 128, 159, 224, 155, 255, 227, 251},
  {187, 129, 160, 225, 156, 255, 228, 251},
  {188, 130, 160, 225, 156, 255, 228, 251},
  {189, 131, 160, 226, 157, 255, 228, 251},
  {190, 131, 160, 226, 158, 255, 228, 251},
  {191, 132, 161, 227, 159, 255, 229, 251},
  {192, 132, 161, 227, 159, 255, 229, 251},
  {193, 133, 162, 228, 160, 255, 230, 252},
  {194, 133, 162, 229, 160, 255, 230, 252},
  {195, 134, 163, 230, 161, 255, 231, 252},
  {196, 134, 163, 230, 161, 255, 231, 252},
  {197, 135, 163, 231, 162, 255, 231, 252},
  {198, 135, 163, 231, 162, 255, 231, 252},
  {199, 136, 164, 232, 163, 255, 232, 252},
  {200, 136, 164, 232, 164, 255, 232, 252},
  {201, 137, 165, 233, 165, 255, 233, 252},
  {201, 137, 165, 233, 165, 255, 233, 252},
  {202, 138, 166, 233, 166, 255, 233, 252},
  {203, 138, 166, 233, 166, 255, 233, 252},
  {204, 139, 166, 234, 167, 255, 234, 252},
  {205, 139, 166, 234, 167, 255, 234, 252},
  {206, 140, 167, 235, 168, 255, 235, 252},
  {206, 140, 167, 235, 168, 255, 235, 252},
  {207, 141, 168, 236, 169, 255, 235, 252},
  {208, 141, 168, 236, 170, 255, 235, 252},
  {209, 142, 169, 237, 171, 255, 236, 252},
  {209, 143, 169, 237, 171, 255, 236, 252},
  {210, 144, 169, 237, 172, 255, 236, 252},
  {211, 144, 169, 237, 172, 255, 236, 252},
  {212, 145, 170, 238, 173, 255, 237, 252},
  {213, 145, 170, 238, 173, 255, 237, 252},
  {214, 146, 171, 239, 174, 255, 237, 253},
  {214, 146, 171, 239, 174, 255, 237, 253},
  {215, 147, 172, 240, 175, 255, 238, 253},
  {215, 147, 172, 240, 175, 255, 238, 253},
  {216, 148, 173, 240, 176, 255, 238, 253},
  {217, 148, 173, 240, 176, 255, 238, 253},
  {218, 149, 173, 241, 177, 255, 239, 253},
  {218, 149, 173, 241, 178, 255, 239, 253},
  {219, 150, 174, 241, 179, 255, 239, 253},
  {219, 151, 174, 241, 179, 255, 239, 253},
  {220, 152, 175, 242, 180, 255, 240, 253},
  {221, 152, 175, 242, 180, 255, 240, 253},
  {222, 153, 176, 242, 181, 255, 240, 253},
  {222, 153, 176, 242, 181, 255, 240, 253},
  {223, 154, 177, 243, 182, 255, 240, 253},
  {223, 154, 177, 243, 182, 255, 240, 253},
  {224, 155, 178, 244, 183, 255, 241, 253},
  {224, 155, 178, 244, 183, 255, 241, 253},
  {225, 156, 178, 244, 184, 255, 241, 253},
  {225, 157, 178, 244, 184, 255, 241, 253},
  {226, 158, 179, 244, 185, 255, 242, 253},
  {227, 158, 179, 244, 185, 255, 242, 253},
  {228, 159, 180, 245, 186, 255, 242, 253},
  {228, 159, 180, 245, 186, 255, 242, 253},
  {229, 160, 181, 245, 187, 255, 242, 253},
  {229, 160, 181, 245, 187, 255, 242, 253},
  {230, 161, 182, 246, 188, 255, 243, 253},
  {230, 162, 182, 246, 188, 255, 243, 253},
  {231, 163, 183, 246, 189, 255, 243, 253},
  {231, 163, 183, 246, 189, 255, 243, 253},
  {232, 164, 184, 247, 190, 255, 243, 253},
  {232, 164, 184, 247, 190, 255, 243, 253},
  {233, 165, 185, 247, 191, 255, 244, 253},
  {233, 165, 185, 247, 191, 255, 244, 253},
  {234, 166, 185, 247, 192, 255, 244, 253},
  {234, 167, 185, 247, 192, 255, 244, 253},
  {235, 168, 186, 248, 193, 255, 244, 253},
  {235, 168, 186, 248, 193, 255, 244, 253},
  {236, 169, 187, 248, 194, 255, 244, 253},
  {236, 169, 187, 248, 194, 255, 244, 253},
  {236, 170, 188, 248, 195, 255, 245, 253},
  {236, 170, 188, 248, 195, 255, 245, 253},
  {237, 171, 189, 249, 196, 255, 245, 254},
  {237, 172, 189, 249, 196, 255, 245, 254},
  {238, 173, 190, 249, 197, 255, 245, 254},
  {238, 173, 190, 249, 197, 255, 245, 254},
  {239, 174, 191, 249, 198, 255, 245, 254},
  {239, 174, 191, 249, 198, 255, 245, 254},
  {240, 175, 192, 249, 199, 255, 246, 254},
  {240, 176, 192, 249, 199, 255, 246, 254},
  {240, 177, 193, 250, 200, 255, 246, 254},
  {240, 177, 193, 250, 200, 255, 246, 254},
  {241, 178, 194, 250, 201, 255, 246, 254},
  {241, 178, 194, 250, 201, 255, 246, 254},
  {242, 179, 195, 250, 202, 255, 246, 254},
  {242, 180, 195, 250, 202, 255, 246, 254},
  {242, 181, 196, 250, 203, 255, 247, 254},
  {242, 181, 196, 250, 203, 255, 247, 254},
  {243, 182, 197, 251, 204, 255, 247, 254},
  {243, 183, 197, 251, 204, 255, 247, 254},
  {244, 184, 198, 251, 205, 255, 247, 254},
  {244, 184, 198, 251, 205, 255, 247, 254},
  {244, 185, 199, 251, 206, 255, 247, 254},
  {244, 185, 199, 251, 206, 255, 247, 254},
  {245, 186, 200, 251, 207, 255, 247, 254},
  {245, 187, 200, 251, 207, 255, 247, 254},
  {246, 188, 201, 252, 207, 255, 248, 254},
  {246, 188, 201, 252, 207, 255, 248, 254},
  {246, 189, 202, 252, 208, 255, 248, 254},
  {246, 190, 202, 252, 208, 255, 248, 254},
  {247, 191, 203, 252, 209, 255, 248, 254},
  {247, 191, 203, 252, 209, 255, 248, 254},
  {247, 192, 204, 252, 210, 255, 248, 254},
  {247, 193, 204, 252, 210, 255, 248, 254},
  {248, 194, 205, 252, 211, 255, 248, 254},
  {248, 194, 205, 252, 211, 255, 248, 254},
  {248, 195, 206, 252, 212, 255, 249, 254},
  {248, 196, 206, 252, 212, 255, 249, 254},
  {249, 197, 207, 253, 213, 255, 249, 254},
  {249, 197, 207, 253, 213, 255, 249, 254},
  {249, 198, 208, 253, 214, 255, 249, 254},
  {249, 199, 209, 253, 214, 255, 249, 254},
  {250, 200, 210, 253, 215, 255, 249, 254},
  {250, 200, 210, 253, 215, 255, 249, 254},
  {250, 201, 211, 253, 215, 255, 249, 254},
  {250, 202, 211, 253, 215, 255, 249, 254},
  {250, 203, 212, 253, 216, 255, 249, 254},
  {250, 203, 212, 253, 216, 255, 249, 254},
  {251, 204, 213, 253, 217, 255, 250, 254},
  {251, 205, 213, 253, 217, 255, 250, 254},
  {251, 206, 214, 254, 218, 255, 250, 254},
  {251, 206, 215, 254, 218, 255, 250, 254},
  {252, 207, 216, 254, 219, 255, 250, 254},
  {252, 208, 216, 254, 219, 255, 250, 254},
  {252, 209, 217, 254, 220, 255, 250, 254},
  {252, 210, 217, 254, 220, 255, 250, 254},
  {252, 211, 218, 254, 221, 255, 250, 254},
  {252, 212, 218, 254, 221, 255, 250, 254},
  {253, 213, 219, 254, 222, 255, 250, 254},
  {253, 213, 220, 254, 222, 255, 250, 254},
  {253, 214, 221, 254, 223, 255, 250, 254},
  {253, 215, 221, 254, 223, 255, 250, 254},
  {253, 216, 222, 254, 224, 255, 251, 254},
  {253, 217, 223, 254, 224, 255, 251, 254},
  {253, 218, 224, 254, 225, 255, 251, 254},
  {253, 219, 224, 254, 225, 255, 251, 254},
  {254, 220, 225, 254, 225, 255, 251, 254},
  {254, 221, 226, 254, 225, 255, 251, 254},
  {254, 222, 227, 255, 226, 255, 251, 254},
  {254, 223, 227, 255, 226, 255, 251, 254},
  {254, 224, 228, 255, 227, 255, 251, 254},
  {254, 225, 229, 255, 227, 255, 251, 254},
  {254, 226, 230, 255, 228, 255, 251, 254},
  {254, 227, 230, 255, 229, 255, 251, 254},
  {255, 228, 231, 255, 230, 255, 251, 254},
  {255, 229, 232, 255, 230, 255, 251, 254},
  {255, 230, 233, 255, 231, 255, 252, 254},
  {255, 231, 234, 255, 231, 255, 252, 254},
  {255, 232, 235, 255, 232, 255, 252, 254},
  {255, 233, 236, 255, 232, 255, 252, 254},
  {255, 235, 237, 255, 233, 255, 252, 254},
  {255, 236, 238, 255, 234, 255, 252, 254},
  {255, 238, 240, 255, 235, 255, 252, 255},
  {255, 239, 241, 255, 235, 255, 252, 254},
  {255, 241, 243, 255, 236, 255, 252, 254},
  {255, 243, 245, 255, 237, 255, 252, 254},
  {255, 246, 247, 255, 239, 255, 253, 255},
  {255, 246, 247, 255, 239, 255, 253, 255},
};

static const vp10_coeff_probs_model default_coef_probs_4x4[PLANE_TYPES] = {
  {  // Y plane
    {  // Intra
      {  // Band 0
        { 195,  29, 183 }, {  84,  49, 136 }, {   8,  42,  71 }
      }, {  // Band 1
        {  31, 107, 169 }, {  35,  99, 159 }, {  17,  82, 140 },
        {   8,  66, 114 }, {   2,  44,  76 }, {   1,  19,  32 }
      }, {  // Band 2
        {  40, 132, 201 }, {  29, 114, 187 }, {  13,  91, 157 },
        {   7,  75, 127 }, {   3,  58,  95 }, {   1,  28,  47 }
      }, {  // Band 3
        {  69, 142, 221 }, {  42, 122, 201 }, {  15,  91, 159 },
        {   6,  67, 121 }, {   1,  42,  77 }, {   1,  17,  31 }
      }, {  // Band 4
        { 102, 148, 228 }, {  67, 117, 204 }, {  17,  82, 154 },
        {   6,  59, 114 }, {   2,  39,  75 }, {   1,  15,  29 }
      }, {  // Band 5
        { 156,  57, 233 }, { 119,  57, 212 }, {  58,  48, 163 },
        {  29,  40, 124 }, {  12,  30,  81 }, {   3,  12,  31 }
      }
    }, {  // Inter
      {  // Band 0
        { 191, 107, 226 }, { 124, 117, 204 }, {  25,  99, 155 }
      }, {  // Band 1
        {  29, 148, 210 }, {  37, 126, 194 }, {   8,  93, 157 },
        {   2,  68, 118 }, {   1,  39,  69 }, {   1,  17,  33 }
      }, {  // Band 2
        {  41, 151, 213 }, {  27, 123, 193 }, {   3,  82, 144 },
        {   1,  58, 105 }, {   1,  32,  60 }, {   1,  13,  26 }
      }, {  // Band 3
        {  59, 159, 220 }, {  23, 126, 198 }, {   4,  88, 151 },
        {   1,  66, 114 }, {   1,  38,  71 }, {   1,  18,  34 }
      }, {  // Band 4
        { 114, 136, 232 }, {  51, 114, 207 }, {  11,  83, 155 },
        {   3,  56, 105 }, {   1,  33,  65 }, {   1,  17,  34 }
      }, {  // Band 5
        { 149,  65, 234 }, { 121,  57, 215 }, {  61,  49, 166 },
        {  28,  36, 114 }, {  12,  25,  76 }, {   3,  16,  42 }
      }
    }
  }, {  // UV plane
    {  // Intra
      {  // Band 0
        { 214,  49, 220 }, { 132,  63, 188 }, {  42,  65, 137 }
      }, {  // Band 1
        {  85, 137, 221 }, { 104, 131, 216 }, {  49, 111, 192 },
        {  21,  87, 155 }, {   2,  49,  87 }, {   1,  16,  28 }
      }, {  // Band 2
        {  89, 163, 230 }, {  90, 137, 220 }, {  29, 100, 183 },
        {  10,  70, 135 }, {   2,  42,  81 }, {   1,  17,  33 }
      }, {  // Band 3
        { 108, 167, 237 }, {  55, 133, 222 }, {  15,  97, 179 },
        {   4,  72, 135 }, {   1,  45,  85 }, {   1,  19,  38 }
      }, {  // Band 4
        { 124, 146, 240 }, {  66, 124, 224 }, {  17,  88, 175 },
        {   4,  58, 122 }, {   1,  36,  75 }, {   1,  18,  37 }
      }, {  //  Band 5
        { 141,  79, 241 }, { 126,  70, 227 }, {  66,  58, 182 },
        {  30,  44, 136 }, {  12,  34,  96 }, {   2,  20,  47 }
      }
    }, {  // Inter
      {  // Band 0
        { 229,  99, 249 }, { 143, 111, 235 }, {  46, 109, 192 }
      }, {  // Band 1
        {  82, 158, 236 }, {  94, 146, 224 }, {  25, 117, 191 },
        {   9,  87, 149 }, {   3,  56,  99 }, {   1,  33,  57 }
      }, {  // Band 2
        {  83, 167, 237 }, {  68, 145, 222 }, {  10, 103, 177 },
        {   2,  72, 131 }, {   1,  41,  79 }, {   1,  20,  39 }
      }, {  // Band 3
        {  99, 167, 239 }, {  47, 141, 224 }, {  10, 104, 178 },
        {   2,  73, 133 }, {   1,  44,  85 }, {   1,  22,  47 }
      }, {  // Band 4
        { 127, 145, 243 }, {  71, 129, 228 }, {  17,  93, 177 },
        {   3,  61, 124 }, {   1,  41,  84 }, {   1,  21,  52 }
      }, {  // Band 5
        { 157,  78, 244 }, { 140,  72, 231 }, {  69,  58, 184 },
        {  31,  44, 137 }, {  14,  38, 105 }, {   8,  23,  61 }
      }
    }
  }
};

static const vp10_coeff_probs_model default_coef_probs_8x8[PLANE_TYPES] = {
  {  // Y plane
    {  // Intra
      {  // Band 0
        { 125,  34, 187 }, {  52,  41, 133 }, {   6,  31,  56 }
      }, {  // Band 1
        {  37, 109, 153 }, {  51, 102, 147 }, {  23,  87, 128 },
        {   8,  67, 101 }, {   1,  41,  63 }, {   1,  19,  29 }
      }, {  // Band 2
        {  31, 154, 185 }, {  17, 127, 175 }, {   6,  96, 145 },
        {   2,  73, 114 }, {   1,  51,  82 }, {   1,  28,  45 }
      }, {  // Band 3
        {  23, 163, 200 }, {  10, 131, 185 }, {   2,  93, 148 },
        {   1,  67, 111 }, {   1,  41,  69 }, {   1,  14,  24 }
      }, {  // Band 4
        {  29, 176, 217 }, {  12, 145, 201 }, {   3, 101, 156 },
        {   1,  69, 111 }, {   1,  39,  63 }, {   1,  14,  23 }
      }, {  // Band 5
        {  57, 192, 233 }, {  25, 154, 215 }, {   6, 109, 167 },
        {   3,  78, 118 }, {   1,  48,  69 }, {   1,  21,  29 }
      }
    }, {  // Inter
      {  // Band 0
        { 202, 105, 245 }, { 108, 106, 216 }, {  18,  90, 144 }
      }, {  // Band 1
        {  33, 172, 219 }, {  64, 149, 206 }, {  14, 117, 177 },
        {   5,  90, 141 }, {   2,  61,  95 }, {   1,  37,  57 }
      }, {  // Band 2
        {  33, 179, 220 }, {  11, 140, 198 }, {   1,  89, 148 },
        {   1,  60, 104 }, {   1,  33,  57 }, {   1,  12,  21 }
      }, {  // Band 3
        {  30, 181, 221 }, {   8, 141, 198 }, {   1,  87, 145 },
        {   1,  58, 100 }, {   1,  31,  55 }, {   1,  12,  20 }
      }, {  // Band 4
        {  32, 186, 224 }, {   7, 142, 198 }, {   1,  86, 143 },
        {   1,  58, 100 }, {   1,  31,  55 }, {   1,  12,  22 }
      }, {  // Band 5
        {  57, 192, 227 }, {  20, 143, 204 }, {   3,  96, 154 },
        {   1,  68, 112 }, {   1,  42,  69 }, {   1,  19,  32 }
      }
    }
  }, {  // UV plane
    {  // Intra
      {  // Band 0
        { 212,  35, 215 }, { 113,  47, 169 }, {  29,  48, 105 }
      }, {  // Band 1
        {  74, 129, 203 }, { 106, 120, 203 }, {  49, 107, 178 },
        {  19,  84, 144 }, {   4,  50,  84 }, {   1,  15,  25 }
      }, {  // Band 2
        {  71, 172, 217 }, {  44, 141, 209 }, {  15, 102, 173 },
        {   6,  76, 133 }, {   2,  51,  89 }, {   1,  24,  42 }
      }, {  // Band 3
        {  64, 185, 231 }, {  31, 148, 216 }, {   8, 103, 175 },
        {   3,  74, 131 }, {   1,  46,  81 }, {   1,  18,  30 }
      }, {  // Band 4
        {  65, 196, 235 }, {  25, 157, 221 }, {   5, 105, 174 },
        {   1,  67, 120 }, {   1,  38,  69 }, {   1,  15,  30 }
      }, {  // Band 5
        {  65, 204, 238 }, {  30, 156, 224 }, {   7, 107, 177 },
        {   2,  70, 124 }, {   1,  42,  73 }, {   1,  18,  34 }
      }
    }, {  // Inter
      {  // Band 0
        { 225,  86, 251 }, { 144, 104, 235 }, {  42,  99, 181 }
      }, {  // Band 1
        {  85, 175, 239 }, { 112, 165, 229 }, {  29, 136, 200 },
        {  12, 103, 162 }, {   6,  77, 123 }, {   2,  53,  84 }
      }, {  // Band 2
        {  75, 183, 239 }, {  30, 155, 221 }, {   3, 106, 171 },
        {   1,  74, 128 }, {   1,  44,  76 }, {   1,  17,  28 }
      }, {  // Band 3
        {  73, 185, 240 }, {  27, 159, 222 }, {   2, 107, 172 },
        {   1,  75, 127 }, {   1,  42,  73 }, {   1,  17,  29 }
      }, {  // Band 4
        {  62, 190, 238 }, {  21, 159, 222 }, {   2, 107, 172 },
        {   1,  72, 122 }, {   1,  40,  71 }, {   1,  18,  32 }
      }, {  // Band 5
        {  61, 199, 240 }, {  27, 161, 226 }, {   4, 113, 180 },
        {   1,  76, 129 }, {   1,  46,  80 }, {   1,  23,  41 }
      }
    }
  }
};

static const vp10_coeff_probs_model default_coef_probs_16x16[PLANE_TYPES] = {
  {  // Y plane
    {  // Intra
      {  // Band 0
        {   7,  27, 153 }, {   5,  30,  95 }, {   1,  16,  30 }
      }, {  // Band 1
        {  50,  75, 127 }, {  57,  75, 124 }, {  27,  67, 108 },
        {  10,  54,  86 }, {   1,  33,  52 }, {   1,  12,  18 }
      }, {  // Band 2
        {  43, 125, 151 }, {  26, 108, 148 }, {   7,  83, 122 },
        {   2,  59,  89 }, {   1,  38,  60 }, {   1,  17,  27 }
      }, {  // Band 3
        {  23, 144, 163 }, {  13, 112, 154 }, {   2,  75, 117 },
        {   1,  50,  81 }, {   1,  31,  51 }, {   1,  14,  23 }
      }, {  // Band 4
        {  18, 162, 185 }, {   6, 123, 171 }, {   1,  78, 125 },
        {   1,  51,  86 }, {   1,  31,  54 }, {   1,  14,  23 }
      }, {  // Band 5
        {  15, 199, 227 }, {   3, 150, 204 }, {   1,  91, 146 },
        {   1,  55,  95 }, {   1,  30,  53 }, {   1,  11,  20 }
      }
    }, {  // Inter
      {  // Band 0
        {  19,  55, 240 }, {  19,  59, 196 }, {   3,  52, 105 }
      }, {  // Band 1
        {  41, 166, 207 }, { 104, 153, 199 }, {  31, 123, 181 },
        {  14, 101, 152 }, {   5,  72, 106 }, {   1,  36,  52 }
      }, {  // Band 2
        {  35, 176, 211 }, {  12, 131, 190 }, {   2,  88, 144 },
        {   1,  60, 101 }, {   1,  36,  60 }, {   1,  16,  28 }
      }, {  // Band 3
        {  28, 183, 213 }, {   8, 134, 191 }, {   1,  86, 142 },
        {   1,  56,  96 }, {   1,  30,  53 }, {   1,  12,  20 }
      }, {  // Band 4
        {  20, 190, 215 }, {   4, 135, 192 }, {   1,  84, 139 },
        {   1,  53,  91 }, {   1,  28,  49 }, {   1,  11,  20 }
      }, {  // Band 5
        {  13, 196, 216 }, {   2, 137, 192 }, {   1,  86, 143 },
        {   1,  57,  99 }, {   1,  32,  56 }, {   1,  13,  24 }
      }
    }
  }, {  // UV plane
    {  // Intra
      {  // Band 0
        { 211,  29, 217 }, {  96,  47, 156 }, {  22,  43,  87 }
      }, {  // Band 1
        {  78, 120, 193 }, { 111, 116, 186 }, {  46, 102, 164 },
        {  15,  80, 128 }, {   2,  49,  76 }, {   1,  18,  28 }
      }, {  // Band 2
        {  71, 161, 203 }, {  42, 132, 192 }, {  10,  98, 150 },
        {   3,  69, 109 }, {   1,  44,  70 }, {   1,  18,  29 }
      }, {  // Band 3
        {  57, 186, 211 }, {  30, 140, 196 }, {   4,  93, 146 },
        {   1,  62, 102 }, {   1,  38,  65 }, {   1,  16,  27 }
      }, {  // Band 4
        {  47, 199, 217 }, {  14, 145, 196 }, {   1,  88, 142 },
        {   1,  57,  98 }, {   1,  36,  62 }, {   1,  15,  26 }
      }, {  // Band 5
        {  26, 219, 229 }, {   5, 155, 207 }, {   1,  94, 151 },
        {   1,  60, 104 }, {   1,  36,  62 }, {   1,  16,  28 }
      }
    }, {  // Inter
      {  // Band 0
        { 233,  29, 248 }, { 146,  47, 220 }, {  43,  52, 140 }
      }, {  // Band 1
        { 100, 163, 232 }, { 179, 161, 222 }, {  63, 142, 204 },
        {  37, 113, 174 }, {  26,  89, 137 }, {  18,  68,  97 }
      }, {  // Band 2
        {  85, 181, 230 }, {  32, 146, 209 }, {   7, 100, 164 },
        {   3,  71, 121 }, {   1,  45,  77 }, {   1,  18,  30 }
      }, {  // Band 3
        {  65, 187, 230 }, {  20, 148, 207 }, {   2,  97, 159 },
        {   1,  68, 116 }, {   1,  40,  70 }, {   1,  14,  29 }
      }, {  // Band 4
        {  40, 194, 227 }, {   8, 147, 204 }, {   1,  94, 155 },
        {   1,  65, 112 }, {   1,  39,  66 }, {   1,  14,  26 }
      }, {  // Band 5
        {  16, 208, 228 }, {   3, 151, 207 }, {   1,  98, 160 },
        {   1,  67, 117 }, {   1,  41,  74 }, {   1,  17,  31 }
      }
    }
  }
};

static const vp10_coeff_probs_model default_coef_probs_32x32[PLANE_TYPES] = {
  {  // Y plane
    {  // Intra
      {  // Band 0
        {  17,  38, 140 }, {   7,  34,  80 }, {   1,  17,  29 }
      }, {  // Band 1
        {  37,  75, 128 }, {  41,  76, 128 }, {  26,  66, 116 },
        {  12,  52,  94 }, {   2,  32,  55 }, {   1,  10,  16 }
      }, {  // Band 2
        {  50, 127, 154 }, {  37, 109, 152 }, {  16,  82, 121 },
        {   5,  59,  85 }, {   1,  35,  54 }, {   1,  13,  20 }
      }, {  // Band 3
        {  40, 142, 167 }, {  17, 110, 157 }, {   2,  71, 112 },
        {   1,  44,  72 }, {   1,  27,  45 }, {   1,  11,  17 }
      }, {  // Band 4
        {  30, 175, 188 }, {   9, 124, 169 }, {   1,  74, 116 },
        {   1,  48,  78 }, {   1,  30,  49 }, {   1,  11,  18 }
      }, {  // Band 5
        {  10, 222, 223 }, {   2, 150, 194 }, {   1,  83, 128 },
        {   1,  48,  79 }, {   1,  27,  45 }, {   1,  11,  17 }
      }
    }, {  // Inter
      {  // Band 0
        {  36,  41, 235 }, {  29,  36, 193 }, {  10,  27, 111 }
      }, {  // Band 1
        {  85, 165, 222 }, { 177, 162, 215 }, { 110, 135, 195 },
        {  57, 113, 168 }, {  23,  83, 120 }, {  10,  49,  61 }
      }, {  // Band 2
        {  85, 190, 223 }, {  36, 139, 200 }, {   5,  90, 146 },
        {   1,  60, 103 }, {   1,  38,  65 }, {   1,  18,  30 }
      }, {  // Band 3
        {  72, 202, 223 }, {  23, 141, 199 }, {   2,  86, 140 },
        {   1,  56,  97 }, {   1,  36,  61 }, {   1,  16,  27 }
      }, {  // Band 4
        {  55, 218, 225 }, {  13, 145, 200 }, {   1,  86, 141 },
        {   1,  57,  99 }, {   1,  35,  61 }, {   1,  13,  22 }
      }, {  // Band 5
        {  15, 235, 212 }, {   1, 132, 184 }, {   1,  84, 139 },
        {   1,  57,  97 }, {   1,  34,  56 }, {   1,  14,  23 }
      }
    }
  }, {  // UV plane
    {  // Intra
      {  // Band 0
        { 181,  21, 201 }, {  61,  37, 123 }, {  10,  38,  71 }
      }, {  // Band 1
        {  47, 106, 172 }, {  95, 104, 173 }, {  42,  93, 159 },
        {  18,  77, 131 }, {   4,  50,  81 }, {   1,  17,  23 }
      }, {  // Band 2
        {  62, 147, 199 }, {  44, 130, 189 }, {  28, 102, 154 },
        {  18,  75, 115 }, {   2,  44,  65 }, {   1,  12,  19 }
      }, {  // Band 3
        {  55, 153, 210 }, {  24, 130, 194 }, {   3,  93, 146 },
        {   1,  61,  97 }, {   1,  31,  50 }, {   1,  10,  16 }
      }, {  // Band 4
        {  49, 186, 223 }, {  17, 148, 204 }, {   1,  96, 142 },
        {   1,  53,  83 }, {   1,  26,  44 }, {   1,  11,  17 }
      }, {  // Band 5
        {  13, 217, 212 }, {   2, 136, 180 }, {   1,  78, 124 },
        {   1,  50,  83 }, {   1,  29,  49 }, {   1,  14,  23 }
      }
    }, {  // Inter
      {  // Band 0
        { 197,  13, 247 }, {  82,  17, 222 }, {  25,  17, 162 }
      }, {  // Band 1
        { 126, 186, 247 }, { 234, 191, 243 }, { 176, 177, 234 },
        { 104, 158, 220 }, {  66, 128, 186 }, {  55,  90, 137 }
      }, {  // Band 2
        { 111, 197, 242 }, {  46, 158, 219 }, {   9, 104, 171 },
        {   2,  65, 125 }, {   1,  44,  80 }, {   1,  17,  91 }
      }, {  // Band 3
        { 104, 208, 245 }, {  39, 168, 224 }, {   3, 109, 162 },
        {   1,  79, 124 }, {   1,  50, 102 }, {   1,  43, 102 }
      }, {  // Band 4
        {  84, 220, 246 }, {  31, 177, 231 }, {   2, 115, 180 },
        {   1,  79, 134 }, {   1,  55,  77 }, {   1,  60,  79 }
      }, {  // Band 5
        {  43, 243, 240 }, {   8, 180, 217 }, {   1, 115, 166 },
        {   1,  84, 121 }, {   1,  51,  67 }, {   1,  16,   6 }
      }
    }
  }
};

static void extend_to_full_distribution(vpx_prob *probs, vpx_prob p) {
  memcpy(probs, vp10_pareto8_full[p - 1], MODEL_NODES * sizeof(vpx_prob));
}

void vp10_model_to_full_probs(const vpx_prob *model, vpx_prob *full) {
  if (full != model)
    memcpy(full, model, sizeof(vpx_prob) * UNCONSTRAINED_NODES);
  // TODO(aconverse): model[PIVOT_NODE] should never be zero.
  // https://code.google.com/p/webm/issues/detail?id=1089
  if (model[PIVOT_NODE] != 0)
    extend_to_full_distribution(&full[UNCONSTRAINED_NODES], model[PIVOT_NODE]);
}

void vp10_default_coef_probs(VP10_COMMON *cm) {
  vp10_copy(cm->fc->coef_probs[TX_4X4], default_coef_probs_4x4);
  vp10_copy(cm->fc->coef_probs[TX_8X8], default_coef_probs_8x8);
  vp10_copy(cm->fc->coef_probs[TX_16X16], default_coef_probs_16x16);
  vp10_copy(cm->fc->coef_probs[TX_32X32], default_coef_probs_32x32);
}

#define COEF_COUNT_SAT 24
#define COEF_MAX_UPDATE_FACTOR 112
#define COEF_COUNT_SAT_KEY 24
#define COEF_MAX_UPDATE_FACTOR_KEY 112
#define COEF_COUNT_SAT_AFTER_KEY 24
#define COEF_MAX_UPDATE_FACTOR_AFTER_KEY 128

static void adapt_coef_probs(VP10_COMMON *cm, TX_SIZE tx_size,
                             unsigned int count_sat,
                             unsigned int update_factor) {
  const FRAME_CONTEXT *pre_fc = &cm->frame_contexts[cm->frame_context_idx];
  vp10_coeff_probs_model *const probs = cm->fc->coef_probs[tx_size];
  const vp10_coeff_probs_model *const pre_probs = pre_fc->coef_probs[tx_size];
  vp10_coeff_count_model *counts = cm->counts.coef[tx_size];
  unsigned int (*eob_counts)[REF_TYPES][COEF_BANDS][COEFF_CONTEXTS] =
      cm->counts.eob_branch[tx_size];
  int i, j, k, l, m;

  for (i = 0; i < PLANE_TYPES; ++i)
    for (j = 0; j < REF_TYPES; ++j)
      for (k = 0; k < COEF_BANDS; ++k)
        for (l = 0; l < BAND_COEFF_CONTEXTS(k); ++l) {
          const int n0 = counts[i][j][k][l][ZERO_TOKEN];
          const int n1 = counts[i][j][k][l][ONE_TOKEN];
          const int n2 = counts[i][j][k][l][TWO_TOKEN];
          const int neob = counts[i][j][k][l][EOB_MODEL_TOKEN];
          const unsigned int branch_ct[UNCONSTRAINED_NODES][2] = {
            { neob, eob_counts[i][j][k][l] - neob },
            { n0, n1 + n2 },
            { n1, n2 }
          };
          for (m = 0; m < UNCONSTRAINED_NODES; ++m)
            probs[i][j][k][l][m] = merge_probs(pre_probs[i][j][k][l][m],
                                               branch_ct[m],
                                               count_sat, update_factor);
        }
}

void vp10_adapt_coef_probs(VP10_COMMON *cm) {
  TX_SIZE t;
  unsigned int count_sat, update_factor;

  if (frame_is_intra_only(cm)) {
    update_factor = COEF_MAX_UPDATE_FACTOR_KEY;
    count_sat = COEF_COUNT_SAT_KEY;
  } else if (cm->last_frame_type == KEY_FRAME) {
    update_factor = COEF_MAX_UPDATE_FACTOR_AFTER_KEY;  /* adapt quickly */
    count_sat = COEF_COUNT_SAT_AFTER_KEY;
  } else {
    update_factor = COEF_MAX_UPDATE_FACTOR;
    count_sat = COEF_COUNT_SAT;
  }
  for (t = TX_4X4; t <= TX_32X32; t++)
    adapt_coef_probs(cm, t, count_sat, update_factor);
}
