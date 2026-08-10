# This file is part of OpenCV project.
# It is subject to the license terms in the LICENSE file found in the top-level directory
# of this distribution and at http://opencv.org/license.html.
# Copyright (C) 2026, BigVision LLC, all rights reserved.
# Third party copyrights are property of their respective owners.

'''
This is a sample script to run text generation in OpenCV using cv.dnn.LLM, covering the
GPT-2, Qwen2.5 and Gemma3 presets.

Exporting a model to ONNX:

    GPT-2 (fixed export window; --prompt must be the same length used at export time):

        git clone -b fix-dynamic-axis-export https://github.com/nklskyoy/build-nanogpt
        pip install -r requirements.txt
        python export2onnx.py --promt=<same-prompt-you-will-pass-to-this-script>

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

Run the script:

    python llm_inference.py --model_type=<gpt2|qwen2.5|gemma3> --model=<path-to-the-onnx-model> \
        --tokenizer=<path-to-the-model's-config.json> --prompt="What is OpenCV?" \
        --max_new_tokens=64

    For a model of the same architecture family with different stop ids, chat template, etc.,
    pass --config=<path-to-a-small-json-file> to override individual LLMConfig fields on top of
    the chosen preset, e.g. {"stop_token_ids": [32000], "prompt_suffix": "..."}.
'''

import argparse
import cv2 as cv

MODEL_TYPES = {
    'gpt2': cv.dnn.LLM_MODEL_GPT2,
    'qwen2.5': cv.dnn.LLM_MODEL_QWEN2_5,
    'gemma3': cv.dnn.LLM_MODEL_GEMMA3,
}

def parse_args():
    parser = argparse.ArgumentParser(description='Run text generation in OpenCV with cv.dnn.LLM',
                                    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--model_type', type=str, required=True, choices=sorted(MODEL_TYPES),
                        help='Which preset to use.')
    parser.add_argument('--model', type=str, required=True, help='Path to the ONNX model file.')
    parser.add_argument('--tokenizer', type=str, required=True,
                        help='Path to the OpenCV tokenizer config.json.')
    parser.add_argument('--prompt', type=str, default='What is OpenCV?', help='User prompt.')
    parser.add_argument('--max_new_tokens', type=int, default=64,
                        help='Maximum number of new tokens to generate.')
    parser.add_argument('--config', type=str, default='',
                        help='Optional JSON file overriding LLMConfig fields on top of the preset.')
    return parser.parse_args()

if __name__ == '__main__':

    args = parse_args()

    print(f'Preparing {args.model_type} model...')
    config = cv.dnn.LLMConfig.defaultConfig(MODEL_TYPES[args.model_type])
    if args.config:
        fs = cv.FileStorage(args.config, cv.FileStorage_READ | cv.FileStorage_FORMAT_JSON)
        if not fs.isOpened():
            raise IOError('Could not open --config file: ' + args.config)
        config.read(fs.root())
    llm = cv.dnn.LLM.create(args.model, args.tokenizer, config)

    print(f'Prompt:\n{args.prompt}')
    print(f'Response:\n{llm.generate(args.prompt, args.max_new_tokens)}')
