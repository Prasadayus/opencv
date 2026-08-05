// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "test_precomp.hpp"

#include <opencv2/dnn/genai.private.hpp>

namespace opencv_test { namespace {

using namespace cv::dnn::genai;

static Mat makeLogits(int seqLen, int vocabSize, const std::vector<int>& argmaxPerRow)
{
    const int sizes[] = {1, seqLen, vocabSize};
    Mat logits(3, sizes, CV_32F, Scalar(0));
    float* data = logits.ptr<float>();
    for (int s = 0; s < seqLen; s++)
        data[(size_t)s * vocabSize + argmaxPerRow[s]] = 1.f;
    return logits;
}

TEST(DNN_GenAI, ArgmaxUsesLastSequencePosition)
{
    // Every row has a different peak; only the last one may be picked.
    Mat logits = makeLogits(3, 5, {0, 4, 2});
    EXPECT_EQ(argmaxLastToken(logits), 2);
}

// Ties must resolve to the lowest index, matching numpy argmax, or greedy decoding diverges
// from the reference scripts on any tied step.
TEST(DNN_GenAI, ArgmaxTieBreaksToFirst)
{
    Mat logits = makeLogits(2, 5, {0, 1});
    float* lastRow = logits.ptr<float>() + 5;
    lastRow[3] = lastRow[1];
    EXPECT_EQ(argmaxLastToken(logits), 1);
}

TEST(DNN_GenAI, ArgmaxAcceptsRank2Logits)
{
    Mat logits(3, 5, CV_32F, Scalar(0));
    logits.at<float>(2, 3) = 1.f;
    EXPECT_EQ(argmaxLastToken(logits), 3);
}

TEST(DNN_GenAI, ArgmaxRejectsUnsupportedShapeAndType)
{
    const int sizes4d[] = {1, 1, 2, 5};
    EXPECT_THROW(argmaxLastToken(Mat(4, sizes4d, CV_32F, Scalar(0))), cv::Exception);

    const int sizes3d[] = {2, 2, 5};
    EXPECT_THROW(argmaxLastToken(Mat(3, sizes3d, CV_32F, Scalar(0))), cv::Exception);

    EXPECT_THROW(argmaxLastToken(Mat(3, 5, CV_64F, Scalar(0))), cv::Exception);
}

TEST(DNN_GenAI, ScatterReplacesOnlyImageTokenPositions)
{
    const int imageTokenId = 7;
    const int hiddenDim = 2;
    const std::vector<int> tokens = {1, imageTokenId, 2, imageTokenId, 3};

    const int embedSizes[] = {1, (int)tokens.size(), hiddenDim};
    Mat embeds(3, embedSizes, CV_32F, Scalar(-1));

    const int featureSizes[] = {1, 2, hiddenDim};
    Mat features(3, featureSizes, CV_32F);
    float* f = features.ptr<float>();
    for (int i = 0; i < 4; i++)
        f[i] = (float)(i + 10);

    scatterImageFeatures(embeds, tokens, imageTokenId, features);

    const float* e = embeds.ptr<float>();
    EXPECT_FLOAT_EQ(e[0], -1.f);   // token 1, untouched
    EXPECT_FLOAT_EQ(e[1], -1.f);
    EXPECT_FLOAT_EQ(e[2], 10.f);   // first image token -> first feature row
    EXPECT_FLOAT_EQ(e[3], 11.f);
    EXPECT_FLOAT_EQ(e[4], -1.f);   // token 2, untouched
    EXPECT_FLOAT_EQ(e[5], -1.f);
    EXPECT_FLOAT_EQ(e[6], 12.f);   // second image token -> second feature row
    EXPECT_FLOAT_EQ(e[7], 13.f);
    EXPECT_FLOAT_EQ(e[8], -1.f);   // token 3, untouched
    EXPECT_FLOAT_EQ(e[9], -1.f);
}

TEST(DNN_GenAI, ScatterRejectsImageTokenFeatureCountMismatch)
{
    const int imageTokenId = 7;
    const int hiddenDim = 2;
    const int featureSizes[] = {1, 2, hiddenDim};
    Mat features(3, featureSizes, CV_32F, Scalar(1));

    // Three image tokens, two feature rows.
    {
        const std::vector<int> tokens = {imageTokenId, imageTokenId, imageTokenId};
        const int embedSizes[] = {1, (int)tokens.size(), hiddenDim};
        Mat embeds(3, embedSizes, CV_32F, Scalar(0));
        EXPECT_THROW(scatterImageFeatures(embeds, tokens, imageTokenId, features), cv::Exception);
    }
    // One image token, two feature rows: the second would be silently dropped.
    {
        const std::vector<int> tokens = {imageTokenId, 1};
        const int embedSizes[] = {1, (int)tokens.size(), hiddenDim};
        Mat embeds(3, embedSizes, CV_32F, Scalar(0));
        EXPECT_THROW(scatterImageFeatures(embeds, tokens, imageTokenId, features), cv::Exception);
    }
}

TEST(DNN_GenAI, ScatterRejectsTokenCountSequenceLengthMismatch)
{
    const int hiddenDim = 2;
    const int embedSizes[] = {1, 3, hiddenDim};
    Mat embeds(3, embedSizes, CV_32F, Scalar(0));
    const int featureSizes[] = {1, 1, hiddenDim};
    Mat features(3, featureSizes, CV_32F, Scalar(1));

    const std::vector<int> tokens = {7, 1};  // 2 tokens against a sequence length of 3
    EXPECT_THROW(scatterImageFeatures(embeds, tokens, 7, features), cv::Exception);
}

TEST(DNN_GenAI, ConcatSequenceKeepsOrder)
{
    const int hiddenDim = 2;
    const int imageSizes[] = {1, 2, hiddenDim};
    Mat image(3, imageSizes, CV_32F, Scalar(5));
    const int textSizes[] = {1, 3, hiddenDim};
    Mat text(3, textSizes, CV_32F, Scalar(9));

    Mat merged = concatSequence(image, text);

    ASSERT_EQ(merged.dims, 3);
    EXPECT_EQ(merged.size[0], 1);
    EXPECT_EQ(merged.size[1], 5);
    EXPECT_EQ(merged.size[2], hiddenDim);

    const float* m = merged.ptr<float>();
    for (int i = 0; i < 2 * hiddenDim; i++)
        EXPECT_FLOAT_EQ(m[i], 5.f) << "image features must come first, at offset " << i;
    for (int i = 2 * hiddenDim; i < 5 * hiddenDim; i++)
        EXPECT_FLOAT_EQ(m[i], 9.f) << "text embeddings must follow, at offset " << i;
}

TEST(DNN_GenAI, ConcatRejectsHiddenSizeMismatch)
{
    const int imageSizes[] = {1, 2, 2};
    Mat image(3, imageSizes, CV_32F, Scalar(1));
    const int textSizes[] = {1, 2, 3};
    Mat text(3, textSizes, CV_32F, Scalar(1));
    EXPECT_THROW(concatSequence(image, text), cv::Exception);
}

TEST(DNN_GenAI, TokenIdsToMatHonoursIdType)
{
    const std::vector<int> ids = {5, 7, 9};

    const Mat wide = tokenIdsToMat(ids.data(), (int)ids.size(), CV_64S);
    ASSERT_EQ(wide.type(), CV_64S);
    ASSERT_EQ(wide.dims, 2);
    EXPECT_EQ(wide.size[0], 1);
    EXPECT_EQ(wide.size[1], 3);
    EXPECT_EQ(wide.ptr<int64_t>()[2], (int64_t)9);

    const Mat narrow = tokenIdsToMat(ids.data(), (int)ids.size(), CV_32S);
    ASSERT_EQ(narrow.type(), CV_32S);
    EXPECT_EQ(narrow.ptr<int>()[2], 9);

    EXPECT_THROW(tokenIdsToMat(ids.data(), 0, CV_64S), cv::Exception);
    EXPECT_THROW(tokenIdsToMat(ids.data(), 3, CV_32F), cv::Exception);
}

// With PaliGemma2's mean/stddev of 0.5 and a 1/255 rescale, black maps to -1 and white to +1.
TEST(DNN_GenAI, PreprocessFixedSizeNormalizesToUnitRange)
{
    VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PALIGEMMA2);
    config.imageSize = Size(4, 4);

    const Mat white(8, 8, CV_8UC3, Scalar::all(255));
    const Mat blob = genai::preprocessFixedSize(white, config);

    ASSERT_EQ(blob.dims, 4);
    EXPECT_EQ(blob.size[0], 1);
    EXPECT_EQ(blob.size[1], 3);
    EXPECT_EQ(blob.size[2], 4);
    EXPECT_EQ(blob.size[3], 4);
    ASSERT_EQ(blob.type(), CV_32F);
    for (size_t i = 0; i < blob.total(); i++)
        ASSERT_NEAR(blob.ptr<float>()[i], 1.f, 1e-5f) << "at " << i;

    const Mat black(8, 8, CV_8UC3, Scalar::all(0));
    const Mat blobBlack = genai::preprocessFixedSize(black, config);
    for (size_t i = 0; i < blobBlack.total(); i++)
        ASSERT_NEAR(blobBlack.ptr<float>()[i], -1.f, 1e-5f) << "at " << i;
}

TEST(DNN_GenAI, PreprocessFixedSizeRejectsBadInput)
{
    const VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PALIGEMMA2);
    EXPECT_THROW(genai::preprocessFixedSize(Mat(), config), cv::Exception);
    EXPECT_THROW(genai::preprocessFixedSize(Mat(8, 8, CV_8UC1, Scalar(0)), config), cv::Exception);

    const Mat image(8, 8, CV_8UC3, Scalar::all(128));
    VLMConfig zeroStd = config;
    zeroStd.stddev = Scalar(0.5, 0.0, 0.5);
    EXPECT_THROW(genai::preprocessFixedSize(image, zeroStd), cv::Exception);

    VLMConfig zeroSize = config;
    zeroSize.imageSize = Size(0, 4);
    EXPECT_THROW(genai::preprocessFixedSize(image, zeroSize), cv::Exception);
}

TEST(DNN_GenAI, GenerateFromEmbeddingsRejectsBadArguments)
{
    Net embedNet, decoderNet;
    const int sizes[] = {1, 2, 4};
    const Mat embeds(3, sizes, CV_32F, Scalar(0));
    VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PALIGEMMA2);

    EXPECT_THROW(generateFromEmbeddings(embedNet, decoderNet, embeds, config, 0), cv::Exception);

    const int flatSizes[] = {2, 4};
    const Mat flat(2, flatSizes, CV_32F, Scalar(0));
    EXPECT_THROW(generateFromEmbeddings(embedNet, decoderNet, flat, config, 8), cv::Exception);

    config.idType = CV_32F;
    EXPECT_THROW(generateFromEmbeddings(embedNet, decoderNet, embeds, config, 8), cv::Exception);
}

TEST(DNN_LLMConfig, Defaults)
{
    LLMConfig config;
    EXPECT_EQ(config.inputIdsName, "input_ids");
    EXPECT_EQ(config.attentionMaskName, "attention_mask");
    EXPECT_TRUE(config.positionIdsName.empty());
    EXPECT_TRUE(config.stopTokenIds.empty());
    EXPECT_EQ(config.bosTokenId, -1);
    EXPECT_TRUE(config.promptPrefix.empty());
    EXPECT_TRUE(config.promptSuffix.empty());
    EXPECT_FALSE(config.useKVCache);
    EXPECT_EQ(config.idType, CV_64S);
    EXPECT_EQ(config.engine, (int)ENGINE_AUTO);
    EXPECT_EQ(config.backend, (int)DNN_BACKEND_DEFAULT);
    EXPECT_EQ(config.target, (int)DNN_TARGET_CPU);
}

// Each preset must match the constants the corresponding samples/dnn script uses.
TEST(DNN_LLMConfig, PresetGpt2)
{
    const LLMConfig config = LLMConfig::defaultConfig(LLM_MODEL_GPT2);
    EXPECT_EQ(config.inputIdsName, "idx");
    EXPECT_TRUE(config.attentionMaskName.empty());
    EXPECT_TRUE(config.positionIdsName.empty());
    EXPECT_EQ(config.stopTokenIds, std::vector<int>({50256}));
    EXPECT_EQ(config.bosTokenId, -1);
    EXPECT_TRUE(config.promptPrefix.empty());
    EXPECT_EQ(config.idType, CV_32S);
    EXPECT_FALSE(config.useKVCache);
}

TEST(DNN_LLMConfig, PresetQwen25)
{
    const LLMConfig config = LLMConfig::defaultConfig(LLM_MODEL_QWEN2_5);
    EXPECT_EQ(config.inputIdsName, "input_ids");
    EXPECT_EQ(config.attentionMaskName, "attention_mask");
    EXPECT_EQ(config.positionIdsName, "position_ids");
    EXPECT_EQ(config.stopTokenIds, std::vector<int>({151645, 151643}));
    EXPECT_EQ(config.bosTokenId, -1);
    EXPECT_EQ(config.promptPrefix, "<|im_start|>user\n");
    EXPECT_EQ(config.promptSuffix, "<|im_end|>\n<|im_start|>assistant\n");
    EXPECT_EQ(config.idType, CV_64S);
    EXPECT_FALSE(config.useKVCache);
}

TEST(DNN_LLMConfig, PresetGemma3)
{
    const LLMConfig config = LLMConfig::defaultConfig(LLM_MODEL_GEMMA3);
    EXPECT_EQ(config.inputIdsName, "input_ids");
    EXPECT_EQ(config.attentionMaskName, "attention_mask");
    EXPECT_TRUE(config.positionIdsName.empty());
    EXPECT_EQ(config.stopTokenIds, std::vector<int>({1, 106}));
    EXPECT_EQ(config.bosTokenId, 2);
    EXPECT_EQ(config.promptPrefix, "<start_of_turn>user\n");
    EXPECT_EQ(config.promptSuffix, "<end_of_turn>\n<start_of_turn>model\n");
    EXPECT_EQ(config.idType, CV_64S);
    EXPECT_FALSE(config.useKVCache);
}

TEST(DNN_LLMConfig, RejectsUnknownModelType)
{
    EXPECT_THROW(LLMConfig::defaultConfig(-1), cv::Exception);
    EXPECT_THROW(LLMConfig::defaultConfig(3), cv::Exception);
}

TEST(DNN_LLMConfig, WriteReadRoundTrip)
{
    LLMConfig written = LLMConfig::defaultConfig(LLM_MODEL_QWEN2_5);
    written.useKVCache = true;
    written.engine = ENGINE_OPENCV;

    FileStorage out(".json", FileStorage::WRITE | FileStorage::MEMORY);
    written.write(out);
    const std::string data = out.releaseAndGetString();

    FileStorage in(data, FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
    ASSERT_TRUE(in.isOpened());
    LLMConfig read;
    read.read(in.root());

    EXPECT_EQ(read.inputIdsName, written.inputIdsName);
    EXPECT_EQ(read.attentionMaskName, written.attentionMaskName);
    EXPECT_EQ(read.positionIdsName, written.positionIdsName);
    EXPECT_EQ(read.stopTokenIds, written.stopTokenIds);
    EXPECT_EQ(read.bosTokenId, written.bosTokenId);
    EXPECT_EQ(read.promptPrefix, written.promptPrefix);
    EXPECT_EQ(read.promptSuffix, written.promptSuffix);
    EXPECT_EQ(read.useKVCache, written.useKVCache);
    EXPECT_EQ(read.idType, written.idType);
    EXPECT_EQ(read.engine, written.engine);
    EXPECT_EQ(read.backend, written.backend);
    EXPECT_EQ(read.target, written.target);
}

TEST(DNN_LLMConfig, ReadOverridesOnlyPresentFields)
{
    FileStorage in("{\"stop_token_ids\": [7, 8], \"bos_token_id\": 5}",
                   FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
    ASSERT_TRUE(in.isOpened());

    LLMConfig config = LLMConfig::defaultConfig(LLM_MODEL_QWEN2_5);
    config.read(in.root());

    EXPECT_EQ(config.stopTokenIds, std::vector<int>({7, 8}));
    EXPECT_EQ(config.bosTokenId, 5);
    EXPECT_EQ(config.promptPrefix, "<|im_start|>user\n");
    EXPECT_EQ(config.positionIdsName, "position_ids");
    EXPECT_EQ(config.idType, CV_64S);
}

TEST(DNN_LLMConfig, ReadRejectsUnsupportedIdType)
{
    FileStorage in("{\"id_type\": 5}",
                   FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
    ASSERT_TRUE(in.isOpened());

    LLMConfig config;
    EXPECT_THROW(config.read(in.root()), cv::Exception);
}

// The argument guards all run before the net is touched, so an empty Net is enough to reach them.
TEST(DNN_GenAI, GenerateFromTokenIdsRejectsBadArguments)
{
    Net net;
    const std::vector<int> ids = {1, 2};

    LLMConfig config = LLMConfig::defaultConfig(LLM_MODEL_QWEN2_5);
    EXPECT_THROW(generateFromTokenIds(net, std::vector<int>(), config, 8), cv::Exception);
    EXPECT_THROW(generateFromTokenIds(net, ids, config, 0), cv::Exception);

    config.idType = CV_32F;
    EXPECT_THROW(generateFromTokenIds(net, ids, config, 8), cv::Exception);

    config = LLMConfig::defaultConfig(LLM_MODEL_QWEN2_5);
    config.inputIdsName.clear();
    EXPECT_THROW(generateFromTokenIds(net, ids, config, 8), cv::Exception);
}

TEST(DNN_LLM, DefaultConstructedHasNoState)
{
    LLM llm;
    EXPECT_THROW(llm.reset(), cv::Exception);
    EXPECT_THROW(llm.lastTokensUsed(), cv::Exception);
    EXPECT_THROW(llm.getTokenizer(), cv::Exception);
    EXPECT_THROW(llm.getConfig(), cv::Exception);
    EXPECT_THROW(llm.getNet(), cv::Exception);
}

TEST(DNN_VLMConfig, Defaults)
{
    VLMConfig config;
    EXPECT_EQ(config.preprocess, (int)VLM_PREPROCESS_FIXED_SIZE);
    EXPECT_EQ(config.merge, (int)VLM_MERGE_SCATTER);
    EXPECT_EQ(config.imageSize, Size(224, 224));
    EXPECT_EQ(config.mean, Scalar::all(0.5));
    EXPECT_EQ(config.stddev, Scalar::all(0.5));
    EXPECT_DOUBLE_EQ(config.rescaleFactor, 1.0 / 255.0);
    EXPECT_EQ(config.imageTokenId, -1);
    EXPECT_FALSE(config.useKVCache);
    EXPECT_EQ(config.idType, CV_64S);
    EXPECT_EQ(config.engine, (int)ENGINE_AUTO);
}

// PaliGemma2 merges by concatenation, so it needs no image placeholder and no image token id.
TEST(DNN_VLMConfig, PresetPaliGemma2)
{
    const VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PALIGEMMA2);
    EXPECT_EQ(config.preprocess, (int)VLM_PREPROCESS_FIXED_SIZE);
    EXPECT_EQ(config.merge, (int)VLM_MERGE_CONCAT);
    EXPECT_EQ(config.imageSize, Size(224, 224));
    EXPECT_EQ(config.stopTokenIds, std::vector<int>({1}));
    EXPECT_EQ(config.imageTokenId, -1);
    EXPECT_TRUE(config.imagePlaceholder.empty());
    EXPECT_TRUE(config.promptPrefix.empty());
    EXPECT_FALSE(config.useKVCache);
    EXPECT_EQ(config.visionNet, "vision_model.onnx");
    EXPECT_EQ(config.embedNet, "embedding.onnx");
    EXPECT_EQ(config.decoderNet, "gemma2_3b.onnx");
}

TEST(DNN_VLMConfig, PresetPaddleOCRVL)
{
    const VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PADDLEOCR_VL);
    EXPECT_EQ(config.preprocess, (int)VLM_PREPROCESS_PATCHIFY);
    EXPECT_EQ(config.merge, (int)VLM_MERGE_SCATTER);
    EXPECT_EQ(config.patchSize, 14);
    EXPECT_EQ(config.mergeSize, 2);
    EXPECT_EQ(config.minPixels, 28 * 28 * 130);
    EXPECT_EQ(config.maxPixels, 28 * 28 * 1280);
    EXPECT_EQ(config.stopTokenIds, std::vector<int>({2}));
    EXPECT_EQ(config.promptPrefix, "<|begin_of_sentence|>User: <|IMAGE_START|>");
    EXPECT_EQ(config.imagePlaceholder, "<|IMAGE_PLACEHOLDER|>");
    EXPECT_EQ(config.promptInfix, "<|IMAGE_END|>");
    EXPECT_EQ(config.promptSuffix, "\nAssistant:\n");
    EXPECT_TRUE(config.useKVCache);
}

TEST(DNN_VLMConfig, PresetGraniteDocling)
{
    const VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_GRANITE_DOCLING);
    EXPECT_EQ(config.preprocess, (int)VLM_PREPROCESS_TILE_GRID);
    EXPECT_EQ(config.merge, (int)VLM_MERGE_SCATTER);
    EXPECT_EQ(config.longestEdge, 1536);
    EXPECT_EQ(config.maxTileEdge, 512);
    EXPECT_EQ(config.imageSeqLen, 64);
    EXPECT_EQ(config.stopTokenIds, std::vector<int>({2}));
    EXPECT_EQ(config.promptPrefix, "<|start_of_role|>user<|end_of_role|>");
    EXPECT_EQ(config.imagePlaceholder, "<image>");
    EXPECT_EQ(config.promptSuffix, "<|end_of_text|>\n<|start_of_role|>assistant<|end_of_role|>");
    EXPECT_TRUE(config.useKVCache);
    EXPECT_EQ(config.decoderNet, "onnx/decoder_model_merged.onnx");
}

TEST(DNN_VLMConfig, RejectsUnknownModelType)
{
    EXPECT_THROW(VLMConfig::defaultConfig(-1), cv::Exception);
    EXPECT_THROW(VLMConfig::defaultConfig(3), cv::Exception);
}

TEST(DNN_VLMConfig, WriteReadRoundTrip)
{
    VLMConfig written = VLMConfig::defaultConfig(VLM_MODEL_GRANITE_DOCLING);
    written.imageTokenId = 100582;
    written.engine = ENGINE_OPENCV;

    FileStorage out(".json", FileStorage::WRITE | FileStorage::MEMORY);
    written.write(out);
    const std::string data = out.releaseAndGetString();

    FileStorage in(data, FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
    ASSERT_TRUE(in.isOpened());
    VLMConfig read;
    read.read(in.root());

    EXPECT_EQ(read.preprocess, written.preprocess);
    EXPECT_EQ(read.merge, written.merge);
    EXPECT_EQ(read.imageSize, written.imageSize);
    EXPECT_EQ(read.longestEdge, written.longestEdge);
    EXPECT_EQ(read.maxTileEdge, written.maxTileEdge);
    EXPECT_EQ(read.imageSeqLen, written.imageSeqLen);
    EXPECT_EQ(read.mean, written.mean);
    EXPECT_EQ(read.stddev, written.stddev);
    EXPECT_DOUBLE_EQ(read.rescaleFactor, written.rescaleFactor);
    EXPECT_EQ(read.imageTokenId, written.imageTokenId);
    EXPECT_EQ(read.stopTokenIds, written.stopTokenIds);
    EXPECT_EQ(read.promptPrefix, written.promptPrefix);
    EXPECT_EQ(read.imagePlaceholder, written.imagePlaceholder);
    EXPECT_EQ(read.promptInfix, written.promptInfix);
    EXPECT_EQ(read.promptSuffix, written.promptSuffix);
    EXPECT_EQ(read.useKVCache, written.useKVCache);
    EXPECT_EQ(read.idType, written.idType);
    EXPECT_EQ(read.visionNet, written.visionNet);
    EXPECT_EQ(read.embedNet, written.embedNet);
    EXPECT_EQ(read.decoderNet, written.decoderNet);
    EXPECT_EQ(read.engine, written.engine);
}

TEST(DNN_VLMConfig, ReadOverridesOnlyPresentFields)
{
    FileStorage in("{\"image_token_id\": 42, \"max_tile_edge\": 256}",
                   FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
    ASSERT_TRUE(in.isOpened());

    VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_GRANITE_DOCLING);
    config.read(in.root());

    EXPECT_EQ(config.imageTokenId, 42);
    EXPECT_EQ(config.maxTileEdge, 256);
    EXPECT_EQ(config.longestEdge, 1536);
    EXPECT_EQ(config.imagePlaceholder, "<image>");
    EXPECT_TRUE(config.useKVCache);
}

TEST(DNN_VLMConfig, ReadRejectsOutOfRangeModes)
{
    VLMConfig config;
    {
        FileStorage in("{\"preprocess\": 7}",
                       FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
        EXPECT_THROW(config.read(in.root()), cv::Exception);
    }
    {
        VLMConfig other;
        FileStorage in("{\"merge\": 5}",
                       FileStorage::READ | FileStorage::MEMORY | FileStorage::FORMAT_JSON);
        EXPECT_THROW(other.read(in.root()), cv::Exception);
    }
}

TEST(DNN_LLM, CreateRejectsEmptyAndMissingPaths)
{
    const LLMConfig config = LLMConfig::defaultConfig(LLM_MODEL_QWEN2_5);
    EXPECT_THROW(LLM::create("", "tokenizer.json", config), cv::Exception);
    EXPECT_THROW(LLM::create("model.onnx", "", config), cv::Exception);
    EXPECT_THROW(LLM::create("/nonexistent/model.onnx", "/nonexistent/config.json", config),
                 cv::Exception);
    EXPECT_THROW(LLM::create(-1, "/nonexistent/model.onnx", "/nonexistent/config.json"),
                 cv::Exception);
}

TEST(DNN_VLM, DefaultConstructedHasNoState)
{
    VLM vlm;
    EXPECT_THROW(vlm.reset(), cv::Exception);
    EXPECT_THROW(vlm.lastTokensUsed(), cv::Exception);
    EXPECT_THROW(vlm.getTokenizer(), cv::Exception);
    EXPECT_THROW(vlm.getConfig(), cv::Exception);
    EXPECT_THROW(vlm.getNets(), cv::Exception);
}

TEST(DNN_VLM, CreateRejectsEmptyAndMissingPaths)
{
    VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PALIGEMMA2);
    EXPECT_THROW(VLM::create("/nonexistent", "", config), cv::Exception);
    EXPECT_THROW(VLM::create("/nonexistent", "/nonexistent/config.json", config), cv::Exception);
    EXPECT_THROW(VLM::create(-1, "/nonexistent", "/nonexistent/config.json"), cv::Exception);

    config.visionNet.clear();
    EXPECT_THROW(VLM::create("/nonexistent", "/nonexistent/config.json", config), cv::Exception);
}

// Empty modelDir must not itself be a failure reason.
TEST(DNN_VLM, CreateAcceptsEmptyModelDirForIndependentNetPaths)
{
    const VLMConfig config = VLMConfig::defaultConfig(VLM_MODEL_PALIGEMMA2);
    try
    {
        VLM::create("", "/nonexistent/tokenizer_config.json", config);
        FAIL() << "expected cv::Exception";
    }
    catch (const cv::Exception& e)
    {
        EXPECT_EQ(std::string(e.what()).find("model directory"), std::string::npos);
    }
}

}} // namespace
