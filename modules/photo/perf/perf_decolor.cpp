// Perf coverage for cv::decolor, which had none. contrast_preserve.hpp:320 runs
// mulTransposed on a 9 x N matrix where N is the number of sampled gradient pairs - a 9x9
// output but O(N) work, so it is the one imgproc/photo caller with enough volume to clear the
// hook's gate by a wide margin (about 26 Mflops at VGA).

#include "perf_precomp.hpp"

namespace opencv_test
{
using namespace perf;

typedef perf::TestBaseWithParam<Size> DecolorTest;

PERF_TEST_P(DecolorTest, decolor, ::testing::Values(Size(320, 240), Size(640, 480), Size(1024, 768)))
{
    const Size sz = GetParam();

    Mat src(sz, CV_8UC3);
    declare.in(src, WARMUP_RNG);

    Mat grayscale, color_boost;

    declare.time(120);

    TEST_CYCLE() cv::decolor(src, grayscale, color_boost);

    SANITY_CHECK_NOTHING();
}

} // namespace
