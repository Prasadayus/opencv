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

VLMConfig::VLMConfig()
    : preprocess(VLM_PREPROCESS_FIXED_SIZE)
    , merge(VLM_MERGE_SCATTER)
    , imageSize(224, 224)
    , patchSize(14)
    , mergeSize(2)
    , minPixels(28 * 28 * 130)
    , maxPixels(28 * 28 * 1280)
    , longestEdge(1536)
    , maxTileEdge(512)
    , imageSeqLen(64)
    , mean(Scalar::all(0.5))
    , stddev(Scalar::all(0.5))
    , rescaleFactor(1.0 / 255.0)
    , imageTokenId(-1)
    , useKVCache(false)
    , idType(CV_64S)
    , engine(ENGINE_AUTO)
    , backend(DNN_BACKEND_DEFAULT)
    , target(DNN_TARGET_CPU)
{
}

VLMConfig VLMConfig::defaultConfig(int modelType)
{
    VLMConfig config;
    switch (modelType)
    {
    case VLM_MODEL_PALIGEMMA2:
        config.preprocess = VLM_PREPROCESS_FIXED_SIZE;
        config.merge = VLM_MERGE_CONCAT;
        config.imageSize = Size(224, 224);
        config.stopTokenIds = {1};
        config.visionNet = "vision_model.onnx";
        config.embedNet = "embedding.onnx";
        config.decoderNet = "gemma2_3b.onnx";
        break;
    case VLM_MODEL_PADDLEOCR_VL:
        config.preprocess = VLM_PREPROCESS_PATCHIFY;
        config.merge = VLM_MERGE_SCATTER;
        config.stopTokenIds = {2};
        config.promptPrefix = "<|begin_of_sentence|>User: <|IMAGE_START|>";
        config.imagePlaceholder = "<|IMAGE_PLACEHOLDER|>";
        config.promptInfix = "<|IMAGE_END|>";
        config.promptSuffix = "\nAssistant:\n";
        config.useKVCache = true;
        config.visionNet = "onnx/vision_encoder.onnx";
        config.embedNet = "onnx/embedding.onnx";
        config.decoderNet = "onnx/decoder.onnx";
        break;
    case VLM_MODEL_GRANITE_DOCLING:
        config.preprocess = VLM_PREPROCESS_TILE_GRID;
        config.merge = VLM_MERGE_SCATTER;
        config.stopTokenIds = {2};
        config.promptPrefix = "<|start_of_role|>user<|end_of_role|>";
        config.imagePlaceholder = "<image>";
        config.promptSuffix = "<|end_of_text|>\n<|start_of_role|>assistant<|end_of_role|>";
        config.useKVCache = true;
        config.visionNet = "onnx/vision_encoder.onnx";
        config.embedNet = "onnx/embed_tokens.onnx";
        config.decoderNet = "onnx/decoder_model_merged.onnx";
        break;
    default:
        CV_Error(Error::StsBadArg, cv::format("DNN/VLM: unknown VLMModelType %d", modelType));
    }
    return config;
}

void VLMConfig::read(const FileNode& fn)
{
    CV_Assert(!fn.empty());

    genai::readIfPresent(fn, "preprocess", preprocess);
    genai::readIfPresent(fn, "merge", merge);
    genai::readIfPresent(fn, "image_size", imageSize);
    genai::readIfPresent(fn, "patch_size", patchSize);
    genai::readIfPresent(fn, "merge_size", mergeSize);
    genai::readIfPresent(fn, "min_pixels", minPixels);
    genai::readIfPresent(fn, "max_pixels", maxPixels);
    genai::readIfPresent(fn, "longest_edge", longestEdge);
    genai::readIfPresent(fn, "max_tile_edge", maxTileEdge);
    genai::readIfPresent(fn, "image_seq_len", imageSeqLen);
    genai::readIfPresent(fn, "mean", mean);
    genai::readIfPresent(fn, "stddev", stddev);
    genai::readIfPresent(fn, "rescale_factor", rescaleFactor);
    genai::readIfPresent(fn, "image_token_id", imageTokenId);
    genai::readIfPresent(fn, "prompt_prefix", promptPrefix);
    genai::readIfPresent(fn, "image_placeholder", imagePlaceholder);
    genai::readIfPresent(fn, "prompt_infix", promptInfix);
    genai::readIfPresent(fn, "prompt_suffix", promptSuffix);
    genai::readIfPresent(fn, "id_type", idType);
    genai::readIfPresent(fn, "vision_net", visionNet);
    genai::readIfPresent(fn, "embed_net", embedNet);
    genai::readIfPresent(fn, "decoder_net", decoderNet);
    genai::readIfPresent(fn, "engine", engine);
    genai::readIfPresent(fn, "backend", backend);
    genai::readIfPresent(fn, "target", target);
    genai::readIntVectorIfPresent(fn, "stop_token_ids", stopTokenIds);
    genai::readBoolIfPresent(fn, "use_kv_cache", useKVCache);

    CV_Check(preprocess, preprocess >= VLM_PREPROCESS_FIXED_SIZE
                             && preprocess <= VLM_PREPROCESS_TILE_GRID,
             "DNN/VLM: preprocess must be a VLMPreprocess value");
    CV_Check(merge, merge == VLM_MERGE_CONCAT || merge == VLM_MERGE_SCATTER,
             "DNN/VLM: merge must be a VLMEmbedMerge value");
    CV_Check(idType, idType == CV_64S || idType == CV_32S,
             "DNN/VLM: id_type must be CV_64S or CV_32S");
}

void VLMConfig::write(FileStorage& fs) const
{
    fs << "preprocess" << preprocess
       << "merge" << merge
       << "image_size" << imageSize
       << "patch_size" << patchSize
       << "merge_size" << mergeSize
       << "min_pixels" << minPixels
       << "max_pixels" << maxPixels
       << "longest_edge" << longestEdge
       << "max_tile_edge" << maxTileEdge
       << "image_seq_len" << imageSeqLen
       << "mean" << mean
       << "stddev" << stddev
       << "rescale_factor" << rescaleFactor
       << "image_token_id" << imageTokenId
       << "stop_token_ids" << stopTokenIds
       << "prompt_prefix" << promptPrefix
       << "image_placeholder" << imagePlaceholder
       << "prompt_infix" << promptInfix
       << "prompt_suffix" << promptSuffix
       << "use_kv_cache" << (int)useKVCache
       << "id_type" << idType
       << "vision_net" << visionNet
       << "embed_net" << embedNet
       << "decoder_net" << decoderNet
       << "engine" << engine
       << "backend" << backend
       << "target" << target;
}

CV__DNN_INLINE_NS_END
}} // namespace cv::dnn
