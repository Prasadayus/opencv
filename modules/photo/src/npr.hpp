/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include "opencv2/photo.hpp"
#include <iostream>
#include <stdlib.h>
#include <limits>
#include <math.h>


using namespace std;
using namespace cv;

double myinf = std::numeric_limits<double>::infinity();

class Domain_Filter
{
    public:
        Mat ct_H, ct_V, horiz, vert, O, O_t, lower_idx, upper_idx;
        void init(const Mat &img, int flags, float sigma_s, float sigma_r);
        void getGradientx( const Mat &img, Mat &gx);
        void getGradienty( const Mat &img, Mat &gy);
        void diffx(const Mat &img, Mat &temp);
        void diffy(const Mat &img, Mat &temp);
        void find_magnitude(Mat &img, Mat &mag);
        void compute_boxfilter(Mat &output, Mat &hz, Mat &psketch, float radius);
        void compute_Rfilter(Mat &O, Mat &horiz, float sigma_h);
        void compute_NCfilter(Mat &O, Mat &horiz, Mat &psketch, float radius);
        void filter(const Mat &img, Mat &res, float sigma_s, float sigma_r, int flags);
        void pencil_sketch(const Mat &img, Mat &sketch, Mat &color_res, float sigma_s, float sigma_r, float shade_factor);
        void Depth_of_field(const Mat &img, Mat &img1, float sigma_s, float sigma_r);
};

void Domain_Filter::diffx(const Mat &img, Mat &temp)
{
    int channel = img.channels();

    for(int i = 0; i < img.size().height; i++)
        for(int j = 0; j < img.size().width-1; j++)
        {
            for(int c =0; c < channel; c++)
            {
                temp.at<float>(i,j*channel+c) =
                    img.at<float>(i,(j+1)*channel+c) - img.at<float>(i,j*channel+c);
            }
        }
}

void Domain_Filter::diffy(const Mat &img, Mat &temp)
{
    int channel = img.channels();

    for(int i = 0; i < img.size().height-1; i++)
        for(int j = 0; j < img.size().width; j++)
        {
            for(int c =0; c < channel; c++)
            {
                temp.at<float>(i,j*channel+c) =
                    img.at<float>((i+1),j*channel+c) - img.at<float>(i,j*channel+c);
            }
        }
}

void Domain_Filter::getGradientx( const Mat &img, Mat &gx)
{
    int w = img.cols;
    int h = img.rows;
    int channel = img.channels();

    for(int i=0;i<h;i++)
        for(int j=0;j<w;j++)
            for(int c=0;c<channel;++c)
            {
                gx.at<float>(i,j*channel+c) =
                    img.at<float>(i,(j+1)*channel+c) - img.at<float>(i,j*channel+c);
            }
}

void Domain_Filter::getGradienty( const Mat &img, Mat &gy)
{
    int w = img.cols;
    int h = img.rows;
    int channel = img.channels();

    for(int i=0;i<h;i++)
        for(int j=0;j<w;j++)
            for(int c=0;c<channel;++c)
            {
                gy.at<float>(i,j*channel+c) =
                    img.at<float>(i+1,j*channel+c) - img.at<float>(i,j*channel+c);

            }
}

void Domain_Filter::find_magnitude(Mat &img, Mat &mag)
{
    int h = img.rows;
    int w = img.cols;

    vector <Mat> planes;
    split(img, planes);

    Mat magXR = Mat(h, w, CV_32FC1);
    Mat magYR = Mat(h, w, CV_32FC1);

    Mat magXG = Mat(h, w, CV_32FC1);
    Mat magYG = Mat(h, w, CV_32FC1);

    Mat magXB = Mat(h, w, CV_32FC1);
    Mat magYB = Mat(h, w, CV_32FC1);

    Sobel(planes[0], magXR, CV_32FC1, 1, 0, 3);
    Sobel(planes[0], magYR, CV_32FC1, 0, 1, 3);

    Sobel(planes[1], magXG, CV_32FC1, 1, 0, 3);
    Sobel(planes[1], magYG, CV_32FC1, 0, 1, 3);

    Sobel(planes[2], magXB, CV_32FC1, 1, 0, 3);
    Sobel(planes[2], magYB, CV_32FC1, 0, 1, 3);

    Mat mag1 = Mat(h,w,CV_32FC1);
    Mat mag2 = Mat(h,w,CV_32FC1);
    Mat mag3 = Mat(h,w,CV_32FC1);

    magnitude(magXR,magYR,mag1);
    magnitude(magXG,magYG,mag2);
    magnitude(magXB,magYB,mag3);

    mag = mag1 + mag2 + mag3;
    mag = 1.0f - mag;
}

void Domain_Filter::compute_Rfilter(Mat &output, Mat &hz, float sigma_h)
{
    int h = output.rows;
    int w = output.cols;
    int channel = output.channels();

    float a = (float) exp((-1.0 * sqrt(2.0)) / sigma_h);

    Mat temp = Mat(h,w,CV_32FC3);

    output.copyTo(temp);
    Mat V = Mat(h,w,CV_32FC1);

    // Each row is an independent recursion, so the sweeps run over rows in parallel. Row i's V is
    // only ever read by row i, so the three passes fold into one. Row pointers replace Mat::at,
    // which recomputed the row base for every one of the four accesses per element.
    parallel_for_(Range(0, h), [&](const Range& rows)
    {
        for(int i = rows.start; i < rows.end; i++)
        {
            const float* hzr = hz.ptr<float>(i);
            float* Vr = V.ptr<float>(i);
            for(int j = 0; j < w; j++)
                Vr[j] = std::pow(a, hzr[j]);

            float* t = temp.ptr<float>(i);

            for(int j = 1; j < w; j++)
            {
                const float v = Vr[j];
                for(int c = 0; c < channel; c++)
                {
                    const int k = j*channel + c;
                    t[k] = t[k] + (t[k-channel] - t[k]) * v;
                }
            }

            for(int j = w-2; j >= 0; j--)
            {
                const float v = Vr[j+1];
                for(int c = 0; c < channel; c++)
                {
                    const int k = j*channel + c;
                    t[k] = t[k] + (t[k+channel] - t[k]) * v;
                }
            }
        }
    }, (double)h*w/(1 << 16));

    temp.copyTo(output);
}

void Domain_Filter::compute_boxfilter(Mat &output, Mat &hz, Mat &psketch, float radius)
{
    int h = output.rows;
    int w = output.cols;

    lower_idx = Mat::zeros(h,w,CV_32FC1);
    upper_idx = Mat::zeros(h,w,CV_32FC1);

    // Rows are independent. The per-row Mats this used to allocate (four of them, plus two
    // full-size lower/upper position images) are replaced by three scratch rows per thread;
    // hz +/- radius is computed where it is read. Note `temp` is deliberately shared between the
    // two scans below, as it was before: a scan that never breaks keeps the previous value.
    parallel_for_(Range(0, h), [&](const Range& rows)
    {
        AutoBuffer<float> _dom(w + 1), _tl(w), _tu(w);
        float* domain = _dom.data();
        float* tl = _tl.data();
        float* tu = _tu.data();

        for(int i = rows.start; i < rows.end; i++)
        {
            const float* hzr = hz.ptr<float>(i);
            float* lo = lower_idx.ptr<float>(i);
            float* hi = upper_idx.ptr<float>(i);

            for(int j = 0; j < w; j++)
                domain[j] = hzr[j];
            domain[w] = (float) myinf;

            tl[0] = 0.f;
            tu[0] = 0.f;
            for(int j = 0; j < w; j++)
                if(domain[j] > hzr[0] - radius) { tl[0] = (float) j; break; }
            for(int j = 0; j < w; j++)
                if(domain[j] > hzr[0] + radius) { tu[0] = (float) j; break; }

            int temp = 0;
            for(int j = 1; j < w; j++)
            {
                const float lower_pos = hzr[j] - radius;
                const float upper_pos = hzr[j] + radius;

                int count = 0;
                for(int k = (int) tl[j-1]; k < w+1; k++)
                {
                    if(domain[k] > lower_pos) { temp = count; break; }
                    count++;
                }
                tl[j] = tl[j-1] + temp;

                count = 0;
                for(int k = (int) tu[j-1]; k < w+1; k++)
                {
                    if(domain[k] > upper_pos) { temp = count; break; }
                    count++;
                }
                tu[j] = tu[j-1] + temp;
            }

            for(int j = 0; j < w; j++)
            {
                lo[j] = tl[j] + 1;
                hi[j] = tu[j] + 1;
            }
        }
    }, (double)h*w/(1 << 16));

    psketch = upper_idx - lower_idx;
}
void Domain_Filter::compute_NCfilter(Mat &output, Mat &hz, Mat &psketch, float radius)
{
    int h = output.rows;
    int w = output.cols;
    int channel = output.channels();

    if (w < 2)
        return;

    compute_boxfilter(output,hz,psketch,radius);

    Mat box_filter = Mat::zeros(h,w+1,CV_32FC3);

    // row-wise prefix sum; rows are independent
    parallel_for_(Range(0, h), [&](const Range& rows)
    {
        for(int i = rows.start; i < rows.end; i++)
        {
            const float* o = output.ptr<float>(i);
            float* bf = box_filter.ptr<float>(i);
            bf[1*channel+0] = o[0*channel+0];
            bf[1*channel+1] = o[0*channel+1];
            bf[1*channel+2] = o[0*channel+2];
            for(int j = 2; j < w+1; j++)
                for(int c = 0; c < channel; c++)
                    bf[j*channel+c] = o[(j-1)*channel+c] + bf[(j-1)*channel+c];
        }
    }, (double)h*w/(1 << 16));

    Mat final =   Mat::zeros(h,w,CV_32FC3);

    // Compute the box filter using a summed area table.
    // a and b used to be built as full images from `flag`, `indices` and two temporaries, one set
    // per channel. They are the same float expression per element, so they are formed here
    // instead; the index decode below is unchanged.
    for(int c=0;c<channel;c++)
    {
        const float cbase = (float)c * (float)(h*(w+1));

        parallel_for_(Range(0, h), [&](const Range& rows)
        {
            for(int i = rows.start; i < rows.end; i++)
            {
                const float* lo = lower_idx.ptr<float>(i);
                const float* hi = upper_idx.ptr<float>(i);
                float* fin = final.ptr<float>(i);
                const float idx = (float)(i+1);

                int p,q,r,rem;
                int p1,q1,r1,rem1;

                for(int j=0;j<w;j++)
                {
                    const float av = cbase + (lo[j] - 1.f) * (float)h + idx;
                    const float bv = cbase + (hi[j] - 1.f) * (float)h + idx;

                    r = (int) bv/(h*(w+1));
                    rem = (int) bv - r*h*(w+1);
                    q = rem/h;
                    p = rem - q*h;
                    if(q==0)
                    {
                        p=h;
                        q=w;
                        r=r-1;
                    }
                    if(p==0)
                    {
                        p=h;
                        q=q-1;
                    }

                    r1 = (int) av/(h*(w+1));
                    rem1 = (int) av - r1*h*(w+1);
                    q1 = rem1/h;
                    p1 = rem1 - q1*h;
                    if(p1==0)
                    {
                        p1=h;
                        q1=q1-1;
                    }

                    fin[j*channel+2-c] = (box_filter.at<float>(p-1,q*channel+(2-r)) - box_filter.at<float>(p1-1,q1*channel+(2-r1)))
                        /(hi[j] - lo[j]);
                }
            }
        }, (double)h*w/(1 << 16));
    }

    final.copyTo(output);
}
void Domain_Filter::init(const Mat &img, int flags, float sigma_s, float sigma_r)
{
    int h = img.size().height;
    int w = img.size().width;
    int channel = img.channels();

    ////////////////////////////////////     horizontal and vertical partial derivatives /////////////////////////////////

    Mat derivx = Mat::zeros(h,w-1,CV_32FC3);
    Mat derivy = Mat::zeros(h-1,w,CV_32FC3);

    diffx(img,derivx);
    diffy(img,derivy);

    Mat distx = Mat::zeros(h,w,CV_32FC1);
    Mat disty = Mat::zeros(h,w,CV_32FC1);

    //////////////////////// Compute the l1-norm distance of neighbor pixels ////////////////////////////////////////////////

    for(int i = 0; i < h; i++)
        for(int j = 0,k=1; j < w-1; j++,k++)
            for(int c = 0; c < channel; c++)
            {
                distx.at<float>(i,k) =
                    distx.at<float>(i,k) + abs(derivx.at<float>(i,j*channel+c));
            }

    for(int i = 0,k=1; i < h-1; i++,k++)
        for(int j = 0; j < w; j++)
            for(int c = 0; c < channel; c++)
            {
                disty.at<float>(k,j) =
                    disty.at<float>(k,j) + abs(derivy.at<float>(i,j*channel+c));
            }

    ////////////////////// Compute the derivatives of the horizontal and vertical domain transforms. /////////////////////////////

    horiz = Mat(h,w,CV_32FC1);
    vert = Mat(h,w,CV_32FC1);

    Mat final = Mat(h,w,CV_32FC3);

    Mat tempx,tempy;
    multiply(distx,sigma_s/sigma_r,tempx);
    multiply(disty,sigma_s/sigma_r,tempy);

    horiz = 1.0f + tempx;
    vert = 1.0f + tempy;

    O = Mat(h,w,CV_32FC3);
    img.copyTo(O);

    O_t = Mat(w,h,CV_32FC3);

    if(flags == 2)
    {

        ct_H = Mat(h,w,CV_32FC1);
        ct_V = Mat(h,w,CV_32FC1);

        for(int i = 0; i < h; i++)
        {
            ct_H.at<float>(i,0) = horiz.at<float>(i,0);
            for(int j = 1; j < w; j++)
            {
                ct_H.at<float>(i,j) = horiz.at<float>(i,j) + ct_H.at<float>(i,j-1);
            }
        }

        for(int j = 0; j < w; j++)
        {
            ct_V.at<float>(0,j) = vert.at<float>(0,j);
            for(int i = 1; i < h; i++)
            {
                ct_V.at<float>(i,j) = vert.at<float>(i,j) + ct_V.at<float>(i-1,j);
            }
        }
    }

}

void Domain_Filter::filter(const Mat &img, Mat &res, float sigma_s = 60, float sigma_r = 0.4, int flags = 1)
{
    int no_of_iter = 3;
    int h = img.size().height;
    int w = img.size().width;
    float sigma_h = sigma_s;

    init(img,flags,sigma_s,sigma_r);

    if(flags == 1)
    {
        Mat vert_t = vert.t();

        for(int i=0;i<no_of_iter;i++)
        {
            sigma_h = (float) (sigma_s * sqrt(3.0) * std::pow(2,(no_of_iter - (i+1))) / sqrt(std::pow(4,no_of_iter) -1));

            compute_Rfilter(O, horiz, sigma_h);

            O_t = O.t();

            compute_Rfilter(O_t, vert_t, sigma_h);

            O = O_t.t();

        }
    }
    else if(flags == 2)
    {

        Mat vert_t = ct_V.t();
        Mat temp = Mat(h,w,CV_32FC1);
        Mat temp1 = Mat(w,h,CV_32FC1);

        float radius;

        for(int i=0;i<no_of_iter;i++)
        {
            sigma_h = (float) (sigma_s * sqrt(3.0) * std::pow(2,(no_of_iter - (i+1))) / sqrt(std::pow(4,no_of_iter) -1));

            radius = (float) sqrt(3.0) * sigma_h;

            compute_NCfilter(O, ct_H, temp,radius);

            O_t = O.t();

            compute_NCfilter(O_t, vert_t, temp1, radius);

            O = O_t.t();
        }
    }

    res = O.clone();
}

void Domain_Filter::pencil_sketch(const Mat &img, Mat &sketch, Mat &color_res, float sigma_s, float sigma_r, float shade_factor)
{

    int no_of_iter = 3;
    init(img,2,sigma_s,sigma_r);
    int h = img.size().height;
    int w = img.size().width;

    /////////////////////// convert to YCBCR model for color pencil drawing //////////////////////////////////////////////////////

    Mat color_sketch = Mat(h,w,CV_32FC3);

    cvtColor(img,color_sketch,COLOR_BGR2YCrCb);

    vector <Mat> YUV_channel;
    Mat vert_t = ct_V.t();

    float sigma_h = sigma_s;

    Mat penx = Mat(h,w,CV_32FC1);

    Mat pen_res = Mat::zeros(h,w,CV_32FC1);
    Mat peny = Mat(w,h,CV_32FC1);

    Mat peny_t;

    float radius;

    for(int i=0;i<no_of_iter;i++)
    {
        sigma_h = (float) (sigma_s * sqrt(3.0) * std::pow(2,(no_of_iter - (i+1))) / sqrt(std::pow(4,no_of_iter) -1));

        radius = (float) sqrt(3.0) * sigma_h;

        compute_boxfilter(O, ct_H, penx, radius);

        O_t = O.t();

        compute_boxfilter(O_t, vert_t, peny, radius);

        O = O_t.t();

        peny_t = peny.t();

        for(int k=0;k<h;k++)
            for(int j=0;j<w;j++)
                pen_res.at<float>(k,j) = (shade_factor * (penx.at<float>(k,j) + peny_t.at<float>(k,j)));

        if(i==0)
        {
            sketch = pen_res.clone();
            split(color_sketch,YUV_channel);
            pen_res.copyTo(YUV_channel[0]);
            merge(YUV_channel,color_sketch);
            cvtColor(color_sketch,color_res,COLOR_YCrCb2BGR);
        }

    }
}
