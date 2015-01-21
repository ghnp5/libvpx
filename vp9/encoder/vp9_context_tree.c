/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vp9/encoder/vp9_context_tree.h"
#include "vp9/encoder/vp9_encoder.h"

static const BLOCK_SIZE square[] = {
  BLOCK_8X8,
  BLOCK_16X16,
  BLOCK_32X32,
  BLOCK_64X64,
};

static void alloc_mode_context(VP9_COMMON *cm, int num_4x4_blk,
                               PICK_MODE_CONTEXT *ctx) {
  const int num_blk = (num_4x4_blk < 4 ? 4 : num_4x4_blk);
  const int num_pix = num_blk << 4;
  int i, k;
  ctx->num_4x4_blk = num_blk;

  CHECK_MEM_ERROR(cm, ctx->zcoeff_blk,
                  vpx_calloc(num_4x4_blk, sizeof(uint8_t)));
  for (i = 0; i < MAX_MB_PLANE; ++i) {
    for (k = 0; k < 3; ++k) {
      CHECK_MEM_ERROR(cm, ctx->coeff[i][k],
                      vpx_memalign(16, num_pix * sizeof(*ctx->coeff[i][k])));
      CHECK_MEM_ERROR(cm, ctx->qcoeff[i][k],
                      vpx_memalign(16, num_pix * sizeof(*ctx->qcoeff[i][k])));
      CHECK_MEM_ERROR(cm, ctx->dqcoeff[i][k],
                      vpx_memalign(16, num_pix * sizeof(*ctx->dqcoeff[i][k])));
      CHECK_MEM_ERROR(cm, ctx->eobs[i][k],
                      vpx_memalign(16, num_pix * sizeof(*ctx->eobs[i][k])));
      ctx->coeff_pbuf[i][k]   = ctx->coeff[i][k];
      ctx->qcoeff_pbuf[i][k]  = ctx->qcoeff[i][k];
      ctx->dqcoeff_pbuf[i][k] = ctx->dqcoeff[i][k];
      ctx->eobs_pbuf[i][k]    = ctx->eobs[i][k];
    }
  }
#if CONFIG_PALETTE
  CHECK_MEM_ERROR(cm, ctx->color_index_map,
                  vpx_memalign(16, num_pix * sizeof(*ctx->color_index_map)));
#endif
}

static void free_mode_context(PICK_MODE_CONTEXT *ctx) {
  int i, k;
  vpx_free(ctx->zcoeff_blk);
  ctx->zcoeff_blk = 0;
  for (i = 0; i < MAX_MB_PLANE; ++i) {
    for (k = 0; k < 3; ++k) {
      vpx_free(ctx->coeff[i][k]);
      ctx->coeff[i][k] = 0;
      vpx_free(ctx->qcoeff[i][k]);
      ctx->qcoeff[i][k] = 0;
      vpx_free(ctx->dqcoeff[i][k]);
      ctx->dqcoeff[i][k] = 0;
      vpx_free(ctx->eobs[i][k]);
      ctx->eobs[i][k] = 0;
    }
  }

#if CONFIG_PALETTE
  vpx_free(ctx->color_index_map);
  ctx->color_index_map = 0;
#endif
}

static void alloc_tree_contexts(VP9_COMMON *cm, PC_TREE *tree,
                                int num_4x4_blk) {
  alloc_mode_context(cm, num_4x4_blk, &tree->none);
  alloc_mode_context(cm, num_4x4_blk/2, &tree->horizontal[0]);
  alloc_mode_context(cm, num_4x4_blk/2, &tree->vertical[0]);

  /* TODO(Jbb): for 4x8 and 8x4 these allocated values are not used.
   * Figure out a better way to do this. */
  alloc_mode_context(cm, num_4x4_blk/2, &tree->horizontal[1]);
  alloc_mode_context(cm, num_4x4_blk/2, &tree->vertical[1]);
}

static void free_tree_contexts(PC_TREE *tree) {
  free_mode_context(&tree->none);
  free_mode_context(&tree->horizontal[0]);
  free_mode_context(&tree->horizontal[1]);
  free_mode_context(&tree->vertical[0]);
  free_mode_context(&tree->vertical[1]);
}

// This function sets up a tree of contexts such that at each square
// partition level. There are contexts for none, horizontal, vertical, and
// split.  Along with a block_size value and a selected block_size which
// represents the state of our search.
void vp9_setup_pc_tree(VP9_COMMON *cm, VP9_COMP *cpi) {
  int i, j;
  const int leaf_nodes = 64;
  const int tree_nodes = 64 + 16 + 4 + 1;
  int pc_tree_index = 0;
  PC_TREE *this_pc;
  PICK_MODE_CONTEXT *this_leaf;
  int square_index = 1;
  int nodes;

  vpx_free(cpi->leaf_tree);
  CHECK_MEM_ERROR(cm, cpi->leaf_tree, vpx_calloc(leaf_nodes,
                                                 sizeof(*cpi->leaf_tree)));
  vpx_free(cpi->pc_tree);
  CHECK_MEM_ERROR(cm, cpi->pc_tree, vpx_calloc(tree_nodes,
                                               sizeof(*cpi->pc_tree)));

  this_pc = &cpi->pc_tree[0];
  this_leaf = &cpi->leaf_tree[0];

  // 4x4 blocks smaller than 8x8 but in the same 8x8 block share the same
  // context so we only need to allocate 1 for each 8x8 block.
  for (i = 0; i < leaf_nodes; ++i)
    alloc_mode_context(cm, 1, &cpi->leaf_tree[i]);

  // Sets up all the leaf nodes in the tree.
  for (pc_tree_index = 0; pc_tree_index < leaf_nodes; ++pc_tree_index) {
    PC_TREE *const tree = &cpi->pc_tree[pc_tree_index];
    tree->block_size = square[0];
    alloc_tree_contexts(cm, tree, 4);
    tree->leaf_split[0] = this_leaf++;
    for (j = 1; j < 4; j++)
      tree->leaf_split[j] = tree->leaf_split[0];
  }

  // Each node has 4 leaf nodes, fill each block_size level of the tree
  // from leafs to the root.
  for (nodes = 16; nodes > 0; nodes >>= 2) {
    for (i = 0; i < nodes; ++i) {
      PC_TREE *const tree = &cpi->pc_tree[pc_tree_index];
      alloc_tree_contexts(cm, tree, 4 << (2 * square_index));
      tree->block_size = square[square_index];
      for (j = 0; j < 4; j++)
        tree->split[j] = this_pc++;
      ++pc_tree_index;
    }
    ++square_index;
  }
  cpi->pc_root = &cpi->pc_tree[tree_nodes - 1];
  cpi->pc_root[0].none.best_mode_index = 2;
}

void vp9_free_pc_tree(VP9_COMP *cpi) {
  const int tree_nodes = 64 + 16 + 4 + 1;
  int i;

  // Set up all 4x4 mode contexts
  for (i = 0; i < 64; ++i)
    free_mode_context(&cpi->leaf_tree[i]);

  // Sets up all the leaf nodes in the tree.
  for (i = 0; i < tree_nodes; ++i)
    free_tree_contexts(&cpi->pc_tree[i]);

  vpx_free(cpi->pc_tree);
  cpi->pc_tree = NULL;
  vpx_free(cpi->leaf_tree);
  cpi->leaf_tree = NULL;
}
