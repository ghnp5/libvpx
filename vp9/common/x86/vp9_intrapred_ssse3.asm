;
;  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may
;  be found in the AUTHORS file in the root of the source tree.
;

%include "third_party/x86inc/x86inc.asm"

SECTION_RODATA

pw_2: times 8 dw 2
pb_7m1: times 8 db 7, -1
pb_15: times 16 db 15

sh_b2w01234577: db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 7, -1, 7, -1
sh_b2w12345677: db 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7, -1, 7, -1
sh_b2w23456777: db 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7, -1, 7, -1, 7, -1
sh_b2w01234567: db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7, -1
sh_b2w12345678: db 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7, -1, 8, -1
sh_b2w23456789: db 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7, -1, 8, -1, 9, -1
sh_b2w89abcdef: db 8, -1, 9, -1, 10, -1, 11, -1, 12, -1, 13, -1, 14, -1, 15, -1
sh_b2w9abcdeff: db 9, -1, 10, -1, 11, -1, 12, -1, 13, -1, 14, -1, 15, -1, 15, -1
sh_b2wabcdefff: db 10, -1, 11, -1, 12, -1, 13, -1, 14, -1, 15, -1, 15, -1, 15, -1
sh_b123456789abcdeff: db 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15
sh_b1233: db 1, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
sh_b12345677: db 1, 2, 3, 4, 5, 6, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0
sh_b2w32104567: db 3, -1, 2, -1, 1, -1, 0, -1, 4, -1, 5, -1, 6, -1, 7, -1
sh_b8091a2b345: db 8, 0, 9, 1, 10, 2, 11, 3, 4, 5, 0, 0, 0, 0, 0, 0
sh_b23456789abcdefff: db 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15
sh_b2w76543210: db 7, -1, 6, -1, 5, -1, 4, -1, 3, -1, 2, -1, 1, -1, 0, -1
sh_b2w65432108: db 6, -1, 5, -1, 4, -1, 3, -1, 2, -1, 1, -1, 0, -1, 8, -1
sh_b2w54321089: db 5, -1, 4, -1, 3, -1, 2, -1, 1, -1, 0, -1, 8, -1, 9, -1

SECTION .text

INIT_MMX ssse3
cglobal h_predictor_4x4, 2, 4, 3, dst, stride, line, left
  movifnidn          leftq, leftmp
  add                leftq, 4
  mov                lineq, -2
  pxor                  m0, m0
.loop:
  movd                  m1, [leftq+lineq*2  ]
  movd                  m2, [leftq+lineq*2+1]
  pshufb                m1, m0
  pshufb                m2, m0
  movd      [dstq        ], m1
  movd      [dstq+strideq], m2
  lea                 dstq, [dstq+strideq*2]
  inc                lineq
  jnz .loop
  REP_RET

INIT_MMX ssse3
cglobal h_predictor_8x8, 2, 4, 3, dst, stride, line, left
  movifnidn          leftq, leftmp
  add                leftq, 8
  mov                lineq, -4
  pxor                  m0, m0
.loop:
  movd                  m1, [leftq+lineq*2  ]
  movd                  m2, [leftq+lineq*2+1]
  pshufb                m1, m0
  pshufb                m2, m0
  movq      [dstq        ], m1
  movq      [dstq+strideq], m2
  lea                 dstq, [dstq+strideq*2]
  inc                lineq
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal h_predictor_16x16, 2, 4, 3, dst, stride, line, left
  movifnidn          leftq, leftmp
  add                leftq, 16
  mov                lineq, -8
  pxor                  m0, m0
.loop:
  movd                  m1, [leftq+lineq*2  ]
  movd                  m2, [leftq+lineq*2+1]
  pshufb                m1, m0
  pshufb                m2, m0
  mova      [dstq        ], m1
  mova      [dstq+strideq], m2
  lea                 dstq, [dstq+strideq*2]
  inc                lineq
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal h_predictor_32x32, 2, 4, 3, dst, stride, line, left
  movifnidn          leftq, leftmp
  add                leftq, 32
  mov                lineq, -16
  pxor                  m0, m0
.loop:
  movd                  m1, [leftq+lineq*2  ]
  movd                  m2, [leftq+lineq*2+1]
  pshufb                m1, m0
  pshufb                m2, m0
  mova   [dstq           ], m1
  mova   [dstq        +16], m1
  mova   [dstq+strideq   ], m2
  mova   [dstq+strideq+16], m2
  lea                 dstq, [dstq+strideq*2]
  inc                lineq
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d45_predictor_4x4, 3, 3, 3, dst, stride, above
  movq                m0, [aboveq]
  pshufb              m2, m0, [sh_b2w23456777]
  pshufb              m1, m0, [sh_b2w12345677]
  pshufb              m0, [sh_b2w01234577]
  paddw               m1, m1
  paddw               m0, m2
  paddw               m1, [pw_2]
  paddw               m0, m1
  psraw               m0, 2
  packuswb            m0, m0
  movd    [dstq        ], m0
  psrldq              m0, 1
  movd    [dstq+strideq], m0
  lea               dstq, [dstq+strideq*2]
  psrldq              m0, 1
  movd    [dstq        ], m0
  psrldq              m0, 1
  movd    [dstq+strideq], m0
  RET

INIT_XMM ssse3
cglobal d45_predictor_8x8, 3, 4, 4, dst, stride, above, line
  movq                m0, [aboveq]
  DEFINE_ARGS dst, stride, stride3, line
  lea           stride3q, [strideq*3]
  pshufb              m3, m0, [sh_b2w23456777]
  pshufb              m2, m0, [sh_b2w12345677]
  pshufb              m1, m0, [sh_b2w01234567]
  pshufb              m0, [pb_7m1]
  paddw               m2, m2
  paddw               m1, m3
  paddw               m2, [pw_2]
  paddw               m1, m2
  psraw               m1, 2
  packuswb            m1, m0
  mov              lined, 2
.loop:
  movq  [dstq          ], m1
  psrldq              m1, 1
  movq  [dstq+strideq  ], m1
  psrldq              m1, 1
  movq  [dstq+strideq*2], m1
  psrldq              m1, 1
  movq  [dstq+stride3q ], m1
  psrldq              m1, 1
  lea               dstq, [dstq+strideq*4]
  dec              lined
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d45_predictor_16x16, 3, 4, 6, dst, stride, above, line
  mova                m0, [aboveq]
  DEFINE_ARGS dst, stride, stride3, line
  lea           stride3q, [strideq*3]
  pshufb              m5, m0, [sh_b2wabcdefff]
  pshufb              m4, m0, [sh_b2w23456789]
  pshufb              m3, m0, [sh_b2w9abcdeff]
  pshufb              m2, m0, [sh_b2w12345678]
  pshufb              m1, m0, [sh_b2w89abcdef]
  pshufb              m0, [sh_b2w01234567]
  paddw               m3, m3
  paddw               m2, m2
  paddw               m0, m4
  paddw               m1, m5
  paddw               m2, [pw_2]
  paddw               m3, [pw_2]
  paddw               m0, m2
  paddw               m1, m3
  psraw               m0, 2
  psraw               m1, 2
  mova                m2, [sh_b123456789abcdeff]
  packuswb            m0, m1
  mov              lined, 4
.loop:
  mova  [dstq          ], m0
  pshufb              m0, m2
  mova  [dstq+strideq  ], m0
  pshufb              m0, m2
  mova  [dstq+strideq*2], m0
  pshufb              m0, m2
  mova  [dstq+stride3q ], m0
  pshufb              m0, m2
  lea               dstq, [dstq+strideq*4]
  dec              lined
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d45_predictor_32x32, 3, 4, 8, dst, stride, above, line
  mova                   m0, [aboveq]
  mova                   m7, [aboveq+16]
  DEFINE_ARGS dst, stride, stride3, line
  lea              stride3q, [strideq*3]
  pshufb                 m6, m7, [sh_b2wabcdefff]
  pshufb                 m5, m7, [sh_b2w23456789]
  pshufb                 m4, m7, [sh_b2w9abcdeff]
  pshufb                 m3, m7, [sh_b2w12345678]
  pshufb                 m2, m7, [sh_b2w89abcdef]
  pshufb                 m7, [sh_b2w01234567]
  paddw                  m4, m4
  paddw                  m3, m3
  paddw                  m5, m7
  paddw                  m6, m2
  paddw                  m3, [pw_2]
  paddw                  m4, [pw_2]
  paddw                  m5, m3
  paddw                  m6, m4
  psraw                  m5, 2
  psraw                  m6, 2
  packuswb               m5, m6
  pshufb                 m4, m0, [sh_b2w23456789]
  pshufb                 m2, m0, [sh_b2w12345678]
  pshufb                 m3, m0, [sh_b2w89abcdef]
  pshufb                 m0, [sh_b2w01234567]
  palignr                m6, m7, m3, 2
  palignr                m7, m3, 4
  paddw                  m2, m2
  paddw                  m6, m6
  paddw                  m0, m4
  paddw                  m3, m7
  paddw                  m2, [pw_2]
  paddw                  m6, [pw_2]
  paddw                  m0, m2
  paddw                  m3, m6
  psraw                  m0, 2
  psraw                  m3, 2
  mova                   m2, [sh_b123456789abcdeff]
  packuswb               m0, m3
  mov                 lined, 8
.loop:
  mova  [dstq             ], m0
  mova  [dstq          +16], m5
  palignr                m1, m5, m0, 1
  pshufb                 m5, m2
  mova  [dstq+strideq     ], m1
  mova  [dstq+strideq  +16], m5
  palignr                m0, m5, m1, 1
  pshufb                 m5, m2
  mova  [dstq+strideq*2   ], m0
  mova  [dstq+strideq*2+16], m5
  palignr                m1, m5, m0, 1
  pshufb                 m5, m2
  mova  [dstq+stride3q    ], m1
  mova  [dstq+stride3q +16], m5
  palignr                m0, m5, m1, 1
  pshufb                 m5, m2
  lea                  dstq, [dstq+strideq*4]
  dec                 lined
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d63_predictor_4x4, 3, 3, 4, dst, stride, above
  movq                m3, [aboveq]
  pshufb              m2, m3, [sh_b2w23456777]
  pshufb              m0, m3, [sh_b2w01234567]
  pshufb              m1, m3, [sh_b2w12345677]
  paddw               m2, m0
  psrldq              m0, m3, 1
  paddw               m2, [pw_2]
  paddw               m1, m1
  pavgb               m3, m0
  paddw               m2, m1
  psraw               m2, 2
  packuswb            m2, m2
  movd    [dstq        ], m3
  movd    [dstq+strideq], m2
  lea               dstq, [dstq+strideq*2]
  psrldq              m3, 1
  psrldq              m2, 1
  movd    [dstq        ], m3
  movd    [dstq+strideq], m2
  RET

INIT_XMM ssse3
cglobal d63_predictor_8x8, 3, 3, 4, dst, stride, above
  movq                m0, [aboveq]
  DEFINE_ARGS dst, stride, stride3, line
  lea           stride3q, [strideq*3]
  pshufb              m3, m0, [sh_b2w23456777]
  pshufb              m2, m0, [sh_b2w12345677]
  pshufb              m1, m0, [sh_b2w01234567]
  pshufb              m0, [pb_7m1]
  paddw               m3, m1
  pavgw               m1, m2
  paddw               m2, m2
  paddw               m3, [pw_2]
  paddw               m3, m2
  psraw               m3, 2
  packuswb            m1, m0
  packuswb            m3, m0
  mov              lined, 2
.loop:
  movq  [dstq          ], m1
  movq  [dstq+strideq  ], m3
  psrldq              m1, 1
  psrldq              m3, 1
  movq  [dstq+strideq*2], m1
  movq  [dstq+stride3q ], m3
  psrldq              m1, 1
  psrldq              m3, 1
  lea               dstq, [dstq+strideq*4]
  dec              lined
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d63_predictor_16x16, 3, 4, 8, dst, stride, above, line
  mova                m0, [aboveq]
  DEFINE_ARGS dst, stride, stride3, line
  lea           stride3q, [strideq*3]
  mova                m1, [sh_b123456789abcdeff]
  pshufb              m7, m0, [sh_b2wabcdefff]
  pshufb              m6, m0, [sh_b2w23456789]
  pshufb              m5, m0, [sh_b2w9abcdeff]
  pshufb              m4, m0, [sh_b2w12345678]
  pshufb              m3, m0, [sh_b2w89abcdef]
  pshufb              m2, m0, [sh_b2w01234567]
  paddw               m4, m4
  paddw               m5, m5
  paddw               m2, m6
  paddw               m3, m7
  paddw               m4, [pw_2]
  paddw               m5, [pw_2]
  paddw               m2, m4
  paddw               m3, m5
  psraw               m2, 2
  psraw               m3, 2
  pshufb              m4, m0, m1
  packuswb            m2, m3                  ; odd lines
  pavgb               m0, m4                  ; even lines
  mov              lined, 4
.loop:
  mova  [dstq          ], m0
  mova  [dstq+strideq  ], m2
  pshufb              m0, m1
  pshufb              m2, m1
  mova  [dstq+strideq*2], m0
  mova  [dstq+stride3q ], m2
  pshufb              m0, m1
  pshufb              m2, m1
  lea               dstq, [dstq+strideq*4]
  dec              lined
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d63_predictor_32x32, 3, 4, 8, dst, stride, above, line
  mova                   m0, [aboveq]
  mova                   m7, [aboveq+16]
  DEFINE_ARGS dst, stride, stride3, line
  lea              stride3q, [strideq*3]
  pshufb                 m6, m7, [sh_b2wabcdefff]
  pshufb                 m5, m7, [sh_b2w23456789]
  pshufb                 m4, m7, [sh_b2w9abcdeff]
  pshufb                 m3, m7, [sh_b2w12345678]
  pshufb                 m2, m7, [sh_b2w89abcdef]
  pshufb                 m1, m7, [sh_b2w01234567]
  paddw                  m4, m4
  paddw                  m3, m3
  paddw                  m5, m1
  paddw                  m6, m2
  paddw                  m3, [pw_2]
  paddw                  m4, [pw_2]
  paddw                  m5, m3
  paddw                  m6, m4
  psraw                  m5, 2
  psraw                  m6, 2
  packuswb               m5, m6                     ; high 16px of even lines
  pshufb                 m4, m0, [sh_b2w23456789]
  pshufb                 m3, m0, [sh_b2w12345678]
  pshufb                 m2, m0, [sh_b2w01234567]
  pshufb                 m6, m0, [sh_b2w89abcdef]
  paddw                  m2, m4
  palignr                m4, m1, m6, 2
  palignr                m1, m6, 4
  paddw                  m3, m3
  paddw                  m4, m4
  paddw                  m6, m1
  paddw                  m3, [pw_2]
  paddw                  m4, [pw_2]
  paddw                  m2, m3
  paddw                  m6, m4
  psraw                  m2, 2
  psraw                  m6, 2
  packuswb               m2, m6                     ; low 16px of even lines
  mova                   m1, [sh_b123456789abcdeff]
  pshufb                 m3, m7, m1                 ; high 16px of odd lines
  pavgb                  m3, m7 ; KEEP - upper avg
  palignr                m7, m0, 1                  ; low 16px of odd lines
  pavgb                  m0, m7 ; KEEP - lower avg
  mov                 lined, 8
.loop:
  mova  [dstq             ], m0
  mova  [dstq          +16], m3
  mova  [dstq+strideq     ], m2
  mova  [dstq+strideq  +16], m5
  palignr                m6, m3, m0, 1
  palignr                m7, m5, m2, 1
  pshufb                 m3, m1
  pshufb                 m5, m1
  mova  [dstq+strideq*2   ], m6
  mova  [dstq+strideq*2+16], m3
  mova  [dstq+stride3q    ], m7
  mova  [dstq+stride3q +16], m5
  palignr                m0, m3, m6, 1
  palignr                m2, m5, m7, 1
  pshufb                 m3, m1
  pshufb                 m5, m1
  lea                  dstq, [dstq+strideq*4]
  dec                 lined
  jnz .loop
  REP_RET

INIT_MMX ssse3
cglobal d27_predictor_4x4, 2, 4, 4, dst, stride, unused, left
  movifnidn        leftq, leftmp
  movd                m0, [leftq]        ; abcd [byte]
  pxor                m2, m2
  pshufb              m1, m0, [sh_b1233] ; bcdd [byte]
  pavgb               m1, m0             ; ab, bc, cd, d [byte]
  punpcklbw           m0, m2             ; a, b, c, d [word]
  pshufw              m2, m0, q3332      ; c, d, d, d [word]
  pshufw              m3, m0, q3321      ; b, c, d, d [word]
  paddw               m2, m0
  paddw               m3, m3
  paddw               m2, [pw_2]
  paddw               m2, m3
  psraw               m2, 2
  packuswb            m2, m2             ; a2bc, b2cd, c3d, d [byte]
  punpcklbw           m1, m2             ; ab, a2bc, bc, b2cd, cd, c3d, d, d
  movd    [dstq        ], m1
  psrlq               m1, 16             ; bc, b2cd, cd, c3d, d, d
  movd    [dstq+strideq], m1
  lea               dstq, [dstq+strideq*2]
  psrlq               m1, 16             ; cd, c3d, d, d
  movd    [dstq        ], m1
  pshufw              m1, m1, q1111      ; d, d, d, d
  movd    [dstq+strideq], m1
  RET

INIT_XMM ssse3
cglobal d27_predictor_8x8, 2, 4, 4, dst, stride, stride3, left
  movifnidn        leftq, leftmp
  movq                m0, [leftq]            ; abcdefgh [byte]
  lea           stride3q, [strideq*3]
  pshufb              m1, m0, [sh_b12345677] ; bcdefghh [byte]
  pavgb               m1, m0                 ; ab, bc, cd, de, ef, fg, gh, h
  pshufb              m2, m0, [sh_b2w23456777] ; c, d, e, f, g, h, h, h [word]
  pshufb              m3, m0, [sh_b2w12345677] ; b, c, d, e, f, g, h, h [word]
  pshufb              m0, [sh_b2w01234567]
  paddw               m2, m0
  paddw               m3, m3
  paddw               m2, [pw_2]
  paddw               m2, m3
  psraw               m2, 2
  packuswb            m2, m2        ; a2bc, b2cd, c2de, d2ef, e2fg, f2gh, g3h, h
  punpcklbw           m1, m2        ; interleaved output
  movq  [dstq          ], m1
  psrldq              m1, 2
  movq  [dstq+strideq  ], m1
  psrldq              m1, 2
  movq  [dstq+strideq*2], m1
  psrldq              m1, 2
  movq  [dstq+stride3q ], m1
  lea               dstq, [dstq+strideq*4]
  pshufhw             m1, m1, q0000 ; de, d2ef, ef, e2fg, fg, f2gh, gh, g3h, 8xh
  psrldq              m1, 2
  movq  [dstq          ], m1
  psrldq              m1, 2
  movq  [dstq+strideq  ], m1
  psrldq              m1, 2
  movq  [dstq+strideq*2], m1
  psrldq              m1, 2
  movq  [dstq+stride3q ], m1
  RET

INIT_XMM ssse3
cglobal d27_predictor_16x16, 2, 4, 7, dst, stride, stride3, left
  lea           stride3q, [strideq*3]
  movifnidn        leftq, leftmp
  mova                m0, [leftq]            ; abcdefghijklmnop [byte]
  pshufb              m1, m0, [sh_b123456789abcdeff] ; bcdefghijklmnopp [byte]
  pavgb               m1, m0                 ; ab, bc, cd .. no, op, pp [byte]
  pshufb              m6, m0, [sh_b2wabcdefff]
  pshufb              m3, m0, [sh_b2w23456789]
  pshufb              m5, m0, [sh_b2w9abcdeff]
  pshufb              m2, m0, [sh_b2w12345678]
  pshufb              m4, m0, [sh_b2w89abcdef]
  pshufb              m0, [sh_b2w01234567]
  paddw               m2, m2
  paddw               m5, m5
  paddw               m0, m3
  paddw               m4, m6
  paddw               m2, [pw_2]
  paddw               m5, [pw_2]
  paddw               m0, m2
  paddw               m4, m5
  psraw               m0, 2
  psraw               m4, 2
  packuswb            m0, m4
  punpckhbw           m4, m1, m0    ; interleaved input
  punpcklbw           m1, m0        ; interleaved output
  mova  [dstq          ], m1
  palignr             m0, m4, m1, 2
  mova  [dstq+strideq  ], m0
  palignr             m0, m4, m1, 4
  mova  [dstq+strideq*2], m0
  palignr             m0, m4, m1, 6
  mova  [dstq+stride3q ], m0
  lea               dstq, [dstq+strideq*4]
  palignr             m0, m4, m1, 8
  mova  [dstq          ], m0
  palignr             m0, m4, m1, 10
  mova  [dstq+strideq  ], m0
  palignr             m0, m4, m1, 12
  mova  [dstq+strideq*2], m0
  palignr             m0, m4, m1, 14
  mova  [dstq+stride3q ], m0
  DEFINE_ARGS dst, stride, stride3, line
  mov              lined, 2
  mova                m0, [sh_b23456789abcdefff]
.loop:
  lea               dstq, [dstq+strideq*4]
  mova  [dstq          ], m4
  pshufb              m4, m0
  mova  [dstq+strideq  ], m4
  pshufb              m4, m0
  mova  [dstq+strideq*2], m4
  pshufb              m4, m0
  mova  [dstq+stride3q ], m4
  pshufb              m4, m0
  dec              lined
  jnz .loop
  REP_RET

INIT_XMM ssse3
cglobal d27_predictor_32x32, 2, 4, 8, dst, stride, stride3, left
  lea           stride3q, [strideq*3]
  movifnidn        leftq, leftmp
  mova                m0, [leftq]              ;  0-15 [byte]
  mova                m7, [leftq+16]           ; 16-31 [byte]
  palignr             m1, m7, m0, 1            ;  1-16
  pshufb              m2, m7, [sh_b123456789abcdeff] ; 17-32 [byte]
  pavgb               m1, m0                   ; avg{ 0-15, 1-16} [byte]
  pavgb               m2, m7                   ; avg{16-31,17-32} [byte]
  palignr             m5, m7, m0, 8            ;  8-24 [byte]
  pshufb              m4, m0, [sh_b2w23456789]
  pshufb              m3, m0, [sh_b2w12345678]
  pshufb              m0, [sh_b2w01234567]
  pshufb              m6, m5, [sh_b2w23456789]
  paddw               m0, m4
  pshufb              m4, m5, [sh_b2w01234567]
  pshufb              m5, [sh_b2w12345678]
  paddw               m3, m3
  paddw               m4, m6
  paddw               m5, m5
  paddw               m3, [pw_2]
  paddw               m5, [pw_2]
  paddw               m0, m3
  paddw               m4, m5
  psraw               m0, 2
  psraw               m4, 2
  packuswb            m0, m4                   ; 3-tap lower result [byte]
  pshufb              m3, m7, [sh_b2w23456789]
  pshufb              m4, m7, [sh_b2w12345678]
  pshufb              m5, m7, [sh_b2w01234567]
  pshufb              m6, m7, [sh_b2w89abcdef]
  paddw               m3, m5
  pshufb              m5, m7, [sh_b2w9abcdeff]
  pshufb              m7, [sh_b2wabcdefff]
  paddw               m4, m4
  paddw               m5, m5
  paddw               m6, m7
  paddw               m3, [pw_2]
  paddw               m6, [pw_2]
  paddw               m3, m4
  paddw               m5, m6
  psraw               m3, 2
  psraw               m5, 2
  packuswb            m3, m5                   ; 3-tap higher result [byte]
  punpckhbw           m6, m1, m0               ; interleaved output 2
  punpcklbw           m1, m0                   ; interleaved output 1
  punpckhbw           m7, m2, m3               ; interleaved output 4
  punpcklbw           m2, m3                   ; interleaved output 3

  ; output 1st 8 lines (and half of 2nd 8 lines)
  DEFINE_ARGS dst, stride, stride3, dst8
  lea                  dst8q, [dstq+strideq*8]
  mova  [dstq              ], m1
  mova  [dstq           +16], m6
  mova  [dst8q             ], m6
  palignr             m0, m6, m1, 2
  palignr             m4, m2, m6, 2
  mova  [dstq +strideq     ], m0
  mova  [dstq +strideq  +16], m4
  mova  [dst8q+strideq     ], m4
  palignr             m0, m6, m1, 4
  palignr             m4, m2, m6, 4
  mova  [dstq +strideq*2   ], m0
  mova  [dstq +strideq*2+16], m4
  mova  [dst8q+strideq*2   ], m4
  palignr             m0, m6, m1, 6
  palignr             m4, m2, m6, 6
  mova  [dstq +stride3q    ], m0
  mova  [dstq +stride3q +16], m4
  mova  [dst8q+stride3q    ], m4
  lea               dstq, [dstq +strideq*4]
  lea              dst8q, [dst8q+strideq*4]
  palignr             m0, m6, m1, 8
  palignr             m4, m2, m6, 8
  mova  [dstq              ], m0
  mova  [dstq           +16], m4
  mova  [dst8q             ], m4
  palignr             m0, m6, m1, 10
  palignr             m4, m2, m6, 10
  mova  [dstq +strideq     ], m0
  mova  [dstq +strideq  +16], m4
  mova  [dst8q+strideq     ], m4
  palignr             m0, m6, m1, 12
  palignr             m4, m2, m6, 12
  mova  [dstq +strideq*2   ], m0
  mova  [dstq +strideq*2+16], m4
  mova  [dst8q+strideq*2   ], m4
  palignr             m0, m6, m1, 14
  palignr             m4, m2, m6, 14
  mova  [dstq +stride3q    ], m0
  mova  [dstq +stride3q +16], m4
  mova  [dst8q+stride3q    ], m4
  lea               dstq, [dstq+strideq*4]
  lea              dst8q, [dst8q+strideq*4]

  ; output 2nd half of 2nd 8 lines and half of 3rd 8 lines
  mova  [dstq           +16], m2
  mova  [dst8q             ], m2
  palignr             m4, m7, m2, 2
  mova  [dstq +strideq  +16], m4
  mova  [dst8q+strideq     ], m4
  palignr             m4, m7, m2, 4
  mova  [dstq +strideq*2+16], m4
  mova  [dst8q+strideq*2   ], m4
  palignr             m4, m7, m2, 6
  mova  [dstq +stride3q +16], m4
  mova  [dst8q+stride3q    ], m4
  lea               dstq, [dstq+strideq*4]
  lea              dst8q, [dst8q+strideq*4]
  palignr             m4, m7, m2, 8
  mova  [dstq           +16], m4
  mova  [dst8q             ], m4
  palignr             m4, m7, m2, 10
  mova  [dstq +strideq  +16], m4
  mova  [dst8q+strideq     ], m4
  palignr             m4, m7, m2, 12
  mova  [dstq +strideq*2+16], m4
  mova  [dst8q+strideq*2   ], m4
  palignr             m4, m7, m2, 14
  mova  [dstq +stride3q +16], m4
  mova  [dst8q+stride3q    ], m4
  lea               dstq, [dstq+strideq*4]
  lea              dst8q, [dst8q+strideq*4]

  ; output 2nd half of 3rd 8 lines and half of 4th 8 lines
  mova                m0, [sh_b23456789abcdefff]
  mova  [dstq           +16], m7
  mova  [dst8q             ], m7
  pshufb              m7, m0
  mova  [dstq +strideq  +16], m7
  mova  [dst8q+strideq     ], m7
  pshufb              m7, m0
  mova  [dstq +strideq*2+16], m7
  mova  [dst8q+strideq*2   ], m7
  pshufb              m7, m0
  mova  [dstq +stride3q +16], m7
  mova  [dst8q+stride3q    ], m7
  pshufb              m7, m0
  lea               dstq, [dstq+strideq*4]
  lea              dst8q, [dst8q+strideq*4]
  mova  [dstq           +16], m7
  mova  [dst8q             ], m7
  pshufb              m7, m0
  mova  [dstq +strideq  +16], m7
  mova  [dst8q+strideq     ], m7
  pshufb              m7, m0
  mova  [dstq +strideq*2+16], m7
  mova  [dst8q+strideq*2   ], m7
  pshufb              m7, m0
  mova  [dstq +stride3q +16], m7
  mova  [dst8q+stride3q    ], m7
  pshufb              m7, m0
  lea               dstq, [dstq+strideq*4]

  ; output last half of 4th 8 lines
  mova  [dstq           +16], m7
  mova  [dstq +strideq  +16], m7
  mova  [dstq +strideq*2+16], m7
  mova  [dstq +stride3q +16], m7
  lea               dstq, [dstq+strideq*4]
  mova  [dstq           +16], m7
  mova  [dstq +strideq  +16], m7
  mova  [dstq +strideq*2+16], m7
  mova  [dstq +stride3q +16], m7

  ; done!
  RET

INIT_XMM ssse3
cglobal d153_predictor_4x4, 4, 4, 4, dst, stride, above, left
  movd                m0, [leftq]               ; l1, l2, l3, l4
  movd                m1, [aboveq-1]            ; tl, t1, t2, t3
  punpckldq           m0, m1                    ; l1, l2, l3, l4, tl, t1, t2, t3
  pshufb              m0, [sh_b2w32104567]      ; l4, l3, l2, l1, tl, t1, t2, t3
  psrldq              m1, m0, 2                 ; l3, l2, l1, tl, t1, t2, t3
  psrldq              m2, m0, 4                 ; l2, l1, tl, t1, t2, t3
  ; comments below are for a predictor like this
  ; A1 B1 C1 D1
  ; A2 B2 A1 B1
  ; A3 B3 A2 B2
  ; A4 B4 A3 B3
  pavgw               m3, m1, m0                ; 2-tap avg A4 A3 A2 A1
  paddw               m1, m1
  paddw               m2, m0
  paddw               m1, [pw_2]
  paddw               m2, m1
  psraw               m2, 2                     ; 3-tap avg B4 B3 B2 B1 C1 D1
  packuswb            m2, m3                    ; B4 B3 B2 B1 C1 D1 x x A4 A3 A2 A1 ..
  DEFINE_ARGS dst, stride, stride3
  lea           stride3q, [strideq*3]
  pshufb              m2, [sh_b8091a2b345]      ; A4 B4 A3 B3 A2 B2 A1 B1 C1 D1 ..
  movd  [dstq+stride3q ], m2
  psrldq              m2, 2                     ; A3 B3 A2 B2 A1 B1 C1 D1 ..
  movd  [dstq+strideq*2], m2
  psrldq              m2, 2                     ; A2 B2 A1 B1 C1 D1 ..
  movd  [dstq+strideq  ], m2
  psrldq              m2, 2                     ; A1 B1 C1 D1 ..
  movd  [dstq          ], m2
  RET

INIT_XMM ssse3
cglobal d153_predictor_8x8, 4, 4, 7, dst, stride, above, left
  movq                m0, [leftq]               ; [0- 7] l1-8 [byte]
  movhps              m0, [aboveq-1]            ; [8-15] tl, t1-7 [byte]
  pshufb              m1, m0, [sh_b2w76543210]  ; l8-1 [word]
  pshufb              m2, m0, [sh_b2w65432108]  ; l7-1,tl [word]
  pshufb              m3, m0, [sh_b2w54321089]  ; l6-1,tl,t1 [word]
  pshufb              m0, [sh_b2w89abcdef]      ; tl,t1-7 [word]
  psrldq              m4, m0, 2                 ; t1-7 [word]
  psrldq              m5, m0, 4                 ; t2-7 [word]
  ; comments below are for a predictor like this
  ; A1 B1 C1 D1 E1 F1 G1 H1
  ; A2 B2 A1 B1 C1 D1 E1 F1
  ; A3 B3 A2 B2 A1 B1 C1 D1
  ; A4 B4 A3 B3 A2 B2 A1 B1
  ; A5 B5 A4 B4 A3 B3 A2 B2
  ; A6 B6 A5 B5 A4 B4 A3 B3
  ; A7 B7 A6 B6 A5 B5 A4 B4
  ; A8 B8 A7 B7 A6 B6 A5 B5
  pavgw               m6, m1, m2                ; 2-tap avg A8-A1
  paddw               m4, m4
  paddw               m2, m2
  paddw               m0, m5
  paddw               m1, m3
  paddw               m4, [pw_2]
  paddw               m2, [pw_2]
  paddw               m0, m4
  paddw               m1, m2
  psraw               m0, 2                     ; 3-tap avg C-H1
  psraw               m1, 2                     ; 3-tap avg B8-1
  packuswb            m6, m6
  packuswb            m1, m1
  packuswb            m0, m0
  punpcklbw           m6, m1                    ; A-B8, A-B7 ... A-B2, A-B1

  DEFINE_ARGS dst, stride, stride3
  lea           stride3q, [strideq*3]

  movhps [dstq+stride3q], m6                    ; A-B4, A-B3, A-B2, A-B1
  palignr             m1, m0, m6, 10            ; A-B3, A-B2, A-B1, C-H1
  movq  [dstq+strideq*2], m1
  psrldq              m1, 2                     ; A-B2, A-B1, C-H1
  movq  [dstq+strideq  ], m1
  psrldq              m1, 2                     ; A-H1
  movq  [dstq          ], m1
  lea               dstq, [dstq+strideq*4]
  movq  [dstq+stride3q ], m6                    ; A-B8, A-B7, A-B6, A-B5
  psrldq              m6, 2                     ; A-B7, A-B6, A-B5, A-B4
  movq  [dstq+strideq*2], m6
  psrldq              m6, 2                     ; A-B6, A-B5, A-B4, A-B3
  movq  [dstq+strideq  ], m6
  psrldq              m6, 2                     ; A-B5, A-B4, A-B3, A-B2
  movq  [dstq          ], m6
  RET
