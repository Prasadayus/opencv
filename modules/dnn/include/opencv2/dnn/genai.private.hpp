// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#ifndef OPENCV_DNN_GENAI_PRIVATE_HPP
#define OPENCV_DNN_GENAI_PRIVATE_HPP

#include <opencv2/dnn.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

//! Building blocks shared by the autoregressive decode loops. Not part of the public API.
namespace genai {

/** @brief Index of the largest logit at the last sequence position.
 *  @param logits `[seq, vocab]` or `[1, seq, vocab]`, CV_32F, continuous.
 */
CV_EXPORTS int argmaxLastToken(const Mat& logits);

/** @brief Overwrite the embedding at each `imageTokenId` position with the next feature row.
 *
 * `tokens` indexes `inputsEmbeds` position for position. The image-token count must equal the
 * feature-row count: a mismatch either way means the prompt and the vision encoder disagree,
 * which would otherwise corrupt or silently drop features.
 */
CV_EXPORTS void scatterImageFeatures(Mat& inputsEmbeds, const std::vector<int>& tokens,
                                     int imageTokenId, const Mat& imageFeatures);

/** @brief `[first | second]` joined along the sequence axis; both are `[1, seq, hidden]` CV_32F. */
CV_EXPORTS Mat concatSequence(const Mat& first, const Mat& second);

/** @brief Token ids as a `[1, count]` Mat of @p idType, owning its data. */
CV_EXPORTS Mat tokenIdsToMat(const int* ids, int count, int idType);

/** @brief Resize to `config.imageSize` and normalize, producing an NCHW `pixel_values` blob. */
CV_EXPORTS Mat preprocessFixedSize(const Mat& imageBgr, const VLMConfig& config);

/** @brief Greedy decode over a graph that takes token ids, returning the ids it generated.
 *
 * A trailing stop token terminates generation and is not returned. With `config.useKVCache` each
 * step after the prefill feeds only the new token, while the attention mask and position ids keep
 * describing the whole context and so stay full length.
 */
CV_EXPORTS std::vector<int> generateFromTokenIds(Net& net, const std::vector<int>& promptIds,
                                                 const LLMConfig& config, int maxNewTokens);

/** @brief Greedy decode over a decoder that takes embeddings, returning the ids it generated.
 *
 * @p embedNet turns each newly chosen id into the embedding the next step feeds. Without a cache
 * the decoder re-reads the whole prefix, so the context Mat grows by one row per step.
 */
CV_EXPORTS std::vector<int> generateFromEmbeddings(Net& embedNet, Net& decoderNet,
                                                   const Mat& promptEmbeds,
                                                   const VLMConfig& config, int maxNewTokens);

} // namespace genai

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn

#endif // OPENCV_DNN_GENAI_PRIVATE_HPP
