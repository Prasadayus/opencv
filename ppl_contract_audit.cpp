// PPL contract audit -- standalone, ZERO OpenCV code involved.
//
// PURPOSE
// -------
// Settle, with a positive demonstration rather than an inference, whether
// Microsoft's PPL/ConcRT runtime honours the four guarantees OpenCV's
// parallel_for_ implicitly relies on. Because nothing here links or includes
// OpenCV, a failure in this program cannot be blamed on OpenCV. That is the
// whole point: you cannot prove PPL is at fault by repeatedly failing to
// convict OpenCV, but you CAN prove it by catching PPL breaking its own
// documented contract.
//
// The companion probe ppl_ctx_handoff_repro.cpp already tested exactly ONE of
// the four properties (caller->worker visibility) and found it clean across
// ~50M reads on the real CI runner. The other three have never been tested.
// This program tests all four, with #1 kept for completeness/regression.
//
// THE FOUR PROPERTIES
//   1. caller -> worker visibility : a plain write made before dispatch is seen
//                                    by every worker.            [previously clean]
//   2. exactly-once delivery       : every index in [first,last) runs exactly
//                                    once -- never skipped, never duplicated.
//   3. worker -> caller visibility : plain writes made by workers are visible to
//                                    the calling thread once parallel_for returns,
//                                    with no explicit synchronisation by us.
//   4. write integrity             : a multi-word payload written by a worker
//                                    arrives whole, not partially.
//
// WHY 2, 3 AND 4 ARE THE ONES THAT MATTER FOR THE OPENCV FAILURES
// ---------------------------------------------------------------
// modules/imgproc/src/contours_truco.cpp does:
//     cv::parallel_for_(cv::Range(1, numLabels), worker);      // nstripes defaults
// With the default nstripes (-1), parallel.cpp:211 sets nstripes = len, i.e. ONE
// STRIPE PER CONNECTED-COMPONENT LABEL, independent of the thread count. Each
// worker fills its own slot of a plain std::vector<AccumulatorT>, and the calling
// thread reads every slot after the join. So the real code depends on precisely
// properties 2, 3 and 4 -- and OpenCV's own test Imgproc_FindTRUContours.
// nthreads_consistency asserts that the result is identical for thread counts
// 1 and 2..39, which MUST hold if those properties hold, because the work
// decomposition itself does not change with the thread count.
//
// REPRODUCING CI CONDITIONS FAITHFULLY
// ------------------------------------
// The GitHub Actions windows-11-arm runner is an Azure Cobalt 100 (Arm Neoverse
// N2) with FOUR vCPUs -- confirmed by the CI log banner "Parallel framework:
// ms-concurrency (nthreads=4)". OpenCV's test then drives setNumThreads(2..39),
// and parallel.cpp:764 turns that into
//     Scheduler::Create(SchedulerPolicy(2, MinConcurrency, n-1, MaxConcurrency, n-1))
// so n=38 means 37 virtual processors on 4 physical cores: roughly 9x
// oversubscription. That is very likely why CI trips these failures while a
// 10-12 core local machine does not -- constant preemption explores far more
// interleavings. This program mirrors that exactly: same scheduler policy, same
// per-call Attach/Detach, same one-index-per-task dispatch shape as
// parallel.cpp:447-450, and the same heavy oversubscription.
//
// BUILD (x64 or arm64 native tools prompt):
//     cl /EHsc /O2 /std:c++17 ppl_contract_audit.cpp
// RUN:
//     ppl_contract_audit.exe
//
// INTERPRETING THE RESULT -- this is the part that settles the argument:
//   ANY property 2/3/4 failure  => PPL/ConcRT breaks its own contract on this
//                                  hardware. PROVEN, and OpenCV is not in the
//                                  binary, so it cannot be OpenCV's fault.
//                                  This is the answer for the CI failures.
//   ALL properties clean        => PPL delivers what OpenCV assumes. The
//                                  nthreads_consistency divergence must then come
//                                  from OpenCV's own code sharing state between
//                                  labels (i.e. the tracer is not actually
//                                  label-exclusive). Run the companion
//                                  backend A/B test next to confirm.

#include <windows.h>
#include <ppl.h>
#include <concrt.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// Mirrors parallel.cpp:764's scheduler policy so the audit runs under the same
// oversubscription the OpenCV tests create on the runner.
class ScopedPplScheduler
{
public:
    explicit ScopedPplScheduler(int nThreads)
        : sched_(nullptr)
    {
        if (nThreads <= 1)
            return;   // OpenCV disables PPL entirely below 2 threads
        sched_ = Concurrency::Scheduler::Create(
            Concurrency::SchedulerPolicy(2,
                Concurrency::MinConcurrency, nThreads - 1,
                Concurrency::MaxConcurrency, nThreads - 1));
    }

    ~ScopedPplScheduler()
    {
        if (sched_)
            sched_->Release();
    }

    // Same attach/dispatch/detach dance as parallel.cpp:598-609.
    template <typename Fn>
    void dispatch(int first, int last, const Fn& fn) const
    {
        if (!sched_ || sched_->Id() == Concurrency::CurrentScheduler::Id())
        {
            Concurrency::parallel_for(first, last, fn);
        }
        else
        {
            sched_->Attach();
            Concurrency::parallel_for(first, last, fn);
            Concurrency::CurrentScheduler::Detach();
        }
    }

    unsigned virtualProcessors() const
    {
        return sched_ ? sched_->GetNumberOfVirtualProcessors() : 0u;
    }

private:
    Concurrency::Scheduler* sched_;
    ScopedPplScheduler(const ScopedPplScheduler&);
    ScopedPplScheduler& operator=(const ScopedPplScheduler&);
};

// Deliberately a multi-word, non-atomic payload. A lone aligned int store is
// atomic in practice on ARM64, which would mask a partial-visibility problem;
// this mirrors OpenCV's real per-label accumulator slot (a vector of vectors)
// far more closely, and lets property 4 detect a half-published write.
struct Payload
{
    uint64_t magic;
    uint64_t index;
    uint64_t derived;
    uint64_t checksum;

    void fill(uint64_t i)
    {
        magic    = 0xA5A5C0DEDEADBEEFull;
        index    = i;
        derived  = i * 6364136223846793005ull + 1442695040888963407ull;
        checksum = magic ^ index ^ derived;
    }

    bool valid(uint64_t i) const
    {
        if (magic != 0xA5A5C0DEDEADBEEFull) return false;
        if (index != i) return false;
        if (derived != i * 6364136223846793005ull + 1442695040888963407ull) return false;
        return checksum == (magic ^ index ^ derived);
    }
};

struct Totals
{
    long long trials              = 0;
    long long staleCallerToWorker = 0;   // property 1
    long long skippedIndices      = 0;   // property 2
    long long duplicatedIndices   = 0;   // property 2
    long long invisibleToCaller   = 0;   // property 3
    long long corruptPayloads     = 0;   // property 4
    long long totalWorkerReads    = 0;
    long long totalIndices        = 0;

    bool clean() const
    {
        return staleCallerToWorker == 0 && skippedIndices == 0 &&
               duplicatedIndices == 0 && invisibleToCaller == 0 &&
               corruptPayloads == 0;
    }
};

// One trial = one parallel_for over `tasks` indices, auditing all four
// properties simultaneously. Returns via `t`.
void runTrial(const ScopedPplScheduler& sched, int tasks, long long trialId, Totals& t)
{
    // --- property 1 setup: plain write by the caller, read by every worker ---
    // Deliberately a plain non-atomic, non-volatile long long -- the same shape as
    // ParallelLoopBodyWrapperContext's `cv::RNG rng` member (parallel.cpp:263),
    // which is written by the caller at :214 and read by every worker at :340
    // with no fence. The question is whether dispatch alone publishes it.
    long long sentinel = trialId;

    // --- property 2 setup: per-index execution counters ---
    // Atomic because *counting* must itself be race-free; we are auditing PPL's
    // delivery, not testing whether unsynchronised counting works.
    std::vector<std::atomic<int>> execCount(tasks);
    for (int i = 0; i < tasks; ++i)
        execCount[i].store(0, std::memory_order_relaxed);

    // --- properties 3 and 4 setup: plain per-index payload slots ---
    // Exactly OpenCV's shape: worker writes its own slot, caller reads all
    // slots after the join with no explicit synchronisation.
    std::vector<Payload> slots(tasks);
    std::memset(slots.data(), 0, slots.size() * sizeof(Payload));

    std::atomic<long long> staleReads{0};
    std::atomic<long long> workerReads{0};

    sched.dispatch(0, tasks, [&](int i) {
        // property 1: did the caller's pre-dispatch write reach us?
        const long long observed = sentinel;
        workerReads.fetch_add(1, std::memory_order_relaxed);
        if (observed != trialId)
            staleReads.fetch_add(1, std::memory_order_relaxed);

        // property 2: record that this index ran
        execCount[i].fetch_add(1, std::memory_order_relaxed);

        // properties 3 and 4: publish a checksummed payload into our own slot
        slots[i].fill((uint64_t)i);
    });

    // ---- audit, on the calling thread, immediately after the join ----
    for (int i = 0; i < tasks; ++i)
    {
        const int runs = execCount[i].load(std::memory_order_relaxed);
        if (runs == 0)      ++t.skippedIndices;
        else if (runs > 1)  t.duplicatedIndices += (runs - 1);

        // Only meaningful to check visibility for indices that actually ran.
        if (runs >= 1)
        {
            const Payload& p = slots[i];
            const bool allZero = (p.magic == 0 && p.index == 0 &&
                                  p.derived == 0 && p.checksum == 0);
            if (allZero)
                ++t.invisibleToCaller;      // property 3: write never became visible
            else if (!p.valid((uint64_t)i))
                ++t.corruptPayloads;        // property 4: arrived partially/garbled
        }
    }

    t.staleCallerToWorker += staleReads.load();
    t.totalWorkerReads    += workerReads.load();
    t.totalIndices        += tasks;
    ++t.trials;
}

void report(const char* label, int nThreads, unsigned vprocs, int tasks, const Totals& t)
{
    printf("\n--- %s (setNumThreads=%d -> %u virtual processors, %d tasks/trial) ---\n",
           label, nThreads, vprocs, tasks);
    printf("  trials                        : %lld\n", t.trials);
    printf("  indices dispatched            : %lld\n", t.totalIndices);
    printf("  worker reads (property 1)     : %lld\n", t.totalWorkerReads);
    printf("  [1] stale caller->worker      : %lld\n", t.staleCallerToWorker);
    printf("  [2] indices never executed    : %lld\n", t.skippedIndices);
    printf("  [2] duplicate executions      : %lld\n", t.duplicatedIndices);
    printf("  [3] writes invisible to caller: %lld\n", t.invisibleToCaller);
    printf("  [4] corrupt/partial payloads  : %lld\n", t.corruptPayloads);
    printf("  verdict                       : %s\n", t.clean() ? "clean" : "*** CONTRACT VIOLATED ***");
}

} // namespace

int main(int argc, char** argv)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    printf("=== PPL/ConcRT contract audit (no OpenCV linked) ===\n");
    printf("PID                  : %lu\n", GetCurrentProcessId());
    printf("logical processors    : %lu\n", si.dwNumberOfProcessors);

    // Optional: pin this process to N logical processors, so a many-core dev box
    // can reproduce the CI runner's oversubscription ratio without a CI round
    // trip. The runner has 4 vCPUs; a 12-core machine running setNumThreads(38)
    // is only ~3x oversubscribed, versus ~9x on CI -- and oversubscription is
    // what drives preemption, which is what exposes ordering bugs. Pinning to 4
    // makes the ratio match.
    //
    //   ppl_contract_audit.exe            -> use every core (default)
    //   ppl_contract_audit.exe 4          -> behave like the 4-vCPU CI runner
    //
    // Caveat worth stating plainly: this reproduces the CORE COUNT, not the
    // silicon. It cannot reproduce anything specific to Cobalt 100 / Neoverse N2
    // that differs from this machine's cores. A failure under pinning is still
    // real and still proves the contract is broken; a clean run under pinning is
    // weaker evidence than a clean run on the runner itself.
    if (argc > 1)
    {
        const int wanted = atoi(argv[1]);
        if (wanted > 0 && (DWORD)wanted <= si.dwNumberOfProcessors)
        {
            DWORD_PTR mask = 0;
            for (int i = 0; i < wanted; ++i)
                mask |= (DWORD_PTR)1 << i;
            if (SetProcessAffinityMask(GetCurrentProcess(), mask))
                printf("affinity              : pinned to %d logical processors "
                       "(mimicking the CI runner)\n", wanted);
            else
                printf("affinity              : FAILED to pin (error %lu), "
                       "running unpinned\n", GetLastError());
        }
        else
        {
            printf("affinity              : ignoring '%s' -- must be 1..%lu\n",
                   argv[1], si.dwNumberOfProcessors);
        }
    }

    printf("\nThe GitHub windows-11-arm runner reports 4 vCPUs (its CI log banner\n");
    printf("reads \"Parallel framework: ms-concurrency (nthreads=4)\"). OpenCV's\n");
    printf("nthreads_consistency test drives setNumThreads(2..39) on it, so the\n");
    printf("high-thread configurations below are ~9x oversubscribed -- matching CI.\n");

    // 997 tasks at EVERY thread count -- do not go back to scaling the task
    // count with the thread count.
    //
    // The first version of this file paired small task counts with small thread
    // counts (64 tasks at 2 and 4 threads, 128 at 8, 256 at 14, 512 at 38, and
    // 997 only at 39). It came back clean, which looked like it exonerated PPL.
    // It did not: the in-situ audit in
    // modules/imgproc/test/test_parallel_backend_ab.cpp drives 997 tasks at
    // EVERY thread count, and its worst failure -- 86 consecutive stripes
    // (911..996, i.e. the entire tail of the range) silently never executed --
    // occurred at nThreads=4, where this file was only ever testing 64 tasks.
    // The two results appeared to contradict each other purely because this one
    // never exercised the failing combination.
    //
    // 997 is prime, so no chunking scheme divides it evenly -- which matters,
    // because the losses observed in situ are CONTIGUOUS RUNS (1, 3, 4, 18 and
    // 86 consecutive indices), the signature of a whole chunk being assigned and
    // then never run.
    //
    // If this now reports [2] indices never executed > 0, that is raw
    // Concurrency::parallel_for failing to execute part of its own range with NO
    // OpenCV linked into the binary -- proof the defect is PPL's, not OpenCV's.
    // 2000 trials, not 20000. Raising the task count to 997 everywhere already
    // multiplies the work; keeping 20000 trials would mean 7 x 20000 x 997 =
    // ~140M dispatches, roughly 4.7x the previous run, on a job that has already
    // had one step hang. 2000 is ample: in situ, the FIRST violation at each
    // thread count appeared at trial 58, 53, 90, 7 and 143 respectively, so
    // 2000 trials leaves better than a 10x margin while total work (~14M
    // dispatches) stays BELOW the previous run's ~30M.
    struct Config { int nThreads; int tasks; long long trials; const char* label; };
    const Config configs[] = {
        {  2, 997, 2000, "2 threads (1 vproc -- no real concurrency)" },
        {  3, 997, 2000, "3 threads"                                  },
        {  4, 997, 2000, "4 threads (= runner vCPUs, worst in situ)"   },
        {  8, 997, 2000, "8 threads"                                   },
        { 14, 997, 2000, "14 threads (a failing count)"                },
        { 38, 997, 2000, "38 threads (a failing count)"                },
        { 39, 997, 2000, "39 threads"                                  },
    };

    bool anyViolation = false;

    for (size_t c = 0; c < sizeof(configs) / sizeof(configs[0]); ++c)
    {
        const Config& cfg = configs[c];
        ScopedPplScheduler sched(cfg.nThreads);

        Totals t;
        for (long long trial = 0; trial < cfg.trials; ++trial)
        {
            runTrial(sched, cfg.tasks, trial, t);

            // Bail out early on the first violation: the point is proof, and a
            // single reproducible violation is already conclusive.
            if (!t.clean())
            {
                printf("\n!!! contract violation on trial %lld of '%s' -- stopping this config\n",
                       trial, cfg.label);
                break;
            }
        }

        report(cfg.label, cfg.nThreads, sched.virtualProcessors(), cfg.tasks, t);
        if (!t.clean())
            anyViolation = true;
    }

    printf("\n================ CONCLUSION ================\n");
    if (anyViolation)
    {
        printf("PPL/ConcRT VIOLATED its documented contract on this machine.\n");
        printf("No OpenCV code is linked into this binary, so this cannot be an\n");
        printf("OpenCV defect. Whichever property failed above is precisely what\n");
        printf("OpenCV's parallel_for_ relies on, and it explains the CI failures.\n");
        printf("This is a positive demonstration, not an inference: PROVEN.\n");
        return 1;
    }

    printf("PPL/ConcRT honoured all four guarantees across every configuration,\n");
    printf("including the heavily oversubscribed ones that match the CI runner.\n");
    printf("This does NOT prove PPL is flawless -- only that the four properties\n");
    printf("OpenCV depends on held here, at this scale. But it means the\n");
    printf("nthreads_consistency divergence is not explained by PPL skipping a\n");
    printf("stripe, running one twice, or failing to publish a worker's writes.\n");
    printf("Next step: the backend A/B test, which holds the work decomposition\n");
    printf("fixed and swaps only the thread pool.\n");
    return 0;
}
