// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "perf_precomp.hpp"

#include <opencv2/dnn/genai.private.hpp>

namespace opencv_test {
using namespace cv::dnn::genai;

// The one per-token op in the decode loop: called once per generated token, over the whole
// vocabulary. Everything else in genai (scatter/concat/preprocess) runs once per generate()
// call, not once per token, so it is a setup cost rather than a hot loop.
PERF_TEST(GenAI, ArgmaxLastToken)
{
    const int sizes[] = {1, 1, 151936};  // Qwen2.5 vocabulary size
    Mat logits(3, sizes, CV_32F);
    randu(logits, -10.f, 10.f);

    int result = 0;
    TEST_CYCLE()
    {
        result = argmaxLastToken(logits);
    }

    EXPECT_GE(result, 0);
    SANITY_CHECK_NOTHING();
}

} // namespace opencv_test
