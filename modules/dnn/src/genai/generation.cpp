// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include <opencv2/dnn/genai.private.hpp>

#include <algorithm>
#include <cstring>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN
namespace genai {

int argmaxLastToken(const Mat& logits)
{
    CV_CheckTypeEQ(logits.type(), CV_32F, "DNN/GenAI: logits must be CV_32F");
    CV_Check(logits.dims, logits.dims == 2 || logits.dims == 3,
             "DNN/GenAI: logits must be [seq, vocab] or [1, seq, vocab]");
    if (logits.dims == 3)
        CV_CheckEQ(logits.size[0], 1, "DNN/GenAI: only batch size 1 is supported");
    CV_Assert(logits.isContinuous());

    const int seqLen = logits.size[logits.dims - 2];
    const int vocabSize = logits.size[logits.dims - 1];
    CV_CheckGT(seqLen, 0, "DNN/GenAI: logits carry an empty sequence");
    CV_CheckGT(vocabSize, 0, "DNN/GenAI: logits carry an empty vocabulary");

    const float* row = logits.ptr<float>() + (size_t)(seqLen - 1) * vocabSize;
    return (int)(std::max_element(row, row + vocabSize) - row);
}

void scatterImageFeatures(Mat& inputsEmbeds, const std::vector<int>& tokens,
                          int imageTokenId, const Mat& imageFeatures)
{
    CV_CheckTypeEQ(inputsEmbeds.type(), CV_32F, "DNN/GenAI: inputsEmbeds must be CV_32F");
    CV_CheckTypeEQ(imageFeatures.type(), CV_32F, "DNN/GenAI: imageFeatures must be CV_32F");
    CV_CheckEQ(inputsEmbeds.dims, 3, "DNN/GenAI: inputsEmbeds must be [1, seq, hidden]");
    CV_CheckEQ(inputsEmbeds.size[0], 1, "DNN/GenAI: only batch size 1 is supported");
    CV_Assert(inputsEmbeds.isContinuous() && imageFeatures.isContinuous());

    const int hiddenDim = inputsEmbeds.size[2];
    CV_CheckEQ((int)tokens.size(), inputsEmbeds.size[1],
               "DNN/GenAI: token count must equal the embedding sequence length");
    CV_CheckEQ((int)(imageFeatures.total() % (size_t)hiddenDim), 0,
               "DNN/GenAI: imageFeatures is not a whole number of hidden-size rows");

    const int numFeatures = (int)(imageFeatures.total() / (size_t)hiddenDim);
    float* embeds = inputsEmbeds.ptr<float>();
    const float* features = imageFeatures.ptr<float>();

    int featureIdx = 0;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] != imageTokenId)
            continue;
        CV_CheckLT(featureIdx, numFeatures,
                   "DNN/GenAI: more image tokens than vision-encoder feature rows");
        std::memcpy(embeds + i * (size_t)hiddenDim,
                    features + (size_t)featureIdx * hiddenDim,
                    hiddenDim * sizeof(float));
        featureIdx++;
    }
    CV_CheckEQ(featureIdx, numFeatures,
               "DNN/GenAI: fewer image tokens than vision-encoder feature rows");
}

Mat concatSequence(const Mat& first, const Mat& second)
{
    CV_CheckTypeEQ(first.type(), CV_32F, "DNN/GenAI: first must be CV_32F");
    CV_CheckTypeEQ(second.type(), CV_32F, "DNN/GenAI: second must be CV_32F");
    CV_CheckEQ(first.dims, 3, "DNN/GenAI: first must be [1, seq, hidden]");
    CV_CheckEQ(second.dims, 3, "DNN/GenAI: second must be [1, seq, hidden]");
    CV_CheckEQ(first.size[0], 1, "DNN/GenAI: only batch size 1 is supported");
    CV_CheckEQ(second.size[0], 1, "DNN/GenAI: only batch size 1 is supported");
    CV_CheckEQ(first.size[2], second.size[2], "DNN/GenAI: hidden size must match");
    CV_Assert(first.isContinuous() && second.isContinuous());

    const int sizes[] = {1, first.size[1] + second.size[1], first.size[2]};
    Mat merged(3, sizes, CV_32F);
    std::memcpy(merged.ptr<float>(), first.ptr<float>(), first.total() * sizeof(float));
    std::memcpy(merged.ptr<float>() + first.total(), second.ptr<float>(),
                second.total() * sizeof(float));
    return merged;
}

template<typename T>
static void fillIdRow(Mat& row, const int* ids, int count)
{
    T* dst = row.ptr<T>();
    for (int i = 0; i < count; i++)
        dst[i] = (T)ids[i];
}

Mat tokenIdsToMat(const int* ids, int count, int idType)
{
    CV_Assert(ids != nullptr);
    CV_CheckGT(count, 0, "DNN/GenAI: token id count must be positive");
    CV_Check(idType, idType == CV_64S || idType == CV_32S,
             "DNN/GenAI: token ids must be CV_64S or CV_32S");

    const int sizes[] = {1, count};
    Mat row(2, sizes, idType);
    if (idType == CV_64S)
        fillIdRow<int64_t>(row, ids, count);
    else
        fillIdRow<int>(row, ids, count);
    return row;
}

std::vector<int> generateFromTokenIds(Net& net, const std::vector<int>& promptIds,
                                      const LLMConfig& config, int maxNewTokens)
{
    CV_CheckFalse(promptIds.empty(), "DNN/LLM: the prompt encodes to no tokens");
    CV_CheckGT(maxNewTokens, 0, "DNN/LLM: maxNewTokens must be positive");
    CV_CheckFalse(config.inputIdsName.empty(), "DNN/LLM: LLMConfig::inputIdsName must be set");
    CV_Check(config.idType, config.idType == CV_64S || config.idType == CV_32S,
             "DNN/LLM: LLMConfig::idType must be CV_64S or CV_32S");

    if (config.useKVCache)
        net.enableKVCache();
    else
        net.disableKVCache();

    std::vector<int> ids = promptIds;
    ids.reserve(promptIds.size() + (size_t)maxNewTokens);
    std::vector<int> generated;
    generated.reserve((size_t)maxNewTokens);

    std::vector<int> mask, positions;
    for (int step = 0; step < maxNewTokens; step++)
    {
        const int contextLen = (int)ids.size();
        const int feedFrom = (config.useKVCache && step > 0) ? contextLen - 1 : 0;
        const int feedLen = contextLen - feedFrom;

        net.setInput(tokenIdsToMat(ids.data() + feedFrom, feedLen, config.idType),
                     config.inputIdsName);
        if (!config.attentionMaskName.empty())
        {
            mask.assign((size_t)contextLen, 1);
            net.setInput(tokenIdsToMat(mask.data(), contextLen, config.idType),
                         config.attentionMaskName);
        }
        if (!config.positionIdsName.empty())
        {
            positions.resize((size_t)feedLen);
            for (int i = 0; i < feedLen; i++)
                positions[i] = feedFrom + i;
            net.setInput(tokenIdsToMat(positions.data(), feedLen, config.idType),
                         config.positionIdsName);
        }

        const int newId = argmaxLastToken(net.forward());
        if (std::find(config.stopTokenIds.begin(), config.stopTokenIds.end(), newId)
                != config.stopTokenIds.end())
            break;
        ids.push_back(newId);
        generated.push_back(newId);
    }
    return generated;
}

// Fixed by the exported graph layout rather than configurable: every VLM export in scope names its
// decoder inputs this way, and a model that does not is a new preprocess mode, not a new field.
static const char* const kInputsEmbeds = "inputs_embeds";
static const char* const kAttentionMask = "attention_mask";
static const char* const kInputIds = "input_ids";

std::vector<int> generateFromEmbeddings(Net& embedNet, Net& decoderNet, const Mat& promptEmbeds,
                                        const VLMConfig& config, int maxNewTokens)
{
    CV_CheckTypeEQ(promptEmbeds.type(), CV_32F, "DNN/VLM: promptEmbeds must be CV_32F");
    CV_CheckEQ(promptEmbeds.dims, 3, "DNN/VLM: promptEmbeds must be [1, seq, hidden]");
    CV_CheckEQ(promptEmbeds.size[0], 1, "DNN/VLM: only batch size 1 is supported");
    CV_CheckGT(maxNewTokens, 0, "DNN/VLM: maxNewTokens must be positive");
    CV_Check(config.idType, config.idType == CV_64S || config.idType == CV_32S,
             "DNN/VLM: VLMConfig::idType must be CV_64S or CV_32S");

    if (config.useKVCache)
        decoderNet.enableKVCache();
    else
        decoderNet.disableKVCache();

    Mat context = promptEmbeds;
    Mat feed = promptEmbeds;
    int contextLen = promptEmbeds.size[1];

    std::vector<int> generated;
    generated.reserve((size_t)maxNewTokens);
    std::vector<int> mask;

    for (int step = 0; step < maxNewTokens; step++)
    {
        mask.assign((size_t)contextLen, 1);
        decoderNet.setInput(feed, kInputsEmbeds);
        decoderNet.setInput(tokenIdsToMat(mask.data(), contextLen, config.idType), kAttentionMask);

        const int newId = argmaxLastToken(decoderNet.forward());
        if (std::find(config.stopTokenIds.begin(), config.stopTokenIds.end(), newId)
                != config.stopTokenIds.end())
            break;
        generated.push_back(newId);
        if (step + 1 == maxNewTokens)
            break;

        embedNet.setInput(tokenIdsToMat(&newId, 1, config.idType), kInputIds);
        // Cloned because the next embedNet.forward() may reuse the buffer this points into.
        const Mat newEmbed = embedNet.forward().clone();
        contextLen++;
        if (config.useKVCache)
        {
            feed = newEmbed;
        }
        else
        {
            context = concatSequence(context, newEmbed);
            feed = context;
        }
    }
    return generated;
}

} // namespace genai

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
