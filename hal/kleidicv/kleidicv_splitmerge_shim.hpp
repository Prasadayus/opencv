// KleidiCV exports kleidicv_split/kleidicv_merge but its adapter wires neither; OpenCV declares
// and calls cv_hal_split*/cv_hal_merge*. OpenCV passes one flattened row, so height is 1 and the
// stride rules do not apply. KleidiCV source is unmodified.
#ifdef OPENCV_CORE_HAL_REPLACEMENT_HPP
#ifndef OPENCV_KLEIDICV_SPLITMERGE_SHIM_HPP
#define OPENCV_KLEIDICV_SPLITMERGE_SHIM_HPP

#include <kleidicv/kleidicv.h>
#include <cstdlib>

// A/B switch: OPENCV_KLEIDICV_SPLITMERGE=0 declines, so one binary runs both arms.
static inline bool kcv_sm_on() {
  static const bool on = getenv("OPENCV_KLEIDICV_SPLITMERGE") == nullptr ||
                         getenv("OPENCV_KLEIDICV_SPLITMERGE")[0] != '0';
  return on;
}

template <typename T>
static inline int kcv_split_impl(const T *src, T **dst, int len, int cn) {
  if (!kcv_sm_on() || cn < 2 || cn > 4 || len <= 0) {
    return CV_HAL_ERROR_NOT_IMPLEMENTED;
  }
  void *dptr[4];
  size_t strides[4];
  for (int i = 0; i < cn; ++i) {
    dptr[i] = dst[i];
    strides[i] = static_cast<size_t>(len) * sizeof(T);
  }
  return kleidicv_split(src, static_cast<size_t>(len) * cn * sizeof(T), dptr,
                        strides, static_cast<size_t>(len), 1,
                        static_cast<size_t>(cn), sizeof(T)) == KLEIDICV_OK
             ? CV_HAL_ERROR_OK
             : CV_HAL_ERROR_NOT_IMPLEMENTED;
}

template <typename T>
static inline int kcv_merge_impl(const T **src, T *dst, int len, int cn) {
  if (!kcv_sm_on() || cn < 2 || cn > 4 || len <= 0) {
    return CV_HAL_ERROR_NOT_IMPLEMENTED;
  }
  const void *sptr[4];
  size_t strides[4];
  for (int i = 0; i < cn; ++i) {
    sptr[i] = src[i];
    strides[i] = static_cast<size_t>(len) * sizeof(T);
  }
  return kleidicv_merge(sptr, strides, dst,
                        static_cast<size_t>(len) * cn * sizeof(T),
                        static_cast<size_t>(len), 1, static_cast<size_t>(cn),
                        sizeof(T)) == KLEIDICV_OK
             ? CV_HAL_ERROR_OK
             : CV_HAL_ERROR_NOT_IMPLEMENTED;
}

#define KCV_SPLITMERGE(suffix, T)                                           \
  static inline int kcv_split##suffix(const T *src, T **dst, int len,       \
                                      int cn) {                             \
    return kcv_split_impl<T>(src, dst, len, cn);                            \
  }                                                                         \
  static inline int kcv_merge##suffix(const T **src, T *dst, int len,       \
                                      int cn) {                             \
    return kcv_merge_impl<T>(src, dst, len, cn);                            \
  }

KCV_SPLITMERGE(8u, uchar)
KCV_SPLITMERGE(16u, ushort)
KCV_SPLITMERGE(32s, int)
KCV_SPLITMERGE(64s, int64)

#undef cv_hal_split8u
#define cv_hal_split8u kcv_split8u
#undef cv_hal_split16u
#define cv_hal_split16u kcv_split16u
#undef cv_hal_split32s
#define cv_hal_split32s kcv_split32s
#undef cv_hal_split64s
#define cv_hal_split64s kcv_split64s

#undef cv_hal_merge8u
#define cv_hal_merge8u kcv_merge8u
#undef cv_hal_merge16u
#define cv_hal_merge16u kcv_merge16u
#undef cv_hal_merge32s
#define cv_hal_merge32s kcv_merge32s
#undef cv_hal_merge64s
#define cv_hal_merge64s kcv_merge64s

#endif
#endif
