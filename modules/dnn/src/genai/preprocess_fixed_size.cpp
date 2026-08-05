// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include <opencv2/dnn/genai.private.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN
namespace genai {

Mat preprocessFixedSize(const Mat& imageBgr, const VLMConfig& config)
{
    CV_CheckFalse(imageBgr.empty(), "DNN/VLM: input image is empty");
    CV_CheckEQ(imageBgr.channels(), 3, "DNN/VLM: input image must be 3-channel BGR");
    CV_CheckGT(config.imageSize.width, 0, "DNN/VLM: imageSize.width must be positive");
    CV_CheckGT(config.imageSize.height, 0, "DNN/VLM: imageSize.height must be positive");
    CV_Check(config.rescaleFactor, config.rescaleFactor > 0.0,
             "DNN/VLM: rescaleFactor must be positive");

    // blobFromImageWithParams computes (image - mean) * scalefactor, so the rescale-then-normalize
    // form the configs use folds into those two per-channel parameters.
    Scalar meanParam, scaleParam;
    for (int c = 0; c < 3; c++)
    {
        CV_Check(config.stddev[c], config.stddev[c] != 0.0,
                 "DNN/VLM: stddev entries must be non-zero");
        meanParam[c] = config.mean[c] / config.rescaleFactor;
        scaleParam[c] = config.rescaleFactor / config.stddev[c];
    }
    scaleParam[3] = 1.0;

    const Image2BlobParams params(scaleParam, config.imageSize, meanParam, true, CV_32F);
    return blobFromImageWithParams(imageBgr, params);
}

} // namespace genai

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
