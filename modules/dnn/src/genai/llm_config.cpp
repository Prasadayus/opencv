// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "../precomp.hpp"

#include "config_io.hpp"

namespace cv {
namespace dnn {
CV__DNN_INLINE_NS_BEGIN

LLMConfig::LLMConfig()
    : inputIdsName("input_ids")
    , attentionMaskName("attention_mask")
    , positionIdsName()
    , bosTokenId(-1)
    , useKVCache(false)
    , idType(CV_64S)
    , engine(ENGINE_AUTO)
    , backend(DNN_BACKEND_DEFAULT)
    , target(DNN_TARGET_CPU)
{
}

LLMConfig LLMConfig::defaultConfig(int modelType)
{
    LLMConfig config;
    switch (modelType)
    {
    case LLM_MODEL_GPT2:
        config.inputIdsName = "idx";
        config.attentionMaskName = String();
        config.stopTokenIds = {50256};
        config.idType = CV_32S;
        break;
    case LLM_MODEL_QWEN2_5:
        config.positionIdsName = "position_ids";
        config.stopTokenIds = {151645, 151643};
        config.promptPrefix = "<|im_start|>user\n";
        config.promptSuffix = "<|im_end|>\n<|im_start|>assistant\n";
        break;
    case LLM_MODEL_GEMMA3:
        config.stopTokenIds = {1, 106};
        config.bosTokenId = 2;
        config.promptPrefix = "<start_of_turn>user\n";
        config.promptSuffix = "<end_of_turn>\n<start_of_turn>model\n";
        break;
    default:
        CV_Error(Error::StsBadArg, cv::format("DNN/LLM: unknown LLMModelType %d", modelType));
    }
    return config;
}

void LLMConfig::read(const FileNode& fn)
{
    CV_Assert(!fn.empty());

    genai::readIfPresent(fn, "input_ids_name", inputIdsName);
    genai::readIfPresent(fn, "attention_mask_name", attentionMaskName);
    genai::readIfPresent(fn, "position_ids_name", positionIdsName);
    genai::readIfPresent(fn, "prompt_prefix", promptPrefix);
    genai::readIfPresent(fn, "prompt_suffix", promptSuffix);
    genai::readIfPresent(fn, "bos_token_id", bosTokenId);
    genai::readIfPresent(fn, "id_type", idType);
    genai::readIfPresent(fn, "engine", engine);
    genai::readIfPresent(fn, "backend", backend);
    genai::readIfPresent(fn, "target", target);
    genai::readIntVectorIfPresent(fn, "stop_token_ids", stopTokenIds);
    genai::readBoolIfPresent(fn, "use_kv_cache", useKVCache);

    CV_Check(idType, idType == CV_64S || idType == CV_32S,
             "DNN/LLM: id_type must be CV_64S or CV_32S");
}

void LLMConfig::write(FileStorage& fs) const
{
    fs << "input_ids_name" << inputIdsName
       << "attention_mask_name" << attentionMaskName
       << "position_ids_name" << positionIdsName
       << "stop_token_ids" << stopTokenIds
       << "bos_token_id" << bosTokenId
       << "prompt_prefix" << promptPrefix
       << "prompt_suffix" << promptSuffix
       << "use_kv_cache" << (int)useKVCache
       << "id_type" << idType
       << "engine" << engine
       << "backend" << backend
       << "target" << target;
}

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
