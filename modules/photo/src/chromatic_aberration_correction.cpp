// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html

#include "precomp.hpp"
#include "opencv2/core/hal/intrin.hpp"

namespace cv {

CV_EXPORTS_W void loadChromaticAberrationParams(const FileNode& fs,
                                                OutputArray coeffMat,
                                                Size& calib_size,
                                                int& degree)
{
    int imgW = 0, imgH = 0;
    fs["image_width"]  >> imgW;
    fs["image_height"] >> imgH;

    CV_Assert(imgW > 0 && imgH > 0);

    auto readChannel = [&](const char* key,
                            std::vector<double>& coeffs_x,
                           std::vector<double>& coeffs_y,
                           int& deg_out)
    {
        FileNode ch = fs[key];
        if (ch.empty())
            CV_Error_(Error::StsParseError,
                        ("Missing channel \"%s\"", key));

        ch["coeffs_x"] >> coeffs_x;
        ch["coeffs_y"] >> coeffs_y;

        if (coeffs_x.empty() || coeffs_y.empty())
            CV_Error_(Error::StsParseError,
                        ("%s: coeffs_x/coeffs_y missing", key));

        if (coeffs_x.size() != coeffs_y.size())
            CV_Error_(Error::StsBadSize,
                        ("%s: coeffs_x (%zu) vs coeffs_y (%zu)",
                        key, coeffs_x.size(), coeffs_y.size()));

        if (!checkRange(coeffs_x, true) || !checkRange(coeffs_y, true))
            CV_Error_(Error::StsBadArg,
                      ("%s: coefficient array contains NaN/Inf", key));
        size_t m = coeffs_x.size();
        double n_float = (std::sqrt(1.0 + 8.0 * m) - 3.0) / 2.0;
        int deg = static_cast<int>(std::round(n_float));
        size_t expected_m = static_cast<size_t>((deg + 1) * (deg + 2) / 2);
        if (m != expected_m){
            CV_Error_(Error::StsBadArg,
                    ("Coefficient count %zu is not triangular for degree %d "
                    "(expected %zu)", m, deg, expected_m));
        }
        deg_out = deg;
    };

    std::vector<double> red_x, red_y, blue_x, blue_y;
    int deg_red = 0, deg_blue = 0;
    readChannel("red_channel",  red_x,  red_y,  deg_red);
    readChannel("blue_channel", blue_x, blue_y, deg_blue);


    if (red_x.size() != blue_x.size()){
        CV_Error_(Error::StsBadSize,
                    ("Red (%zu) and blue (%zu) coefficient counts differ",
                    red_x.size(), blue_x.size()));
    }
    if (deg_red != deg_blue){
        CV_Error_(Error::StsBadArg,
                    ("Red (%d) and blue (%d) degrees differ",
                    deg_red, deg_blue));
    }

    const int mterms = (int)red_x.size();

    Mat tmp(4, mterms, CV_32F);

    float* Bx = tmp.ptr<float>(0);
    float* By = tmp.ptr<float>(1);
    float* Rx = tmp.ptr<float>(2);
    float* Ry = tmp.ptr<float>(3);

    for (int i = 0; i < mterms; ++i) {
        Bx[i] = static_cast<float>(blue_x[i]);
        By[i] = static_cast<float>(blue_y[i]);
        Rx[i] = static_cast<float>(red_x[i]);
        Ry[i] = static_cast<float>(red_y[i]);
    }

    calib_size = Size(imgW, imgH);
    degree = deg_red;
    tmp.copyTo(coeffMat);
}

static void buildRemapsFromCoeffMat(int height, int width,
                             const Mat& coeffs,
                             int degree,
                             int rowX, int rowY,
                             Mat& map_x, Mat& map_y)
{
    if (coeffs.type() != CV_32F) {
        CV_Error_(Error::StsUnsupportedFormat,
                  ("coeffs Mat must be CV_32F (got type=%d)", coeffs.type()));
    }

    if (coeffs.rows != 4) {
        CV_Error_(Error::StsBadSize,
                  ("coeffs.rows must be 4 (Bx,By,Rx,Ry); got %d", coeffs.rows));
    }

    if (rowX < 0 || rowX >= coeffs.rows) {
        CV_Error_(Error::StsOutOfRange,
                  ("rowX index %d out of range [0,%d)", rowX, coeffs.rows));
    }
    if (rowY < 0 || rowY >= coeffs.rows) {
        CV_Error_(Error::StsOutOfRange,
                  ("rowY index %d out of range [0,%d)", rowY, coeffs.rows));
    }

    if (degree < 0) {
        CV_Error_(Error::StsBadArg,
                  ("degree must be non-negative; got %d", degree));
    }

    const int expected_terms = (degree + 1) * (degree + 2) / 2;
    if (coeffs.cols != expected_terms) {
        CV_Error_(Error::StsBadSize,
                  ("coeffs.cols (%d) != expected polynomial term count (%d) for degree=%d",
                   coeffs.cols, expected_terms, degree));
    }

    if (width <= 0 || height <= 0) {
        CV_Error_(Error::StsBadArg,
                  ("width (%d) and height (%d) must be positive", width, height));
    }

    map_x.create(height, width, CV_32F);
    map_y.create(height, width, CV_32F);

    const double mean_x    = width  * 0.5;
    const double mean_y    = height * 0.5;
    const double inv_std_x = 1.0 / mean_x;
    const double inv_std_y = 1.0 / mean_y;

    const float* Cx = coeffs.ptr<float>(rowX);
    const float* Cy = coeffs.ptr<float>(rowY);


    // fixed-width f64 lanes only; the power table is held in registers, so a scalable backend
    // cannot size it at compile time and keeps the scalar path
#if CV_SIMD_64F
    const int MAX_VEC_DEGREE = 15;
    const bool useVec = degree <= MAX_VEC_DEGREE;
#endif

    parallel_for_(Range(0, height), [&](const Range& rows){
        std::vector<double> x_pow(degree + 1);
        std::vector<double> y_pow(degree + 1);
#if CV_SIMD_64F
        v_float64 vx_pow[MAX_VEC_DEGREE + 1];
        double lane_x[VTraits<v_float64>::max_nlanes];
        double lane_dx[VTraits<v_float64>::max_nlanes], lane_dy[VTraits<v_float64>::max_nlanes];
        const int vlanes = VTraits<v_float64>::vlanes();
#endif
        for (int y = rows.start; y < rows.end; ++y) {
            // the grids held nothing but the loop indices, so the coordinate, the offset and the
            // final subtraction all stay in registers instead of six full-size float buffers
            float* MX = map_x.ptr<float>(y);
            float* MY = map_y.ptr<float>(y);
            const double yn = ((float)y - mean_y) * inv_std_y;

            // y_pow is constant along the row
            y_pow[0] = 1.0;
            for (int k = 1; k <= degree; ++k)
                y_pow[k] = y_pow[k-1] * yn;

            int x = 0;
#if CV_SIMD_64F
            if (useVec)
            {
                // lanes are consecutive pixels, so each one accumulates the terms in exactly the
                // order the scalar loop does and the result is unchanged
                for (; x <= width - vlanes; x += vlanes)
                {
                    for (int k = 0; k < vlanes; ++k)
                        lane_x[k] = (double)(x + k);
                    const v_float64 vxn = v_mul(v_sub(vx_load(lane_x), vx_setall_f64(mean_x)),
                                                vx_setall_f64(inv_std_x));
                    vx_pow[0] = vx_setall_f64(1.0);
                    for (int k = 1; k <= degree; ++k)
                        vx_pow[k] = v_mul(vx_pow[k-1], vxn);

                    v_float64 vdx = vx_setzero_f64(), vdy = vx_setzero_f64();
                    int vidx = 0;
                    for (int t = 0; t <= degree; ++t){
                        for (int i = 0; i <= t; ++i){
                            const v_float64 term = v_mul(vx_pow[i], vx_setall_f64(y_pow[t - i]));
                            vdx = v_add(vdx, v_mul(vx_setall_f64(Cx[vidx]), term));
                            vdy = v_add(vdy, v_mul(vx_setall_f64(Cy[vidx]), term));
                            ++vidx;
                        }
                    }
                    v_store(lane_dx, vdx);
                    v_store(lane_dy, vdy);
                    for (int k = 0; k < vlanes; ++k) {
                        MX[x + k] = (float)(x + k) - (float)lane_dx[k];
                        MY[x + k] = (float)y - (float)lane_dy[k];
                    }
                }
            }
#endif
            for (; x < width; ++x) {
                const double xn = ((float)x - mean_x) * inv_std_x;

                x_pow[0] = 1.0;
                for (int k = 1; k <= degree; ++k)
                    x_pow[k] = x_pow[k-1] * xn;

                double dxv = 0.0, dyv = 0.0;
                int idx = 0;
                for (int t = 0; t <= degree; ++t){
                    for (int i = 0; i <= t; ++i){
                        const int j = t - i;
                        const double term = x_pow[i] * y_pow[j];
                        dxv += Cx[idx] * term;
                        dyv += Cy[idx] * term;
                        ++idx;
                    }
                }

                MX[x] = (float)x - (float)dxv;
                MY[x] = (float)y - (float)dyv;
            }
        }
    });
}

void correctChromaticAberration(InputArray input_image,
                                InputArray coefficients,
                                OutputArray output_image,
                                const Size& calib_size,
                                int calib_degree,
                                int bayer_pattern)
{
    Mat image = input_image.getMat();
    const Mat coeffMat = coefficients.getMat();

    if (image.channels() == 1) {
        if (bayer_pattern < 0) {
            CV_Error_(Error::StsBadArg,
                      ("Single-channel input detected: must pass a valid bayer_pattern"));
        }
        Mat dem;
        demosaicing(image, dem, bayer_pattern);
        image = dem;
    }

    const int height = image.rows;
    const int width  = image.cols;

    if (height != calib_size.height || width != calib_size.width) {
        CV_Error_(Error::StsBadArg,
                  ("Image size %dx%d does not match calibration %dx%d",
                   width, height, calib_size.width, calib_size.height));
    }

    std::vector<Mat> channels;
    split(image, channels);
    Mat b = channels[0], g = channels[1], r = channels[2];

    Mat map_x_r, map_y_r, map_x_b, map_y_b;
    buildRemapsFromCoeffMat(height, width, coeffMat, calib_degree, 2, 3, map_x_r, map_y_r);
    buildRemapsFromCoeffMat(height, width, coeffMat, calib_degree, 0, 1, map_x_b, map_y_b);

    Mat r_corr, b_corr;
    remap(r, r_corr, map_x_r, map_y_r, INTER_LINEAR, BORDER_REPLICATE);
    remap(b, b_corr, map_x_b, map_y_b, INTER_LINEAR, BORDER_REPLICATE);

    std::vector<Mat> corrected_channels = {b_corr, g, r_corr};
    Mat corrected_image;
    merge(corrected_channels, output_image);
}
}
