// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

#include "../precomp.hpp"
#include "layers_common.hpp"

namespace cv {
namespace dnn {

class BatchNorm2LayerImpl CV_FINAL : public BatchNorm2Layer {
public:
    BatchNorm2LayerImpl(const LayerParams& params) {
        setParamsFrom(params);
      
        epsilon = params.get<float>("epsilon", params.get<float>("eps", 1e-5f));
        useGlobalStats = params.get<bool>("use_global_stats", true);
        hasWeights = params.get<bool>("has_weight", false);
        hasBias = params.get<bool>("has_bias", false);

        if (blobs.size() >= 4) {
            dynamicInputs = false;
            
            Mat mean = blobs[0];
            Mat var = blobs[1];
            Mat scale = blobs[2];
            Mat bias = blobs[3];
            
            weights_.create(scale.size(), CV_32F);
            bias_.create(scale.size(), CV_32F);
            
            float* wData = weights_.ptr<float>();
            float* bData = bias_.ptr<float>();
            const float* sData = scale.ptr<float>();
            const float* vData = var.ptr<float>();
            const float* mData = mean.ptr<float>();
            const float* betaData = bias.ptr<float>();
            
            size_t n = scale.total();
            for (size_t i = 0; i < n; ++i) {
                float w = sData[i] / std::sqrt(vData[i] + epsilon);
                wData[i] = w;
                bData[i] = betaData[i] - mData[i] * w;
            }
        } else {
            dynamicInputs = true;
        }
    }

    virtual bool supportBackend(int backendId) CV_OVERRIDE {
        return backendId == DNN_BACKEND_OPENCV;
    }

    virtual bool dynamicOutputShapes() const CV_OVERRIDE {
        return dynamicInputs;
    }

    virtual bool getMemoryShapes(const std::vector<MatShape>& inputs,
                                 const int requiredOutputs,
                                 std::vector<MatShape>& outputs,
                                 std::vector<MatShape>& internals) const CV_OVERRIDE
    {
        CV_Assert(!inputs.empty());
        outputs.assign(requiredOutputs, inputs[0]);
        return false;
    }

    virtual void getTypes(const std::vector<MatType>& inputs,
                          const int requiredOutputs,
                          const int requiredInternals,
                          std::vector<MatType>& outputs,
                          std::vector<MatType>& internals) const CV_OVERRIDE
    {
        CV_Assert(!inputs.empty());
        outputs.assign(requiredOutputs, inputs[0]);
    }

    void forward(InputArrayOfArrays inputs_arr, OutputArrayOfArrays outputs_arr, OutputArrayOfArrays internals_arr) CV_OVERRIDE {
        CV_TRACE_FUNCTION();

        if (inputs_arr.depth() == CV_16F)
        {
            forward_fallback(inputs_arr, outputs_arr, internals_arr);
            return;
        }

        std::vector<Mat> inputs;
        inputs_arr.getMatVector(inputs);

        const Mat &X = inputs[0];
        Mat Y;
        Mat w, b;
        
        if (dynamicInputs) {
            CV_Assert(inputs.size() == 5);
            
            Mat scale = inputs[1];
            Mat bias_in = inputs[2];
            Mat mean = inputs[3];
            Mat var = inputs[4];
            
            w.create(scale.size(), CV_32F);
            b.create(scale.size(), CV_32F);
            
            if (scale.isContinuous() && var.isContinuous() && mean.isContinuous() && bias_in.isContinuous()) {
                const float* sData = scale.ptr<float>();
                const float* vData = var.ptr<float>();
                const float* mData = mean.ptr<float>();
                const float* betaData = bias_in.ptr<float>();
                float* wData = w.ptr<float>();
                float* bData = b.ptr<float>();
                
                size_t n = scale.total();
                for (size_t i = 0; i < n; ++i) {
                    float val = sData[i] / std::sqrt(vData[i] + epsilon);
                    wData[i] = val;
                    bData[i] = betaData[i] - mData[i] * val;
                }
            } else {
                 CV_Error(Error::StsNotImplemented, "Dynamic BN inputs must be continuous.");
            }
        } else {
            w = weights_;
            b = bias_;
        }
        
        if (w.empty() || b.empty())
             CV_Error(Error::StsBadArg, "BatchNorm2Layer: Weights not initialized");

        MatShape outShape = shape(X);
        auto kind = outputs_arr.kind();
        if (kind == _InputArray::STD_VECTOR_MAT) {
            std::vector<Mat>& outs = outputs_arr.getMatVecRef();
            CV_Assert(outs.size() >= 1);
            outs[0].fit(outShape, X.type());
            Y = outs[0];
        } else if (kind == _InputArray::STD_VECTOR_UMAT) {
            std::vector<UMat>& uouts = outputs_arr.getUMatVecRef();
            CV_Assert(uouts.size() >= 1);
            uouts[0].fit(outShape, X.type());
            Y = uouts[0].getMat(ACCESS_WRITE);
        } else {
            CV_Error(Error::StsBadArg, "Unsupported output array kind");
        }

        const int C = (X.dims >= 2) ? X.size[1] : 1;
        const int N = X.size[0];
        const size_t planeSize = X.total() / (N * C);
        
        CV_Assert(w.total() == C);

        parallel_for_(Range(0, N * C), [&](const Range& r) {
            for (int i = r.start; i < r.end; ++i) {
                int c = i % C;
                
                float scale_val = w.ptr<float>()[c];
                float shift_val = b.ptr<float>()[c];
                
                const float* srcPtr = X.ptr<float>() + i * planeSize;
                float* dstPtr = Y.ptr<float>() + i * planeSize;
                
                int j = 0;
#if CV_SIMD128
                v_float32x4 v_scale = v_setall_f32(scale_val);
                v_float32x4 v_shift = v_setall_f32(shift_val);
                for (; j <= (int)planeSize - 4; j += 4) {
                    v_float32x4 v_src = v_load(srcPtr + j);
                    v_float32x4 v_dst = v_muladd(v_src, v_scale, v_shift);
                    v_store(dstPtr + j, v_dst);
                }
#endif
                for (; j < (int)planeSize; ++j) {
                    dstPtr[j] = srcPtr[j] * scale_val + shift_val;
                }
            }
        });
    }
};

Ptr<BatchNorm2Layer> BatchNorm2Layer::create(const LayerParams& params) {
    return makePtr<BatchNorm2LayerImpl>(params);
}

}} // namespace cv::dnn