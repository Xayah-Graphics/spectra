#include <cstdlib>
#include <spectra/pathtracer/util/memory.h>
#ifdef SPECTRA_HAVE_MALLOC_H
#include <malloc.h> // for both memalign and _aligned_malloc
#endif
#ifdef SPECTRA_IS_WINDOWS
// clang-format off
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
// clang-format on
#endif // SPECTRA_IS_WINDOWS
#ifdef SPECTRA_IS_LINUX
#include <cstdio>
#include <unistd.h>
#endif // SPECTRA_IS_LINUX
#ifdef SPECTRA_IS_OSX
#include <mach/mach.h>
#endif // SPECTRA_IS_OSX

namespace spectra {
    /*
     * Author:  David Robert Nadeau
     * Site:    http://NadeauSoftware.com/
     * License: Creative Commons Attribution 3.0 Unported License
     *          http://creativecommons.org/licenses/by/3.0/deed.en_US
     *
     * Returns the current resident set size (physical memory use) measured
     * in bytes, or zero if the value cannot be determined on this OS.
     */
    size_t GetCurrentRSS() {
#ifdef SPECTRA_IS_WINDOWS
        PROCESS_MEMORY_COUNTERS info;
        GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
        return (size_t) info.WorkingSetSize;
#elif defined(SPECTRA_IS_OSX)
        struct mach_task_basic_info info;
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &infoCount) != KERN_SUCCESS) return (size_t) 0L; /* Can't access? */
        return (size_t) info.resident_size;

#elif defined(SPECTRA_IS_LINUX)
        FILE* fp;
        if ((fp = fopen("/proc/self/statm", "r")) == nullptr) SPECTRA_FATAL("Unable to open /proc/self/statm.");

        long rss = 0L;
        if (fscanf(fp, "%*s%ld", &rss) != 1) {
            fclose(fp);
            SPECTRA_FATAL("Unable to read memory usage from /proc/self/statm.");
        }
        fclose(fp);
        return (size_t) rss * (size_t) sysconf(_SC_PAGESIZE);
#else
#error "GetCurrentRSS is only implemented for Windows, macOS, and Linux."
#endif
    }
} // namespace spectra
