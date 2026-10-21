// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "test_precomp.hpp"

namespace opencv_test { namespace {

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

// The voting stages accumulate integer counts, so splitting them across threads must reproduce
// the single-threaded result exactly. This is the only check on the shared-histogram writes.
TEST(Imgproc_GeneralizedHoughGuil, MultithreadReproducibility)
{
    const int nThreads = cv::getNumThreads();
    if (nThreads == 1)
        throw SkipTestException("Single thread environment");

    Mat templ, image;
    makeScene(Size(640, 480), templ, image);

    std::vector<Vec4f> many, one;

    for (int pass = 0; pass < 2; pass++)
    {
        cv::setNumThreads(pass == 0 ? nThreads : 1);

        Ptr<GeneralizedHoughGuil> gh = createGeneralizedHoughGuil();
        gh->setTemplate(templ);
        gh->setMinAngle(0);
        gh->setMaxAngle(90);
        gh->setAngleStep(10);
        gh->setMinScale(0.8);
        gh->setMaxScale(1.2);
        gh->setScaleStep(0.1);

        gh->detect(image, pass == 0 ? many : one);
    }
    cv::setNumThreads(nThreads);

    ASSERT_EQ(one.size(), many.size());
    for (size_t i = 0; i < one.size(); i++)
        EXPECT_EQ(Vec4f(one[i]), Vec4f(many[i])) << "detection " << i;
}

TEST(Imgproc_GeneralizedHoughBallard, MultithreadReproducibility)
{
    const int nThreads = cv::getNumThreads();
    if (nThreads == 1)
        throw SkipTestException("Single thread environment");

    Mat templ, image;
    makeScene(Size(640, 480), templ, image);

    std::vector<Vec4f> many, one;

    for (int pass = 0; pass < 2; pass++)
    {
        cv::setNumThreads(pass == 0 ? nThreads : 1);

        Ptr<GeneralizedHoughBallard> gh = createGeneralizedHoughBallard();
        gh->setTemplate(templ);
        gh->setVotesThreshold(200);
        gh->detect(image, pass == 0 ? many : one);
    }
    cv::setNumThreads(nThreads);

    ASSERT_EQ(one.size(), many.size());
    for (size_t i = 0; i < one.size(); i++)
        EXPECT_EQ(Vec4f(one[i]), Vec4f(many[i])) << "detection " << i;
}

}} // namespace
