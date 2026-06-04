#include <pathtracer/util/check.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/pstd.cuh>

namespace pstd {
    namespace pmr {
        memory_resource::~memory_resource() {}

        class NewDeleteResource : public memory_resource {
            void* do_allocate(size_t size, size_t alignment) {
#if defined(SPECTRA_HAVE__ALIGNED_MALLOC)
                return _aligned_malloc(size, alignment);
#elif defined(SPECTRA_HAVE_POSIX_MEMALIGN)
                void* ptr;
                if (alignment < sizeof(void*)) return malloc(size);
                if (posix_memalign(&ptr, alignment, size) != 0) ptr = nullptr;
                return ptr;
#else
                return memalign(alignment, size);
#endif
            }

            void do_deallocate(void* ptr, size_t bytes, size_t alignment) {
                if (!ptr) return;
#if defined(SPECTRA_HAVE__ALIGNED_MALLOC)
                _aligned_free(ptr);
#else
                free(ptr);
#endif
            }

            bool do_is_equal(const memory_resource& other) const noexcept {
                return this == &other;
            }
        };

        static NewDeleteResource* ndr;

        memory_resource* new_delete_resource() noexcept {
            if (!ndr) ndr = new NewDeleteResource;
            return ndr;
        }

        static memory_resource* defaultMemoryResource = new_delete_resource();

        memory_resource* set_default_resource(memory_resource* r) noexcept {
            memory_resource* orig = defaultMemoryResource;
            defaultMemoryResource = r;
            return orig;
        }

        memory_resource* get_default_resource() noexcept {
            return defaultMemoryResource;
        }

        void* monotonic_buffer_resource::do_allocate(size_t bytes, size_t align) {
            if (bytes > block_size)
                // We've got a big allocation; let the current block be so that
                // smaller allocations have a chance at using up more of it.
                return upstream->allocate(bytes, align);

            if ((current_pos % align) != 0) current_pos += align - (current_pos % align);
            DCHECK_EQ(0, current_pos % align);

            if (!current || current_pos + bytes > current->size) {
                current     = allocate_block(block_size);
                current_pos = 0;
            }

            void* ptr = (char*) current->ptr + current_pos;
            current_pos += bytes;
            return ptr;
        }
    } // namespace pmr
} // namespace pstd
