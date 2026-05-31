#ifndef SPECTRA_PATHTRACER_UTIL_PROGRESSREPORTER_H
#define SPECTRA_PATHTRACER_UTIL_PROGRESSREPORTER_H

#include <spectra/pathtracer/util/float.h>

#include <spectra/pathtracer/util/pstd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <cuda_runtime.h>
#include <vector>

namespace spectra
{
    // Timer Definition
    class Timer
    {
    public:
        Timer() { start = clock::now(); }

        double ElapsedSeconds() const
        {
            clock::time_point now = clock::now();
            int64_t elapseduS =
                std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            return elapseduS / 1000000.;
        }


    private:
        using clock = std::chrono::steady_clock;
        clock::time_point start;
    };

    // ProgressReporter Definition
    class ProgressReporter
    {
    public:
        // ProgressReporter Public Methods
        ProgressReporter() : quiet(true)
        {
        }

        ProgressReporter(int64_t totalWork, std::string title, bool quiet, bool gpu = false);

        ~ProgressReporter();

        void Update(int64_t num = 1);
        void Done();
        double ElapsedSeconds() const;


    private:
        // ProgressReporter Private Methods
        void printBar();

        // ProgressReporter Private Members
        int64_t totalWork;
        std::string title;
        bool quiet;
        Timer timer;
        std::atomic<int64_t> workDone;
        std::atomic<bool> exitThread;
        std::thread updateThread;
        pstd::optional<float> finishTime;

        std::vector<cudaEvent_t> gpuEvents;
        std::atomic<size_t> gpuEventsLaunchedOffset;
        int gpuEventsFinishedOffset;
    };

    // ProgressReporter Inline Method Definitions
    inline double ProgressReporter::ElapsedSeconds() const
    {
        return finishTime ? *finishTime : timer.ElapsedSeconds();
    }

    inline void ProgressReporter::Update(int64_t num)
    {
        if (gpuEvents.size() > 0)
        {
            if (gpuEventsLaunchedOffset + num <= gpuEvents.size())
            {
                while (num-- > 0)
                {
                    CHECK_EQ(cudaEventRecord(gpuEvents[gpuEventsLaunchedOffset]),
                             cudaSuccess);
                    ++gpuEventsLaunchedOffset;
                }
            }
            return;
        }
        if (num == 0 || quiet)
            return;
        workDone += num;
    }
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_UTIL_PROGRESSREPORTER_H
