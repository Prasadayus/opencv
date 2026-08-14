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

/** @brief Aspect-preserving resize to a patch-grid-aligned size, then split into
 * `config.patchSize`-square patches, producing a `[1, gridH*gridW, 3, patchSize, patchSize]`
 * `pixel_values` blob.
 * @param[out] gridH, gridW  patch grid dimensions, needed for `image_grid_thw` and the prompt.
 */
CV_EXPORTS Mat preprocessPatchify(const Mat& imageBgr, const VLMConfig& config,
                                  int& gridH, int& gridW);

/** @brief Tile an image to `config.longestEdge` on its long side, split into
 * `config.maxTileEdge`-square tiles plus one global thumbnail, producing a
 * `[1, rows*cols+1, 3, maxTileEdge, maxTileEdge]` `pixel_values` blob.
 * @param[out] rows, cols  tile grid dimensions, needed for the prompt.
 */
CV_EXPORTS Mat preprocessTileGrid(const Mat& imageBgr, const VLMConfig& config,
                                  int& rows, int& cols);

/** @brief PaddleOCR-VL-style prompt: `config.promptPrefix` + `config.imagePlaceholder` repeated
 * once per merged patch + `config.promptInfix` + @p userPrompt + `config.promptSuffix`.
 */
CV_EXPORTS String buildPatchifyPrompt(const VLMConfig& config, int gridH, int gridW,
                                      const String& userPrompt);

/** @brief GraniteDocling-style prompt: a `<row_R_col_C>`-tagged block per tile plus a
 * global-thumbnail block, each holding `config.imageSeqLen` copies of `config.imagePlaceholder`,
 * wrapped in `config.promptPrefix`/`config.promptSuffix`.
 */
CV_EXPORTS String buildTileGridPrompt(const VLMConfig& config, int rows, int cols,
                                      const String& userPrompt);

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
