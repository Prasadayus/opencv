// Backend A/B diagnostic: is the FindTRUContours thread-count divergence caused
// by OpenCV's own code, or by the PPL/ConcRT runtime underneath it?
//
// This file is a DIAGNOSTIC, not a regression test. Both tests are DISABLED_ by
// default and must be asked for explicitly:
//
//   opencv_test_imgproc --gtest_also_run_disabled_tests \
//                       --gtest_filter='Imgproc_ParallelBackendAB.*'
//
// ---------------------------------------------------------------------------
// WHY THIS IS A CLEAN EXPERIMENT
// ---------------------------------------------------------------------------
// findTRUContours dispatches with:
//
//     cv::parallel_for_(cv::Range(1, numLabels), worker);     // contours_truco.cpp
//
// nstripes is left at its default (-1), so parallel.cpp:211 sets
// nstripes = len -- ONE STRIPE PER CONNECTED-COMPONENT LABEL. That decomposition
// does not depend on the thread count at all; raising the thread count only
// changes WHICH thread picks up WHICH label. The tracer's stated correctness
// argument is that a trace can never leave its own label's pixels (8-connectivity
// guarantees any foreground neighbour shares the label), so each stripe is an
// independent unit.
//
// If that holds, the thread count CANNOT change the result: it is a pure map over
// independent units. Imgproc_FindTRUContours.nthreads_consistency says the result
// does change. So exactly one of these is true:
//
//   (a) the tracer is not actually label-exclusive       -> OpenCV defect
//   (b) the runtime does not deliver each stripe exactly
//       once, or does not publish worker writes at the
//       join                                             -> PPL/ConcRT defect
//
// Test 1 below decides between them by holding the decomposition fixed and
// swapping ONLY the thread pool. Test 2 audits (b) directly, in situ.
//
// The substitution is exact, not approximate. For the PPL backend,
// parallel.cpp:447-450 dispatches each stripe as:
//     ProxyLoopBody::operator()(int i) -> ParallelLoopBodyWrapper::operator()(Range(i, i+1))
// For a custom backend, parallel.cpp:541-546 dispatches as:
//     parallel_for_cb(start, end, data) -> ParallelLoopBodyWrapper::operator()(Range(start, end))
// so calling body_callback(i, i+1, data) reaches the *same* function with the
// *same* argument. Stripe count, stripe->range mapping, RNG propagation, FP
// denormal handling and exception capture are all shared code, untouched.
//
// The one genuine difference is the synchronisation at the end: std::thread::join
// carries a happens-before edge guaranteed by the C++ standard itself, whereas
// PPL's run_and_wait() relies on whatever concrt140.dll actually implements
// (closed source, so unverifiable by reading).
//
// ---------------------------------------------------------------------------
// HOW TO READ THE RESULT
// ---------------------------------------------------------------------------
//   PPL mismatches > 0  AND  std::thread mismatches > 0
//       -> OpenCV defect, PROVEN. The failure survives with PPL entirely out of
//          the picture, so case (a) holds: the tracer shares state between labels.
//
//   PPL mismatches > 0  AND  std::thread mismatches == 0 (over many repeats)
//       -> Strong evidence for the runtime. Same code, same decomposition, same
//          hardware; only the thread pool changed. Honest caveat: a different
//          pool has different timing, so this is very strong but not absolute --
//          it does not by itself prove PPL breaks a documented guarantee. Pair it
//          with ppl_contract_audit.cpp, which tests that directly and, if it
//          fails, is conclusive.
//
//   Both == 0
//       -> Not reproduced here. Raise kRepeats, or note that the CI runner has
//          only 4 vCPUs (its log banner reads "ms-concurrency (nthreads=4)")
//          while a dev box has ~10-12, so setNumThreads(38) is ~9x oversubscribed
//          on CI versus ~3x locally. Oversubscription is very likely what makes
//          CI trip this, so a low-core machine reproduces it far better.

#include "test_precomp.hpp"

#include <opencv2/core/parallel/parallel_backend.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32
// For SetProcessAffinityMask / DWORD_PTR / GetLastError, used by the optional
// CPU-pinning helper below. Not relied on transitively -- included explicitly.
#include <windows.h>
#endif

namespace opencv_test { namespace {

// ---------------------------------------------------------------------------
// A minimal, deliberately boring parallel backend built on std::thread.
//
// It is intentionally NOT clever: no work stealing, no thread reuse across
// calls, no spin-waiting. Threads are spawned, they pull indices off one atomic
// counter, and they are joined. Fewer moving parts means fewer places for this
// diagnostic to have a bug of its own -- and std::thread::join gives the
// standard-guaranteed happens-before edge that is the whole point of the swap.
// ---------------------------------------------------------------------------
class StdThreadParallelBackend CV_FINAL : public cv::parallel::ParallelForAPI
{
public:
    StdThreadParallelBackend() : numThreads_(cv::getNumberOfCPUs()) {}

    void parallel_for(int tasks, FN_parallel_for_body_cb_t body_callback, void* callback_data) CV_OVERRIDE
    {
        if (tasks <= 0)
            return;

        int nThreads = numThreads_.load();
        if (nThreads < 1)
            nThreads = 1;
        // Never spawn more threads than there is work for.
        if (nThreads > tasks)
            nThreads = tasks;

        if (nThreads == 1)
        {
            // Same one-index-per-call shape as the threaded path, so the serial
            // and parallel routes differ only in who runs the body.
            for (int i = 0; i < tasks; ++i)
                body_callback(i, i + 1, callback_data);
            return;
        }

        std::atomic<int> next(0);

        // The body is expected not to throw: OpenCV's ParallelLoopBodyWrapper
        // already catches and records exceptions before they reach us
        // (parallel.cpp:358-380). The catch here is only so that a rogue
        // exception terminates the process instead of escaping a thread.
        auto workerLoop = [&]()
        {
            for (;;)
            {
                const int i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= tasks)
                    return;
                try
                {
                    body_callback(i, i + 1, callback_data);
                }
                catch (...)
                {
                    fprintf(stderr, "StdThreadParallelBackend: body threw; aborting\n");
                    std::terminate();
                }
            }
        };

        std::vector<std::thread> pool;
        pool.reserve((size_t)nThreads - 1);
        for (int t = 0; t < nThreads - 1; ++t)
            pool.emplace_back(workerLoop);

        // The calling thread participates too, matching how PPL's parallel_for
        // uses the caller as one of its workers.
        workerLoop();

        for (size_t t = 0; t < pool.size(); ++t)
            pool[t].join();     // <-- standard-guaranteed happens-before edge
    }

    // WARNING for anyone reusing this backend outside this file: returning a
    // constant 0 is only safe because nothing in the findContours path calls
    // cv::getThreadNum() -- verified by grepping modules/imgproc/src and
    // modules/core/src (no hits outside parallel.cpp itself). If it were used to
    // index per-thread storage, every thread would collide on slot 0 and this
    // backend would MANUFACTURE a race that PPL does not have, which would make
    // Test A wrongly report "both backends diverge => OpenCV defect". Re-check
    // that grep before reusing this for a different code path.
    int getThreadNum() const CV_OVERRIDE { return 0; }
    int getNumThreads() const CV_OVERRIDE { return numThreads_.load(); }

    int setNumThreads(int nThreads) CV_OVERRIDE
    {
        numThreads_.store(nThreads > 0 ? nThreads : cv::getNumberOfCPUs());
        return numThreads_.load();
    }

    const char* getName() const CV_OVERRIDE { return "std_thread_diagnostic"; }

private:
    mutable std::atomic<int> numThreads_;
};

// Installs a backend and puts the built-in one back, even if an assertion unwinds.
//
// Note on the restore: getCurrentParallelForAPI() is not part of the public API
// (only setParallelForBackend and ParallelForAPI are exported from
// core/parallel/parallel_backend.hpp), so the previous backend cannot be read
// back and saved. Restoring an empty shared_ptr is the correct inverse here
// regardless: parallel.cpp:562-563 tests `if (api)`, so a null pointer falls
// through to the compiled-in framework -- ms-concurrency/PPL in this build. That
// is exactly the state a normal test run starts in, since
// createParallelForAPI() also yields an empty pointer when no plugin backend is
// selected. If someone forces a plugin backend via OPENCV_PARALLEL_BACKEND, this
// would reset to the built-in one instead of that plugin -- acceptable for a
// diagnostic that is DISABLED_ by default and run deliberately.
struct ScopedBackendSwap
{
    explicit ScopedBackendSwap(const std::shared_ptr<cv::parallel::ParallelForAPI>& api)
    {
        cv::parallel::setParallelForBackend(api);
    }
    ~ScopedBackendSwap()
    {
        cv::parallel::setParallelForBackend(std::shared_ptr<cv::parallel::ParallelForAPI>());
    }
};

// Restores the thread count on scope exit.
struct ScopedNumThreads
{
    explicit ScopedNumThreads(int n) : saved_(cv::getNumThreads()) { cv::setNumThreads(n); }
    ~ScopedNumThreads() { cv::setNumThreads(saved_); }
    int saved_;
};

// Optionally restrict the process to N logical CPUs, driven by an environment
// variable so no shell quoting is involved:
//
//   set OPENCV_TEST_PIN_CPUS=4          (cmd)
//   $env:OPENCV_TEST_PIN_CPUS=4         (PowerShell)
//
// Why it matters: the GitHub windows-11-arm runner has FOUR vCPUs (its CI log
// banner reads "Parallel framework: ms-concurrency (nthreads=4)"), so
// setNumThreads(38) is ~9x oversubscribed there. A 10-12 core dev box is only
// ~3x oversubscribed, and oversubscription is what drives the preemption that
// exposes ordering bugs -- which is very likely why CI trips these failures and
// a dev machine does not. Pinning to 4 reproduces that ratio.
//
// Honest limit: this reproduces the CORE COUNT, not the silicon. It cannot
// reproduce anything specific to Cobalt 100 / Neoverse N2. A divergence found
// while pinned is real; a clean pinned run is weaker evidence than a clean run
// on the runner itself.
static void applyOptionalCpuPinning()
{
#ifdef _WIN32
    const char* env = getenv("OPENCV_TEST_PIN_CPUS");
    if (!env || !*env)
        return;

    const int wanted = atoi(env);
    const int available = cv::getNumberOfCPUs();
    if (wanted <= 0 || wanted > available)
    {
        std::cout << "OPENCV_TEST_PIN_CPUS=" << env << " ignored (must be 1.."
                  << available << ")" << std::endl;
        return;
    }

    DWORD_PTR mask = 0;
    for (int i = 0; i < wanted; ++i)
        mask |= (DWORD_PTR)1 << i;

    if (SetProcessAffinityMask(GetCurrentProcess(), mask))
    {
        std::cout << "pinned to " << wanted << " of " << available
                  << " logical CPUs (mimicking the 4-vCPU CI runner)" << std::endl;
    }
    else
    {
        std::cout << "failed to pin CPUs (error " << GetLastError()
                  << "), running unpinned" << std::endl;
    }
#endif
}

// ---------------------------------------------------------------------------
// Order-insensitive comparison of two contour sets.
//
// findContours(RETR_LIST) makes no documented ordering promise, and the label
// -> thread assignment legitimately changes the order the results are appended
// in. So compare the SET of contours, not the sequence -- same spirit as
// trucoContoursMatch() in test_contours_truco.cpp. Each contour is reduced to a
// signature (length, first point, and a checksum over every point, so the
// signature is sensitive to any actual change in geometry), then the signatures
// are sorted before comparison.
// ---------------------------------------------------------------------------
struct ContourSignature
{
    size_t   npoints;
    int      x0, y0;
    uint64_t checksum;

    bool operator<(const ContourSignature& o) const
    {
        if (npoints != o.npoints) return npoints < o.npoints;
        if (x0 != o.x0)           return x0 < o.x0;
        if (y0 != o.y0)           return y0 < o.y0;
        return checksum < o.checksum;
    }
    bool operator!=(const ContourSignature& o) const
    {
        return npoints != o.npoints || x0 != o.x0 || y0 != o.y0 || checksum != o.checksum;
    }
};

static std::vector<ContourSignature> signatureOf(const std::vector<std::vector<Point> >& contours)
{
    std::vector<ContourSignature> sigs;
    sigs.reserve(contours.size());
    for (size_t i = 0; i < contours.size(); ++i)
    {
        const std::vector<Point>& c = contours[i];
        ContourSignature s;
        s.npoints  = c.size();
        s.x0       = c.empty() ? 0 : c[0].x;
        s.y0       = c.empty() ? 0 : c[0].y;
        s.checksum = 1469598103934665603ull;    // FNV-1a offset basis
        for (size_t k = 0; k < c.size(); ++k)
        {
            s.checksum ^= (uint64_t)(uint32_t)c[k].x;
            s.checksum *= 1099511628211ull;
            s.checksum ^= (uint64_t)(uint32_t)c[k].y;
            s.checksum *= 1099511628211ull;
        }
        sigs.push_back(s);
    }
    std::sort(sigs.begin(), sigs.end());
    return sigs;
}

// Direction matters, so report it rather than just "differs".
//
// Note this is deliberately STRICTER than trucoContoursMatch() in
// test_contours_truco.cpp, which drives nthreads_consistency. That helper does:
//
//     for (auto& h1 : hashes1)
//         if (hashes2.find(h1) == hashes2.end()) return false;
//     return true;
//
// i.e. it only checks hashes1 is a SUBSET of hashes2 -- never the reverse, and
// never the sizes -- and it accumulates into a std::set, which silently collapses
// duplicates. So the existing test cannot see extra or duplicated contours at
// all; it only fires when the threaded run LOSES a contour the reference found.
// That is a useful constraint on the real failure, and worth keeping in mind:
// "nthreads_consistency failed" specifically means contours went missing.
//
// Sorting a vector (rather than inserting into a set) keeps duplicates visible
// here, so this comparison detects losses, gains and duplications alike.
struct SetDiff
{
    int missing = 0;    // in reference, absent from the threaded run  <-- what the real test catches
    int extra   = 0;    // in the threaded run, absent from reference  <-- invisible to the real test
    bool equal() const { return missing == 0 && extra == 0; }
};

static SetDiff diffContourSets(const std::vector<ContourSignature>& ref,
                               const std::vector<ContourSignature>& got)
{
    // Both inputs are already sorted by signatureOf(), so walk them in step.
    SetDiff d;
    size_t i = 0, j = 0;
    while (i < ref.size() && j < got.size())
    {
        if (ref[i] < got[j])       { ++d.missing; ++i; }
        else if (got[j] < ref[i])  { ++d.extra;   ++j; }
        else                       { ++i; ++j; }
    }
    d.missing += (int)(ref.size() - i);
    d.extra   += (int)(got.size() - j);
    return d;
}

// The same workload nthreads_consistency uses: 1000x1000 uniform noise, box
// blurred, then thresholded. Built from a fixed local seed rather than theRNG()
// so it is byte-identical on every repeat, on every backend and on every
// machine -- essential for a controlled comparison, and it sidesteps the fact
// that parallel_for_'s finalize() perturbs theRNG() differently when threaded
// (parallel.cpp:234-243, "this behaviour is not equal to single-threaded mode").
static Mat makeWorkloadImage(uint64_t seed)
{
    const Size sz(1000, 1000);
    RNG rng(seed);
    Mat noise(sz, CV_8UC1);
    rng.fill(noise, RNG::UNIFORM, 0, 256);
    Mat blurred, img;
    boxFilter(noise, blurred, CV_8U, Size(5, 5));
    cv::threshold(blurred, img, 128, 255, THRESH_BINARY);
    return img;
}

// Sweep the SAME thread range the real test does: nthreads_consistency loops
// `for(int i=2;i<40;i++)` (test_contours_truco.cpp:92). An earlier version of
// this file sampled only {2,4,8,14,38} and found nothing -- sampling 5 of 38
// values is a poor way to hunt an intermittent failure, so probe them all.
static std::vector<int> threadCountsToProbe()
{
    std::vector<int> v;
    for (int i = 2; i < 40; ++i)
        v.push_back(i);
    return v;
}

// All four approximation methods, NOT just CHAIN_APPROX_SIMPLE.
//
// This matters and was the flaw in the first version of this test: the CI
// failures were reported at "nthreads=38 method=3" and "nthreads=14 method=4",
// i.e. CHAIN_APPROX_TC89_L1 (3) and CHAIN_APPROX_TC89_KCOS (4) -- while this
// test hardcoded CHAIN_APPROX_SIMPLE (2). It was not exercising the failing
// configurations at all, so its clean result meant nothing. The TC89 variants
// run a whole extra polygon-approximation stage over each traced contour, so
// there is no reason to assume method 2 behaves like methods 3 and 4.
static const ContourApproximationModes kMethods[] = {
    CHAIN_APPROX_NONE,        // 1
    CHAIN_APPROX_SIMPLE,      // 2
    CHAIN_APPROX_TC89_L1,     // 3  <-- observed failing on CI
    CHAIN_APPROX_TC89_KCOS,   // 4  <-- observed failing on CI
};

// 4 methods x 38 thread counts = 152 comparisons per repeat per backend, so
// keep repeats modest. Raise this if a run comes back clean -- the CI failure is
// intermittent, so absence over a few hundred comparisons is weak evidence.
static const int kRepeats = 5;

struct BackendResult
{
    int comparisons = 0;
    int mismatches  = 0;
    int withMissing = 0;    // comparisons where contours were LOST (what the real test catches)
    int withExtra   = 0;    // comparisons where contours were GAINED (real test is blind to these)
    std::vector<std::string> details;
};

// Runs a 1-thread reference per method, then sweeps thread counts 2..39,
// counting how often the contour set diverges from that method's reference.
// Sweeps every approximation method x every thread count x kRepeats, and counts
// how often the contour set diverges from that method's own 1-thread reference.
static BackendResult measureDivergence(const Mat& img)
{
    BackendResult r;
    const std::vector<int> threadCounts = threadCountsToProbe();

    for (size_t mi = 0; mi < sizeof(kMethods) / sizeof(kMethods[0]); ++mi)
    {
        const ContourApproximationModes method = kMethods[mi];

        // setNumThreads(1) makes parallel_for_ take the serial path outright
        // (parallel.cpp:551 gate), for any backend -- so the reference is
        // genuinely single-threaded and backend-independent. One reference per
        // method, since each method legitimately yields different contours.
        std::vector<ContourSignature> refSig;
        {
            ScopedNumThreads one(1);
            std::vector<std::vector<Point> > refContours;
            findContours(img, refContours, RETR_LIST, method);
            refSig = signatureOf(refContours);
        }

        for (int rep = 0; rep < kRepeats; ++rep)
        {
            for (size_t ti = 0; ti < threadCounts.size(); ++ti)
            {
                const int t = threadCounts[ti];
                ScopedNumThreads nt(t);

                std::vector<std::vector<Point> > contours;
                findContours(img, contours, RETR_LIST, method);
                const std::vector<ContourSignature> sig = signatureOf(contours);

                ++r.comparisons;
                const SetDiff d = diffContourSets(refSig, sig);
                if (!d.equal())
                {
                    ++r.mismatches;
                    if (d.missing > 0) ++r.withMissing;
                    if (d.extra > 0)   ++r.withExtra;
                    if (r.details.size() < 20)
                    {
                        r.details.push_back(cv::format(
                            "method=%d nthreads=%d repeat=%d: %d missing, %d extra "
                            "(%d contours vs %d reference)",
                            (int)method, t, rep, d.missing, d.extra,
                            (int)sig.size(), (int)refSig.size()));
                    }
                }
            }
        }
    }
    return r;
}

static void printResult(const char* backendName, const BackendResult& r)
{
    std::cout << "  " << backendName << ": " << r.mismatches << " / "
              << r.comparisons << " comparisons diverged from the 1-thread reference"
              << " (" << r.withMissing << " lost contours, "
              << r.withExtra << " gained contours)" << std::endl;
    for (size_t i = 0; i < r.details.size(); ++i)
        std::cout << "      " << r.details[i] << std::endl;
}

// ---------------------------------------------------------------------------
// TEST 1 -- the substitution experiment.
// ---------------------------------------------------------------------------
TEST(Imgproc_ParallelBackendAB, DISABLED_findContours_ppl_vs_stdthread)
{
    applyOptionalCpuPinning();

    const Mat img = makeWorkloadImage(0x5EEDC0FFEEull);

    std::cout << "\nworkload: 1000x1000 thresholded noise, findContours(RETR_LIST)" << std::endl;
    std::cout << "methods: 1 (NONE), 2 (SIMPLE), 3 (TC89_L1), 4 (TC89_KCOS)"
              << "  -- CI failures were seen at methods 3 and 4" << std::endl;
    std::cout << "thread counts: 2..39 (the same range nthreads_consistency sweeps)" << std::endl;
    std::cout << "repeats: " << kRepeats << "  => "
              << (int)(sizeof(kMethods) / sizeof(kMethods[0])) * 38 * kRepeats
              << " comparisons per backend" << std::endl;
    std::cout << "machine: " << cv::getNumberOfCPUs() << " logical CPUs total "
              << "(the CI runner has 4; see pinning note above)\n" << std::endl;

    // --- A: whatever OpenCV was built with (ms-concurrency / PPL on this CI) ---
    BackendResult pplResult;
    {
        // currentParallelFramework() comes from core/private.hpp, which
        // test_precomp.hpp already pulls in. Printing it matters: it confirms
        // from inside the run that the framework really is ms-concurrency,
        // rather than assuming it from the CI log banner.
        const char* fw = cv::currentParallelFramework();
        std::cout << "[A] built-in backend: " << (fw ? fw : "(none/serial)") << std::endl;
        pplResult = measureDivergence(img);
        printResult("built-in", pplResult);
    }

    // --- B: identical decomposition, plain std::thread pool, standard join ---
    BackendResult stdResult;
    {
        std::cout << "\n[B] std::thread backend (same stripes, standard join)" << std::endl;
        ScopedBackendSwap swap(std::make_shared<StdThreadParallelBackend>());
        stdResult = measureDivergence(img);
        printResult("std::thread", stdResult);
    }

    std::cout << "\n----------------- verdict -----------------" << std::endl;
    if (pplResult.mismatches > 0 && stdResult.mismatches > 0)
    {
        std::cout << "BOTH backends diverge => OpenCV defect, PROVEN.\n"
                  << "The failure survives with PPL entirely removed, so the tracer\n"
                  << "is not genuinely label-exclusive. Look for state shared between\n"
                  << "labels in contours_truco.cpp." << std::endl;
    }
    else if (pplResult.mismatches > 0 && stdResult.mismatches == 0)
    {
        std::cout << "Only the built-in (PPL) backend diverges => strong evidence for\n"
                  << "the runtime. Same code, same stripe decomposition, same hardware;\n"
                  << "only the thread pool differs. Not absolute proof on its own -- a\n"
                  << "different pool has different timing. Run ppl_contract_audit.cpp,\n"
                  << "which tests PPL's documented guarantees directly." << std::endl;
    }
    else if (pplResult.mismatches == 0 && stdResult.mismatches > 0)
    {
        std::cout << "Only the std::thread backend diverges. Unexpected: suspect this\n"
                  << "diagnostic's own backend before concluding anything about OpenCV." << std::endl;
    }
    else
    {
        std::cout << "Neither backend diverged -- not reproduced on this machine.\n"
                  << "This box has " << cv::getNumberOfCPUs() << " CPUs; the CI runner has 4,\n"
                  << "so setNumThreads(38) is far more oversubscribed there (~9x vs ~3x)\n"
                  << "and explores many more interleavings. Raise kRepeats, or run on CI."
                  << std::endl;
    }

    // Deliberately no assertion on the built-in backend: this test's job is to
    // report which side diverges, not to fail the suite. The only genuine defect
    // in the diagnostic itself would be case 3 above.
    EXPECT_FALSE(pplResult.mismatches == 0 && stdResult.mismatches > 0)
        << "std::thread backend diverged while PPL did not -- the diagnostic backend "
           "is the prime suspect, not OpenCV.";
}

// ---------------------------------------------------------------------------
// TEST 2 -- in-situ audit of the runtime's stripe delivery.
//
// Goes through OpenCV's real cv::parallel_for_ (so the actual production
// dispatch path, wrapper and all) with a body that only records which stripes
// ran and publishes a plain per-stripe payload.
//
// OpenCV computes the stripe count deterministically BEFORE handing it to the
// runtime, so if a stripe never runs, or runs twice, no OpenCV defect can be
// responsible -- that is purely the runtime's delivery, and a failure here is
// conclusive on its own.
// ---------------------------------------------------------------------------
class StripeAuditBody : public ParallelLoopBody
{
public:
    StripeAuditBody(std::vector<std::atomic<int> >& execCount, std::vector<int>& payload)
        : execCount_(execCount), payload_(payload) {}

    void operator()(const Range& r) const CV_OVERRIDE
    {
        for (int i = r.start; i < r.end; ++i)
        {
            execCount_[i].fetch_add(1, std::memory_order_relaxed);
            // Plain, non-atomic write into this stripe's own slot -- the same
            // shape as findTRUContours' per-label accumulator. The calling
            // thread reads every slot after the join with no synchronisation of
            // our own, exactly as findTRUContoursImpl does.
            payload_[i] = i + 1;
        }
    }

private:
    std::vector<std::atomic<int> >& execCount_;
    std::vector<int>& payload_;
    StripeAuditBody& operator=(const StripeAuditBody&);
};

TEST(Imgproc_ParallelBackendAB, DISABLED_stripe_delivery_audit)
{
    applyOptionalCpuPinning();

    const int kTrials = 2000;
    const int kStripes = 997;       // prime, so chunking cannot divide evenly

    std::cout << "\nauditing cv::parallel_for_ stripe delivery: " << kStripes
              << " stripes x " << kTrials << " trials per thread count" << std::endl;
    const char* fw = cv::currentParallelFramework();
    std::cout << "backend: " << (fw ? fw : "(none/serial)")
              << ", machine has " << cv::getNumberOfCPUs() << " logical CPUs\n" << std::endl;

    long long totalSkipped = 0, totalDuplicated = 0, totalUnpublished = 0;

    // A representative spread rather than all of 2..39: this audit runs kTrials
    // dispatches per thread count, so the full sweep would be 38x the work for
    // little gain -- stripe delivery either holds or it does not, and it does
    // not interact with the contour approximation method at all.
    const int auditThreadCounts[] = { 2, 4, 8, 14, 38, 39 };

    for (size_t ti = 0; ti < sizeof(auditThreadCounts) / sizeof(auditThreadCounts[0]); ++ti)
    {
        const int t = auditThreadCounts[ti];
        ScopedNumThreads nt(t);

        long long skipped = 0, duplicated = 0, unpublished = 0;

        for (int trial = 0; trial < kTrials; ++trial)
        {
            std::vector<std::atomic<int> > execCount(kStripes);
            for (int i = 0; i < kStripes; ++i)
                execCount[i].store(0, std::memory_order_relaxed);
            std::vector<int> payload((size_t)kStripes, 0);

            StripeAuditBody body(execCount, payload);
            cv::parallel_for_(Range(0, kStripes), body);

            for (int i = 0; i < kStripes; ++i)
            {
                const int runs = execCount[i].load(std::memory_order_relaxed);
                if (runs == 0)     ++skipped;
                else if (runs > 1) duplicated += (runs - 1);

                if (runs >= 1 && payload[i] != i + 1)
                    ++unpublished;
            }

            if (skipped || duplicated || unpublished)
            {
                std::cout << "  !!! violation at nthreads=" << t
                          << " trial=" << trial << std::endl;
                break;
            }
        }

        std::cout << "  nthreads=" << t
                  << ": skipped=" << skipped
                  << " duplicated=" << duplicated
                  << " unpublished=" << unpublished << std::endl;

        totalSkipped += skipped;
        totalDuplicated += duplicated;
        totalUnpublished += unpublished;
    }

    std::cout << "\n----------------- verdict -----------------" << std::endl;
    if (totalSkipped || totalDuplicated || totalUnpublished)
    {
        std::cout << "The runtime failed to deliver stripes correctly. OpenCV fixes the\n"
                  << "stripe count before dispatch, so no OpenCV defect can skip or\n"
                  << "duplicate one, nor lose a worker's plain write across the join.\n"
                  << "This is the runtime's contract, and it is broken here: CONCLUSIVE."
                  << std::endl;
    }
    else
    {
        std::cout << "Stripe delivery is correct: every stripe ran exactly once and every\n"
                  << "worker write was visible after the join. So the FindTRUContours\n"
                  << "divergence is NOT explained by lost/duplicated stripes or by\n"
                  << "join visibility -- which points back at state shared between\n"
                  << "labels in OpenCV's own tracer." << std::endl;
    }

    EXPECT_EQ(0, totalSkipped)      << "cv::parallel_for_ never executed some stripes";
    EXPECT_EQ(0, totalDuplicated)   << "cv::parallel_for_ executed some stripes more than once";
    EXPECT_EQ(0, totalUnpublished)  << "a worker's plain write was not visible to the caller after the join";
}

}} // namespace
