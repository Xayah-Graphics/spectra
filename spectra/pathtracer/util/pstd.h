#ifndef SPECTRA_PATHTRACER_UTIL_PSTD_H
#define SPECTRA_PATHTRACER_UTIL_PSTD_H

#include <spectra/pathtracer/util/check.h>

#include <cuda/std/array>
#include <cuda/std/bit>
#include <cuda/std/cmath>
#include <cuda/std/complex>
#include <cuda/std/optional>
#include <cuda/std/span>
#include <cuda/std/tuple>
#include <cuda/std/utility>

#include <float.h>
#include <limits.h>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pstd
{
    using cuda::std::abs;
    using cuda::std::bit_cast;
    using cuda::std::ceil;
    using cuda::std::complex;
    using cuda::std::copysign;
    using cuda::std::floor;
    using cuda::std::fmod;
    using cuda::std::get;
    using cuda::std::imag;
    using cuda::std::norm;
    using cuda::std::optional;
    using cuda::std::real;
    using cuda::std::round;
    using cuda::std::sqrt;
    using cuda::std::swap;
    using cuda::std::tuple;

    template <typename T, int N>
    using array = cuda::std::array<T, static_cast<std::size_t>(N)>;

    template <typename T>
    using span = cuda::std::span<T>;

    inline constexpr std::size_t dynamic_extent = cuda::std::dynamic_extent;

    namespace span_internal
    {
        // Wrappers for access to container data pointers.
        template <typename C>
        SPECTRA_CPU_GPU inline constexpr auto GetDataImpl(C& c, char) noexcept
            -> decltype(c.data())
        {
            return c.data();
        }

        template <typename C>
        SPECTRA_CPU_GPU inline constexpr auto GetData(C& c) noexcept -> decltype(GetDataImpl(c, 0))
        {
            return GetDataImpl(c, 0);
        }
    } // namespace span_internal

    template <int&... ExplicitArgumentBarrier, typename T>
    SPECTRA_CPU_GPU inline constexpr span<T> MakeSpan(T* ptr, size_t size) noexcept
    {
        return span<T>(ptr, size);
    }

    template <int&... ExplicitArgumentBarrier, typename T>
    SPECTRA_CPU_GPU inline span<T> MakeSpan(T* begin, T* end) noexcept
    {
        return span<T>(begin, static_cast<size_t>(end - begin));
    }

    template <int&... ExplicitArgumentBarrier, typename T>
    inline span<T> MakeSpan(std::vector<T>& v) noexcept
    {
        return span<T>(v.data(), static_cast<size_t>(v.size()));
    }

    template <int&... ExplicitArgumentBarrier, typename C>
    SPECTRA_CPU_GPU inline constexpr auto MakeSpan(C& c) noexcept
        -> decltype(MakeSpan(span_internal::GetData(c), static_cast<size_t>(c.size())))
    {
        return MakeSpan(span_internal::GetData(c), static_cast<size_t>(c.size()));
    }

    template <int&... ExplicitArgumentBarrier, typename T, size_t N>
    SPECTRA_CPU_GPU inline constexpr span<T> MakeSpan(T (&array)[N]) noexcept
    {
        return span<T>(array, N);
    }

    template <int&... ExplicitArgumentBarrier, typename T>
    SPECTRA_CPU_GPU inline constexpr span<const T> MakeConstSpan(T* ptr, size_t size) noexcept
    {
        return span<const T>(ptr, size);
    }

    template <int&... ExplicitArgumentBarrier, typename T>
    SPECTRA_CPU_GPU inline span<const T> MakeConstSpan(T* begin, T* end) noexcept
    {
        return span<const T>(begin, static_cast<size_t>(end - begin));
    }

    template <int&... ExplicitArgumentBarrier, typename T>
    inline span<const T> MakeConstSpan(const std::vector<T>& v) noexcept
    {
        return span<const T>(v.data(), static_cast<size_t>(v.size()));
    }

    template <int&... ExplicitArgumentBarrier, typename C>
    SPECTRA_CPU_GPU inline constexpr auto MakeConstSpan(const C& c) noexcept
        -> decltype(MakeSpan(c))
    {
        return MakeSpan(c);
    }

    template <int&... ExplicitArgumentBarrier, typename T, size_t N>
    SPECTRA_CPU_GPU inline constexpr span<const T> MakeConstSpan(const T (&array)[N]) noexcept
    {
        return span<const T>(array, N);
    }

    // memory_resource...

    namespace pmr
    {
        class memory_resource
        {
            static constexpr size_t max_align = alignof(std::max_align_t);

        public:
            virtual ~memory_resource();

            void* allocate(size_t bytes, size_t alignment = max_align)
            {
                if (bytes == 0)
                    return nullptr;
                return do_allocate(bytes, alignment);
            }

            void deallocate(void* p, size_t bytes, size_t alignment = max_align)
            {
                if (!p)
                    return;
                return do_deallocate(p, bytes, alignment);
            }

            bool is_equal(const memory_resource& other) const noexcept
            {
                return do_is_equal(other);
            }

        private:
            virtual void* do_allocate(size_t bytes, size_t alignment) = 0;
            virtual void do_deallocate(void* p, size_t bytes, size_t alignment) = 0;
            virtual bool do_is_equal(const memory_resource& other) const noexcept = 0;
        };

        inline bool operator==(const memory_resource& a, const memory_resource& b) noexcept
        {
            return a.is_equal(b);
        }

        inline bool operator!=(const memory_resource& a, const memory_resource& b) noexcept
        {
            return !(a == b);
        }

        // global memory resources
        memory_resource* new_delete_resource() noexcept;
        memory_resource* set_default_resource(memory_resource* r) noexcept;
        memory_resource* get_default_resource() noexcept;

        class alignas(64) monotonic_buffer_resource : public memory_resource
        {
        public:
            explicit monotonic_buffer_resource(memory_resource* upstream) : upstream(upstream)
            {
#ifndef NDEBUG
                constructTID = std::this_thread::get_id();
#endif
            }

            monotonic_buffer_resource(size_t block_size, memory_resource* upstream)
                : block_size(block_size), upstream(upstream)
            {
#ifndef NDEBUG
                constructTID = std::this_thread::get_id();
#endif
            }

            monotonic_buffer_resource() : monotonic_buffer_resource(get_default_resource())
            {
            }

            explicit monotonic_buffer_resource(size_t initial_size)
                : monotonic_buffer_resource(initial_size, get_default_resource())
            {
            }

            monotonic_buffer_resource(const monotonic_buffer_resource&) = delete;

            ~monotonic_buffer_resource() { release(); }

            monotonic_buffer_resource operator=(const monotonic_buffer_resource&) = delete;

            void release()
            {
                block* b = block_list;
                while (b)
                {
                    block* next = b->next;
                    free_block(b);
                    b = next;
                }
                block_list = nullptr;
                current = nullptr;
            }

            memory_resource* upstream_resource() const { return upstream; }

        protected:
            void* do_allocate(size_t bytes, size_t align) override;

            void do_deallocate(void* p, size_t bytes, size_t alignment) override
            {
                if (bytes > block_size)
                    // do_allocate() passes large allocations on to the upstream memory resource,
                    // so we might as well deallocate when it's possible.
                    upstream->deallocate(p, bytes);
            }

            bool do_is_equal(const memory_resource& other) const noexcept override
            {
                return this == &other;
            }

        private:
            struct block
            {
                void* ptr;
                size_t size;
                block* next;
            };

            block* allocate_block(size_t size)
            {
                // Single allocation for both the block and its memory. This means
                // that strictly speaking MemoryBlock::ptr is redundant, but let's not get too
                // fancy here...
                block* b = static_cast<block*>(
                    upstream->allocate(sizeof(block) + size, alignof(block)));

                b->ptr = reinterpret_cast<char*>(b) + sizeof(block);
                b->size = size;
                b->next = block_list;
                block_list = b;

                return b;
            }

            void free_block(block* b) { upstream->deallocate(b, sizeof(block) + b->size); }

#ifndef NDEBUG
            std::thread::id constructTID;
#endif
            memory_resource* upstream;
            size_t block_size = 256 * 1024;
            block* current = nullptr;
            size_t current_pos = 0;
            block* block_list = nullptr;
        };

        template <class Tp = std::byte>
        class polymorphic_allocator
        {
        public:
            using value_type = Tp;

            polymorphic_allocator() noexcept { memoryResource = new_delete_resource(); }

            polymorphic_allocator(memory_resource* r) : memoryResource(r)
            {
            }

            polymorphic_allocator(const polymorphic_allocator& other) = default;

            template <class U>
            polymorphic_allocator(const polymorphic_allocator<U>& other) noexcept
                : memoryResource(other.resource())
            {
            }

            polymorphic_allocator& operator=(const polymorphic_allocator& rhs) = delete;

            // member functions
            [[nodiscard]] Tp* allocate(size_t n)
            {
                return static_cast<Tp*>(resource()->allocate(n * sizeof(Tp), alignof(Tp)));
            }

            void deallocate(Tp* p, size_t n) { resource()->deallocate(p, n * sizeof(Tp)); }

            void* allocate_bytes(size_t nbytes, size_t alignment = alignof(max_align_t))
            {
                return resource()->allocate(nbytes, alignment);
            }

            void deallocate_bytes(void* p, size_t nbytes,
                                  size_t alignment = alignof(std::max_align_t))
            {
                return resource()->deallocate(p, nbytes, alignment);
            }

            template <class T>
            T* allocate_object(size_t n = 1)
            {
                return static_cast<T*>(allocate_bytes(n * sizeof(T), alignof(T)));
            }

            template <class T>
            void deallocate_object(T* p, size_t n = 1)
            {
                deallocate_bytes(p, n * sizeof(T), alignof(T));
            }

            template <class T, class... Args>
            T* new_object(Args&&... args)
            {
                // NOTE: this doesn't handle constructors that throw exceptions...
                T* p = allocate_object<T>();
                construct(p, std::forward<Args>(args)...);
                return p;
            }

            template <class T>
            void delete_object(T* p)
            {
                destroy(p);
                deallocate_object(p);
            }

            template <class T, class... Args>
            void construct(T* p, Args&&... args)
            {
                ::new((void*)p) T(std::forward<Args>(args)...);
            }

            template <class T>
            void destroy(T* p)
            {
                p->~T();
            }

            // polymorphic_allocator select_on_container_copy_construction() const;

            memory_resource* resource() const { return memoryResource; }

        private:
            memory_resource* memoryResource;
        };

        template <class T1, class T2>
        bool operator==(const polymorphic_allocator<T1>& a,
                        const polymorphic_allocator<T2>& b) noexcept
        {
            return a.resource() == b.resource();
        }

        template <class T1, class T2>
        bool operator!=(const polymorphic_allocator<T1>& a,
                        const polymorphic_allocator<T2>& b) noexcept
        {
            return !(a == b);
        }
    } // namespace pmr

    template <typename T, class Allocator = pmr::polymorphic_allocator<T>>
    class vector
    {
    public:
        using value_type = T;
        using allocator_type = Allocator;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = T*;
        using const_pointer = const T*;
        using iterator = T*;
        using const_iterator = const T*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const iterator>;

        vector(const Allocator& alloc = {}) : alloc(alloc)
        {
        }

        vector(size_t count, const T& value, const Allocator& alloc = {}) : alloc(alloc)
        {
            reserve(count);
            for (size_t i = 0; i < count; ++i)
                this->alloc.template construct<T>(ptr + i, value);
            nStored = count;
        }

        vector(size_t count, const Allocator& alloc = {}) : vector(count, T{}, alloc)
        {
        }

        vector(const vector& other, const Allocator& alloc = {}) : alloc(alloc)
        {
            reserve(other.size());
            for (size_t i = 0; i < other.size(); ++i)
                this->alloc.template construct<T>(ptr + i, other[i]);
            nStored = other.size();
        }

        template <class InputIt>
        vector(InputIt first, InputIt last, const Allocator& alloc = {}) : alloc(alloc)
        {
            reserve(last - first);
            size_t i = 0;
            for (InputIt iter = first; iter != last; ++iter, ++i)
                this->alloc.template construct<T>(ptr + i, *iter);
            nStored = nAlloc;
        }

        vector(vector&& other) : alloc(other.alloc)
        {
            nStored = other.nStored;
            nAlloc = other.nAlloc;
            ptr = other.ptr;

            other.nStored = other.nAlloc = 0;
            other.ptr = nullptr;
        }

        vector(vector&& other, const Allocator& alloc)
        {
            if (alloc == other.alloc)
            {
                ptr = other.ptr;
                nAlloc = other.nAlloc;
                nStored = other.nStored;

                other.ptr = nullptr;
                other.nAlloc = other.nStored = 0;
            }
            else
            {
                reserve(other.size());
                for (size_t i = 0; i < other.size(); ++i)
                    alloc.template construct<T>(ptr + i, std::move(other[i]));
                nStored = other.size();
            }
        }

        vector(std::initializer_list<T> init, const Allocator& alloc = {})
            : vector(init.begin(), init.end(), alloc)
        {
        }

        vector& operator=(const vector& other)
        {
            if (this == &other)
                return *this;

            clear();
            reserve(other.size());
            for (size_t i = 0; i < other.size(); ++i)
                alloc.template construct<T>(ptr + i, other[i]);
            nStored = other.size();

            return *this;
        }

        vector& operator=(vector&& other)
        {
            if (this == &other)
                return *this;

            if (alloc == other.alloc)
            {
                pstd::swap(ptr, other.ptr);
                pstd::swap(nAlloc, other.nAlloc);
                pstd::swap(nStored, other.nStored);
            }
            else
            {
                clear();
                reserve(other.size());
                for (size_t i = 0; i < other.size(); ++i)
                    alloc.template construct<T>(ptr + i, std::move(other[i]));
                nStored = other.size();
            }

            return *this;
        }

        vector& operator=(std::initializer_list<T>& init)
        {
            reserve(init.size());
            clear();
            iterator iter = begin();
            for (const auto& value : init)
            {
                *iter = value;
                ++iter;
            }
            return *this;
        }

        void assign(size_type count, const T& value)
        {
            clear();
            reserve(count);
            for (size_t i = 0; i < count; ++i)
                push_back(value);
        }

        template <class InputIt>
        void assign(InputIt first, InputIt last)
        {
            SPECTRA_FATAL("TODO");
            // TODO
        }

        void assign(std::initializer_list<T>& init) { assign(init.begin(), init.end()); }

        ~vector()
        {
            clear();
            alloc.deallocate_object(ptr, nAlloc);
        }

        SPECTRA_CPU_GPU
        iterator begin() { return ptr; }

        SPECTRA_CPU_GPU
        iterator end() { return ptr + nStored; }

        SPECTRA_CPU_GPU
        const_iterator begin() const { return ptr; }

        SPECTRA_CPU_GPU
        const_iterator end() const { return ptr + nStored; }

        SPECTRA_CPU_GPU
        const_iterator cbegin() const { return ptr; }

        SPECTRA_CPU_GPU
        const_iterator cend() const { return ptr + nStored; }

        SPECTRA_CPU_GPU
        reverse_iterator rbegin() { return reverse_iterator(end()); }

        SPECTRA_CPU_GPU
        reverse_iterator rend() { return reverse_iterator(begin()); }

        SPECTRA_CPU_GPU
        const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }

        SPECTRA_CPU_GPU
        const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

        allocator_type get_allocator() const { return alloc; }
        SPECTRA_CPU_GPU
        size_t size() const { return nStored; }

        SPECTRA_CPU_GPU
        bool empty() const { return size() == 0; }

        SPECTRA_CPU_GPU
        size_t max_size() const { return (size_t)-1; }

        SPECTRA_CPU_GPU
        size_t capacity() const { return nAlloc; }

        void reserve(size_t n)
        {
            if (nAlloc >= n)
                return;

            T* ra = alloc.template allocate_object<T>(n);
            for (int i = 0; i < nStored; ++i)
            {
                alloc.template construct<T>(ra + i, std::move(begin()[i]));
                alloc.destroy(begin() + i);
            }

            alloc.deallocate_object(ptr, nAlloc);
            nAlloc = n;
            ptr = ra;
        }

        // TODO: shrink_to_fit

        SPECTRA_CPU_GPU
        reference operator[](size_type index)
        {
            DCHECK_LT(index, size());
            return ptr[index];
        }

        SPECTRA_CPU_GPU
        const_reference operator[](size_type index) const
        {
            DCHECK_LT(index, size());
            return ptr[index];
        }

        SPECTRA_CPU_GPU
        reference front() { return ptr[0]; }

        SPECTRA_CPU_GPU
        const_reference front() const { return ptr[0]; }

        SPECTRA_CPU_GPU
        reference back() { return ptr[nStored - 1]; }

        SPECTRA_CPU_GPU
        const_reference back() const { return ptr[nStored - 1]; }

        SPECTRA_CPU_GPU
        pointer data() { return ptr; }

        SPECTRA_CPU_GPU
        const_pointer data() const { return ptr; }

        void clear()
        {
            for (int i = 0; i < nStored; ++i)
                alloc.destroy(&ptr[i]);
            nStored = 0;
        }

        iterator insert(const_iterator, const T& value)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        iterator insert(const_iterator, T&& value)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        iterator insert(const_iterator pos, size_type count, const T& value)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        template <class InputIt>
        iterator insert(const_iterator pos, InputIt first, InputIt last)
        {
            if (pos == end())
            {
                size_t firstOffset = size();
                for (auto iter = first; iter != last; ++iter)
                    push_back(*iter);
                return begin() + firstOffset;
            }
            else
                SPECTRA_FATAL("TODO");
        }

        iterator insert(const_iterator pos, std::initializer_list<T> init)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        template <class... Args>
        iterator emplace(const_iterator pos, Args&&... args)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        template <class... Args>
        void emplace_back(Args&&... args)
        {
            if (nAlloc == nStored)
                reserve(nAlloc == 0 ? 4 : 2 * nAlloc);

            alloc.construct(ptr + nStored, std::forward<Args>(args)...);
            ++nStored;
        }

        iterator erase(const_iterator pos)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        iterator erase(const_iterator first, const_iterator last)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        void push_back(const T& value)
        {
            if (nAlloc == nStored)
                reserve(nAlloc == 0 ? 4 : 2 * nAlloc);

            alloc.construct(ptr + nStored, value);
            ++nStored;
        }

        void push_back(T&& value)
        {
            if (nAlloc == nStored)
                reserve(nAlloc == 0 ? 4 : 2 * nAlloc);

            alloc.construct(ptr + nStored, std::move(value));
            ++nStored;
        }

        void pop_back()
        {
            DCHECK(!empty());
            alloc.destroy(ptr + nStored - 1);
            --nStored;
        }

        void resize(size_type n)
        {
            if (n < size())
            {
                for (size_t i = n; i < size(); ++i)
                    alloc.destroy(ptr + i);
                if (n == 0)
                {
                    alloc.deallocate_object(ptr, nAlloc);
                    ptr = nullptr;
                    nAlloc = 0;
                }
            }
            else if (n > size())
            {
                reserve(n);
                for (size_t i = nStored; i < n; ++i)
                    alloc.construct(ptr + i);
            }
            nStored = n;
        }

        void resize(size_type count, const value_type& value)
        {
            // TODO
            SPECTRA_FATAL("TODO");
        }

        void swap(vector& other)
        {
            CHECK(alloc == other.alloc); // TODO: handle this
            std::swap(ptr, other.ptr);
            std::swap(nAlloc, other.nAlloc);
            std::swap(nStored, other.nStored);
        }

    private:
        Allocator alloc;
        T* ptr = nullptr;
        size_t nAlloc = 0, nStored = 0;
    };
} // namespace pstd

#endif  // SPECTRA_PATHTRACER_UTIL_PSTD_H
