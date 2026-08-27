
#include "precomp.hpp"
#include "contours_common.hpp"
#include "opencv2/core/hal/intrin.hpp"
#include <map>

namespace{

// Tunable block size. 1024 points = 8KB (Fits easily in L1 Cache)
template <size_t BLOCK_SIZE = 2048>
class TRUCOPagedContour {
public:
    struct Block {
        cv::Point data[BLOCK_SIZE];
    };

    TRUCOPagedContour() {
        allocateBlock();
        // Initialize pointers to the start of the first block
        curr_ptr_ = all_blocks_[0]->data;
        end_ptr_  = curr_ptr_ + BLOCK_SIZE;
    }

    ~TRUCOPagedContour() {
        for (Block* b : all_blocks_) cv::fastFree(b);
    }

    // --- HOT PATH: Minimal instructions ---
    // No counter updates, just raw pointer arithmetic.
    inline void push_back(const cv::Point& pt) {
        if (curr_ptr_ == end_ptr_) {
            current_block_idx_++;
            if (current_block_idx_ == all_blocks_.size()) {
                allocateBlock();
            }
            curr_ptr_ = all_blocks_[current_block_idx_]->data;
            end_ptr_  = curr_ptr_ + BLOCK_SIZE;
        }
        *curr_ptr_++ = pt;
    }

    inline void pop_back() {
        // Safety check: do nothing if completely empty
        if (current_block_idx_ == 0 && curr_ptr_ == all_blocks_[0]->data) return;

        // Check if we are at the start of the current block
        if (curr_ptr_ == all_blocks_[current_block_idx_]->data) {
            // Move to the previous block
            current_block_idx_--;
            // Point to the end of the previous block
            curr_ptr_ = all_blocks_[current_block_idx_]->data + BLOCK_SIZE;
            end_ptr_  = curr_ptr_;
        }
        curr_ptr_--;
    }

    inline const cv::Point& back() const {
        // Handle case where back() crosses block boundary
        if (curr_ptr_ == all_blocks_[current_block_idx_]->data) {
            return all_blocks_[current_block_idx_ - 1]->data[BLOCK_SIZE - 1];
        }
        return *(curr_ptr_ - 1);
    }

    inline const cv::Point& front() const {
        return all_blocks_[0]->data[0];
    }

    // Calculated on demand (O(1) arithmetic, but slightly more math than reading a variable)
    size_t size() const {
        size_t elements_in_last = curr_ptr_ - all_blocks_[current_block_idx_]->data;
        return (current_block_idx_ * BLOCK_SIZE) + elements_in_last;
    }

    void clear() {
        current_block_idx_ = 0;
        if (!all_blocks_.empty()) {
            curr_ptr_ = all_blocks_[0]->data;
            end_ptr_  = curr_ptr_ + BLOCK_SIZE;
        }
    }

    // Optimized Copy: Uses block-wise memcpy
    void copyTo(std::vector<cv::Point>& out) const {
        size_t total = size();
        out.resize(total);
        if (total == 0) return;

        cv::Point* dst = out.data();

        // 1. Copy full blocks
        for (size_t i = 0; i < current_block_idx_; ++i) {
            std::memcpy(dst, all_blocks_[i]->data, BLOCK_SIZE * sizeof(cv::Point));
            dst += BLOCK_SIZE;
        }

        // 2. Copy partial last block
        size_t last_block_count = curr_ptr_ - all_blocks_[current_block_idx_]->data;
        if (last_block_count > 0) {
            std::memcpy(dst, all_blocks_[current_block_idx_]->data, last_block_count * sizeof(cv::Point));
        }
    }

private:
    void grow() {
        current_block_idx_++;
        if (current_block_idx_ == all_blocks_.size()) {
            allocateBlock();
        }
        curr_ptr_ = all_blocks_[current_block_idx_]->data;
        end_ptr_  = curr_ptr_ + BLOCK_SIZE;
    }

    void allocateBlock() {
        Block* b = (Block*)cv::fastMalloc(sizeof(Block));
        all_blocks_.push_back(b);
    }

    std::vector<Block*> all_blocks_;
    size_t current_block_idx_ = 0;

    // Fast pointers for the hot loop
    cv::Point* curr_ptr_ = nullptr;
    cv::Point* end_ptr_  = nullptr;
};


////IMPLEMENTATION

struct AccumulatorT:public std::vector<std::vector<cv::Point>>{
    std::vector<int> idx_internal_lastLine,idx_external_firstLine;
};


class TRUCOntourTracer : public cv::ParallelLoopBody
{
public:

    // We use a pointer to the accumulator to avoid passing huge objects
    // Accumulator: Vector of (Vector of Contours), where Contour is Vector of Points
    using AccumulatorType = std::vector<AccumulatorT>;

    // labels/stats come from cv::connectedComponentsWithStats(img, ...): labels
    // is a CV_32S map the same size as img (0 = background, 1..numLabels-1 =
    // unique component ids), stats is the Nx5 CV_32S per-label bounding-box
    // table it also produces. See operator() for why partitioning work by
    // label -- instead of by row -- makes every stripe's pixel set provably
    // disjoint from every other's, with no bound or lock required.
    TRUCOntourTracer(const cv::Mat& img,
                     const cv::Mat& labels,
                     const cv::Mat& stats,
                     AccumulatorType& accumulator,
                     size_t minSize)
        : padded_(img), labels_(labels), stats_(stats), accumulator_(accumulator), minSize_(minSize)
    {
        step_ = padded_.step;
        int istep = (int)step_;
        // 0: East (Right)
        offsets_[0] = 1;
        // 1: NE (Up-Right)
        offsets_[1] = -istep + 1;
        // 2: North (Up)
        offsets_[2] = -istep;
        // 3: NW (Up-Left)
        offsets_[3] = -istep - 1;
        // 4: West (Left)
        offsets_[4] = -1;
        // 5: SW (Down-Left)
        offsets_[5] = istep - 1;
        // 6: South (Down)
        offsets_[6] = istep;
        // 7: SE (Down-Right)
        offsets_[7] = istep + 1;

        memcpy(offsets_ + 8, offsets_, 8 * sizeof(int));

    }

    // No stripe-boundary "mock" pass anymore: since a trace can never leave its
    // own label's pixels (see operator()), every row gets a full, real external
    // trace directly. traceExternalContourMock existed only to support the old
    // row-stripe boundary handling and is no longer needed.
    bool traceContour( TRUCOPagedContour<4096>* buffer,  int r,int c,uchar *row_ptr, bool isExternal)const{

        buffer->clear();

        int curr_x = c , curr_y = r;
        int start_dir = -1 ;
        int search_idx = isExternal ? 5 :1;
        uchar* curr_ptr = row_ptr + c , * start_ptr = curr_ptr;
        int dir=-1;
        // int sign=isExternal?1:-1;

        bool is_first_move = true;


        // QUick test to find the first element of an internal contour. We know it should be NE
        if(!isExternal){
            int n=0;
            for ( n = 0; n < 8; ++n)
            {
                int idx = search_idx + n;
                if ( *(curr_ptr + offsets_[idx]) == BACKGROUND) continue;
                if(curr_x+ dx_[ idx & 7]!=c+1 || curr_y+ dy_[ idx & 7]!=r-1) return false;
                break;
            }
            if(n==8) return false;//isolated pixels must not be considered as internal
        }
        // 3. TRACING LOOP
        while(true)
        {
            buffer->push_back({ (curr_x - 1),  (curr_y - 1)});
            // Check neighbors
            for (int n = 0; n < 8; ++n)
            {
                int idx = search_idx + n;
                // Use offset cache
                uchar* neighbor = curr_ptr + offsets_[idx];
                if (*neighbor == BACKGROUND) continue;

                dir = idx & 7;
                // --- EXECUTE MOVE ---
                curr_y += dy_[dir];
                curr_x += dx_[dir];

                // No range bound needed here: every 8-connected foreground
                // neighbor a trace can step to is, by definition of connected
                // components, part of the same label as the pixel it started
                // from -- so this walk can never leave the pixels this thread
                // exclusively owns (see operator()).
                if ((search_idx <= 1)  || (dir <= search_idx - 2))
                {
                    *curr_ptr = VISITED_OUTER_RIGHT;
                }
                else if (*curr_ptr == FOREGROUND)
                {
                    *curr_ptr = VISITED_;
                }

                // Short-circuit Jacob's Check
                if (curr_ptr == start_ptr) {
                    if (!is_first_move && dir == start_dir) {
                        return true;//done
                    }
                }

                curr_ptr = neighbor;//move ptr

                // Reset search index for Moore neighbor
                search_idx = (dir +6) & 7;
                break;

            }
            if (is_first_move) {
                if(dir==-1){//single pixel
                    *curr_ptr = VISITED_OUTER_RIGHT;
                    break;//not moved
                }
                start_dir = dir;
                is_first_move = false;
            }

        }
        return true;
    }

    // Processes one row, but only the pixels on it that belong to myLabel.
    // Any foreground run whose label doesn't match is skipped outright (it
    // belongs to some other label, which some other call -- possibly running
    // concurrently -- owns exclusively). Since a trace starting on myLabel's
    // pixels can never step onto a different label's pixels (8-connected
    // foreground pixels always share a label, by definition of connected
    // components), this is safe to call concurrently for different labels
    // with no lock, and needs no row-range bound at all.
    void processRow(int r, int myLabel, TRUCOPagedContour<4096>* buffer, AccumulatorT& local_contours) const
    {
        int cols = padded_.cols;
        uchar* row_ptr = padded_.data + r * step_;
        const int* label_row = labels_.ptr<int>(r);

        // "c" is updated by the find* functions
        for (int c = 1; c < cols - 1; )
        {
            // 1. FAST SCAN: Skip background pixels
            if ((c = findStartContourPoint(row_ptr, cols, c)) == cols) break;

            // 2. CHECK: only trace if this run is actually foreground and is
            // this thread's own label -- otherwise it belongs to a different
            // component and is left untouched for whoever owns it.
            if (row_ptr[c] == FOREGROUND && label_row[c] == myLabel)
            {
                if( traceContour(buffer,r,c,row_ptr,true)){
                    // Post-processing
                    if (buffer->size() > 1 && buffer->back() == buffer->front()) {
                        buffer->pop_back();
                    }
                    if (buffer->size() >= minSize_) {
                        // Instead of copying the vector, we move it.
                        local_contours.emplace_back();
                        buffer->copyTo(local_contours.back());
                    }
                }
            }

            // 3. FAST SCAN: Find end of current component to skip processing it again
            c = findEndContourPoint(row_ptr, cols, c + 1);
            if(c>=cols)break;//end of row
            //internal contour -- same label-ownership guard as above
            if(label_row[c-1]==myLabel && row_ptr[c-1]>VISITED_OUTER_RIGHT){

                if(traceContour(buffer,r,c-1,row_ptr,false)){
                    // Post-processing
                    if (buffer->size() > 1 && buffer->back() == buffer->front()) {
                        buffer->pop_back();
                    }
                    if (buffer->size() >= minSize_) {
                        local_contours.emplace_back();
                        buffer->copyTo(local_contours.back());
                    }
                }
            }
        }
    }

    // One call per label range via cv::parallel_for_. Each label's full
    // contour set (its external boundary plus any internal holes) is traced
    // entirely by whichever thread handles it, top to bottom, in the same
    // natural row order a single-threaded scan would use -- there's no
    // cross-thread boundary left to reconcile afterward, because connected
    // components already guarantees no two labels' pixels ever overlap.
    void operator()(const cv::Range& labelRange) const CV_OVERRIDE
    {
        TRUCOPagedContour<4096> buffer;

        for (int label = labelRange.start; label < labelRange.end; ++label)
        {
            auto& local_contours = accumulator_[label];
            local_contours.reserve(64);

            const int top    = stats_.at<int>(label, cv::CC_STAT_TOP);
            const int height = stats_.at<int>(label, cv::CC_STAT_HEIGHT);
            for (int r = top; r < top + height; ++r)
                processRow(r, label, &buffer, local_contours);
        }
    }

    static inline int findStartContourPoint(uchar* src_data, int width, int j)
    {
#if (CV_SIMD || CV_SIMD_SCALABLE)
        cv::v_uint8 v_zero = cv::vx_setzero_u8();
        for (; j <= width - cv::VTraits<cv::v_uint8>::vlanes(); j += cv::VTraits<cv::v_uint8>::vlanes())
        {
            cv::v_uint8 vmask = (cv::v_ne(cv::vx_load((uchar*)(src_data + j)), v_zero));
            if (cv::v_check_any(vmask))
            {
                j += cv::v_scan_forward(vmask);
                return j;
            }
        }
#endif
        for (; j < width && !src_data[j]; ++j)
            ;
        return j;
    }

    inline static int findEndContourPoint(uchar* src_data,int width, int j)
    {
#if (CV_SIMD || CV_SIMD_SCALABLE)
        if (j <  width && !src_data[j])
        {
            return j;
        }
        else
        {
            cv::v_uint8 v_zero = cv::vx_setzero_u8();
            for (; j <=  width - cv::VTraits<cv::v_uint8>::vlanes(); j += cv::VTraits<cv::v_uint8>::vlanes())
            {
                cv::v_uint8 vmask = (cv::v_eq(cv::vx_load((uchar*)(src_data + j)), v_zero));
                if (cv::v_check_any(vmask))
                {
                    j += cv::v_scan_forward(vmask);
                    return j;
                }
            }
        }
#endif
        for (; j < width && src_data[j]; ++j)
            ;

        return j;
    }


private:
    cv::Mat padded_;
    const cv::Mat& labels_;
    const cv::Mat& stats_;
    AccumulatorType& accumulator_;
    size_t minSize_;
    size_t step_;
    int offsets_[16];

    // 0=E, 1=NE, 2=N, 3=NW, 4=W, 5=SW, 6=S, 7=SE (CCW Rotation)
    const int dx_[8] = {  1,  1,  0, -1, -1, -1,  0,  1 };
    const int dy_[8] = {  0, -1, -1, -1,  0,  1,  1,  1 };
    // Constants defined once
    const uchar FOREGROUND = 255;
    const uchar BACKGROUND = 0;
    const uchar VISITED_OUTER_RIGHT    = 100;
    const uchar VISITED_    = 200;
};


void approxContour(std::vector<cv::Point> &inout,cv::ContourApproximationModes contApprox_)   {
    size_t n = inout.size();
    if (n <= 1) return;

    if (contApprox_ == cv::CHAIN_APPROX_SIMPLE) {
        std::vector<cv::Point> result;
        result.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            size_t prev_i = (i == 0) ? n - 1 : i - 1;
            size_t next_i = (i == n - 1) ? 0 : i + 1;

            cv::Point v1 = inout[i] - inout[prev_i];
            cv::Point v2 = inout[next_i] - inout[i];

            if (v1 != v2) {
                result.push_back(inout[i]);
            }
        }
        if (result.empty() && n > 0) result.push_back(inout[0]);
        inout = std::move(result);
    } else if (contApprox_ == cv::CHAIN_APPROX_TC89_L1 || contApprox_ == cv::CHAIN_APPROX_TC89_KCOS) {
        auto getCode = [](cv::Point d) -> schar {
            if (d.x == 1) {
                if (d.y == 0) return 0;
                if (d.y == -1) return 1;
                if (d.y == 1) return 7;
            } else if (d.x == 0) {
                if (d.y == -1) return 2;
                if (d.y == 1) return 6;
            } else if (d.x == -1) {
                if (d.y == -1) return 3;
                if (d.y == 0) return 4;
                if (d.y == 1) return 5;
            }
            return 0;
        };

        cv::ContourCodesStorage::storage_t codesStorage;
        cv::ContourCodesStorage codes(&codesStorage);
        for (size_t i = 0; i < n; ++i) {
            cv::Point delta = inout[(i + 1) % n] - inout[i];
            codes.push_back(getCode(delta));
        }

        cv::ContourPointsStorage::storage_t pointsStorage;
        cv::ContourPointsStorage points(&pointsStorage);
        cv::approximateChainTC89(codes, inout[0], contApprox_, points);

        inout.clear();
        inout.reserve(points.size());
        for (size_t i = 0; i < points.size(); ++i) {
            inout.push_back(points.at(i));
        }
    }
}

// ==========================================================
// 1. The Core Implementation (Operates on std::vector directly)
// ==========================================================
void findTRUContoursImpl(cv::Mat& padded,
                           std::vector<std::vector<cv::Point>>& outContours,
                           int minSize,int contApprox)
{
    // Pass 1: label connected components with OpenCV's own proven, already-
    // parallel implementation (modules/imgproc/src/connectedcomponents.cpp).
    // This guarantees every 8-connected foreground component gets a unique,
    // immutable label before any tracing starts. Partitioning the tracing
    // work by label instead of by row range then gives each thread pixels no
    // other thread will ever touch -- guaranteed by the definition of
    // connected components, not by any lock or bound this file adds.
    // CCL_SAUF specifically forces row-major label ordering (unlike the
    // default Spaghetti algorithm) -- so label 1 is whichever component is
    // encountered first scanning top-to-bottom, left-to-right, same as a
    // classic raster-scan contour algorithm would find it. Since output
    // order below follows label order, this keeps findContours' output
    // ordering close to what callers relying on raster-scan discovery order
    // (e.g. classic Suzuki-Abe) already expect.
    cv::Mat labels, stats, centroids;
    int numLabels = cv::connectedComponentsWithStats(padded, labels, stats, centroids, 8, CV_32S, cv::CCL_SAUF);
    outContours.clear();
    if (numLabels <= 1) return; // no foreground pixels at all (only the background label exists)

    // Pass 2: parallel, one call per label range. Each label's complete
    // contour set (external boundary plus any internal holes) is traced
    // entirely by whichever thread handles it -- no merge step needed
    // afterward, since there's no stripe boundary left to reconcile.
    std::vector<AccumulatorT> perLabelContours(numLabels); // index 0 (background) unused
    TRUCOntourTracer worker(padded, labels, stats, perLabelContours, minSize);
    cv::parallel_for_(cv::Range(1, numLabels), worker);

    size_t totalContours = 0;
    for (auto& lVec : perLabelContours)
        totalContours += lVec.size();

    outContours.reserve(totalContours);
    // move the contours from per-label accumulators to output without copying pixel data
    for (auto& lVec : perLabelContours) {
        // move_iterator moves the vector internals (pointers) without copying pixel data
        outContours.insert(outContours.end(),
                           std::make_move_iterator(lVec.begin()),
                           std::make_move_iterator(lVec.end()));
    }

    //7. now, lets do the contour approximation if needed in parallel
    if(contApprox!=cv::CHAIN_APPROX_NONE){
        cv::parallel_for_(cv::Range(0, (int)outContours.size()), [&](const cv::Range& range){
            for(int i=range.start;i<range.end;i++){
                approxContour(outContours[i],(cv::ContourApproximationModes)contApprox);
            }
        });
    }

}
}

namespace cv{

// ==========================================================
//
//  Public API: Handles OutputArray and dispatches to the core implementation
//  This is a modified version of the original TRUCO parallel algorithm to produce the exact same output
//  as original findContours with RETR_LIST mode (no hierarchy, all contours are external). It also supports contour approximation.
//
// ==========================================================
void findTRUContours(InputArray _src, OutputArrayOfArrays _contours, int minSize, bool binarize,int method)
{
    CV_INSTRUMENT_REGION();
    Mat src = _src.getMat();
    CV_Assert(src.type() == CV_8UC1);

    // Buffer handling
    cv::Mat padded;
    cv::copyMakeBorder(src, padded, 1, 1, 1, 1, cv::BORDER_CONSTANT, 0);
    if (binarize)
        cv::threshold(padded, padded, 0, 255, cv::THRESH_BINARY);


    // Fast path: caller passed std::vector<std::vector<cv::Point>> directly.
    // Write into it without any intermediate copy.
    if (_contours.kind() == _InputArray::STD_VECTOR_VECTOR) {
        auto* vec = reinterpret_cast<std::vector<std::vector<cv::Point>>*>(_contours.getObj());
        findTRUContoursImpl(padded, *vec, minSize,method);
    }
    else{ // Slow path: generic OutputArray — build in a temp vector then copy.
        std::vector<std::vector<cv::Point>> tempContours;
        findTRUContoursImpl(padded, tempContours, minSize,method);

        _contours.create((int)tempContours.size(), 1, 0, -1, true);
        for (size_t i = 0; i < tempContours.size(); i++) {
            _contours.create((int)tempContours[i].size(), 1, CV_32SC2, (int)i, true);
            Mat m = _contours.getMat((int)i);
            std::memcpy(m.data, tempContours[i].data(), tempContours[i].size() * sizeof(cv::Point));
        }
    }
}

}
