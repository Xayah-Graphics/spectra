#include <spectra/pathtracer/gpu/util.h>

#include <spectra/pathtracer/core/options.h>
#include <spectra/pathtracer/util/check.h>
#include <spectra/pathtracer/util/error.h>
#include <spectra/pathtracer/core/diagnostics.h>
#ifdef NVTX
#ifdef SPECTRA_IS_WINDOWS
#include <windows.h>
#else
#include <sys/syscall.h>
#endif  // SPECTRA_IS_WINDOWS
#include "nvtx3/nvToolsExt.h"
#include "nvtx3/nvToolsExtCuda.h"
#endif

namespace spectra
{
    void GPUInit()
    {
        cudaFree(nullptr);

        int driverVersion;
        CUDA_CHECK(cudaDriverGetVersion(&driverVersion));
        int runtimeVersion;
        CUDA_CHECK(cudaRuntimeGetVersion(&runtimeVersion));
        auto versionToString = [](int version)
        {
            int major = version / 1000;
            int minor = (version - major * 1000) / 10;
            return StringPrintf("%d.%d", major, minor);
        };

        int nDevices;
        CUDA_CHECK(cudaGetDeviceCount(&nDevices));
        std::string devices;
        for (int i = 0; i < nDevices; ++i)
        {
            cudaDeviceProp deviceProperties;
            CUDA_CHECK(cudaGetDeviceProperties(&deviceProperties, i));
            CHECK(deviceProperties.canMapHostMemory);

            int clockRateKHz = 0;
            cudaDeviceGetAttribute(&clockRateKHz, cudaDevAttrClockRate, i);
            float clockRate = clockRateKHz;

            std::string deviceString = StringPrintf(
                "CUDA device %d (%s) with %f MiB, %d SMs running at %f MHz "
                "with shader model %d.%d",
                i, deviceProperties.name, deviceProperties.totalGlobalMem / (1024. * 1024.),
                deviceProperties.multiProcessorCount, clockRate / 1000.,
                deviceProperties.major, deviceProperties.minor);
            devices += deviceString + "\n";
        }

#ifdef SPECTRA_IS_WINDOWS
        if (nDevices > 1)
            ErrorExit("Found multiple GPUs.\n"
                      "On Windows, this unfortunately causes a significant slowdown with "
                      "pbrt.\n"
                      "Please select a single GPU and use the --gpu-device command line "
                      "option to specify it.\n"
                      "Found devices:\n%s",
                      devices);
#endif

        int device = Options->gpuDevice ? *Options->gpuDevice : 0;
#ifdef NVTX
        nvtxNameCuDevice(device, "SPECTRA_GPU");
#endif
        CUDA_CHECK(cudaSetDevice(device));

        int hasUnifiedAddressing;
        CUDA_CHECK(cudaDeviceGetAttribute(&hasUnifiedAddressing, cudaDevAttrUnifiedAddressing,
            device));
        if (!hasUnifiedAddressing)
            SPECTRA_FATAL("The selected GPU device (%d) does not support unified addressing.",
                  device);

        CUDA_CHECK(cudaDeviceSetLimit(cudaLimitStackSize, 8192));
        size_t stackSize;
        CUDA_CHECK(cudaDeviceGetLimit(&stackSize, cudaLimitStackSize));

        CUDA_CHECK(cudaDeviceSetLimit(cudaLimitPrintfFifoSize, 32 * 1024 * 1024));

        CUDA_CHECK(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

#ifdef NVTX
#ifdef SPECTRA_IS_WINDOWS
        nvtxNameOsThread(GetCurrentThreadId(), "MAIN_THREAD");
#else
        nvtxNameOsThread(syscall(SYS_gettid), "MAIN_THREAD");
#endif
#endif  // NVTX
    }

    void GPUThreadInit()
    {
        int device = Options->gpuDevice ? *Options->gpuDevice : 0;
        CUDA_CHECK(cudaSetDevice(device));
    }

    void GPURegisterThread(const char* name)
    {
#ifdef NVTX
#ifdef SPECTRA_IS_WINDOWS
        nvtxNameOsThread(GetCurrentThreadId(), name);
#else
        nvtxNameOsThread(syscall(SYS_gettid), name);
#endif
#endif
    }

    void GPUNameStream(cudaStream_t stream, const char* name)
    {
#ifdef NVTX
        nvtxNameCuStream(stream, name);
#endif
    }

    void GPUWait()
    {
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    void GPUMemset(void* ptr, int byte, size_t bytes)
    {
        CUDA_CHECK(cudaMemset(ptr, byte, bytes));
    }
} // namespace spectra
