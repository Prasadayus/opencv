// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

#include "perf_precomp.hpp"

namespace opencv_test
{
namespace
{
struct ExposureSeq
{
    std::vector<Mat> images;
    std::vector<float> times;
};

ExposureSeq loadExposureSeq(const std::string& list_filename)
{
    std::ifstream list_file(list_filename);
    EXPECT_TRUE(list_file.is_open());
    string name;
    float val;
    const String path(list_filename.substr(0, list_filename.find_last_of("\\/") + 1));
    ExposureSeq seq;
    while (list_file >> name >> val)
    {
        Mat img = imread(path + name);
        EXPECT_FALSE(img.empty()) << "Could not load input image " << path + name;
        seq.images.push_back(img);
        seq.times.push_back(1 / val);
    }
    list_file.close();
    return seq;
}

PERF_TEST(HDR, Mertens)
{
    const ExposureSeq seq = loadExposureSeq(getDataPath("cv/hdr/exposures/list.txt"));
    Ptr<MergeMertens> merge = createMergeMertens();
    Mat result(seq.images.front().size(), seq.images.front().type());
    TEST_CYCLE() merge->process(seq.images, result);
    SANITY_CHECK_NOTHING();
}

PERF_TEST(HDR, Debevec)
{
    const ExposureSeq seq = loadExposureSeq(getDataPath("cv/hdr/exposures/list.txt"));
    Ptr<MergeDebevec> merge = createMergeDebevec();
    Mat result(seq.images.front().size(), seq.images.front().type());
    TEST_CYCLE() merge->process(seq.images, result, seq.times);
    SANITY_CHECK_NOTHING();
}

PERF_TEST(HDR, Robertson)
{
    const ExposureSeq seq = loadExposureSeq(getDataPath("cv/hdr/exposures/list.txt"));
    Ptr<MergeRobertson> merge = createMergeRobertson();
    Mat result(seq.images.front().size(), seq.images.front().type());
    TEST_CYCLE() merge->process(seq.images, result, seq.times);
    SANITY_CHECK_NOTHING();
}

// CalibrateDebevec had no perf coverage, and it is the one HDR path that does a large solve:
// calibrate.cpp:152 is a DECOMP_SVD least-squares on an
// (samples*images + 257) x (256 + samples) CV_32F system - 467 x 326 at the default 70 samples,
// well past every LAPACK threshold. Synthetic exposures keep this off OPENCV_TEST_DATA_PATH;
// the system size depends only on sample and image counts, not on image content.
typedef perf::TestBaseWithParam<int> CalibrateTest;

PERF_TEST_P(CalibrateTest, CalibrateDebevec, ::testing::Values(70, 200))
{
    const int samples = GetParam();
    const Size sz(640, 480);

    std::vector<Mat> images;
    std::vector<float> times;
    for (int i = 0; i < 3; i++)
    {
        Mat img(sz, CV_8UC3);
        randu(img, Scalar::all(0), Scalar::all(256));
        images.push_back(img);
        times.push_back(1.f / (15 << i));
    }

    Ptr<CalibrateDebevec> calibrate = createCalibrateDebevec(samples);
    Mat response;

    declare.time(60);

    TEST_CYCLE() calibrate->process(images, response, times);

    SANITY_CHECK_NOTHING();
}

} // namespace
} // namespace opencv_test
