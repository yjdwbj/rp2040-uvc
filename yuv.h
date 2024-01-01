// Copyright 2010 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// inline YUV<->RGB conversion function
//
// The exact naming is Y'CbCr, following the ITU-R BT.601 standard.
// More information at: http://en.wikipedia.org/wiki/YCbCr
// Y = 0.2569 * R + 0.5044 * G + 0.0979 * B + 16
// U = -0.1483 * R - 0.2911 * G + 0.4394 * B + 128
// V = 0.4394 * R - 0.3679 * G - 0.0715 * B + 128
// We use 16bit fixed point operations for RGB->YUV conversion (YUV_FIX).
//
// For the Y'CbCr to RGB conversion, the BT.601 specification reads:
//   R = 1.164 * (Y-16) + 1.596 * (V-128)
//   G = 1.164 * (Y-16) - 0.813 * (V-128) - 0.391 * (U-128)
//   B = 1.164 * (Y-16)                   + 2.018 * (U-128)
// where Y is in the [16,235] range, and U/V in the [16,240] range.
// In the table-lookup version (WEBP_YUV_USE_TABLE), the common factor
// "1.164 * (Y-16)" can be handled as an offset in the VP8kClip[] table.
// So in this case the formulae should read:
//   R = 1.164 * [Y + 1.371 * (V-128)                  ] - 18.624
//   G = 1.164 * [Y - 0.698 * (V-128) - 0.336 * (U-128)] - 18.624
//   B = 1.164 * [Y                   + 1.733 * (U-128)] - 18.624
// once factorized.
// For YUV->RGB conversion, only 14bit fixed precision is used (YUV_FIX2).
// That's the maximum possible for a convenient ARM implementation.
//
// Author: Skal (pascal.massimino@gmail.com)
#ifndef RP2040_YUV_H_
#define RP2040_YUV_H_

#include <stdint.h>

enum {
    YUV_FIX = 16, // fixed-point precision for RGB->YUV
    YUV_HALF = 1 << (YUV_FIX - 1),
    YUV_MASK = (256 << YUV_FIX) - 1,
    YUV_RANGE_MIN = -227,      // min value of r/g/b output
    YUV_RANGE_MAX = 256 + 226, // max value of r/g/b output
    YUV_FIX2 = 14,             // fixed-point precision for YUV->RGB
    YUV_HALF2 = 1 << (YUV_FIX2 - 1),
    YUV_MASK2 = (256 << YUV_FIX2) - 1
};
// These constants are 14b fixed-point version of ITU-R BT.601 constants.
#define kYScale 19077 // 1.164 = 255 / 219
#define kVToR 26149   // 1.596 = 255 / 112 * 0.701
#define kUToG 6419    // 0.391 = 255 / 112 * 0.886 * 0.114 / 0.587
#define kVToG 13320   // 0.813 = 255 / 112 * 0.701 * 0.299 / 0.587
#define kUToB 33050   // 2.018 = 255 / 112 * 0.886
#define kRCst (-kYScale * 16 - kVToR * 128 + YUV_HALF2)
#define kGCst (-kYScale * 16 + kUToG * 128 + kVToG * 128 + YUV_HALF2)
#define kBCst (-kYScale * 16 - kUToB * 128 + YUV_HALF2)
//------------------------------------------------------------------------------
static inline int VP8Clip8(int v) {
    return ((v & ~YUV_MASK2) == 0) ? (v >> YUV_FIX2) : (v < 0) ? 0
                                                               : 255;
}
static inline int VP8YUVToR(int y, int v) {
    return VP8Clip8(kYScale * y + kVToR * v + kRCst);
}
static inline int VP8YUVToG(int y, int u, int v) {
    return VP8Clip8(kYScale * y - kUToG * u - kVToG * v + kGCst);
}
static inline int VP8YUVToB(int y, int u) {
    return VP8Clip8(kYScale * y + kUToB * u + kBCst);
}

#define WEBP_SWAP_16BIT_CSP
static inline void VP8YuvToRgb565(int y, int u, int v,
                                  uint8_t *const rgb) {
    const int r = VP8YUVToR(y, v);    // 5 usable bits
    const int g = VP8YUVToG(y, u, v); // 6 usable bits
    const int b = VP8YUVToB(y, u);    // 5 usable bits
    const int rg = (r & 0xf8) | (g >> 5);
    const int gb = ((g << 3) & 0xe0) | (b >> 3);
#ifdef WEBP_SWAP_16BIT_CSP
    rgb[0] = (uint8_t)gb;
    rgb[1] = (uint8_t)rg;
#else
    rgb[0] = (uint8_t)rg;
    rgb[1] = (uint8_t)gb;
#endif
}

uint16_t yuv422_to_rgb565(int y, int u, int v);

#endif // RP2040_YUV_H_