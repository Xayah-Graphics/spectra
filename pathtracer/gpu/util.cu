#include <cstdio>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/util/check.cuh>
#include <string>

namespace spectra {
    int GPUInit(std::optional<int> cudaDevice) {
        cudaFree(nullptr);

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
        if (nDevices > 1 && !cudaDevice.has_value())
            throw std::runtime_error(diagnostics::Format("Found multiple GPUs.\n"
                                                         "On Windows, this unfortunately causes a significant slowdown with "
                                                         "Spectra.\n"
                                                         "Please select a single GPU and use the --gpu-device command line "
                                                         "option to specify it.\n"
                                                         "Found devices:\n%s",
                devices));
#endif

        int device = cudaDevice.value_or(0);
        CUDA_CHECK(cudaSetDevice(device));

        int hasUnifiedAddressing;
        CUDA_CHECK(cudaDeviceGetAttribute(&hasUnifiedAddressing, cudaDevAttrUnifiedAddressing, device));
        if (!hasUnifiedAddressing) SPECTRA_FATAL("The selected GPU device (%d) does not support unified addressing.", device);

        CUDA_CHECK(cudaDeviceSetLimit(cudaLimitStackSize, 8192));
        CUDA_CHECK(cudaDeviceSetLimit(cudaLimitPrintfFifoSize, 32 * 1024 * 1024));
        CUDA_CHECK(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

        return device;
    }

    void GPUThreadInit(const int cudaDevice) {
        CUDA_CHECK(cudaSetDevice(cudaDevice));
    }

    void GPUWait() {
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    void GPUMemset(void* ptr, int byte, size_t bytes) {
        CUDA_CHECK(cudaMemset(ptr, byte, bytes));
    }
} // namespace spectra
