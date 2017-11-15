#ifndef REDREAM_MATH_H
#define REDREAM_MATH_H

#include <float.h>
#include <math.h>
#include <stdint.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, lo, hi) MAX((lo), MIN((hi), (x)))
#define ABS(x) ((x) < 0 ? -(x) : (x))

#define ALIGN_UP(v, alignment) (((v) + (alignment)-1) & ~((alignment)-1))
#define ALIGN_DOWN(v, alignment) ((v) & ~((alignment)-1))

/*
 * bitwise ops
 */
static inline uint32_t bswap24(uint32_t v) {
  return ((v & 0xff) << 16) | (v & 0x00ff00) | ((v & 0xff0000) >> 16);
}

static inline int popcnt32(uint32_t v) {
  /* avoid using popcnt intrinsics to support older processors such as the
     core 2 duo which are plenty fast enough to support */
  v = (v & 0x55555555) + ((v >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  v = (v & 0x0f0f0f0f) + ((v >> 4) & 0x0f0f0f0f);
  v = (v & 0x00ff00ff) + ((v >> 8) & 0x00ff00ff);
  v = (v & 0x0000ffff) + ((v >> 16) & 0x0000ffff);
  return (int)v;
}

#if COMPILER_MSVC

#include <intrin.h>

static inline int clz32(uint32_t v) {
  unsigned long r = 0;
  _BitScanReverse(&r, v);
  return 31 - r;
}
static inline int clz64(uint64_t v) {
  unsigned long r = 0;
  _BitScanReverse64(&r, v);
  return 63 - r;
}
static inline int ctz32(uint32_t v) {
  unsigned long r = 0;
  _BitScanForward(&r, v);
  return r;
}
static inline int ctz64(uint64_t v) {
  unsigned long r = 0;
  _BitScanForward64(&r, v);
  return r;
}

static inline uint16_t bswap16(uint16_t v) {
  return _byteswap_ushort(v);
}
static inline uint32_t bswap32(uint32_t v) {
  return _byteswap_ulong(v);
}

#else

static inline int clz32(uint32_t v) {
  return __builtin_clz(v);
}
static inline int clz64(uint64_t v) {
  return __builtin_clzll(v);
}
static inline int ctz32(uint32_t v) {
  return __builtin_ctz(v);
}
static inline int ctz64(uint64_t v) {
  return __builtin_ctzll(v);
}

static inline uint16_t bswap16(uint16_t v) {
  return __builtin_bswap16(v);
}
static inline uint32_t bswap32(uint32_t v) {
  return __builtin_bswap32(v);
}

#endif

/*
 * scalar ops
 */
static inline uint32_t npow2(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

/*
 * vector ops
 */
static inline float vec3_dot(float *a, float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float vec3_len(float *a) {
  return sqrtf(vec3_dot(a, a));
}

static inline float vec3_normalize(float *a) {
  float len = vec3_len(a);
  if (len) {
    a[0] /= len;
    a[1] /= len;
    a[2] /= len;
  }
  return len;
}

static inline void vec3_add(float *out, float *a, float *b) {
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
}
static inline void vec3_sub(float *out, float *a, float *b) {
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
}

static inline void vec3_cross(float *out, float *a, float *b) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static inline void vec2_add(float *out, float *a, float *b) {
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
}

static inline void vec2_sub(float *out, float *a, float *b) {
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
}

#endif
