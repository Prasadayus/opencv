// Perf coverage for cv::triangulatePoints, which had none. Its inner loop is one 4x4 SVD per
// point - far below the size at which cv_hal_SVD64f engages, so it always lands on OpenCV's
// Jacobi. Point counts are swept because that is the only dimension: the matrices are always 4x4.

#include "perf_precomp.hpp"

namespace opencv_test
{
using namespace perf;

typedef perf::TestBaseWithParam<std::tuple<int, MatDepth>> TriangulateTest;

PERF_TEST_P(TriangulateTest, triangulatePoints, ::testing::Combine(
    ::testing::Values(64, 512, 4096, 32768, 262144),
    ::testing::Values(CV_32F, CV_64F)
    ))
{
    auto p = GetParam();
    const int n     = std::get<0>(p);
    const int mtype = std::get<1>(p);

    // two calibrated views of a random cloud in front of both cameras, so the systems are
    // rank 3 and reasonably conditioned - degenerate input would exercise a different path
    Matx34d P1(1000, 0, 320, 0,
               0, 1000, 240, 0,
               0, 0, 1, 0);
    Matx34d P2(1000, 0, 320, -100,
               0, 1000, 240, 0,
               0, 0, 1, 0);

    Mat pts1(1, n, CV_MAKETYPE(mtype, 2)), pts2(1, n, CV_MAKETYPE(mtype, 2));
    RNG& rng = theRNG();
    for (int i = 0; i < n; i++)
    {
        Point3d X(rng.uniform(-2.0, 2.0), rng.uniform(-2.0, 2.0), rng.uniform(4.0, 12.0));
        Matx41d Xh(X.x, X.y, X.z, 1.0);
        Matx31d a = P1 * Xh, b = P2 * Xh;
        Point2d ia(a(0) / a(2), a(1) / a(2)), ib(b(0) / b(2), b(1) / b(2));
        if (mtype == CV_32F)
        {
            pts1.at<Point2f>(0, i) = Point2f((float)ia.x, (float)ia.y);
            pts2.at<Point2f>(0, i) = Point2f((float)ib.x, (float)ib.y);
        }
        else
        {
            pts1.at<Point2d>(0, i) = ia;
            pts2.at<Point2d>(0, i) = ib;
        }
    }

    Mat out;
    declare.in(pts1, pts2).time(60);

    TEST_CYCLE() cv::triangulatePoints(Mat(P1), Mat(P2), pts1, pts2, out);

    SANITY_CHECK_NOTHING();
}

} // namespace
