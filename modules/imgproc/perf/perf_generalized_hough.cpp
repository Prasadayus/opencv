// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "perf_precomp.hpp"

namespace opencv_test { namespace {

// A template shape and a scene containing a few copies of it. Synthetic, so the fixture carries
// no test-data dependency and the edge count scales predictably with size.
static void makeScene(Size sz, Mat& templ, Mat& image)
{
    templ = Mat::zeros(Size(64, 64), CV_8UC1);
    ellipse(templ, Point(32, 32), Size(26, 16), 20, 0, 360, Scalar::all(255), 3);
    line(templ, Point(8, 56), Point(56, 8), Scalar::all(255), 3);

    image = Mat::zeros(sz, CV_8UC1);
    for (int i = 1; i <= 3; i++)
        for (int j = 1; j <= 3; j++)
        {
            Point c(sz.width * j / 4, sz.height * i / 4);
            ellipse(image, c, Size(26, 16), 20, 0, 360, Scalar::all(255), 3);
            line(image, c + Point(-24, 24), c + Point(24, -24), Scalar::all(255), 3);
        }
}

typedef perf::TestBaseWithParam<Size> Size_GH;

PERF_TEST_P(Size_GH, GeneralizedHoughBallard, testing::Values(::perf::szVGA, ::perf::sz720p, ::perf::szQVGA, Size(240, 180)))
{
    Mat templ, image;
    makeScene(GetParam(), templ, image);

    Ptr<GeneralizedHoughBallard> gh = createGeneralizedHoughBallard();
    gh->setTemplate(templ);
    gh->setVotesThreshold(200);

    std::vector<Vec4f> pos;
    declare.in(image).time(60);
    TEST_CYCLE() gh->detect(image, pos);
    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(Size_GH, GeneralizedHoughGuil, testing::Values(::perf::szVGA, ::perf::sz720p, ::perf::szQVGA, Size(240, 180)))
{
    Mat templ, image;
    makeScene(GetParam(), templ, image);

    Ptr<GeneralizedHoughGuil> gh = createGeneralizedHoughGuil();
    gh->setTemplate(templ);
    gh->setMinAngle(0);
    gh->setMaxAngle(90);
    gh->setAngleStep(10);
    gh->setMinScale(0.8);
    gh->setMaxScale(1.2);
    gh->setScaleStep(0.1);

    std::vector<Vec4f> pos;
    declare.in(image).time(120);
    TEST_CYCLE() gh->detect(image, pos);
    SANITY_CHECK_NOTHING();
}

// A deliberately light workload: one small shape, so the feature lists are short and the voting
// loops have little to divide. This is where thread dispatch would show up if it were going to.
PERF_TEST(GH_Sparse, GeneralizedHoughGuil_fewFeatures)
{
    Mat templ = Mat::zeros(Size(48, 48), CV_8UC1);
    ellipse(templ, Point(24, 24), Size(18, 11), 20, 0, 360, Scalar::all(255), 2);

    Mat image = Mat::zeros(Size(160, 120), CV_8UC1);
    ellipse(image, Point(80, 60), Size(18, 11), 20, 0, 360, Scalar::all(255), 2);

    Ptr<GeneralizedHoughGuil> gh = createGeneralizedHoughGuil();
    gh->setTemplate(templ);
    gh->setMinAngle(0);
    gh->setMaxAngle(90);
    gh->setAngleStep(10);
    gh->setMinScale(0.8);
    gh->setMaxScale(1.2);
    gh->setScaleStep(0.1);

    std::vector<Vec4f> pos;
    declare.in(image).time(60);
    TEST_CYCLE() gh->detect(image, pos);
    SANITY_CHECK_NOTHING();
}

}} // namespace
