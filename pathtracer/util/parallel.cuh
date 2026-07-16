#ifndef SPECTRA_PATHTRACER_UTIL_PARALLEL_H
#define SPECTRA_PATHTRACER_UTIL_PARALLEL_H

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace spectra {
    // Parallel Function Declarations
    void ParallelInit(int nThreads, int cudaDevice);
    void ParallelCleanup();

    int RunningThreads();

    // ThreadLocal Definition
    template <typename T>
    class ThreadLocal {
    public:
        // ThreadLocal Public Methods
        ThreadLocal() : hashTable(4 * RunningThreads()), create([]() { return T(); }) {}

        ThreadLocal(std::function<T(void)>&& c) : hashTable(4 * RunningThreads()), create(c) {}

        T& Get();

        template <typename F>
        void ForAll(F&& func);

    private:
        // ThreadLocal Private Members
        struct Entry {
            std::thread::id tid;
            T value;
        };

        std::shared_mutex mutex;
        std::vector<std::unique_ptr<Entry>> hashTable;
        std::function<T(void)> create;
    };

    // ThreadLocal Inline Methods
    template <typename T>
    T& ThreadLocal<T>::Get() {
        std::thread::id tid = std::this_thread::get_id();
        while (true) {
            std::unique_lock<std::shared_mutex> lock(mutex);
            if (hashTable.empty()) hashTable.resize(4);

            size_t hash = std::hash<std::thread::id>()(tid) % hashTable.size();
            size_t step = 1;
            for (size_t tries = 0; tries < hashTable.size(); ++tries) {
                if (hashTable[hash] && hashTable[hash]->tid == tid) return hashTable[hash]->value;

                if (!hashTable[hash]) {
                    T newItem       = create();
                    hashTable[hash] = std::make_unique<Entry>(Entry{tid, std::move(newItem)});
                    return hashTable[hash]->value;
                }

                hash += step;
                ++step;
                if (hash >= hashTable.size()) hash %= hashTable.size();
            }

            std::vector<std::unique_ptr<Entry>> newHashTable(2 * hashTable.size());
            for (std::unique_ptr<Entry>& entry : hashTable) {
                if (!entry) continue;

                size_t hash = std::hash<std::thread::id>()(entry->tid) % newHashTable.size();
                size_t step = 1;
                while (newHashTable[hash]) {
                    hash += step;
                    ++step;
                    if (hash >= newHashTable.size()) hash %= newHashTable.size();
                }
                newHashTable[hash] = std::move(entry);
            }
            hashTable.swap(newHashTable);
        }
    }

    template <typename T>
    template <typename F>
    void ThreadLocal<T>::ForAll(F&& func) {
        mutex.lock();
        for (auto& entry : hashTable) {
            if (entry) func(entry->value);
        }
        mutex.unlock();
    }

    class AtomicDouble {
    public:
        __host__ __device__ explicit AtomicDouble(double v = 0) : value(v) {}

        __host__ __device__ operator double() const {
            return value;
        }

        __host__ __device__ double operator=(double v) {
            value = v;
            return value;
        }

        __host__ __device__ void Add(double v) {
#if defined(__CUDA_ARCH__)
            atomicAdd(&value, v);
#else
            value += v;
#endif
        }

    private:
        double value{};
    };

    void ParallelFor(int64_t start, int64_t end, std::function<void(int64_t, int64_t)> func);
    void ParallelFor2D(const Bounds2i& extent, std::function<void(Bounds2i)> func);

    // Parallel Inline Functions
    inline void ParallelFor(int64_t start, int64_t end, std::function<void(int64_t)> func) {
        ParallelFor(start, end, [&func](int64_t start, int64_t end) {
            for (int64_t i = start; i < end; ++i) func(i);
        });
    }

    inline void ParallelFor2D(const Bounds2i& extent, std::function<void(Point2i)> func) {
        ParallelFor2D(extent, [&func](Bounds2i b) {
            for (Point2i p : b) func(p);
        });
    }

    class ThreadPool;

    // ParallelJob Definition
    class ParallelJob {
    public:
        // ParallelJob Public Methods
        virtual ~ParallelJob() {
        }

        virtual bool HaveWork() const                            = 0;
        virtual void RunStep(std::unique_lock<std::mutex>* lock) = 0;

        bool Finished() const {
            return !HaveWork() && activeWorkers == 0;
        }


        // ParallelJob Public Members
        static ThreadPool* threadPool;

    protected:
    private:
        // ParallelJob Private Members
        friend class ThreadPool;
        int activeWorkers = 0;
        ParallelJob *prev = nullptr, *next = nullptr;
        bool removed = false;
    };

    // ThreadPool Definition
    class ThreadPool {
    public:
        // ThreadPool Public Methods
        ThreadPool(int nThreads, int cudaDevice);

        ~ThreadPool();

        size_t size() const {
            return threads.size();
        }

        std::unique_lock<std::mutex> AddToJobList(ParallelJob* job);
        void RemoveFromJobList(ParallelJob* job);

        void WorkOrWait(std::unique_lock<std::mutex>* lock);
        bool WorkOrReturn();

    private:
        // ThreadPool Private Methods
        void Worker();

        // ThreadPool Private Members
        std::vector<std::thread> threads;
        int cudaDevice{};
        mutable std::mutex mutex;
        bool shutdownThreads = false;
        ParallelJob* jobList = nullptr;
        std::condition_variable jobListCondition;
    };

    bool DoParallelWork();

    // AsyncJob Definition
    template <typename T>
    class AsyncJob : public ParallelJob {
    public:
        // AsyncJob Public Methods
        AsyncJob(std::function<T(void)> w) : func(std::move(w)) {}

        ~AsyncJob() override {
            Wait();
        }

        bool HaveWork() const {
            return !started;
        }

        void RunStep(std::unique_lock<std::mutex>* lock) {
            threadPool->RemoveFromJobList(this);
            started = true;
            lock->unlock();

            try {
                T r = func();
                std::lock_guard<std::mutex> resultLock(mutex);
                result = std::move(r);
            } catch (...) {
                std::lock_guard<std::mutex> resultLock(mutex);
                exception = std::current_exception();
            }
            cv.notify_all();
        }

        bool IsReady() const {
            std::lock_guard<std::mutex> lock(mutex);
            return result.has_value() || exception != nullptr;
        }

        T GetResult() {
            Wait();
            std::lock_guard<std::mutex> lock(mutex);
            if (exception) std::rethrow_exception(exception);
            return *result;
        }

        pstd::optional<T> TryGetResult(std::unique_lock<std::mutex>& externalLock) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (exception) std::rethrow_exception(exception);
                if (result) return result;
            }

            externalLock.unlock();
            DoParallelWork();
            externalLock.lock();
            return {};
        }

        void Wait() {
            while (!IsReady() && DoParallelWork());
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this]() { return result.has_value() || exception != nullptr; });
        }

        void DoWork() {
            try {
                T r = func();
                std::lock_guard<std::mutex> resultLock(mutex);
                result = std::move(r);
            } catch (...) {
                std::lock_guard<std::mutex> resultLock(mutex);
                exception = std::current_exception();
            }
            cv.notify_all();
        }

    private:
        // AsyncJob Private Members
        std::function<T(void)> func;
        bool started = false;
        pstd::optional<T> result;
        std::exception_ptr exception;
        mutable std::mutex mutex;
        std::condition_variable cv;
    };

    // Asynchronous Task Launch Function Definitions
    template <typename F, typename... Args>
    auto RunAsync(F func, Args&&... args) {
        // Create _AsyncJob_ for _func_ and _args_
        auto fvoid       = std::bind(func, std::forward<Args>(args)...);
        using R          = std::invoke_result_t<F, Args...>;
        auto job          = std::make_unique<AsyncJob<R>>(std::move(fvoid));

        // Enqueue _job_ or run it immediately
        std::unique_lock<std::mutex> lock;
        if (RunningThreads() == 1)
            job->DoWork();
        else
            lock = ParallelJob::threadPool->AddToJobList(job.get());

        return job;
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_PARALLEL_H
