// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/dnn/genai.private.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN
namespace genai {

static void resizeAntialias(const Mat& src, Mat& dst, Size size)
{
    const bool shrinking = (int64_t)size.width * size.height < (int64_t)src.cols * src.rows;
    resize(src, dst, size, 0, 0, shrinking ? INTER_AREA : INTER_LANCZOS4);
}

static void normalizeTileInto(const Mat& tileBgr, const VLMConfig& config, int tileSize,
                              Mat& pixelValues, int tileIdx)
{
    Mat tileRgb;
    cvtColor(tileBgr, tileRgb, COLOR_BGR2RGB);
    tileRgb.convertTo(tileRgb, CV_32F, config.rescaleFactor);

    std::vector<Mat> channels(3);
    split(tileRgb, channels);
    for (int c = 0; c < 3; c++)
    {
        CV_Check(config.stddev[c], config.stddev[c] != 0.0,
                 "DNN/VLM: stddev entries must be non-zero");
        Mat dst(tileSize, tileSize, CV_32F, pixelValues.ptr<float>(0, tileIdx, c));
        channels[c].convertTo(dst, -1, 1.0 / config.stddev[c], -config.mean[c] / config.stddev[c]);
    }
}

Mat preprocessTileGrid(const Mat& imageBgr, const VLMConfig& config, int& rows, int& cols)
{
    CV_CheckFalse(imageBgr.empty(), "DNN/VLM: input image is empty");
    CV_CheckEQ(imageBgr.channels(), 3, "DNN/VLM: input image must be 3-channel BGR");
    CV_CheckGT(config.longestEdge, 0, "DNN/VLM: longestEdge must be positive");
    CV_CheckGT(config.maxTileEdge, 0, "DNN/VLM: maxTileEdge must be positive");
    CV_Check(config.rescaleFactor, config.rescaleFactor > 0.0,
             "DNN/VLM: rescaleFactor must be positive");

    const int h0 = imageBgr.rows, w0 = imageBgr.cols;
    int newW, newH;
    if (w0 >= h0)
    {
        newW = config.longestEdge;
        newH = std::max(1, cvRound((double)config.longestEdge * h0 / w0));
    }
    else
    {
        newH = config.longestEdge;
        newW = std::max(1, cvRound((double)config.longestEdge * w0 / h0));
    }

    Mat resized;
    resizeAntialias(imageBgr, resized, Size(newW, newH));

    const int tileSize = config.maxTileEdge;
    rows = (newH + tileSize - 1) / tileSize;
    cols = (newW + tileSize - 1) / tileSize;

    Mat grid;
    resizeAntialias(resized, grid, Size(cols * tileSize, rows * tileSize));

    const int numTiles = rows * cols + 1;
    const int sizes[] = {1, numTiles, 3, tileSize, tileSize};
    Mat pixelValues(5, sizes, CV_32F);

    int idx = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
        {
            const Rect roi(c * tileSize, r * tileSize, tileSize, tileSize);
            normalizeTileInto(grid(roi), config, tileSize, pixelValues, idx++);
        }
    Mat thumbnail;
    resizeAntialias(resized, thumbnail, Size(tileSize, tileSize));
    normalizeTileInto(thumbnail, config, tileSize, pixelValues, idx);

    return pixelValues;
}

} // namespace genai
CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
