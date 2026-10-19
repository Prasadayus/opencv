// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "perf_precomp.hpp"

namespace opencv_test { namespace {

// A smooth random image plus a smooth random displacement field, as in the DIS flow fixture:
// real motion over real texture, with no test data dependency.
static void makeFlowPair(Mat& frame1, Mat& frame2)
{
    const int src_scale = 2, of_scale = 6;
    const double sigma = frame1.cols / 300.0;

    Mat tmp(Size(frame1.cols >> src_scale, frame1.rows >> src_scale), CV_8U);
    randu(tmp, 0, 255);
    resize(tmp, frame1, frame1.size(), 0.0, 0.0, INTER_LINEAR_EXACT);
    resize(tmp, frame2, frame2.size(), 0.0, 0.0, INTER_LINEAR_EXACT);

    Mat field(Size(frame1.cols >> of_scale, frame1.rows >> of_scale), CV_32FC2);
    randn(field, 0.0, sigma);
    resize(field, field, frame2.size(), 0.0, 0.0, INTER_CUBIC);
    for (int i = 0; i < field.rows; i++)
        for (int j = 0; j < field.cols; j++)
            field.at<Vec2f>(i, j) += Vec2f((float)j, (float)i);

    remap(frame2, frame2, field, Mat(), INTER_LINEAR, BORDER_REPLICATE);
}

CV_ENUM(FarnebackFlags, 0, OPTFLOW_FARNEBACK_GAUSSIAN)
typedef tuple<Size, FarnebackFlags> FarnebackParams;
typedef TestBaseWithParam<FarnebackParams> DenseOpticalFlow_Farneback;

// the two flag values select different flow-update kernels, so both are measured
PERF_TEST_P(DenseOpticalFlow_Farneback, perf,
            Combine(Values(szVGA, sz720p, sz1080p), FarnebackFlags::all()))
{
    const Size sz = get<0>(GetParam());
    const int flags = get<1>(GetParam());

    Mat frame1(sz, CV_8U), frame2(sz, CV_8U), flow;
    makeFlowPair(frame1, frame2);

    // flow is allocated by the call, so it must not be declared as an output here
    declare.in(frame1, frame2).time(60);

    TEST_CYCLE() calcOpticalFlowFarneback(frame1, frame2, flow, 0.5, 3, 15, 3, 5, 1.2, flags);

    SANITY_CHECK_NOTHING();
}

}} // namespace
