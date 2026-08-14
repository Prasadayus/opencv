// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright (C) 2026, BigVision LLC, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "test_precomp.hpp"
#include <opencv2/dnn/shape_utils.hpp>
#include <opencv2/dnn/all_layers.hpp>

namespace opencv_test { namespace {

TEST(SkipSimplifiedLayerNormalizationLayer, RequiresAtLeastThreeInputs)
{
    LayerParams lp;
    lp.type = "SkipSimplifiedLayerNormalization";
    lp.name = "test_skip_norm";
    lp.set("epsilon", 1e-5f);
    Ptr<Layer> layer = LayerFactory::createLayerInstance("SkipSimplifiedLayerNormalization", lp);
    CV_Assert(layer);

    std::vector<MatShape> inputs = { MatShape({1, 1, 4}), MatShape({1, 1, 4}) };
    std::vector<MatShape> outputs, internals;
    EXPECT_ANY_THROW(layer->getMemoryShapes(inputs, 2, outputs, internals));
}

}} // namespace opencv_test::(anonymous)
