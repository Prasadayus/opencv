# This file is part of OpenCV project.
# It is subject to the license terms in the LICENSE file found in the top-level directory
# of this distribution and at http://opencv.org/license.html.
# Copyright (C) 2026, BigVision LLC, all rights reserved.
# Third party copyrights are property of their respective owners.

'''
This is a sample script to run image-conditioned text generation in OpenCV using cv.dnn.VLM,
covering the PaliGemma2, PaddleOCR-VL and GraniteDocling presets.

Model: https://huggingface.co/google/paligemma2-3b-pt-224
ONNX:  https://huggingface.co/nklskyoy/paligemma2-3b-pt-224-onnx

Run the script:

    python vlm_inference.py --model_type=paligemma2 --model_dir=<dir-with-the-onnx-files> \
        --tokenizer=<path-to-opencv-tokenizer-config.json> --input=<path-to-image> \
        --prompt="cap en\n"

    The tokenizer path should point to an OpenCV-format config.json, NOT the
    HuggingFace tokenizer_config.json.

    If the three ONNX graphs are not siblings under one directory, override their paths
    individually with --vision_net/--embedding_net/--decoder_net (each may be absolute, or relative
    to model_dir) and pass --model_dir="" if none of them share a common directory at all.

    For a model of the same architecture family with different stop ids, image_token_id, etc.,
    pass --config=<path-to-a-small-json-file> to override individual VLMConfig fields on top of
    the chosen preset, e.g. {"stop_token_ids": [32000], "image_token_id": 100582}.
'''

import argparse
import cv2 as cv

MODEL_TYPES = {
    'paligemma2': cv.dnn.VLM_MODEL_PALIGEMMA2,
    'paddleocr-vl': cv.dnn.VLM_MODEL_PADDLEOCR_VL,
    'granite-docling': cv.dnn.VLM_MODEL_GRANITE_DOCLING,
}

def parse_args():
    parser = argparse.ArgumentParser(
        description='Run image-conditioned text generation in OpenCV with cv.dnn.VLM',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--model_type', type=str, required=True, choices=sorted(MODEL_TYPES),
                        help='Which preset to use.')
    parser.add_argument('--model_dir', type=str, required=True,
                        help="Directory holding the model's ONNX graphs.")
    parser.add_argument('--tokenizer', type=str, required=True,
                        help='Path to the OpenCV tokenizer config.json.')
    parser.add_argument('--input', '-i', type=str, required=True, help='Path to the input image.')
    parser.add_argument('--prompt', type=str, default='cap en\n',
                        help='Task prompt (default captions in English; PaliGemma2-specific).')
    parser.add_argument('--max_new_tokens', type=int, default=512,
                        help='Maximum number of new tokens to generate.')
    parser.add_argument('--vision_net', type=str, default='',
                        help='Override the preset vision-encoder path.')
    parser.add_argument('--embedding_net', type=str, default='',
                        help='Override the preset embedding-net path.')
    parser.add_argument('--decoder_net', type=str, default='',
                        help='Override the preset decoder path.')
    parser.add_argument('--config', type=str, default='',
                        help='Optional JSON file overriding VLMConfig fields on top of the preset.')
    return parser.parse_args()

if __name__ == '__main__':

    args = parse_args()

    image = cv.imread(args.input)
    if image is None:
        raise IOError('Could not read input image: ' + args.input)

    print(f'Preparing {args.model_type} model...')
    config = cv.dnn.VLMConfig.defaultConfig(MODEL_TYPES[args.model_type])
    if args.config:
        fs = cv.FileStorage(args.config, cv.FileStorage_READ | cv.FileStorage_FORMAT_JSON)
        if not fs.isOpened():
            raise IOError('Could not open --config file: ' + args.config)
        config.read(fs.root())
    if args.vision_net:
        config.visionNet = args.vision_net
    if args.embedding_net:
        config.embedNet = args.embedding_net
    if args.decoder_net:
        config.decoderNet = args.decoder_net
    vlm = cv.dnn.VLM.create(args.model_dir, args.tokenizer, config)

    response = vlm.generate(image, args.prompt, args.max_new_tokens)
    print(f'Response:\n{response}')
