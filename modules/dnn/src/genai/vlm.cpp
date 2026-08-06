// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include "generative_model_impl.hpp"
#include <opencv2/dnn/genai.private.hpp>
#include <opencv2/core/utils/filesystem.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

struct VLM_Impl CV_FINAL : public GenerativeModel::Impl
{
    void resetState() CV_OVERRIDE { decoderNet.resetKVCache(); }

    Net visionNet;
    Net embedNet;
    Net decoderNet;
    VLMConfig config;
};

static VLM_Impl& vlmImplRef(const Ptr<GenerativeModel::Impl>& impl)
{
    CV_Assert(impl != nullptr && impl.dynamicCast<VLM_Impl>() != nullptr);
    return *impl.dynamicCast<VLM_Impl>();
}

VLM::VLM()
{
}

VLM VLM::create(const String& modelDir, const String& tokenizerConfig, const VLMConfig& config)
{
    // modelDir may be empty when every VLMConfig net path is already a complete, independent
    // path (utils::fs::join returns the unjoined path for an empty base): this is how the
    // PaliGemma2 sample takes --siglip/--embedding/--gemma as three unrelated file arguments.
    CV_CheckFalse(tokenizerConfig.empty(), "DNN/VLM: tokenizer config path is empty");
    CV_CheckFalse(config.visionNet.empty(), "DNN/VLM: VLMConfig::visionNet is empty");
    CV_CheckFalse(config.embedNet.empty(), "DNN/VLM: VLMConfig::embedNet is empty");
    CV_CheckFalse(config.decoderNet.empty(), "DNN/VLM: VLMConfig::decoderNet is empty");

    Ptr<VLM_Impl> vlmImpl = makePtr<VLM_Impl>();
    vlmImpl->config = config;
    vlmImpl->tokenizer = Tokenizer::load(tokenizerConfig);

    vlmImpl->visionNet =
        readNetFromONNX(utils::fs::join(modelDir, config.visionNet), config.engine);
    vlmImpl->embedNet =
        readNetFromONNX(utils::fs::join(modelDir, config.embedNet), config.engine);
    vlmImpl->decoderNet =
        readNetFromONNX(utils::fs::join(modelDir, config.decoderNet), config.engine);

    Net* const nets[] = {&vlmImpl->visionNet, &vlmImpl->embedNet, &vlmImpl->decoderNet};
    for (Net* net : nets)
    {
        net->setPreferableBackend(config.backend);
        net->setPreferableTarget(config.target);
    }

    VLM vlm;
    vlm.impl = vlmImpl;
    return vlm;
}

VLM VLM::create(int modelType, const String& modelDir, const String& tokenizerConfig)
{
    return create(modelDir, tokenizerConfig, VLMConfig::defaultConfig(modelType));
}

String VLM::generate(InputArray image, const String& prompt, int maxNewTokens)
{
    VLM_Impl& vlmImpl = vlmImplRef(impl);
    const VLMConfig& config = vlmImpl.config;
    const Mat imageBgr = image.getMat();

    Mat imageFeatures;
    String fullPrompt;
    switch (config.preprocess)
    {
    case VLM_PREPROCESS_FIXED_SIZE:
    {
        vlmImpl.visionNet.setInput(genai::preprocessFixedSize(imageBgr, config), "pixel_values");
        imageFeatures = vlmImpl.visionNet.forward();
        fullPrompt = config.promptPrefix + prompt + config.promptSuffix;
        break;
    }
    case VLM_PREPROCESS_PATCHIFY:
    {
        int gridH, gridW;
        Mat pixelValues = genai::preprocessPatchify(imageBgr, config, gridH, gridW);
        const int gridShape[] = {1, 3};
        Mat imageGridThw(2, gridShape, CV_64S);
        imageGridThw.at<int64_t>(0, 0) = 1;
        imageGridThw.at<int64_t>(0, 1) = gridH;
        imageGridThw.at<int64_t>(0, 2) = gridW;

        vlmImpl.visionNet.setInput(pixelValues, "pixel_values");
        vlmImpl.visionNet.setInput(imageGridThw, "image_grid_thw");
        imageFeatures = vlmImpl.visionNet.forward();
        fullPrompt = genai::buildPatchifyPrompt(config, gridH, gridW, prompt);
        break;
    }
    case VLM_PREPROCESS_TILE_GRID:
    {
        int rows, cols;
        Mat pixelValues = genai::preprocessTileGrid(imageBgr, config, rows, cols);
        const int maskShape[] = {1, pixelValues.size[1], pixelValues.size[3], pixelValues.size[4]};
        Mat pixelAttentionMask(4, maskShape, CV_Bool, Scalar(1));

        vlmImpl.visionNet.setInput(pixelValues, "pixel_values");
        vlmImpl.visionNet.setInput(pixelAttentionMask, "pixel_attention_mask");
        imageFeatures = vlmImpl.visionNet.forward();
        fullPrompt = genai::buildTileGridPrompt(config, rows, cols, prompt);
        break;
    }
    default:
        CV_Error(Error::StsBadArg, "DNN/VLM: VLMConfig::preprocess is not a VLMPreprocess value");
    }

    const std::vector<int> ids = vlmImpl.tokenizer.encode(fullPrompt);
    vlmImpl.embedNet.setInput(
        genai::tokenIdsToMat(ids.data(), (int)ids.size(), config.idType), "input_ids");
    const Mat textEmbeds = vlmImpl.embedNet.forward();

    Mat inputsEmbeds;
    if (config.merge == VLM_MERGE_CONCAT)
    {
        inputsEmbeds = genai::concatSequence(imageFeatures, textEmbeds);
    }
    else
    {
        CV_CheckGE(config.imageTokenId, 0,
                   "DNN/VLM: VLMConfig::imageTokenId must be set for VLM_MERGE_SCATTER");
        textEmbeds.copyTo(inputsEmbeds);
        genai::scatterImageFeatures(inputsEmbeds, ids, config.imageTokenId, imageFeatures);
    }

    const std::vector<int> generated = genai::generateFromEmbeddings(
        vlmImpl.embedNet, vlmImpl.decoderNet, inputsEmbeds, config, maxNewTokens);
    vlmImpl.lastTokensUsed = inputsEmbeds.size[1] + (int)generated.size();
    return vlmImpl.tokenizer.decode(generated);
}

VLMConfig VLM::getConfig() const
{
    return vlmImplRef(impl).config;
}

std::vector<Net> VLM::getNets() const
{
    const VLM_Impl& vlmImpl = vlmImplRef(impl);
    return {vlmImpl.visionNet, vlmImpl.embedNet, vlmImpl.decoderNet};
}

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
