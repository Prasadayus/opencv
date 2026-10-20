// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "perf_precomp.hpp"

namespace opencv_test { namespace {

typedef perf::TestBaseWithParam<Size> Size_NPR;

// All four of these run the same domain-transform core, so one size sweep covers the shared cost.
static Mat makeInput(Size sz)
{
    Mat tmp(Size(sz.width / 4, sz.height / 4), CV_8UC3);
    randu(tmp, 0, 256);
    Mat src;
    resize(tmp, src, sz, 0, 0, INTER_LINEAR);   // smooth content, so the edge term does real work
    return src;
}

PERF_TEST_P(Size_NPR, edgePreservingFilter, testing::Values(::perf::szVGA, ::perf::sz720p, ::perf::szQVGA, Size(160, 120)))
{
    Mat src = makeInput(GetParam()), dst;
    declare.in(src).time(60);
    TEST_CYCLE() edgePreservingFilter(src, dst);
    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(Size_NPR, detailEnhance, testing::Values(::perf::szVGA, ::perf::sz720p, ::perf::szQVGA, Size(160, 120)))
{
    Mat src = makeInput(GetParam()), dst;
    declare.in(src).time(60);
    TEST_CYCLE() detailEnhance(src, dst);
    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(Size_NPR, pencilSketch, testing::Values(::perf::szVGA, ::perf::sz720p, ::perf::szQVGA, Size(160, 120)))
{
    Mat src = makeInput(GetParam()), dst1, dst2;
    declare.in(src).time(60);
    TEST_CYCLE() pencilSketch(src, dst1, dst2);
    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(Size_NPR, stylization, testing::Values(::perf::szVGA, ::perf::sz720p, ::perf::szQVGA, Size(160, 120)))
{
    Mat src = makeInput(GetParam()), dst;
    declare.in(src).time(60);
    TEST_CYCLE() stylization(src, dst);
    SANITY_CHECK_NOTHING();
}

}} // namespace
