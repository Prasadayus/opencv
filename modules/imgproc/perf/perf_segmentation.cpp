// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

#include "perf_precomp.hpp"

namespace opencv_test {

typedef perf::TestBaseWithParam<Size> Size_PyrMeanShift;

// Deterministic synthetic image: no dependency on opencv_extra.
PERF_TEST_P(Size_PyrMeanShift, pyrMeanShiftFiltering,
            testing::Values(::perf::szQVGA, ::perf::szVGA))
{
    Size sz = GetParam();
    Mat src(sz, CV_8UC3);
    RNG rng(20261004);
    for (int y = 0; y < sz.height; y++)
        for (int x = 0; x < sz.width; x++)
        {
            int b = 40 + 60*(((x/32) + (y/32)) & 3);
            src.at<Vec3b>(y,x) = Vec3b(saturate_cast<uchar>(b       + rng.uniform(-12,12)),
                                       saturate_cast<uchar>(200 - b + rng.uniform(-12,12)),
                                       saturate_cast<uchar>(128     + rng.uniform(-12,12)));
        }
    Mat dst(sz, CV_8UC3);

    declare.in(src).out(dst).time(120);

    TEST_CYCLE() pyrMeanShiftFiltering(src, dst, 10, 20, 1);

    SANITY_CHECK_NOTHING();
}

} // namespace
