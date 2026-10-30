// Perf coverage for the fitEllipse family, which had none. fitEllipseAMS (shapedescr.cpp:544) and
// fitEllipseDirect (:745) each run mulTransposed on an n x 6 design matrix. The 6x6 output would
// have been refused by the old "output dim < 16" rule; the work-based gate admits it once
// 6*6*n >= 4096, i.e. from about 114 points. Sizes bracket that crossover deliberately - plain
// cv::fitEllipse is not here because it takes the n x 5 SVD path and never reaches the hook.

#include "perf_precomp.hpp"

namespace opencv_test
{
using namespace perf;

typedef perf::TestBaseWithParam<int> FitEllipseTest;

static std::vector<Point2f> ellipsePoints(int n)
{
    std::vector<Point2f> pts;
    pts.reserve(n);
    RNG& rng = theRNG();
    const double c = std::cos(0.4), s = std::sin(0.4);
    for (int i = 0; i < n; i++)
    {
        // a rotated ellipse with light noise, so the design matrix is well conditioned and the
        // fit takes the normal path rather than one of the degenerate fallbacks
        const double t = 2 * CV_PI * i / n;
        const double x = 120.0 * std::cos(t), y = 70.0 * std::sin(t);
        pts.push_back(Point2f((float)(320 + c * x - s * y + rng.uniform(-0.5, 0.5)),
                              (float)(240 + s * x + c * y + rng.uniform(-0.5, 0.5))));
    }
    return pts;
}

PERF_TEST_P(FitEllipseTest, fitEllipseAMS, ::testing::Values(64, 128, 512, 4096, 32768))
{
    const std::vector<Point2f> pts = ellipsePoints(GetParam());
    RotatedRect box;

    declare.time(30);

    TEST_CYCLE() box = cv::fitEllipseAMS(pts);

    EXPECT_GT(box.size.width, 0.f);
    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(FitEllipseTest, fitEllipseDirect, ::testing::Values(64, 128, 512, 4096, 32768))
{
    const std::vector<Point2f> pts = ellipsePoints(GetParam());
    RotatedRect box;

    declare.time(30);

    TEST_CYCLE() box = cv::fitEllipseDirect(pts);

    EXPECT_GT(box.size.width, 0.f);
    SANITY_CHECK_NOTHING();
}

} // namespace
