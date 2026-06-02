#include <cstdio>
#include <spectra/pathtracer/core/diagnostics.cuh>
#include <spectra/pathtracer/core/options.cuh>
#include <spectra/pathtracer/gpu/util.cuh>
#include <spectra/pathtracer/util/check.cuh>
#include <string>

#ifdef NVTX
#ifdef SPECTRA_IS_WINDOWS
#include <windows.h>
#else
#include <sys/syscall.h>
#endif // SPECTRA_IS_WINDOWS
#include "nvtx3/nvToolsExt.h"
#include "nvtx3/nvToolsExtCuda.h"
#endif

namespace spectra {
    void GPUInit() {
        cudaFree(nullptr);

        int driverVersion;
        CUDA_CHECK(cudaDriverGetVersion(&driverVersion));
        int runtimeVersion;
        CUDA_CHECK(cudaRuntimeGetVersion(&runtimeVersion));
        auto versionToString = [](int version) {
            int major = version / 1000;
            int minor = (version - major * 1000) / 10;
            return std::to_string(major) + "." + std::to_string(minor);
        };

        int nDevices;
        CUDA_CHECK(cudaGetDeviceCount(&nDevices));
        std::string devices;
        for (int i = 0; i < nDevices; ++i) {
            cudaDeviceProp deviceProperties;
            CUDA_CHECK(cudaGetDeviceProperties(&deviceProperties, i));
            CHECK(deviceProperties.canMapHostMemory);

            int clockRateKHz = 0;
            cudaDeviceGetAttribute(&clockRateKHz, cudaDevAttrClockRate, i);
            float clockRate = clockRateKHz;

            char deviceString[512];
            std::snprintf(deviceString, sizeof(deviceString),
                "CUDA device %d (%s) with %g MiB, %d SMs running at %g MHz "
                "with shader model %d.%d",
                i, deviceProperties.name, static_cast<double>(deviceProperties.totalGlobalMem) / (1024. * 1024.), deviceProperties.multiProcessorCount, static_cast<double>(clockRate) / 1000., deviceProperties.major, deviceProperties.minor);
            devices += deviceString;
            devices += "\n";
        }

#ifdef SPECTRA_IS_WINDOWS
        if (nDevices > 1)
            throw std::runtime_error(diagnostics::Format("Found multiple GPUs.\n"
                                                                  "On Windows, this unfortunately causes a significant slowdown with "
                                                                  "Spectra.\n"
                                                                  "Please select a single GPU and use the --gpu-device command line "
                                                                  "option to specify it.\n"
                                                                  "Found devices:\n%s",
                devices));
#endif

        int device = Options->gpuDevice ? *Options->gpuDevice : 0;
#ifdef NVTX
        nvtxNameCuDevice(device, "__device__");
#endif
        CUDA_CHECK(cudaSetDevice(device));

        int hasUnifiedAddressing;
        CUDA_CHECK(cudaDeviceGetAttribute(&hasUnifiedAddressing, cudaDevAttrUnifiedAddressing, device));
        if (!hasUnifiedAddressing) SPECTRA_FATAL("The selected GPU device (%d) does not support unified addressing.", device);

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
#endif // NVTX
    }

    void GPUThreadInit() {
        int device = Options->gpuDevice ? *Options->gpuDevice : 0;
        CUDA_CHECK(cudaSetDevice(device));
    }

    void GPURegisterThread(const char* name) {
#ifdef NVTX
#ifdef SPECTRA_IS_WINDOWS
        nvtxNameOsThread(GetCurrentThreadId(), name);
#else
        nvtxNameOsThread(syscall(SYS_gettid), name);
#endif
#endif
    }

    void GPUNameStream(cudaStream_t stream, const char* name) {
#ifdef NVTX
        nvtxNameCuStream(stream, name);
#endif
    }

    void GPUWait() {
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    void GPUMemset(void* ptr, int byte, size_t bytes) {
        CUDA_CHECK(cudaMemset(ptr, byte, bytes));
    }
} // namespace spectra
