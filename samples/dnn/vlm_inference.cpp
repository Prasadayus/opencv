// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

/*
Run image-conditioned text generation with cv::dnn::VLM, covering the PaliGemma2, PaddleOCR-VL
and GraniteDocling presets. See the model-specific export instructions in vlm_inference.py.

To run:
    ./example_dnn_vlm_inference --model_type=paligemma2 --model_dir=/path/to/paligemma2 \
        --tokenizer=/path/to/paligemma2/config.json --input=cat.jpg --prompt="cap en\n"

If the three ONNX graphs are not siblings under one directory, override their paths
individually with --vision_net/--embedding_net/--decoder_net (each may be absolute, or relative to
model_dir) and pass --model_dir="" if none of them share a common directory at all.

For a model of the same architecture family with different stop ids, image_token_id, etc., pass
--config=/path/to/a/small/json/file to override individual VLMConfig fields on top of the chosen
preset, e.g. {"stop_token_ids": [32000], "image_token_id": 100582}.
*/

#include <iostream>
#include <map>

#include <opencv2/dnn.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace cv;
using namespace cv::dnn;
using namespace std;

const string param_keys =
    "{ help    h |     | Print help message. }"
    "{ model_type|     | Which preset to use: paligemma2, paddleocr-vl, or granite-docling"
                        " (required). }"
    "{ model_dir |     | Directory holding the model's ONNX graphs (required). }"
    "{ tokenizer |     | Path to the OpenCV tokenizer config.json (required). }"
    "{ input   i |     | Path to the input image (required). }"
    "{ prompt    |     | Task prompt (defaults to English captioning; PaliGemma2-specific). }"
    "{ max_new_tokens | 512 | Maximum number of new tokens to generate. }"
    "{ vision_net    |     | Override the preset vision-encoder path. }"
    "{ embedding_net |     | Override the preset embedding-net path. }"
    "{ decoder_net   |     | Override the preset decoder path. }"
    "{ config        |     | Optional JSON file overriding VLMConfig fields on top of the"
                             " preset. }";

int main(int argc, char** argv)
{
    CommandLineParser parser(argc, argv, param_keys);
    parser.about("Run image-conditioned text generation with cv::dnn::VLM");

    if (parser.has("help") || !parser.has("model_type") || !parser.has("model_dir")
        || !parser.has("tokenizer") || !parser.has("input"))
    {
        parser.printMessage();
        return 0;
    }

    static const map<string, VLMModelType> MODEL_TYPES = {
        {"paligemma2",      VLM_MODEL_PALIGEMMA2},
        {"paddleocr-vl",    VLM_MODEL_PADDLEOCR_VL},
        {"granite-docling", VLM_MODEL_GRANITE_DOCLING},
    };

    const string modelTypeArg = parser.get<String>("model_type");
    auto it = MODEL_TYPES.find(modelTypeArg);
    if (it == MODEL_TYPES.end())
    {
        cerr << "Unknown model_type: " << modelTypeArg
             << " (expected paligemma2, paddleocr-vl, or granite-docling)" << endl;
        return 1;
    }

    const string modelDir = parser.get<String>("model_dir");
    const string tokenizerPath = parser.get<String>("tokenizer");
    const string inputPath = parser.get<String>("input");
    const string promptArg = parser.get<String>("prompt");
    const string prompt = promptArg.empty() ? "cap en\n" : promptArg;
    const int maxNewTokens = parser.get<int>("max_new_tokens");

    Mat image = imread(inputPath);
    if (image.empty())
    {
        cerr << "Could not read input image: " << inputPath << endl;
        return 1;
    }

    cout << "Preparing " << modelTypeArg << " model..." << endl;
    VLMConfig config = VLMConfig::defaultConfig(it->second);
    if (parser.has("config"))
    {
        const String configPath = parser.get<String>("config");
        FileStorage fs(configPath, FileStorage::READ | FileStorage::FORMAT_JSON);
        if (!fs.isOpened())
        {
            cerr << "Could not open --config file: " << configPath << endl;
            return 1;
        }
        config.read(fs.root());
    }
    if (parser.has("vision_net"))
        config.visionNet = parser.get<String>("vision_net");
    if (parser.has("embedding_net"))
        config.embedNet = parser.get<String>("embedding_net");
    if (parser.has("decoder_net"))
        config.decoderNet = parser.get<String>("decoder_net");
    VLM vlm = VLM::create(modelDir, tokenizerPath, config);

    cout << "Response:\n" << vlm.generate(image, prompt, maxNewTokens) << endl;

    return 0;
}
