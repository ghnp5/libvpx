# This file automatically generated by configure. Do not edit!
TOOLCHAIN := x86_64-linux-gcc
ALL_TARGETS += libs
ALL_TARGETS += examples
ALL_TARGETS += docs

PREFIX=/usr/local
ifeq ($(MAKECMDGOALS),dist)
DIST_DIR?=vpx-vp8-nodocs-x86_64-linux-v0.9.7-p1-906-g03f2634
else
DIST_DIR?=$(DESTDIR)/usr/local
endif
LIBSUBDIR=lib

VERSION_STRING=v0.9.7-p1-906-g03f2634

VERSION_MAJOR=0
VERSION_MINOR=9
VERSION_PATCH=7

CONFIGURE_ARGS=--enable-internal-stats --enable-experimental --enable-uvintra --enable-newentropy --enable-high-precision-mv --enable-sixteenth-subpel-uv --enable-enhanced-interp --enable-expanded-coef-context --enable-newintramodes --enable-adaptive-entropy --enable-newupdate --enable-pred-filter --enable-hybridtransform --enable-debug --disable-optimizations
CONFIGURE_ARGS?=--enable-internal-stats --enable-experimental --enable-uvintra --enable-newentropy --enable-high-precision-mv --enable-sixteenth-subpel-uv --enable-enhanced-interp --enable-expanded-coef-context --enable-newintramodes --enable-adaptive-entropy --enable-newupdate --enable-pred-filter --enable-hybridtransform --enable-debug --disable-optimizations
