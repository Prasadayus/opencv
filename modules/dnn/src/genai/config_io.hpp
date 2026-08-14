// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#ifndef OPENCV_DNN_GENAI_CONFIG_IO_HPP
#define OPENCV_DNN_GENAI_CONFIG_IO_HPP

#include <opencv2/core/persistence.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN
namespace genai {

//! An absent key leaves @p value alone, so a config file may override a preset in part.
template<typename T>
static inline void readIfPresent(const FileNode& fn, const char* key, T& value)
{
    read(fn[key], value, value);
}

//! FileStorage has no bool overload, so booleans travel as ints.
static inline void readBoolIfPresent(const FileNode& fn, const char* key, bool& value)
{
    int current = value ? 1 : 0;
    read(fn[key], current, current);
    value = current != 0;
}

static inline void readIntVectorIfPresent(const FileNode& fn, const char* key,
                                          std::vector<int>& value)
{
    const FileNode node = fn[key];
    if (!node.empty())
        node >> value;
}

} // namespace genai
CV__DNN_INLINE_NS_END
}} // namespace cv::dnn

#endif // OPENCV_DNN_GENAI_CONFIG_IO_HPP
