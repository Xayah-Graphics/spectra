#ifndef SPECTRA_PATHTRACER_UTIL_BUFFERCACHE_H
#define SPECTRA_PATHTRACER_UTIL_BUFFERCACHE_H

#include <atomic>
#include <cstring>
#include <shared_mutex>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/hash.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>
#include <unordered_set>

namespace spectra {
    // BufferCache Definition
    template <typename T>
    class BufferCache {
    public:
        // BufferCache Public Methods
        const T* LookupOrAdd(pstd::span<const T> buf, Allocator alloc) {
            // Return pointer to data if _buf_ contents are already in the cache
            Buffer lookupBuffer(buf.data(), buf.size());
            int shardIndex = uint32_t(lookupBuffer.hash) >> (32 - logShards);
            DCHECK(shardIndex >= 0 && shardIndex < nShards);
            mutex[shardIndex].lock_shared();
            if (auto iter = cache[shardIndex].find(lookupBuffer); iter != cache[shardIndex].end()) {
                const T* ptr = iter->ptr;
                mutex[shardIndex].unlock_shared();
                DCHECK(std::memcmp(buf.data(), iter->ptr, buf.size() * sizeof(T)) == 0);
                return ptr;
            }

            // Add _buf_ contents to cache and return pointer to cached copy
            mutex[shardIndex].unlock_shared();
            T* ptr = alloc.allocate_object<T>(buf.size());
            std::copy(buf.begin(), buf.end(), ptr);
            bytesUsed += buf.size() * sizeof(T);
            mutex[shardIndex].lock();
            // Handle the case of another thread adding the buffer first
            if (auto iter = cache[shardIndex].find(lookupBuffer); iter != cache[shardIndex].end()) {
                const T* cachePtr = iter->ptr;
                mutex[shardIndex].unlock();
                alloc.deallocate_object(ptr, buf.size());
                return cachePtr;
            }

            cache[shardIndex].insert(Buffer(ptr, buf.size()));
            mutex[shardIndex].unlock();
            return ptr;
        }

        size_t BytesUsed() const {
            return bytesUsed;
        }

    private:
        // BufferCache::Buffer Definition
        struct Buffer {
            // BufferCache::Buffer Public Methods
            Buffer() = default;

            Buffer(const T* ptr, size_t size) : ptr(ptr), size(size) {
                hash = HashBuffer(ptr, size);
            }

            bool operator==(const Buffer& b) const {
                return size == b.size && hash == b.hash && std::memcmp(ptr, b.ptr, size * sizeof(T)) == 0;
            }

            const T* ptr = nullptr;
            size_t size  = 0, hash;
        };

        // BufferCache::BufferHasher Definition
        struct BufferHasher {
            size_t operator()(const Buffer& b) const {
                return b.hash;
            }
        };

        // BufferCache Private Members
        static constexpr int logShards = 6;
        static constexpr int nShards   = 1 << logShards;
        std::shared_mutex mutex[nShards];
        std::unordered_set<Buffer, BufferHasher> cache[nShards];
        std::atomic<size_t> bytesUsed{};
    };

    class MeshBufferCache {
    public:
        const int* Lookup(pstd::span<const int> values, Allocator alloc) {
            return intBuffers.LookupOrAdd(values, alloc);
        }

        const Point2f* Lookup(pstd::span<const Point2f> values, Allocator alloc) {
            return point2Buffers.LookupOrAdd(values, alloc);
        }

        const Point3f* Lookup(pstd::span<const Point3f> values, Allocator alloc) {
            return point3Buffers.LookupOrAdd(values, alloc);
        }

        const Vector3f* Lookup(pstd::span<const Vector3f> values, Allocator alloc) {
            return vector3Buffers.LookupOrAdd(values, alloc);
        }

        const Normal3f* Lookup(pstd::span<const Normal3f> values, Allocator alloc) {
            return normal3Buffers.LookupOrAdd(values, alloc);
        }

    private:
        BufferCache<int> intBuffers{};
        BufferCache<Point2f> point2Buffers{};
        BufferCache<Point3f> point3Buffers{};
        BufferCache<Vector3f> vector3Buffers{};
        BufferCache<Normal3f> normal3Buffers{};
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_BUFFERCACHE_H
