// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include <opencv2/dnn/genai.private.hpp>

#include <sstream>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN
namespace genai {

String buildPatchifyPrompt(const VLMConfig& config, int gridH, int gridW, const String& userPrompt)
{
    CV_CheckGT(config.mergeSize, 0, "DNN/VLM: mergeSize must be positive");
    const int repeats = (gridH * gridW) / (config.mergeSize * config.mergeSize);
    CV_CheckGT(repeats, 0, "DNN/VLM: patch grid produced zero image tokens");

    std::ostringstream imagePart;
    for (int i = 0; i < repeats; i++)
        imagePart << config.imagePlaceholder;

    return config.promptPrefix + imagePart.str() + config.promptInfix + userPrompt
         + config.promptSuffix;
}

// GraniteDocling/Idefics3-style layout: one <row_R_col_C>-tagged block per tile, then a
// global-thumbnail block, each holding imageSeqLen image-placeholder tokens. The markup tokens
// themselves belong to this tokenization scheme, not to any one model, so they are literal here
// rather than VLMConfig fields.
String buildTileGridPrompt(const VLMConfig& config, int rows, int cols, const String& userPrompt)
{
    CV_CheckGT(config.imageSeqLen, 0, "DNN/VLM: imageSeqLen must be positive");

    std::ostringstream imagePart;
    for (int h = 0; h < rows; h++)
    {
        for (int w = 0; w < cols; w++)
        {
            imagePart << "<fake_token_around_image><row_" << (h + 1) << "_col_" << (w + 1) << ">";
            for (int i = 0; i < config.imageSeqLen; i++)
                imagePart << config.imagePlaceholder;
        }
        imagePart << "\n";
    }
    imagePart << "\n<fake_token_around_image><global-img>";
    for (int i = 0; i < config.imageSeqLen; i++)
        imagePart << config.imagePlaceholder;
    imagePart << "<fake_token_around_image>";

    return config.promptPrefix + imagePart.str() + userPrompt + config.promptSuffix;
}

} // namespace genai
CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
