/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                          License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Copyright (C) 2015, Itseez Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include "hal_internal.hpp"

#ifdef HAVE_LAPACK

#include "opencv_lapack.h"

#ifdef HAVE_ARMPL
// The interleaved-batch family is ARMPL-specific - no other LAPACK exposes it - so everything
// built on it is guarded. opencv_lapack.h pulls in cblas.h and lapack.h but not this one; it
// resolves through ARMPL_INCLUDE_DIRS, which OpenCVFindLibsPerf.cmake:45 puts on every target.
#include <armpl_interleave_batch.h>
#endif

#include <cmath>
#include <algorithm>
#include <typeinfo>
#include <limits>
#include <complex>
#include <vector>
#include <cstring>

#define HAL_GEMM_SMALL_COMPLEX_MATRIX_THRESH 100
#define HAL_GEMM_SMALL_MATRIX_THRESH 100
#define HAL_SVD_SMALL_MATRIX_THRESH 25
#define HAL_QR_SMALL_MATRIX_THRESH 30
#define HAL_LU_SMALL_MATRIX_THRESH 100
#define HAL_CHOLESKY_SMALL_MATRIX_THRESH 100
#define HAL_EIGEN_SMALL_MATRIX_THRESH 16
#define HAL_EIGEN_VECTORS_MATRIX_THRESH 48
#define HAL_EIGEN_NONSYM_SMALL_MATRIX_THRESH 16   // provisional, set from the measurement
#define HAL_BATCHDIST_MIN_MACS 2e7                // m*n*len; provisional, set from the measurement
#define HAL_BATCHDIST_MIN_LEN 16                  // overhead is 1/m + 1/n + 1/len extra passes and
                                                  // the 1/len term never improves with size
#define HAL_NULLSPACE4X4_MIN_COUNT 64             // provisional, set from the measurement
#define HAL_NULLSPACE4X4_NINTER 4                 // small multiple of the f64 vector length (2)
#define HAL_MULTRANSPOSED_MIN_FLOPS 4096.0   // was: output dim < 16, i.e. 16^3 square
#define HAL_SCALEADD_SMALL_THRESH 64
#define HAL_MAHALANOBIS_SMALL_THRESH 16
#define HAL_TRANSFORM_SMALL_THRESH 256

#if defined(__clang__) && defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define CV_ANNOTATE_MEMORY_IS_INITIALIZED(address, size) \
__msan_unpoison(address, size)
#define CV_ANNOTATE_NO_SANITIZE_MEMORY __attribute__((no_sanitize("memory")))
#endif
#endif
#ifndef CV_ANNOTATE_MEMORY_IS_INITIALIZED
#define CV_ANNOTATE_MEMORY_IS_INITIALIZED(address, size) do { } while(0)
#define CV_ANNOTATE_NO_SANITIZE_MEMORY
#endif

#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

//lapack stores matrices in column-major order so transposing is needed everywhere
template <typename fptype> static inline void
transpose_square_inplace(fptype *src, size_t src_ld, size_t m)
{
    for(size_t i = 0; i < m - 1; i++)
        for(size_t j = i + 1; j < m; j++)
            std::swap(src[j*src_ld + i], src[i*src_ld + j]);
}

template <typename fptype> static inline void
transpose(const fptype *src, size_t src_ld, fptype* dst, size_t dst_ld, size_t m, size_t n)
{
    for(size_t i = 0; i < m; i++)
        for(size_t j = 0; j < n; j++)
            dst[j*dst_ld + i] = src[i*src_ld + j];
}

template <typename fptype> static inline void
copy_matrix(const fptype *src, size_t src_ld, fptype* dst, size_t dst_ld, size_t m, size_t n)
{
    for(size_t i = 0; i < m; i++)
        for(size_t j = 0; j < n; j++)
            dst[i*dst_ld + j] = src[i*src_ld + j];
}

template <typename fptype> static inline void
set_value(fptype *dst, size_t dst_ld, fptype value, size_t m, size_t n)
{
    for(size_t i = 0; i < m; i++)
        for(size_t j = 0; j < n; j++)
            dst[i*dst_ld + j] = value;
}

// MSAN can't see that the fortran LAPACK functions initialize `info`
template <typename fptype> static inline int
CV_ANNOTATE_NO_SANITIZE_MEMORY lapack_LU(fptype* a, size_t a_step, int m, fptype* b, size_t b_step, int n, int* info)
{
#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    cv::AutoBuffer<long> piv_buff(m);
    long lda = (long)(a_step / sizeof(fptype));
    long _m = static_cast<long>(m), _n = static_cast<long>(n);
    long _info[1];
#else
    cv::AutoBuffer<int> piv_buff(m);
    int lda = (int)(a_step / sizeof(fptype));
    int _m = m, _n = n;
    int* _info = info;
#endif
    auto piv = piv_buff.data();

    transpose_square_inplace(a, lda, m);

    if(b)
    {
        if(n == 1 && b_step == sizeof(fptype))
        {
            if(typeid(fptype) == typeid(float))
                sgesv_(&_m, &_n, (float*)a, &lda, piv, (float*)b, &_m, _info);
            else if(typeid(fptype) == typeid(double))
                dgesv_(&_m, &_n, (double*)a, &lda, piv, (double*)b, &_m, _info);
        }
        else
        {
            int ldb = (int)(b_step / sizeof(fptype));
            fptype* tmpB = new fptype[m*n];

            transpose(b, ldb, tmpB, m, m, n);

            if(typeid(fptype) == typeid(float))
                sgesv_(&_m, &_n, (float*)a, &lda, piv, (float*)tmpB, &_m, _info);
            else if(typeid(fptype) == typeid(double))
                dgesv_(&_m, &_n, (double*)a, &lda, piv, (double*)tmpB, &_m, _info);

            transpose(tmpB, m, b, ldb, n, m);
            delete[] tmpB;
        }
    }
    else
    {
        if(typeid(fptype) == typeid(float))
            sgetrf_(&_m, &_m, (float*)a, &lda, piv, _info);
        else if(typeid(fptype) == typeid(double))
            dgetrf_(&_m, &_m, (double*)a, &lda, piv, _info);
    }

#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    *info = static_cast<int>(_info[0]);
#endif

    int sign = 0;
    if(*info == 0)
    {
        for(int i = 0; i < m; i++)
            sign ^= piv[i] != i + 1;
        *info = sign ? -1 : 1;
    }
    else
        *info = 0; //in opencv LU function zero means error

    return CV_HAL_ERROR_OK;
}

template <typename fptype> static inline int
lapack_Cholesky(fptype* a, size_t a_step, int m, fptype* b, size_t b_step, int n, bool* info)
{
#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    long _m = static_cast<long>(m), _n = static_cast<long>(n);
    long lapackStatus = 0;
    long lda = (long)(a_step / sizeof(fptype));
#else
    int _m = m, _n = n;
    int lapackStatus = 0;
    int lda = (int)(a_step / sizeof(fptype));
#endif
    char L[] = {'L', '\0'};

    if(b)
    {
        if(n == 1 && b_step == sizeof(fptype))
        {
            if(typeid(fptype) == typeid(float))
                OCV_LAPACK_FUNC(sposv)(L, &_m, &_n, (float*)a, &lda, (float*)b, &_m, &lapackStatus);
            else if(typeid(fptype) == typeid(double))
                OCV_LAPACK_FUNC(dposv)(L, &_m, &_n, (double*)a, &lda, (double*)b, &_m, &lapackStatus);
        }
        else
        {
            int ldb = (int)(b_step / sizeof(fptype));
            fptype* tmpB = new fptype[m*n];
            transpose(b, ldb, tmpB, m, m, n);

            if(typeid(fptype) == typeid(float))
                OCV_LAPACK_FUNC(sposv)(L, &_m, &_n, (float*)a, &lda, (float*)tmpB, &_m, &lapackStatus);
            else if(typeid(fptype) == typeid(double))
                OCV_LAPACK_FUNC(dposv)(L, &_m, &_n, (double*)a, &lda, (double*)tmpB, &_m, &lapackStatus);

            transpose(tmpB, m, b, ldb, n, m);
            delete[] tmpB;
        }
    }
    else
    {
        if(typeid(fptype) == typeid(float))
            OCV_LAPACK_FUNC(spotrf)(L, &_m, (float*)a, &lda, &lapackStatus);
        else if(typeid(fptype) == typeid(double))
            OCV_LAPACK_FUNC(dpotrf)(L, &_m, (double*)a, &lda, &lapackStatus);
    }

    if(lapackStatus == 0) *info = true;
    else *info = false; //in opencv Cholesky function false means error

    return CV_HAL_ERROR_OK;
}

template <typename fptype> static inline int
lapack_SVD(fptype* a, size_t a_step, fptype *w, fptype* u, size_t u_step, fptype* vt, size_t v_step, int m, int n, int flags, int* info)
{
#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    long _m = static_cast<long>(m), _n = static_cast<long>(n);
    long _info[1];
    long lda = (long)(a_step / sizeof(fptype));
    long ldv = (long)(v_step / sizeof(fptype));
    long ldu = (long)(u_step / sizeof(fptype));
    long lwork = -1;
    cv::AutoBuffer<long> iworkBuf_(8 * std::min(m, n));
#else
    int _m = m, _n = n;
    int* _info = info;
    int lda = (int)(a_step / sizeof(fptype));
    int ldv = (int)(v_step / sizeof(fptype));
    int ldu = (int)(u_step / sizeof(fptype));
    int lwork = -1;
    cv::AutoBuffer<int> iworkBuf_(8 * std::min(m, n));
#endif
    auto iworkBuf = iworkBuf_.data();
    fptype work1 = 0;

    //A already transposed and m>=n
    char mode[] = { ' ', '\0'};
    if(flags & CV_HAL_SVD_NO_UV)
    {
        ldv = 1;
        mode[0] = 'N';
    }
    else if((flags & CV_HAL_SVD_SHORT_UV) && (flags & CV_HAL_SVD_MODIFY_A)) //short SVD, U stored in a
        mode[0] = 'O';
    else if((flags & CV_HAL_SVD_SHORT_UV) && !(flags & CV_HAL_SVD_MODIFY_A)) //short SVD, U stored in u if m>=n
        mode[0] = 'S';
    else if(flags & CV_HAL_SVD_FULL_UV) //full SVD, U stored in u or in a
        mode[0] = 'A';

    if((flags & CV_HAL_SVD_MODIFY_A) && (flags & CV_HAL_SVD_FULL_UV)) //U stored in a
    {
        u = new fptype[m*m];
        ldu = m;
    }

    if(typeid(fptype) == typeid(float))
        OCV_LAPACK_FUNC(sgesdd)(mode, &_m, &_n, (float*)a, &lda, (float*)w, (float*)u, &ldu, (float*)vt, &ldv, (float*)&work1, &lwork, iworkBuf, _info);
    else if(typeid(fptype) == typeid(double))
        OCV_LAPACK_FUNC(dgesdd)(mode, &_m, &_n, (double*)a, &lda, (double*)w, (double*)u, &ldu, (double*)vt, &ldv, (double*)&work1, &lwork, iworkBuf, _info);

    lwork = (int)round(work1); //optimal buffer size
    fptype* buffer = new fptype[lwork + 1];

    // Make sure MSAN sees the memory as having been written.
    // MSAN does not think it has been written because a different language is called.
    // Note: we do this here because if dgesdd is C++, MSAN errors can be reported within it.
    CV_ANNOTATE_MEMORY_IS_INITIALIZED(buffer, sizeof(fptype) * (lwork + 1));

    if(typeid(fptype) == typeid(float))
        OCV_LAPACK_FUNC(sgesdd)(mode, &_m, &_n, (float*)a, &lda, (float*)w, (float*)u, &ldu, (float*)vt, &ldv, (float*)buffer, &lwork, iworkBuf, _info);
    else if(typeid(fptype) == typeid(double))
        OCV_LAPACK_FUNC(dgesdd)(mode, &_m, &_n, (double*)a, &lda, (double*)w, (double*)u, &ldu, (double*)vt, &ldv, (double*)buffer, &lwork, iworkBuf, _info);

#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    *info = static_cast<int>(_info[0]);
#endif

    // Make sure MSAN sees the memory as having been written.
    // MSAN does not think it has been written because a different language was called.
    CV_ANNOTATE_MEMORY_IS_INITIALIZED(a, a_step * n);
    if (u)
      CV_ANNOTATE_MEMORY_IS_INITIALIZED(u, u_step * m);
    if (vt)
      CV_ANNOTATE_MEMORY_IS_INITIALIZED(vt, v_step * n);
    if (w)
      CV_ANNOTATE_MEMORY_IS_INITIALIZED(w, sizeof(fptype) * std::min(m, n));

    if(!(flags & CV_HAL_SVD_NO_UV))
        transpose_square_inplace(vt, ldv, n);

    if((flags & CV_HAL_SVD_MODIFY_A) && (flags & CV_HAL_SVD_FULL_UV))
    {
        for(int i = 0; i < m; i++)
            for(int j = 0; j < m; j++)
                a[i*lda + j] = u[i*m + j];
        delete[] u;
    }

    delete[] buffer;
    return CV_HAL_ERROR_OK;
}

template <typename fptype> static inline int
lapack_QR(fptype* a, size_t a_step, int m, int n, int k, fptype* b, size_t b_step, fptype* dst, int* info)
{
#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    long _m = static_cast<long>(m), _n = static_cast<long>(n), _k = static_cast<long>(k);
    long _info[1];
    long lda = (long)(a_step / sizeof(fptype));
    long lwork = -1;
    long ldtmpA;
#else
    int _m = m, _n = n, _k = k;
    int* _info = info;
    int lda = (int)(a_step / sizeof(fptype));
    int lwork = -1;
    int ldtmpA;
#endif

    char mode[] = { 'N', '\0' };
    if(m < n)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    std::vector<fptype> tmpAMemHolder;
    fptype* tmpA;

    if (m == n)
    {
        transpose_square_inplace(a, lda, m);
        tmpA = a;
        ldtmpA = lda;
    }
    else
    {
        tmpAMemHolder.resize(m*n);
        tmpA = &tmpAMemHolder.front();
        ldtmpA = m;
        transpose(a, lda, tmpA, m, m, n);
    }

    fptype work1 = 0.;

    if (b)
    {
        if (k == 1 && b_step == sizeof(fptype))
        {
            if (typeid(fptype) == typeid(float))
                OCV_LAPACK_FUNC(sgels)(mode, &_m, &_n, &_k, (float*)tmpA, &ldtmpA, (float*)b, &_m, (float*)&work1, &lwork, _info);
            else if (typeid(fptype) == typeid(double))
                OCV_LAPACK_FUNC(dgels)(mode, &_m, &_n, &_k, (double*)tmpA, &ldtmpA, (double*)b, &_m, (double*)&work1, &lwork, _info);

            lwork = cvRound(work1); //optimal buffer size
            std::vector<fptype> workBufMemHolder(lwork + 1);
            fptype* buffer = &workBufMemHolder.front();

            if (typeid(fptype) == typeid(float))
                OCV_LAPACK_FUNC(sgels)(mode, &_m, &_n, &_k, (float*)tmpA, &ldtmpA, (float*)b, &_m, (float*)buffer, &lwork, _info);
            else if (typeid(fptype) == typeid(double))
                OCV_LAPACK_FUNC(dgels)(mode, &_m, &_n, &_k, (double*)tmpA, &ldtmpA, (double*)b, &_m, (double*)buffer, &lwork, _info);
        }
        else
        {
            std::vector<fptype> tmpBMemHolder(m*k);
            fptype* tmpB = &tmpBMemHolder.front();
            int ldb = (int)(b_step / sizeof(fptype));
            transpose(b, ldb, tmpB, m, m, k);

            if (typeid(fptype) == typeid(float))
                OCV_LAPACK_FUNC(sgels)(mode, &_m, &_n, &_k, (float*)tmpA, &ldtmpA, (float*)tmpB, &_m, (float*)&work1, &lwork, _info);
            else if (typeid(fptype) == typeid(double))
                OCV_LAPACK_FUNC(dgels)(mode, &_m, &_n, &_k, (double*)tmpA, &ldtmpA, (double*)tmpB, &_m, (double*)&work1, &lwork, _info);

            lwork = cvRound(work1); //optimal buffer size
            std::vector<fptype> workBufMemHolder(lwork + 1);
            fptype* buffer = &workBufMemHolder.front();

            if (typeid(fptype) == typeid(float))
                OCV_LAPACK_FUNC(sgels)(mode, &_m, &_n, &_k, (float*)tmpA, &ldtmpA, (float*)tmpB, &_m, (float*)buffer, &lwork, _info);
            else if (typeid(fptype) == typeid(double))
                OCV_LAPACK_FUNC(dgels)(mode, &_m, &_n, &_k, (double*)tmpA, &ldtmpA, (double*)tmpB, &_m, (double*)buffer, &lwork, _info);

            transpose(tmpB, m, b, ldb, k, m);
        }
    }
    else
    {
        if (typeid(fptype) == typeid(float))
            sgeqrf_(&_m, &_n, (float*)tmpA, &ldtmpA, (float*)dst, (float*)&work1, &lwork, _info);
        else if (typeid(fptype) == typeid(double))
            dgeqrf_(&_m, &_n, (double*)tmpA, &ldtmpA, (double*)dst, (double*)&work1, &lwork, _info);

        lwork = cvRound(work1); //optimal buffer size
        std::vector<fptype> workBufMemHolder(lwork + 1);
        fptype* buffer = &workBufMemHolder.front();

        if (typeid(fptype) == typeid(float))
            sgeqrf_(&_m, &_n, (float*)tmpA, &ldtmpA, (float*)dst, (float*)buffer, &lwork, _info);
        else if (typeid(fptype) == typeid(double))
            dgeqrf_(&_m, &_n, (double*)tmpA, &ldtmpA, (double*)dst, (double*)buffer, &lwork, _info);
    }

    CV_ANNOTATE_MEMORY_IS_INITIALIZED(info, sizeof(int));
    if (m == n)
        transpose_square_inplace(a, lda, m);
    else
        transpose(tmpA, m, a, lda, n, m);

#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    *info = static_cast<int>(_info[0]);
#endif

    if (*info != 0)
        *info = 0;
    else
        *info = 1;

    return CV_HAL_ERROR_OK;
}

template <typename fptype> static inline int
lapack_gemm(const fptype *src1, size_t src1_step, const fptype *src2, size_t src2_step, fptype alpha,
            const fptype *src3, size_t src3_step, fptype beta, fptype *dst, size_t dst_step, int a_m, int a_n, int d_n, int flags)
{
    int ldsrc1 = (int)(src1_step / sizeof(fptype));
    int ldsrc2 = (int)(src2_step / sizeof(fptype));
    int ldsrc3 = (int)(src3_step / sizeof(fptype));
    int lddst = (int)(dst_step / sizeof(fptype));
    int c_m, c_n, d_m;
    CBLAS_TRANSPOSE transA, transB;

    if(flags & CV_HAL_GEMM_2_T)
    {
        transB = CblasTrans;
        if(flags & CV_HAL_GEMM_1_T )
        {
            d_m = a_n;
        }
        else
        {
            d_m = a_m;
        }
    }
    else
    {
        transB = CblasNoTrans;
        if(flags & CV_HAL_GEMM_1_T )
        {
            d_m = a_n;
        }
        else
        {
            d_m = a_m;
        }
    }

    if(flags & CV_HAL_GEMM_3_T)
    {
        c_m = d_n;
        c_n = d_m;
    }
    else
    {
        c_m = d_m;
        c_n = d_n;
    }

    if(flags & CV_HAL_GEMM_1_T )
    {
        transA = CblasTrans;
        std::swap(a_n, a_m);
    }
    else
    {
        transA = CblasNoTrans;
    }

    if(src3 != dst && beta != 0.0 && src3_step != 0) {
        if(flags & CV_HAL_GEMM_3_T)
            transpose(src3, ldsrc3, dst, lddst, c_m, c_n);
        else
            copy_matrix(src3, ldsrc3, dst, lddst, c_m, c_n);
    }
    else if (src3 == dst && (flags & CV_HAL_GEMM_3_T)) //actually transposing C in this case done by openCV
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    else if(src3_step == 0 && beta != 0.0)
        set_value(dst, lddst, (fptype)0.0, d_m, d_n);

    if(typeid(fptype) == typeid(float))
        cblas_sgemm(CblasRowMajor, transA, transB, a_m, d_n, a_n, (float)alpha, (float*)src1, ldsrc1, (float*)src2, ldsrc2, (float)beta, (float*)dst, lddst);
    else if(typeid(fptype) == typeid(double))
        cblas_dgemm(CblasRowMajor, transA, transB, a_m, d_n, a_n, (double)alpha, (double*)src1, ldsrc1, (double*)src2, ldsrc2, (double)beta, (double*)dst, lddst);

    return CV_HAL_ERROR_OK;
}

template <typename fptype> static inline int
lapack_gemm_c(const fptype *src1, size_t src1_step, const fptype *src2, size_t src2_step, fptype alpha,
            const fptype *src3, size_t src3_step, fptype beta, fptype *dst, size_t dst_step, int a_m, int a_n, int d_n, int flags)
{
    int ldsrc1 = (int)(src1_step / sizeof(std::complex<fptype>));
    int ldsrc2 = (int)(src2_step / sizeof(std::complex<fptype>));
    int ldsrc3 = (int)(src3_step / sizeof(std::complex<fptype>));
    int lddst = (int)(dst_step / sizeof(std::complex<fptype>));
    int c_m, c_n, d_m;
    CBLAS_TRANSPOSE transA, transB;
    std::complex<fptype> cAlpha(alpha, 0.0);
    std::complex<fptype> cBeta(beta, 0.0);

    if(flags & CV_HAL_GEMM_2_T)
    {
        transB = CblasTrans;
        if(flags & CV_HAL_GEMM_1_T )
        {
            d_m = a_n;
        }
        else
        {
            d_m = a_m;
        }
    }
    else
    {
        transB = CblasNoTrans;
        if(flags & CV_HAL_GEMM_1_T )
        {
            d_m = a_n;
        }
        else
        {
            d_m = a_m;
        }
    }

    if(flags & CV_HAL_GEMM_3_T)
    {
        c_m = d_n;
        c_n = d_m;
    }
    else
    {
        c_m = d_m;
        c_n = d_n;
    }

    if(flags & CV_HAL_GEMM_1_T )
    {
        transA = CblasTrans;
        std::swap(a_n, a_m);
    }
    else
    {
        transA = CblasNoTrans;
    }

    if(src3 != dst && beta != 0.0 && src3_step != 0) {
        if(flags & CV_HAL_GEMM_3_T)
            transpose((std::complex<fptype>*)src3, ldsrc3, (std::complex<fptype>*)dst, lddst, c_m, c_n);
        else
            copy_matrix((std::complex<fptype>*)src3, ldsrc3, (std::complex<fptype>*)dst, lddst, c_m, c_n);
    }
    else if (src3 == dst && (flags & CV_HAL_GEMM_3_T)) //actually transposing C in this case done by openCV
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    else if(src3_step == 0 && beta != 0.0)
        set_value((std::complex<fptype>*)dst, lddst, std::complex<fptype>(0.0, 0.0), d_m, d_n);

    // FIXME: this is a workaround. Support ILP64 in HAL API.
#if defined (ACCELERATE_NEW_LAPACK) && defined (ACCELERATE_LAPACK_ILP64)
    int M = a_m, N = d_n, K = a_n;
    if(typeid(fptype) == typeid(float)) {
        auto src1_cast = (std::complex<float>*)(src1);
        auto src2_cast = (std::complex<float>*)(src2);
        auto dst_cast = (std::complex<float>*)(dst);
        long lda = ldsrc1, ldb = ldsrc2, ldc = lddst;
        cblas_cgemm(CblasRowMajor, transA, transB, M, N, K, (std::complex<float>*)&cAlpha, src1_cast, lda, src2_cast, ldb, (std::complex<float>*)&cBeta, dst_cast, ldc);
    }
    else if(typeid(fptype) == typeid(double)) {
        auto src1_cast = (std::complex<double>*)(src1);
        auto src2_cast = (std::complex<double>*)(src2);
        auto dst_cast = (std::complex<double>*)(dst);
        long lda = ldsrc1, ldb = ldsrc2, ldc = lddst;
        cblas_zgemm(CblasRowMajor, transA, transB, M, N, K, (std::complex<double>*)&cAlpha, src1_cast, lda, src2_cast, ldb, (std::complex<double>*)&cBeta, dst_cast, ldc);
    }
#else
    if(typeid(fptype) == typeid(float))
        cblas_cgemm(CblasRowMajor, transA, transB, a_m, d_n, a_n, (float*)reinterpret_cast<fptype(&)[2]>(cAlpha), (float*)src1, ldsrc1, (float*)src2, ldsrc2, (float*)reinterpret_cast<fptype(&)[2]>(cBeta), (float*)dst, lddst);
    else if(typeid(fptype) == typeid(double))
        cblas_zgemm(CblasRowMajor, transA, transB, a_m, d_n, a_n, (double*)reinterpret_cast<fptype(&)[2]>(cAlpha), (double*)src1, ldsrc1, (double*)src2, ldsrc2, (double*)reinterpret_cast<fptype(&)[2]>(cBeta), (double*)dst, lddst);
#endif

    return CV_HAL_ERROR_OK;
}
int lapack_LU32f(float* a, size_t a_step, int m, float* b, size_t b_step, int n, int* info)
{
    if(m < HAL_LU_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_LU(a, a_step, m, b, b_step, n, info);
}

int lapack_LU64f(double* a, size_t a_step, int m, double* b, size_t b_step, int n, int* info)
{
    if(m < HAL_LU_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_LU(a, a_step, m, b, b_step, n, info);
}

int lapack_Cholesky32f(float* a, size_t a_step, int m, float* b, size_t b_step, int n, bool *info)
{
    if(m < HAL_CHOLESKY_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_Cholesky(a, a_step, m, b, b_step, n, info);
}

int lapack_Cholesky64f(double* a, size_t a_step, int m, double* b, size_t b_step, int n, bool *info)
{
    if(m < HAL_CHOLESKY_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_Cholesky(a, a_step, m, b, b_step, n, info);
}

int lapack_SVD32f(float* a, size_t a_step, float *w, float* u, size_t u_step, float* vt, size_t v_step, int m, int n, int flags)
{

    if(m < HAL_SVD_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    int info;
    return lapack_SVD(a, a_step, w, u, u_step, vt, v_step, m, n, flags, &info);
}

int lapack_SVD64f(double* a, size_t a_step, double *w, double* u, size_t u_step, double* vt, size_t v_step, int m, int n, int flags)
{

    if(m < HAL_SVD_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    int info;
    return lapack_SVD(a, a_step, w, u, u_step, vt, v_step, m, n, flags, &info);
}

int lapack_QR32f(float* src1, size_t src1_step, int m, int n, int k, float* src2, size_t src2_step, float* dst, int* info)
{
    if (m < HAL_QR_SMALL_MATRIX_THRESH)
      return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_QR(src1, src1_step, m, n, k, src2, src2_step, dst, info);
}

int lapack_QR64f(double* src1, size_t src1_step, int m, int n, int k, double* src2, size_t src2_step, double* dst, int* info)
{
    if (m < HAL_QR_SMALL_MATRIX_THRESH)
      return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_QR(src1, src1_step, m, n, k, src2, src2_step, dst, info);
}

int lapack_gemm32f(const float *src1, size_t src1_step, const float *src2, size_t src2_step, float alpha,
                   const float *src3, size_t src3_step, float beta, float *dst, size_t dst_step, int m, int n, int k, int flags)
{
    if(m < HAL_GEMM_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    // OpenCV's threshold above looks at m only, so a rank-1 update (k=1) with many rows passes it.
    return lapack_gemm(src1, src1_step, src2, src2_step, alpha, src3, src3_step, beta, dst, dst_step, m, n, k, flags);
}

int lapack_gemm64f(const double *src1, size_t src1_step, const double *src2, size_t src2_step, double alpha,
                   const double *src3, size_t src3_step, double beta, double *dst, size_t dst_step, int m, int n, int k, int flags)
{
    if(m < HAL_GEMM_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_gemm(src1, src1_step, src2, src2_step, alpha, src3, src3_step, beta, dst, dst_step, m, n, k, flags);
}

int lapack_gemm32fc(const float *src1, size_t src1_step, const float *src2, size_t src2_step, float alpha,
                   const float *src3, size_t src3_step, float beta, float *dst, size_t dst_step, int m, int n, int k, int flags)
{
    if(m < HAL_GEMM_SMALL_COMPLEX_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_gemm_c(src1, src1_step, src2, src2_step, alpha, src3, src3_step, beta, dst, dst_step, m, n, k, flags);
}
int lapack_gemm64fc(const double *src1, size_t src1_step, const double *src2, size_t src2_step, double alpha,
                   const double *src3, size_t src3_step, double beta, double *dst, size_t dst_step, int m, int n, int k, int flags)
{
    if(m < HAL_GEMM_SMALL_COMPLEX_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;
    return lapack_gemm_c(src1, src1_step, src2, src2_step, alpha, src3, src3_step, beta, dst, dst_step, m, n, k, flags);
}

// syevd destroys its input, but a symmetric matrix is the same bytes in row and column major,
// so a plain copy is enough - no transpose as in the LU and SVD paths. LAPACK returns ascending
// eigenvalues with the vectors as columns; OpenCV wants descending with the vectors as rows,
// and column-major columns are row-major rows, so only the order has to be reversed.
template <typename fptype> static inline int
lapack_eigen(const fptype* src, size_t src_step, int n, fptype* evals,
             fptype* evects, size_t evects_step, bool* info)
{
    // Accumulating the eigenvectors costs enough that syevd only wins from a larger size than
    // the values-only path: measured 0.72x at n=16 with vectors against 2.77x without.
    if(n < (evects ? HAL_EIGEN_VECTORS_MATRIX_THRESH : HAL_EIGEN_SMALL_MATRIX_THRESH))
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    char jobz[] = { evects ? 'V' : 'N', '\0' };
    char uplo[] = { 'U', '\0' };
    int _n = n, lda = n, lwork = -1, liwork = -1, _info = 0;

    cv::AutoBuffer<fptype> abuf((size_t)n * n);
    fptype* a = abuf.data();
    for(int i = 0; i < n; i++)
        memcpy(a + (size_t)i * n, (const uchar*)src + (size_t)i * src_step, n * sizeof(fptype));

    fptype work1 = 0;
    int iwork1 = 0;
    if(typeid(fptype) == typeid(float))
        OCV_LAPACK_FUNC(ssyevd)(jobz, uplo, &_n, (float*)a, &lda, (float*)evals,
                                (float*)&work1, &lwork, &iwork1, &liwork, &_info);
    else
        OCV_LAPACK_FUNC(dsyevd)(jobz, uplo, &_n, (double*)a, &lda, (double*)evals,
                                (double*)&work1, &lwork, &iwork1, &liwork, &_info);
    if(_info != 0)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    lwork  = (int)round((double)work1);
    liwork = iwork1;
    cv::AutoBuffer<fptype> wbuf(lwork + 1);
    cv::AutoBuffer<int> iwbuf(liwork + 1);

    if(typeid(fptype) == typeid(float))
        OCV_LAPACK_FUNC(ssyevd)(jobz, uplo, &_n, (float*)a, &lda, (float*)evals,
                                (float*)wbuf.data(), &lwork, iwbuf.data(), &liwork, &_info);
    else
        OCV_LAPACK_FUNC(dsyevd)(jobz, uplo, &_n, (double*)a, &lda, (double*)evals,
                                (double*)wbuf.data(), &lwork, iwbuf.data(), &liwork, &_info);
    if(_info != 0)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    for(int i = 0; i < n / 2; i++)
        std::swap(evals[i], evals[n - 1 - i]);

    if(evects)
        for(int i = 0; i < n; i++)
            memcpy((uchar*)evects + (size_t)i * evects_step,
                   a + (size_t)(n - 1 - i) * n, n * sizeof(fptype));

    *info = true;
    return CV_HAL_ERROR_OK;
}

// geev replaces the hand-transcribed JAMA orthes()+hqr2() in lda.cpp with one call. LAPACK is
// column major, so the input transposes in and the eigenvectors transpose out - both O(n^2)
// against an O(n^3) decomposition. Eigenvalues come back unsorted with the vectors in columns,
// which is what the JAMA path produced and what both callers sort afterwards.
template <typename fptype> static inline int
lapack_eigenNonSymmetric(const fptype* src, size_t src_step, int n, fptype* evals,
                         fptype* evects, size_t evects_step, bool* info)
{
    if(n < HAL_EIGEN_NONSYM_SMALL_MATRIX_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    char jobvl[] = { 'N', '\0' };
    char jobvr[] = { evects ? 'V' : 'N', '\0' };
    int _n = n, lda = n, ldvl = 1, ldvr = evects ? n : 1, lwork = -1, _info = 0;

    cv::AutoBuffer<fptype> abuf((size_t)n * n);
    fptype* a = abuf.data();
    transpose(src, src_step / sizeof(fptype), a, (size_t)n, (size_t)n, (size_t)n);

    // vl is never referenced with jobvl='N', but some LAPACK builds still validate the pointer
    cv::AutoBuffer<fptype> wibuf(n), vlbuf(1), vrbuf(evects ? (size_t)n * n : 1);
    fptype *wi = wibuf.data(), *vl = vlbuf.data(), *vr = vrbuf.data();

    fptype work1 = 0;
    if(typeid(fptype) == typeid(float))
        OCV_LAPACK_FUNC(sgeev)(jobvl, jobvr, &_n, (float*)a, &lda, (float*)evals, (float*)wi,
                               (float*)vl, &ldvl, (float*)vr, &ldvr, (float*)&work1, &lwork, &_info);
    else
        OCV_LAPACK_FUNC(dgeev)(jobvl, jobvr, &_n, (double*)a, &lda, (double*)evals, (double*)wi,
                               (double*)vl, &ldvl, (double*)vr, &ldvr, (double*)&work1, &lwork, &_info);
    if(_info != 0)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    lwork = (int)round((double)work1);
    cv::AutoBuffer<fptype> wbuf(lwork + 1);

    if(typeid(fptype) == typeid(float))
        OCV_LAPACK_FUNC(sgeev)(jobvl, jobvr, &_n, (float*)a, &lda, (float*)evals, (float*)wi,
                               (float*)vl, &ldvl, (float*)vr, &ldvr, (float*)wbuf.data(), &lwork, &_info);
    else
        OCV_LAPACK_FUNC(dgeev)(jobvl, jobvr, &_n, (double*)a, &lda, (double*)evals, (double*)wi,
                               (double*)vl, &ldvl, (double*)vr, &ldvr, (double*)wbuf.data(), &lwork, &_info);
    if(_info != 0)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    // wi is dropped: the JAMA path stored only the real parts too
    if(evects)
        transpose(vr, (size_t)n, evects, evects_step / sizeof(fptype), (size_t)n, (size_t)n);

    *info = true;
    return CV_HAL_ERROR_OK;
}

// ||a-b||^2 = |a|^2 + |b|^2 - 2 a.b, so the cross term over every pair is a single gemm.
//
// A probe of ARMPL's sgemm settled the shape of this before it was written. Serial sgemm runs at
// ~48 G MAC/s at every size here - that is about 92% of one Oryon core's peak, so it is not slow,
// it is one core against a baseline that uses all twelve. Tiling serial gemms across
// parallel_for_ measured 0.11x to 0.48x of one threaded call (and collapsed outright at
// 512x512x64, where 43-row tiles are too small to amortise packing). So: one threaded gemm here,
// called from outside batchDistance's parallel_for_, and the epilogue parallelised separately.
int lapack_batchDistL2Sqr32f(const float* src1, size_t src1_step, int m,
                             const float* src2, size_t src2_step, int n,
                             int len, float* dst, size_t dst_step, bool sqrt_dist)
{
    const double macs = (double)m * n * len;
    if(m <= 0 || n <= 0 || len < HAL_BATCHDIST_MIN_LEN || macs < HAL_BATCHDIST_MIN_MACS)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;   // nothing written yet, the caller can still run

    const int lda = (int)(src1_step / sizeof(float));
    const int ldb = (int)(src2_step / sizeof(float));
    const int ldc = (int)(dst_step  / sizeof(float));

    // normL2Sqr_ against a zero vector reuses OpenCV's own NEON kernel, so this part of the
    // arithmetic stays bit-identical to the path being replaced. Separate cblas_sdot calls per row
    // would cost more in per-call overhead than they save.
    cv::AutoBuffer<float> buf((size_t)len + m + n);
    float* zeros = buf.data();
    float* rn    = zeros + len;
    float* cn    = rn + m;
    memset(zeros, 0, len * sizeof(float));
    for(int i = 0; i < m; i++)
        rn[i] = cv::hal::normL2Sqr_(src1 + (size_t)i * lda, zeros, len);
    for(int j = 0; j < n; j++)
        cn[j] = cv::hal::normL2Sqr_(src2 + (size_t)j * ldb, zeros, len);

    // CblasTrans on B reads src2 as the n x len array it already is - no repacking. alpha = -2 is
    // exact. beta = 0 means dst is never read, so an uninitialised output cannot leak in.
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, m, n, len,
                -2.0f, src1, lda, src2, ldb, 0.0f, dst, ldc);

    // m x n of memory-bound work. Serial, this costs more than the gemm itself at the larger
    // shapes - 1M elements at 1000x1000 against a 0.28 ms gemm.
    cv::parallel_for_(cv::Range(0, m), [&](const cv::Range& r)
    {
        for(int i = r.start; i < r.end; i++)
        {
            float* d = dst + (size_t)i * ldc;
            const float ri = rn[i];
            for(int j = 0; j < n; j++)
            {
                // (rn + cn) first: both are about 2s, so the cancellation against the cross term
                // is exact by Sterbenz. Adding the cross term first cancels, then re-rounds the
                // small result against a large one.
                float v = (ri + cn[j]) + d[j];
                // '<= 0' rather than '< 0': -0.0f compares equal to zero and would survive a
                // '< 0' test, and -0.0f reinterpreted as int is INT_MIN, which sorts below every
                // real distance in the K-selection loop at batch_distance.cpp:238. NaN fails both
                // compares and is preserved, which std::max would not do.
                if(v <= 0.f) v = 0.f;
                d[j] = v;
            }
            // second pass over a row that is now in L1, so the branch stays out of the hot loop
            if(sqrt_dist)
                for(int j = 0; j < n; j++)
                    d[j] = std::sqrt(d[j]);
        }
    });

    return CV_HAL_ERROR_OK;
}

// Null space of many independent 4x4s at once, for cv::triangulatePoints.
//
// OpenCV runs hal::SVD64f per point (triangulate.cpp:128) and takes the last row of V. At 4x4 that
// declines the SVD hook and falls to OpenCV's Jacobi: several sweeps of six rotations, each with a
// square root - about 20-30 sqrts per point. Householder QR on a 4x4 is four reflections and four
// sqrts.
//
// RRQR gives A P = Q R. For a rank-3 4x4, R(3,3) ~ 0, so with R = [[R11, r12], [0, ~0]] the null
// space is y = [y1; 1] where R11 y1 = -r12, and x = P y normalised.
//
// The interleaved layout and the 0-based pivots below were both confirmed empirically before this
// was written (armpl_layout_probe.py, armpl_rrqr_probe.py): element (i,j) of matrix l in batch b
// lives at A_p[b*bstrd + i*istrd + j*jstrd + l], armpl_dge_interleave adds the l itself, and the
// destination strides must already carry the ninter factor. Residuals came back at 6e-16 even for
// matrices squeezed to a 1e-9 rank gap, which is the near-degenerate case that worried us.
int lapack_nullspace4x4Batch64f(const double* src, int count, double* dst)
{
#ifndef HAVE_ARMPL
    (void)src; (void)count; (void)dst;
    return CV_HAL_ERROR_NOT_IMPLEMENTED;
#else
    if(count < HAL_NULLSPACE4X4_MIN_COUNT)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    const armpl_int_t NI = HAL_NULLSPACE4X4_NINTER;
    const armpl_int_t nbatch = (count + NI - 1) / NI;
    const armpl_int_t padded = nbatch * NI;

    const armpl_int_t jstrd_A = NI, istrd_A = NI * 4, bstrd_A = NI * 16;
    const armpl_int_t istrd_v = NI, bstrd_v = NI * 4;                 // jpvt and tau, 4 per matrix
    const armpl_int_t istrd_B = NI, jstrd_B = NI, bstrd_B = NI * 3;   // the 3x1 rhs

    cv::AutoBuffer<double>     abuf((size_t)bstrd_A * nbatch);
    cv::AutoBuffer<double>     tbuf((size_t)bstrd_v * nbatch);
    cv::AutoBuffer<double>     bbuf((size_t)bstrd_B * nbatch);
    cv::AutoBuffer<armpl_int_t> jbuf((size_t)bstrd_v * nbatch);
    cv::AutoBuffer<armpl_int_t> rbuf((size_t)padded);
    double* A_p = abuf.data();
    double* B_p = bbuf.data();
    const armpl_int_t* jp = jbuf.data();

    for(armpl_int_t idx = 0; idx < padded; idx++)
    {
        const armpl_int_t b = idx / NI, l = idx % NI;
        // pad by repeating the last real matrix - same conditioning as real data, result discarded
        const double* M = src + (size_t)(idx < count ? idx : count - 1) * 16;
        if(armpl_dge_interleave(NI, l, 4, 4, M, 4, 1,
                                A_p + (size_t)b * bstrd_A, istrd_A, jstrd_A) != ARMPL_STATUS_SUCCESS)
            return CV_HAL_ERROR_NOT_IMPLEMENTED;
    }

    if(armpl_dgeqrfrr_interleave_batch(NI, nbatch, 4, 4, A_p, bstrd_A, istrd_A, jstrd_A,
                                       jbuf.data(), bstrd_v, istrd_v,
                                       tbuf.data(), bstrd_v, istrd_v,
                                       rbuf.data()) != ARMPL_STATUS_SUCCESS)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    for(armpl_int_t idx = 0; idx < padded; idx++)
    {
        const armpl_int_t b = idx / NI, l = idx % NI;
        for(int i = 0; i < 3; i++)
            B_p[(size_t)b * bstrd_B + i * istrd_B + l] =
                -A_p[(size_t)b * bstrd_A + i * istrd_A + 3 * jstrd_A + l];
    }

    if(armpl_dtrsm_interleave_batch(NI, nbatch, 'L', 'U', 'N', 'N', 3, 1, 1.0,
                                    A_p, bstrd_A, istrd_A, jstrd_A,
                                    B_p, bstrd_B, istrd_B, jstrd_B) != ARMPL_STATUS_SUCCESS)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    // nothing above this point has touched dst, so every decline path is still safe
    for(int idx = 0; idx < count; idx++)
    {
        const armpl_int_t b = idx / NI, l = idx % NI;
        double y[4] = { 0, 0, 0, 1.0 };
        for(int i = 0; i < 3; i++)
            y[i] = B_p[(size_t)b * bstrd_B + i * istrd_B + l];

        double x[4] = { 0, 0, 0, 0 };
        for(int j = 0; j < 4; j++)
        {
            const armpl_int_t k = jp[(size_t)b * bstrd_v + j * istrd_v + l];   // 0-based, verified
            if(k >= 0 && k < 4)
                x[k] = y[j];
        }
        const double s = std::sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2] + x[3]*x[3]);
        double* d = dst + (size_t)idx * 4;
        if(s > 0)
        {
            const double inv = 1.0 / s;
            for(int k = 0; k < 4; k++) d[k] = x[k] * inv;
        }
        else
            for(int k = 0; k < 4; k++) d[k] = x[k];
    }

    return CV_HAL_ERROR_OK;
#endif
}

// syrk computes half the flops of the equivalent gemm because the result is symmetric; it fills
// one triangle, which is then mirrored. Row major needs no transpose either way.
template <typename fptype> static inline int
lapack_mulTransposed(const fptype* src, size_t src_step, fptype* dst, size_t dst_step,
                     int rows, int cols, bool ata, double scale)
{
    const int n = ata ? cols : rows;   // output is n x n
    const int k = ata ? rows : cols;   // contracted dimension

    // Gate on work, not on the output dimension. cv::decolor does mulTransposed on a 9 x 320000
    // matrix: a 9x9 output but 26 Mflops of work, which an n < 16 test wrongly declined.
    const double work = (double)n * n * k;
    if(work < HAL_MULTRANSPOSED_MIN_FLOPS)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    const int lda = (int)(src_step / sizeof(fptype));
    const int ldc = (int)(dst_step / sizeof(fptype));

    if(typeid(fptype) == typeid(float))
        cblas_ssyrk(CblasRowMajor, CblasUpper, ata ? CblasTrans : CblasNoTrans, n, k,
                    (float)scale, (const float*)src, lda, 0.f, (float*)dst, ldc);
    else
        cblas_dsyrk(CblasRowMajor, CblasUpper, ata ? CblasTrans : CblasNoTrans, n, k,
                    scale, (const double*)src, lda, 0.0, (double*)dst, ldc);

    for(int i = 0; i < n; i++)
        for(int j = 0; j < i; j++)
            dst[(size_t)i * ldc + j] = dst[(size_t)j * ldc + i];

    return CV_HAL_ERROR_OK;
}

// axpy works in place on y, so src2 has to be copied into dst first: two passes where OpenCV
// fuses into one. Measured to confirm rather than assumed.
template <typename fptype> static inline int
lapack_scaleAdd(const fptype* src1, const fptype* src2, fptype* dst, int len, fptype alpha)
{
    if(len < HAL_SCALEADD_SMALL_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    if(dst != src2)  // scaleAdd allows dst to alias src2
        memcpy(dst, src2, (size_t)len * sizeof(fptype));
    if(typeid(fptype) == typeid(float))
        cblas_saxpy(len, (float)alpha, (const float*)src1, 1, (float*)dst, 1);
    else
        cblas_daxpy(len, (double)alpha, (const double*)src1, 1, (double*)dst, 1);
    return CV_HAL_ERROR_OK;
}

int lapack_scaleAdd32f(const float* src1, const float* src2, float* dst, int len, float alpha)
{
    return lapack_scaleAdd(src1, src2, dst, len, alpha);
}

int lapack_scaleAdd64f(const double* src1, const double* src2, double* dst, int len, double alpha)
{
    return lapack_scaleAdd(src1, src2, dst, len, alpha);
}

// Interleaved pixels are already a column-major scn x len matrix - each element is one column -
// so no repacking is needed. m is row-major dcn x (scn+1), which is the transpose in column-major
// terms, hence CblasTrans with lda = scn+1. The final column of m is the offset: when it is
// non-zero dst is prefilled with it and beta is 1, costing an extra pass.
template <typename fptype> static inline int
lapack_transform(const fptype* src, fptype* dst, const fptype* m, int len, int scn, int dcn)
{
    if(len < HAL_TRANSFORM_SMALL_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    // M=3, K=3 is a poor gemm shape - packing overhead dominates and single-threaded ARMPL loses
    // to OpenCV's loop (0.69x). It only wins by splitting the long N dimension across threads, so
    // this hook is worth having only against the threaded ARMPL (WITH_OPENMP=ON, armpl_lp64_mp).
    // Built against the serial library it will regress; there is no runtime check for that here
    // because detecting it meant reaching into ARMPL's own OpenMP runtime, which was removed.

    // An offset forces a prefill of dst before gemm can accumulate onto it - two passes against
    // OpenCV's one, measured at 0.35x. There is no single-pass BLAS route (ger instead of the
    // prefill is still a second pass), so decline and let OpenCV keep the affine case.
    const int mstride = scn + 1;
    for(int i = 0; i < dcn; i++)
        if(m[i * mstride + scn] != (fptype)0)
            return CV_HAL_ERROR_NOT_IMPLEMENTED;

    if(typeid(fptype) == typeid(float))
        cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, dcn, len, scn,
                    1.f, (const float*)m, mstride, (const float*)src, scn,
                    0.f, (float*)dst, dcn);
    else
        cblas_dgemm(CblasColMajor, CblasTrans, CblasNoTrans, dcn, len, scn,
                    1.0, (const double*)m, mstride, (const double*)src, scn,
                    0.0, (double*)dst, dcn);
    return CV_HAL_ERROR_OK;
}

int lapack_transform32f(const float* src, float* dst, const float* m, int len, int scn, int dcn)
{
    return lapack_transform(src, dst, m, len, scn, dcn);
}

int lapack_transform64f(const double* src, double* dst, const double* m, int len, int scn, int dcn)
{
    return lapack_transform(src, dst, m, len, scn, dcn);
}

// OpenCV's kernel here is a plain scalar loop with 4x unrolling and no SIMD, over an O(len^2)
// quadratic form - gemv should beat it comfortably.
int lapack_Mahalanobis64f(const double* v1, const double* v2, const double* icovar,
                          size_t icovar_step, int len, double* result)
{
    if(len < HAL_MAHALANOBIS_SMALL_THRESH)
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    // No thread gate here on purpose. Threading costs 0.75-0.86x below len 256, but the gate
    // itself costs about 1.5us - more than the saving on a 2-4us call - so gating measured worse
    // than simply letting ARMPL thread. Per-call thread control needs work above roughly 10us.

    cv::AutoBuffer<double> buf((size_t)len * 2);
    double* diff = buf.data();
    double* tmp  = diff + len;

    for(int i = 0; i < len; i++)
        diff[i] = v1[i] - v2[i];

    const int lda = (int)(icovar_step / sizeof(double));
    cblas_dgemv(CblasRowMajor, CblasNoTrans, len, len, 1.0, icovar, lda, diff, 1, 0.0, tmp, 1);
    *result = std::sqrt(cblas_ddot(len, diff, 1, tmp, 1));
    return CV_HAL_ERROR_OK;
}

int lapack_mulTransposed32f(const float* src, size_t src_step, float* dst, size_t dst_step,
                            int rows, int cols, bool ata, double scale)
{
    return lapack_mulTransposed(src, src_step, dst, dst_step, rows, cols, ata, scale);
}

int lapack_mulTransposed64f(const double* src, size_t src_step, double* dst, size_t dst_step,
                            int rows, int cols, bool ata, double scale)
{
    return lapack_mulTransposed(src, src_step, dst, dst_step, rows, cols, ata, scale);
}

int lapack_eigen32f(const float* src, size_t src_step, int n, float* evals,
                    float* evects, size_t evects_step, bool* info)
{
    return lapack_eigen(src, src_step, n, evals, evects, evects_step, info);
}

int lapack_eigen64f(const double* src, size_t src_step, int n, double* evals,
                    double* evects, size_t evects_step, bool* info)
{
    return lapack_eigen(src, src_step, n, evals, evects, evects_step, info);
}

int lapack_eigenNonSymmetric32f(const float* src, size_t src_step, int n, float* evals,
                                float* evects, size_t evects_step, bool* info)
{
    return lapack_eigenNonSymmetric(src, src_step, n, evals, evects, evects_step, info);
}

int lapack_eigenNonSymmetric64f(const double* src, size_t src_step, int n, double* evals,
                                double* evects, size_t evects_step, bool* info)
{
    return lapack_eigenNonSymmetric(src, src_step, n, evals, evects, evects_step, info);
}

#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif //HAVE_LAPACK
