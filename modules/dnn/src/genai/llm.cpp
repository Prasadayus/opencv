// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include "generative_model_impl.hpp"
#include <opencv2/dnn/genai.private.hpp>

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

struct LLM_Impl CV_FINAL : public GenerativeModel::Impl
{
    void resetState() CV_OVERRIDE { net.resetKVCache(); }

    Net net;
    LLMConfig config;
};

static LLM_Impl& llmImplRef(const Ptr<GenerativeModel::Impl>& impl)
{
    CV_Assert(impl != nullptr && impl.dynamicCast<LLM_Impl>() != nullptr);
    return *impl.dynamicCast<LLM_Impl>();
}

LLM::LLM()
{
}

LLM LLM::create(const String& model, const String& tokenizerConfig, const LLMConfig& config)
{
    CV_CheckFalse(model.empty(), "DNN/LLM: model path is empty");
    CV_CheckFalse(tokenizerConfig.empty(), "DNN/LLM: tokenizer config path is empty");

    Ptr<LLM_Impl> llmImpl = makePtr<LLM_Impl>();
    llmImpl->config = config;
    llmImpl->tokenizer = Tokenizer::load(tokenizerConfig);
    llmImpl->net = readNetFromONNX(model, config.engine);
    llmImpl->net.setPreferableBackend(config.backend);
    llmImpl->net.setPreferableTarget(config.target);

    LLM llm;
    llm.impl = llmImpl;
    return llm;
}

LLM LLM::create(int modelType, const String& model, const String& tokenizerConfig)
{
    return create(model, tokenizerConfig, LLMConfig::defaultConfig(modelType));
}

String LLM::generate(const String& prompt, int maxNewTokens)
{
    LLM_Impl& llmImpl = llmImplRef(impl);
    const LLMConfig& config = llmImpl.config;

    std::vector<int> ids =
        llmImpl.tokenizer.encode(config.promptPrefix + prompt + config.promptSuffix);
    // BOS is prepended after encoding, so the tokenizer never sees it as text to be split.
    if (config.bosTokenId >= 0)
        ids.insert(ids.begin(), config.bosTokenId);

    const std::vector<int> generated =
        genai::generateFromTokenIds(llmImpl.net, ids, config, maxNewTokens);
    llmImpl.lastTokensUsed = (int)(ids.size() + generated.size());
    return llmImpl.tokenizer.decode(generated);
}

LLMConfig LLM::getConfig() const
{
    return llmImplRef(impl).config;
}

Net LLM::getNet() const
{
    return llmImplRef(impl).net;
}

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
