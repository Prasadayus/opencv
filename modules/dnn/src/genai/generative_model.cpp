// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include "generative_model_impl.hpp"

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

GenerativeModel::GenerativeModel()
{
}

void GenerativeModel::reset()
{
    CV_Assert(impl);
    impl->resetState();
}

int GenerativeModel::lastTokensUsed() const
{
    CV_Assert(impl);
    return impl->lastTokensUsed;
}

Tokenizer GenerativeModel::getTokenizer() const
{
    CV_Assert(impl);
    return impl->tokenizer;
}

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
