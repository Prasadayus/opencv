// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#ifndef OPENCV_DNN_GENAI_GENERATIVE_MODEL_IMPL_HPP
#define OPENCV_DNN_GENAI_GENERATIVE_MODEL_IMPL_HPP

#include <opencv2/dnn.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

struct GenerativeModel::Impl
{
    virtual ~Impl() {}
    Impl() : lastTokensUsed(0) {}
    Impl(const Impl&) = delete;
    Impl(Impl&&) = delete;

    //! Concrete models own a different number of nets, so each clears its own generation state.
    virtual void resetState() = 0;

    Tokenizer tokenizer;
    int lastTokensUsed;
};

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn

#endif // OPENCV_DNN_GENAI_GENERATIVE_MODEL_IMPL_HPP
