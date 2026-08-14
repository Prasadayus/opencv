// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

/*
Run text generation with cv::dnn::LLM, covering the GPT-2, Qwen2.5 and Gemma3 presets.

Exporting a model to ONNX:

    GPT-2 (fixed export window; --prompt must be the same length used at export time):

        git clone -b fix-dynamic-axis-export https://github.com/nklskyoy/build-nanogpt
        pip install -r requirements.txt
        python export2onnx.py --promt=<same-prompt-you-will-pass-to-this-sample>

    Qwen2.5 (https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct):

        pip install optimum[exporters] optimum-onnx[onnxruntime] torch transformers

        Without KV-cache:

            optimum-cli export onnx --model Qwen/Qwen2.5-0.5B-Instruct \
                --task causal-lm qwen2.5_instruct_onnx/

        With KV-cache (recommended, faster autoregressive inference):

            optimum-cli export onnx --model Qwen/Qwen2.5-0.5B-Instruct \
                --task causal-lm-with-past qwen2.5_instruct_onnx_with_past/

    Gemma3 (https://huggingface.co/google/gemma-3-1b-it):

        pip install optimum[exporters] optimum-onnx[onnxruntime] torch transformers

        Without KV-cache:

            optimum-cli export onnx --model google/gemma-3-1b-it \
                --task causal-lm gemma3_instruct_onnx/

        With KV-cache (recommended, faster autoregressive inference):

            optimum-cli export onnx --model google/gemma-3-1b-it \
                --task causal-lm-with-past gemma3_instruct_onnx_with_past/

To run:
    ./example_dnn_llm_inference --model_type=<gpt2|qwen2.5|gemma3> --model=/path/to/the/onnx/model \
        --tokenizer=/path/to/the/model's/config.json --prompt="What is OpenCV?" --max_new_tokens=64

For a model of the same architecture family with different stop ids, chat template, etc., pass
--config=/path/to/a/small/json/file to override individual LLMConfig fields on top of the chosen
preset, e.g. {"stop_token_ids": [32000], "prompt_suffix": "..."}.
*/

#include <iostream>
#include <map>

#include <opencv2/dnn.hpp>

using namespace cv;
using namespace cv::dnn;
using namespace std;

const string param_keys =
    "{ help    h |          | Print help message. }"
    "{ model_type|          | Which preset to use: gpt2, qwen2.5, or gemma3 (required). }"
    "{ model     |          | Path to the ONNX model file (required). }"
    "{ tokenizer |          | Path to the OpenCV tokenizer config.json (required). }"
    "{ prompt    | What is OpenCV? | User prompt text. }"
    "{ max_new_tokens | 64  | Maximum number of new tokens to generate. }"
    "{ config    |          | Optional JSON file overriding LLMConfig fields on top of the"
                             " preset. }";

int main(int argc, char** argv)
{
    CommandLineParser parser(argc, argv, param_keys);
    parser.about("Run text generation with cv::dnn::LLM");

    if (parser.has("help") || !parser.has("model_type") || !parser.has("model")
        || !parser.has("tokenizer"))
    {
        parser.printMessage();
        return 0;
    }

    static const map<string, LLMModelType> MODEL_TYPES = {
        {"gpt2",    LLM_MODEL_GPT2},
        {"qwen2.5", LLM_MODEL_QWEN2_5},
        {"gemma3",  LLM_MODEL_GEMMA3},
    };

    const string modelTypeArg = parser.get<String>("model_type");
    auto it = MODEL_TYPES.find(modelTypeArg);
    if (it == MODEL_TYPES.end())
    {
        cerr << "Unknown model_type: " << modelTypeArg << " (expected gpt2, qwen2.5, or gemma3)"
             << endl;
        return 1;
    }

    const string modelPath = parser.get<String>("model");
    const string tokenizerPath = parser.get<String>("tokenizer");
    const string prompt = parser.get<String>("prompt");
    const int maxNewTokens = parser.get<int>("max_new_tokens");

    cout << "Preparing " << modelTypeArg << " model..." << endl;
    LLMConfig config = LLMConfig::defaultConfig(it->second);
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
    LLM llm = LLM::create(modelPath, tokenizerPath, config);

    cout << "Prompt:\n" << prompt << endl;
    cout << "Response:\n" << llm.generate(prompt, maxNewTokens) << endl;

    return 0;
}
