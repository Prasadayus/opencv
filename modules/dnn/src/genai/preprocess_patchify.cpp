// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/dnn/genai.private.hpp>

#include <cmath>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN
namespace genai {

// Qwen2-VL-family "smart resize": snaps to multiples of factor = patchSize * mergeSize while
// keeping the resized area within [minPixels, maxPixels].
static void smartResize(int height, int width, int factor, int minPixels, int maxPixels,
                        int& outHeight, int& outWidth)
{
    if (height < factor)
    {
        width = cvRound((double)width * factor / height);
        height = factor;
    }
    if (width < factor)
    {
        height = cvRound((double)height * factor / width);
        width = factor;
    }
    CV_CheckLE((double)std::max(height, width) / std::min(height, width), 200.0,
               "DNN/VLM: image aspect ratio is too large for smart resize");

    int hBar = cvRound((double)height / factor) * factor;
    int wBar = cvRound((double)width / factor) * factor;
    if ((int64_t)hBar * wBar > maxPixels)
    {
        const double beta = std::sqrt((double)height * width / maxPixels);
        hBar = (int)std::floor(height / beta / factor) * factor;
        wBar = (int)std::floor(width / beta / factor) * factor;
    }
    else if ((int64_t)hBar * wBar < minPixels)
    {
        const double beta = std::sqrt((double)minPixels / ((double)height * width));
        hBar = (int)std::ceil(height * beta / factor) * factor;
        wBar = (int)std::ceil(width * beta / factor) * factor;
    }
    outHeight = hBar;
    outWidth = wBar;
}

Mat preprocessPatchify(const Mat& imageBgr, const VLMConfig& config, int& gridH, int& gridW)
{
    CV_CheckFalse(imageBgr.empty(), "DNN/VLM: input image is empty");
    CV_CheckEQ(imageBgr.channels(), 3, "DNN/VLM: input image must be 3-channel BGR");
    CV_CheckGT(config.patchSize, 0, "DNN/VLM: patchSize must be positive");
    CV_CheckGT(config.mergeSize, 0, "DNN/VLM: mergeSize must be positive");
    CV_CheckGT(config.minPixels, 0, "DNN/VLM: minPixels must be positive");
    CV_Check(config.maxPixels, config.maxPixels >= config.minPixels,
             "DNN/VLM: maxPixels must be >= minPixels");
    CV_Check(config.rescaleFactor, config.rescaleFactor > 0.0,
             "DNN/VLM: rescaleFactor must be positive");

    const int factor = config.patchSize * config.mergeSize;
    int resizedHeight, resizedWidth;
    smartResize(imageBgr.rows, imageBgr.cols, factor, config.minPixels, config.maxPixels,
                resizedHeight, resizedWidth);

    Mat resized;
    resize(imageBgr, resized, Size(resizedWidth, resizedHeight), 0, 0, INTER_CUBIC);
    Mat rgb;
    cvtColor(resized, rgb, COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32F, config.rescaleFactor);

    std::vector<Mat> channels(3);
    split(rgb, channels);
    for (int c = 0; c < 3; c++)
    {
        CV_Check(config.stddev[c], config.stddev[c] != 0.0,
                 "DNN/VLM: stddev entries must be non-zero");
        channels[c].convertTo(channels[c], -1, 1.0 / config.stddev[c],
                              -config.mean[c] / config.stddev[c]);
    }

    gridH = resizedHeight / config.patchSize;
    gridW = resizedWidth / config.patchSize;
    const int numPatches = gridH * gridW;

    const int sizes[] = {1, numPatches, 3, config.patchSize, config.patchSize};
    Mat pixelValues(5, sizes, CV_32F);

    int idx = 0;
    for (int h = 0; h < gridH; h++)
        for (int w = 0; w < gridW; w++)
        {
            const Rect roi(w * config.patchSize, h * config.patchSize,
                           config.patchSize, config.patchSize);
            for (int c = 0; c < 3; c++)
            {
                Mat dst(config.patchSize, config.patchSize, CV_32F,
                       pixelValues.ptr<float>(0, idx, c));
                channels[c](roi).copyTo(dst);
            }
            idx++;
        }
    return pixelValues;
}

} // namespace genai
CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
