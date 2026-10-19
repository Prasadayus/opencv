// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "precomp.hpp"
#include "opencv2/core/hal/intrin.hpp"

namespace cv {

// Matches cvtColor's RGBA<->mRGBA fixed-point rounding, so results stay bit-consistent.
static inline int mulDiv255(int a, int b)
{
    return (a * b + 128) / 255;
}

#if (CV_SIMD || CV_SIMD_SCALABLE)
// (x + (x>>8) + 1) >> 8 is exactly x/255 below 65535, and x peaks at 255*255+128 here, so the
// wrapping forms are both exact and cheaper than the saturating v_mul/v_add on 16-bit lanes.
static inline v_uint16 vMulDiv255(const v_uint16& a, const v_uint16& b)
{
    v_uint16 x = v_add_wrap(v_mul_wrap(a, b), vx_setall_u16(128));
    return v_shr<8>(v_add_wrap(v_add_wrap(x, v_shr<8>(x)), vx_setall_u16(1)));
}

template<int K0, int K1>
static inline v_uint16 compositeF(const v_uint16& a)
{
    if constexpr (K0 == 0 && K1 == 0)      { CV_UNUSED(a); return vx_setzero_u16(); }
    else if constexpr (K0 == 1 && K1 == 0) { CV_UNUSED(a); return vx_setall_u16(255); }
    else if constexpr (K0 == 0 && K1 == 1) return a;
    else                                   return v_sub_wrap(vx_setall_u16(255), a);
}
#endif

// One Porter-Duff weight, F = K0*255 + K1*a, where a is the *other* operand's alpha.
template<int K0, int K1>
struct CompositeTerm
{
    static inline int apply(int c, int f) { return mulDiv255(c, f); }
#if (CV_SIMD || CV_SIMD_SCALABLE)
    static inline v_uint16 apply(const v_uint16& c, const v_uint16& f) { return vMulDiv255(c, f); }
#endif
};

// F is identically 255, so the multiplication drops out. Keeps OVER as cheap as a bare copy.
template<>
struct CompositeTerm<1, 0>
{
    static inline int apply(int c, int) { return c; }
#if (CV_SIMD || CV_SIMD_SCALABLE)
    static inline v_uint16 apply(const v_uint16& c, const v_uint16&) { return c; }
#endif
};

typedef void (*CompositeRowFunc)(const uchar* overlay, const uchar* background, uchar* dst, int width);

template<int CN, int A0, int A1, int B0, int B1>
static void compositeRow(const uchar* ov, const uchar* bg, uchar* d, int width)
{
    typedef CompositeTerm<A0, A1> OverlayTerm;
    typedef CompositeTerm<B0, B1> BackgroundTerm;

    int x = 0;
#if (CV_SIMD || CV_SIMD_SCALABLE)
    const int vlanes = VTraits<v_uint8>::vlanes();
    for (; x <= width - vlanes; x += vlanes, ov += 4 * vlanes, bg += CN * vlanes, d += CN * vlanes)
    {
        v_uint8 o0, o1, o2, oa, g0, g1, g2, ga;
        v_load_deinterleave(ov, o0, o1, o2, oa);
        if constexpr (CN == 4)
            v_load_deinterleave(bg, g0, g1, g2, ga);
        else
        {
            v_load_deinterleave(bg, g0, g1, g2);
            ga = vx_setall_u8(255);
        }

        v_uint16 asLo, asHi, adLo, adHi;
        v_expand(oa, asLo, asHi);
        v_expand(ga, adLo, adHi);
        const v_uint16 faLo = compositeF<A0, A1>(adLo), faHi = compositeF<A0, A1>(adHi);
        const v_uint16 fbLo = compositeF<B0, B1>(asLo), fbHi = compositeF<B0, B1>(asHi);

        // each term is at most 255, so the sum stays well inside int16 before the saturating pack
        auto blend = [&](const v_uint8& oc, const v_uint8& gc)
        {
            v_uint16 oLo, oHi, gLo, gHi;
            v_expand(oc, oLo, oHi);
            v_expand(gc, gLo, gHi);
            v_uint16 lo = v_add_wrap(OverlayTerm::apply(oLo, faLo), BackgroundTerm::apply(gLo, fbLo));
            v_uint16 hi = v_add_wrap(OverlayTerm::apply(oHi, faHi), BackgroundTerm::apply(gHi, fbHi));
            return v_pack_u(v_reinterpret_as_s16(lo), v_reinterpret_as_s16(hi));
        };

        v_uint8 r0 = blend(o0, g0), r1 = blend(o1, g1), r2 = blend(o2, g2);
        if constexpr (CN == 4)
            v_store_interleave(d, r0, r1, r2, blend(oa, ga));
        else
            v_store_interleave(d, r0, r1, r2);
    }
#endif
    for (; x < width; ++x, ov += 4, bg += CN, d += CN)
    {
        int as = ov[3];
        int ad = (CN == 4) ? bg[3] : 255;
        int fa = A0 * 255 + A1 * ad;
        int fb = B0 * 255 + B1 * as;
        d[0] = saturate_cast<uchar>(OverlayTerm::apply(ov[0], fa) + BackgroundTerm::apply(bg[0], fb));
        d[1] = saturate_cast<uchar>(OverlayTerm::apply(ov[1], fa) + BackgroundTerm::apply(bg[1], fb));
        d[2] = saturate_cast<uchar>(OverlayTerm::apply(ov[2], fa) + BackgroundTerm::apply(bg[2], fb));
        if (CN == 4)
            d[3] = saturate_cast<uchar>(OverlayTerm::apply(as, fa) + BackgroundTerm::apply(ad, fb));
    }
}

template<int A0, int A1, int B0, int B1>
static CompositeRowFunc rowFuncFor(int bgChannels)
{
    return bgChannels == 4 ? &compositeRow<4, A0, A1, B0, B1> : &compositeRow<3, A0, A1, B0, B1>;
}

// F_a is a function of the background's alpha and F_b of the overlay's, as in the W3C table.
static CompositeRowFunc compositeRowFunc(int op, int bgChannels)
{
    switch (op)
    {                                   //      F_a       F_b
    case ALPHA_COMPOSITE_CLEAR:     return rowFuncFor< 0, 0,  0, 0>(bgChannels);
    case ALPHA_COMPOSITE_SOURCE:    return rowFuncFor< 1, 0,  0, 0>(bgChannels);
    case ALPHA_COMPOSITE_DEST:      return rowFuncFor< 0, 0,  1, 0>(bgChannels);
    case ALPHA_COMPOSITE_OVER:      return rowFuncFor< 1, 0,  1,-1>(bgChannels);
    case ALPHA_COMPOSITE_DEST_OVER: return rowFuncFor< 1,-1,  1, 0>(bgChannels);
    case ALPHA_COMPOSITE_IN:        return rowFuncFor< 0, 1,  0, 0>(bgChannels);
    case ALPHA_COMPOSITE_DEST_IN:   return rowFuncFor< 0, 0,  0, 1>(bgChannels);
    case ALPHA_COMPOSITE_OUT:       return rowFuncFor< 1,-1,  0, 0>(bgChannels);
    case ALPHA_COMPOSITE_DEST_OUT:  return rowFuncFor< 0, 0,  1,-1>(bgChannels);
    case ALPHA_COMPOSITE_ATOP:      return rowFuncFor< 0, 1,  1,-1>(bgChannels);
    case ALPHA_COMPOSITE_DEST_ATOP: return rowFuncFor< 1,-1,  0, 1>(bgChannels);
    case ALPHA_COMPOSITE_XOR:       return rowFuncFor< 1,-1,  1,-1>(bgChannels);
    case ALPHA_COMPOSITE_PLUS:      return rowFuncFor< 1, 0,  1, 0>(bgChannels);
    default:
        CV_Error_(Error::StsBadArg, ("unknown alpha compositing operator: %d", op));
    }
}

class AlphaCompositeInvoker : public ParallelLoopBody
{
public:
    AlphaCompositeInvoker(const Mat& overlay, const Mat& background, Mat& dst, CompositeRowFunc rowFunc)
        : overlay_(overlay), background_(background), dst_(dst), rowFunc_(rowFunc)
    {
    }

    void operator()(const Range& range) const CV_OVERRIDE
    {
        for (int y = range.start; y < range.end; ++y)
            rowFunc_(overlay_.ptr<uchar>(y), background_.ptr<uchar>(y), dst_.ptr<uchar>(y), overlay_.cols);
    }

private:
    const Mat& overlay_;
    const Mat& background_;
    Mat& dst_;
    CompositeRowFunc rowFunc_;
};

} // namespace cv

void cv::alphaComposite(InputArray _overlay, InputArray _background, OutputArray _dst,
                        int op, bool premultiplied)
{
    CV_INSTRUMENT_REGION();

    int bgType = _background.type();
    int bgChannels = CV_MAT_CN(bgType);

    CV_Assert(_overlay.type() == CV_8UC4);
    CV_Assert(CV_MAT_DEPTH(bgType) == CV_8U && (bgChannels == 3 || bgChannels == 4));
    CV_Assert(_overlay.size() == _background.size());

    CompositeRowFunc rowFunc = compositeRowFunc(op, bgChannels);

    Mat overlay = _overlay.getMat();
    Mat background = _background.getMat();

    Mat overlayPremul;
    if (premultiplied)
        overlayPremul = overlay;
    else
        cvtColor(overlay, overlayPremul, COLOR_RGBA2mRGBA);

    Mat backgroundPremul;
    if (bgChannels == 3 || premultiplied)
        backgroundPremul = background;
    else
        cvtColor(background, backgroundPremul, COLOR_RGBA2mRGBA);

    _dst.create(background.size(), bgType);
    Mat dst = _dst.getMat();

    // 4-channel straight-alpha output needs a temp buffer before un-premultiplying into dst.
    bool needsUnpremultiply = (bgChannels == 4 && !premultiplied);
    Mat composited = needsUnpremultiply ? Mat(background.size(), bgType) : dst;

    AlphaCompositeInvoker invoker(overlayPremul, backgroundPremul, composited, rowFunc);
    parallel_for_(Range(0, background.rows), invoker, background.total() / (double)(1 << 16));

    if (needsUnpremultiply)
        cvtColor(composited, dst, COLOR_mRGBA2RGBA);
}
