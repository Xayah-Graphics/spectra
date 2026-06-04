#ifndef SPECTRA_PATHTRACER_UTIL_CONTAINERS_H
#define SPECTRA_PATHTRACER_UTIL_CONTAINERS_H

#include <algorithm>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <shared_mutex>
#include <pathtracer/util/check.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <string>
#include <tuple>
#include <type_traits>

namespace spectra {
    // TypePack Definition
    template <typename... Ts>
    struct TypePack {
        static constexpr size_t count = sizeof...(Ts);
    };

    // TypePack Operations
    template <typename T, typename... Ts>
    struct IndexOf {
        static constexpr int count = 0;
        static_assert(!std::is_same_v<T, T>, "Type not present in TypePack");
    };

    template <typename T, typename... Ts>
    struct IndexOf<T, TypePack<T, Ts...>> {
        static constexpr int count = 0;
    };

    template <typename T, typename U, typename... Ts>
    struct IndexOf<T, TypePack<U, Ts...>> {
        static constexpr int count = 1 + IndexOf<T, TypePack<Ts...>>::count;
    };

    template <typename T, typename... Ts>
    struct HasType {
        static constexpr bool value = false;
    };

    template <typename T, typename Tfirst, typename... Ts>
    struct HasType<T, TypePack<Tfirst, Ts...>> {
        static constexpr bool value = (std::is_same_v<T, Tfirst> || HasType<T, TypePack<Ts...>>::value);
    };

    template <typename T>
    struct GetFirst {};

    template <typename T, typename... Ts>
    struct GetFirst<TypePack<T, Ts...>> {
        using type = T;
    };

    template <typename T>
    struct RemoveFirst {};

    template <typename T, typename... Ts>
    struct RemoveFirst<TypePack<T, Ts...>> {
        using type = TypePack<Ts...>;
    };

    template <int index, typename T, typename... Ts>
    struct RemoveFirstN;

    template <int index, typename T, typename... Ts>
    struct RemoveFirstN<index, TypePack<T, Ts...>> {
        using type = RemoveFirstN<index - 1, TypePack<Ts...>>::type;
    };

    template <typename T, typename... Ts>
    struct RemoveFirstN<0, TypePack<T, Ts...>> {
        using type = TypePack<T, Ts...>;
    };

    template <typename... Ts>
    struct Prepend;

    template <typename T, typename... Ts>
    struct Prepend<T, TypePack<Ts...>> {
        using type = TypePack<T, Ts...>;
    };

    template <typename... Ts>
    struct Prepend<void, TypePack<Ts...>> {
        using type = TypePack<Ts...>;
    };

    template <int index, typename T, typename... Ts>
    struct TakeFirstN;

    template <int index, typename T, typename... Ts>
    struct TakeFirstN<index, TypePack<T, Ts...>> {
        using type = Prepend<T, typename TakeFirstN<index - 1, TypePack<Ts...>>::type>::type;
    };

    template <typename T, typename... Ts>
    struct TakeFirstN<1, TypePack<T, Ts...>> {
        using type = TypePack<T>;
    };

    template <template <typename> class M, typename... Ts>
    struct MapType;

    template <template <typename> class M, typename T>
    struct MapType<M, TypePack<T>> {
        using type = TypePack<M<T>>;
    };

    template <template <typename> class M, typename T, typename... Ts>
    struct MapType<M, TypePack<T, Ts...>> {
        using type = Prepend<M<T>, typename MapType<M, TypePack<Ts...>>::type>::type;
    };

    template <typename Base, typename... Ts>
    inline constexpr bool AllInheritFrom(TypePack<Ts...>);

    template <typename Base>
    inline constexpr bool AllInheritFrom(TypePack<>) {
        return true;
    }

    template <typename Base, typename T, typename... Ts>
    inline constexpr bool AllInheritFrom(TypePack<T, Ts...>) {
        return std::is_base_of_v<Base, T> && AllInheritFrom<Base>(TypePack<Ts...>());
    }

    template <typename F, typename... Ts>
    void ForEachType(F func, TypePack<Ts...>);

    template <typename F, typename T, typename... Ts>
    void ForEachType(F func, TypePack<T, Ts...>) {
        func.template operator()<T>();
        ForEachType(func, TypePack<Ts...>());
    }

    template <typename F>
    void ForEachType(F func, TypePack<>) {}

    // Array2D Definition
    template <typename T>
    class Array2D {
    public:
        // Array2D Type Definitions
        using value_type     = T;
        using iterator       = value_type*;
        using const_iterator = const value_type*;
        using allocator_type = pstd::pmr::polymorphic_allocator<std::byte>;

        // Array2D Public Methods
        Array2D(allocator_type allocator = {}) : Array2D({{0, 0}, {0, 0}}, allocator) {}

        Array2D(Bounds2i extent, Allocator allocator = {}) : extent(extent), allocator(allocator) {
            int n  = extent.Area();
            values = allocator.allocate_object<T>(n);
            for (int i = 0; i < n; ++i) allocator.construct(values + i);
        }

        Array2D(Bounds2i extent, T def, allocator_type allocator = {}) : Array2D(extent, allocator) {
            std::fill(begin(), end(), def);
        }

        template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt> && std::is_base_of<std::input_iterator_tag, typename std::iterator_traits<InputIt>::iterator_category>::value>>
        Array2D(InputIt first, InputIt last, int nx, int ny, allocator_type allocator = {}) : Array2D({{0, 0}, {nx, ny}}, allocator) {
            std::copy(first, last, begin());
        }

        Array2D(int nx, int ny, allocator_type allocator = {}) : Array2D({{0, 0}, {nx, ny}}, allocator) {}

        Array2D(int nx, int ny, T def, allocator_type allocator = {}) : Array2D({{0, 0}, {nx, ny}}, def, allocator) {}

        Array2D(const Array2D& a, allocator_type allocator = {}) : Array2D(a.begin(), a.end(), a.XSize(), a.YSize(), allocator) {}

        ~Array2D() {
            int n = extent.Area();
            for (int i = 0; i < n; ++i) allocator.destroy(values + i);
            allocator.deallocate_object(values, n);
        }

        Array2D(Array2D&& a, allocator_type allocator = {}) : extent(a.extent), allocator(allocator) {
            if (allocator == a.allocator) {
                values   = a.values;
                a.extent = Bounds2i({0, 0}, {0, 0});
                a.values = nullptr;
            } else {
                values = allocator.allocate_object<T>(extent.Area());
                std::copy(a.begin(), a.end(), begin());
            }
        }

        Array2D& operator=(const Array2D& a) = delete;

        Array2D& operator=(Array2D&& other) {
            if (allocator == other.allocator) {
                pstd::swap(extent, other.extent);
                pstd::swap(values, other.values);
            } else if (extent == other.extent) {
                int n = extent.Area();
                for (int i = 0; i < n; ++i) {
                    allocator.destroy(values + i);
                    allocator.construct(values + i, other.values[i]);
                }
                extent = other.extent;
            } else {
                int n = extent.Area();
                for (int i = 0; i < n; ++i) allocator.destroy(values + i);
                allocator.deallocate_object(values, n);

                int no = other.extent.Area();
                values = allocator.allocate_object<T>(no);
                for (int i = 0; i < no; ++i) allocator.construct(values + i, other.values[i]);
                extent = other.extent;
            }
            return *this;
        }

        __host__ __device__ T& operator[](Point2i p) {
            DCHECK(InsideExclusive(p, extent));
            p.x -= extent.pMin.x;
            p.y -= extent.pMin.y;
            return values[p.x + (extent.pMax.x - extent.pMin.x) * p.y];
        }

        __host__ __device__ T& operator()(int x, int y) {
            return (*this)[{x, y}];
        }

        __host__ __device__ const T& operator()(int x, int y) const {
            return (*this)[{x, y}];
        }
        __host__ __device__ const T& operator[](Point2i p) const {
            DCHECK(InsideExclusive(p, extent));
            p.x -= extent.pMin.x;
            p.y -= extent.pMin.y;
            return values[p.x + (extent.pMax.x - extent.pMin.x) * p.y];
        }

        __host__ __device__ int size() const {
            return extent.Area();
        }
        __host__ __device__ int XSize() const {
            return extent.pMax.x - extent.pMin.x;
        }
        __host__ __device__ int YSize() const {
            return extent.pMax.y - extent.pMin.y;
        }

        __host__ __device__ iterator begin() {
            return values;
        }
        __host__ __device__ iterator end() {
            return begin() + size();
        }

        __host__ __device__ const_iterator begin() const {
            return values;
        }
        __host__ __device__ const_iterator end() const {
            return begin() + size();
        }

        __host__ __device__ operator pstd::span<T>() {
            return pstd::span<T>(values, static_cast<size_t>(size()));
        }
        __host__ __device__ operator pstd::span<const T>() const {
            return pstd::span<const T>(values, static_cast<size_t>(size()));
        }

    private:
        // Array2D Private Members
        Bounds2i extent;
        Allocator allocator;
        T* values;
    };

    template <typename T, int N, class Allocator = pstd::pmr::polymorphic_allocator<T>>
    class InlinedVector {
    public:
        using value_type             = T;
        using allocator_type         = Allocator;
        using size_type              = std::size_t;
        using difference_type        = std::ptrdiff_t;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using pointer                = T*;
        using const_pointer          = const T*;
        using iterator               = T*;
        using const_iterator         = const T*;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        InlinedVector(const Allocator& alloc = {}) : alloc(alloc) {}

        InlinedVector(size_t count, const T& value, const Allocator& alloc = {}) : alloc(alloc) {
            reserve(count);
            for (size_t i = 0; i < count; ++i) this->alloc.template construct<T>(begin() + i, value);
            nStored = count;
        }

        InlinedVector(size_t count, const Allocator& alloc = {}) : InlinedVector(count, T{}, alloc) {}

        InlinedVector(const InlinedVector& other, const Allocator& alloc = {}) : alloc(alloc) {
            reserve(other.size());
            for (size_t i = 0; i < other.size(); ++i) this->alloc.template construct<T>(begin() + i, other[i]);
            nStored = other.size();
        }

        template <class InputIt>
        InlinedVector(InputIt first, InputIt last, const Allocator& alloc = {}) : alloc(alloc) {
            reserve(last - first);
            for (InputIt iter = first; iter != last; ++iter, ++nStored) this->alloc.template construct<T>(begin() + nStored, *iter);
        }

        InlinedVector(InlinedVector&& other) : alloc(other.alloc) {
            nStored = other.nStored;
            nAlloc  = other.nAlloc;
            ptr     = other.ptr;
            if (other.nStored <= N)
                for (int i = 0; i < other.nStored; ++i) alloc.template construct<T>(fixed + i, std::move(other.fixed[i]));
            // Leave other.nStored as is, so that the detrius left after we
            // moved out of fixed has its destructors run...
            else
                other.nStored = 0;

            other.nAlloc = 0;
            other.ptr    = nullptr;
        }

        InlinedVector(std::initializer_list<T> init, const Allocator& alloc = {}) : InlinedVector(init.begin(), init.end(), alloc) {}

        InlinedVector& operator=(const InlinedVector& other) {
            if (this == &other) return *this;

            clear();
            reserve(other.size());
            for (size_t i = 0; i < other.size(); ++i) alloc.template construct<T>(begin() + i, other[i]);
            nStored = other.size();

            return *this;
        }

        InlinedVector& operator=(InlinedVector&& other) {
            if (this == &other) return *this;

            clear();
            if (alloc == other.alloc) {
                pstd::swap(ptr, other.ptr);
                pstd::swap(nAlloc, other.nAlloc);
                pstd::swap(nStored, other.nStored);
                if (nStored > 0 && !ptr) {
                    for (int i = 0; i < nStored; ++i) alloc.template construct<T>(fixed + i, std::move(other.fixed[i]));
                    other.nStored = nStored; // so that dtors run...
                }
            } else {
                reserve(other.size());
                for (size_t i = 0; i < other.size(); ++i) alloc.template construct<T>(begin() + i, std::move(other[i]));
                nStored = other.size();
            }

            return *this;
        }

        InlinedVector& operator=(std::initializer_list<T>& init) {
            clear();
            reserve(init.size());
            for (const auto& value : init) {
                alloc.template construct<T>(begin() + nStored, value);
                ++nStored;
            }
            return *this;
        }

        void assign(size_type count, const T& value) {
            clear();
            reserve(count);
            for (size_t i = 0; i < count; ++i) alloc.template construct<T>(begin() + i, value);
            nStored = count;
        }

        ~InlinedVector() {
            clear();
            alloc.deallocate_object(ptr, nAlloc);
        }

        __host__ __device__ iterator begin() {
            return ptr ? ptr : fixed;
        }
        __host__ __device__ iterator end() {
            return begin() + nStored;
        }
        __host__ __device__ const_iterator begin() const {
            return ptr ? ptr : fixed;
        }
        __host__ __device__ const_iterator end() const {
            return begin() + nStored;
        }
        __host__ __device__ const_iterator cbegin() const {
            return ptr ? ptr : fixed;
        }
        __host__ __device__ const_iterator cend() const {
            return begin() + nStored;
        }

        __host__ __device__ reverse_iterator rbegin() {
            return reverse_iterator(end());
        }
        __host__ __device__ reverse_iterator rend() {
            return reverse_iterator(begin());
        }
        __host__ __device__ const_reverse_iterator rbegin() const {
            return const_reverse_iterator(end());
        }
        __host__ __device__ const_reverse_iterator rend() const {
            return const_reverse_iterator(begin());
        }

        allocator_type get_allocator() const {
            return alloc;
        }
        __host__ __device__ size_t size() const {
            return nStored;
        }
        __host__ __device__ bool empty() const {
            return size() == 0;
        }
        __host__ __device__ size_t max_size() const {
            return (size_t) -1;
        }
        __host__ __device__ size_t capacity() const {
            return ptr ? nAlloc : N;
        }

        void reserve(size_t n) {
            if (capacity() >= n) return;

            T* ra = alloc.template allocate_object<T>(n);
            for (int i = 0; i < nStored; ++i) {
                alloc.template construct<T>(ra + i, std::move(begin()[i]));
                alloc.destroy(begin() + i);
            }

            alloc.deallocate_object(ptr, nAlloc);
            nAlloc = n;
            ptr    = ra;
        }

        __host__ __device__ reference operator[](size_type index) {
            DCHECK_LT(index, size());
            return begin()[index];
        }

        __host__ __device__ const_reference operator[](size_type index) const {
            DCHECK_LT(index, size());
            return begin()[index];
        }

        __host__ __device__ reference front() {
            return *begin();
        }
        __host__ __device__ const_reference front() const {
            return *begin();
        }
        __host__ __device__ reference back() {
            return *(begin() + nStored - 1);
        }
        __host__ __device__ const_reference back() const {
            return *(begin() + nStored - 1);
        }
        __host__ __device__ pointer data() {
            return ptr ? ptr : fixed;
        }
        __host__ __device__ const_pointer data() const {
            return ptr ? ptr : fixed;
        }

        void clear() {
            for (int i = 0; i < nStored; ++i) alloc.destroy(begin() + i);
            nStored = 0;
        }

        template <class InputIt>
        iterator insert(const_iterator pos, InputIt first, InputIt last) {
            if (pos == end()) {
                reserve(size() + (last - first));
                iterator pos = end();
                for (auto iter = first; iter != last; ++iter, ++pos) alloc.template construct<T>(pos, *iter);
                nStored += last - first;
                return pos;
            } else {
                SPECTRA_FATAL("InlinedVector::insert only supports appending ranges");
            }
        }

        iterator erase(const_iterator cpos) {
            iterator pos = begin() + (cpos - begin()); // non-const iterator, thank you very much
            while (pos != end() - 1) {
                *pos = std::move(*(pos + 1));
                ++pos;
            }
            alloc.destroy(pos);
            --nStored;
            return begin() + (cpos - begin());
        }

        void push_back(const T& value) {
            if (size() == capacity()) reserve(2 * capacity());

            alloc.construct(begin() + nStored, value);
            ++nStored;
        }

        void push_back(T&& value) {
            if (size() == capacity()) reserve(2 * capacity());

            alloc.construct(begin() + nStored, std::move(value));
            ++nStored;
        }

        void pop_back() {
            DCHECK(!empty());
            alloc.destroy(begin() + nStored - 1);
            --nStored;
        }

        void resize(size_type n) {
            if (n < size()) {
                for (size_t i = n; i < size(); ++i) alloc.destroy(begin() + i);
            } else if (n > size()) {
                reserve(n);
                for (size_t i = nStored; i < n; ++i) alloc.construct(begin() + i);
            }
            nStored = n;
        }

    private:
        Allocator alloc;
        // ptr non-null is discriminator for whether fixed[] is valid...
        T* ptr = nullptr;

        union {
            T fixed[N];
        };

        size_t nAlloc = 0, nStored = 0;
    };

    // HashMap Definition
    template <typename Key, typename Value, typename Hash = std::hash<Key>, typename Allocator = pstd::pmr::polymorphic_allocator<pstd::optional<std::pair<Key, Value>>>>
    class HashMap {
    public:
        // HashMap Type Definitions
        using TableEntry = pstd::optional<std::pair<Key, Value>>;

        class Iterator {
        public:
            __host__ __device__ Iterator& operator++() {
                while (++ptr < end && !ptr->has_value());
                return *this;
            }

            __host__ __device__ Iterator operator++(int) {
                Iterator old = *this;
                operator++();
                return old;
            }

            __host__ __device__ bool operator==(const Iterator& iter) const {
                return ptr == iter.ptr;
            }
            __host__ __device__ bool operator!=(const Iterator& iter) const {
                return ptr != iter.ptr;
            }

            __host__ __device__ std::pair<Key, Value>& operator*() {
                return ptr->value();
            }
            __host__ __device__ const std::pair<Key, Value>& operator*() const {
                return ptr->value();
            }

            __host__ __device__ std::pair<Key, Value>* operator->() {
                return &ptr->value();
            }
            __host__ __device__ const std::pair<Key, Value>* operator->() const {
                return &ptr->value();
            }

        private:
            friend class HashMap;

            Iterator(TableEntry* ptr, TableEntry* end) : ptr(ptr), end(end) {}

            TableEntry* ptr;
            TableEntry* end;
        };

        using iterator       = Iterator;
        using const_iterator = const iterator;

        // HashMap Public Methods
        __host__ __device__ size_t size() const {
            return nStored;
        }
        __host__ __device__ size_t capacity() const {
            return table.size();
        }

        void Clear() {
            table.clear();
            nStored = 0;
        }

        HashMap(Allocator alloc) : table(8, alloc) {}

        HashMap(const HashMap&)            = delete;
        HashMap& operator=(const HashMap&) = delete;

        void Insert(const Key& key, const Value& value) {
            size_t offset = FindOffset(key);
            if (table[offset].has_value() == false) {
                // Grow hash table if it is too full
                if (3 * ++nStored > capacity()) {
                    Grow();
                    offset = FindOffset(key);
                }
            }
            table[offset] = std::make_pair(key, value);
        }

        __host__ __device__ bool HasKey(const Key& key) const {
            return table[FindOffset(key)].has_value();
        }

        __host__ __device__ const Value& operator[](const Key& key) const {
            size_t offset = FindOffset(key);
            CHECK(table[offset].has_value());
            return table[offset]->second;
        }

        __host__ __device__ iterator begin() {
            Iterator iter(table.data(), table.data() + capacity());
            while (iter.ptr < iter.end && !iter.ptr->has_value()) ++iter.ptr;
            return iter;
        }

        __host__ __device__ iterator end() {
            return Iterator(table.data() + capacity(), table.data() + capacity());
        }

    private:
        // HashMap Private Methods
        __host__ __device__ size_t FindOffset(const Key& key) const {
            size_t baseOffset = Hash()(key) & (capacity() - 1);
            for (int nProbes = 0;; ++nProbes) {
                // Find offset for _key_ using quadratic probing
                size_t offset = (baseOffset + nProbes / 2 + nProbes * nProbes / 2) & (capacity() - 1);
                if (table[offset].has_value() == false || key == table[offset]->first) return offset;
            }
        }

        void Grow() {
            size_t currentCapacity = capacity();
            pstd::vector<TableEntry> newTable(std::max<size_t>(64, 2 * currentCapacity), table.get_allocator());
            size_t newCapacity = newTable.size();
            for (size_t i = 0; i < currentCapacity; ++i) {
                // Insert _table[i]_ into _newTable_ if it is set
                if (!table[i].has_value()) continue;
                size_t baseOffset = Hash()(table[i]->first) & (newCapacity - 1);
                for (int nProbes = 0;; ++nProbes) {
                    size_t offset = (baseOffset + nProbes / 2 + nProbes * nProbes / 2) & (newCapacity - 1);
                    if (!newTable[offset]) {
                        newTable[offset] = std::move(*table[i]);
                        break;
                    }
                }
            }
            table = std::move(newTable);
        }

        // HashMap Private Members
        pstd::vector<TableEntry> table;
        size_t nStored = 0;
    };

    // SampledGrid Definition
    template <typename T>
    class SampledGrid {
    public:
        using const_iterator = pstd::vector<T>::const_iterator;
        // SampledGrid Public Methods
        SampledGrid() = default;

        SampledGrid(Allocator alloc) : values(alloc) {}

        SampledGrid(pstd::span<const T> v, int nx, int ny, int nz, Allocator alloc) : values(v.begin(), v.end(), alloc), nx(nx), ny(ny), nz(nz) {
            CHECK_EQ(nx * ny * nz, values.size());
        }

        __host__ __device__ size_t BytesAllocated() const {
            return values.size() * sizeof(T);
        }
        __host__ __device__ int XSize() const {
            return nx;
        }
        __host__ __device__ int YSize() const {
            return ny;
        }
        __host__ __device__ int ZSize() const {
            return nz;
        }

        const_iterator begin() const {
            return values.begin();
        }
        const_iterator end() const {
            return values.end();
        }

        template <typename F>
        __host__ __device__ auto Lookup(Point3f p, F convert) const {
            // Compute voxel coordinates and offsets for _p_
            Point3f pSamples(p.x * nx - .5f, p.y * ny - .5f, p.z * nz - .5f);
            Point3i pi = (Point3i) Floor(pSamples);
            Vector3f d = pSamples - (Point3f) pi;

            // Return trilinearly interpolated voxel values
            auto d00 = Lerp(d.x, Lookup(pi, convert), Lookup(pi + Vector3i(1, 0, 0), convert));
            auto d10 = Lerp(d.x, Lookup(pi + Vector3i(0, 1, 0), convert), Lookup(pi + Vector3i(1, 1, 0), convert));
            auto d01 = Lerp(d.x, Lookup(pi + Vector3i(0, 0, 1), convert), Lookup(pi + Vector3i(1, 0, 1), convert));
            auto d11 = Lerp(d.x, Lookup(pi + Vector3i(0, 1, 1), convert), Lookup(pi + Vector3i(1, 1, 1), convert));
            return Lerp(d.z, Lerp(d.y, d00, d10), Lerp(d.y, d01, d11));
        }

        __host__ __device__ T Lookup(Point3f p) const {
            // Compute voxel coordinates and offsets for _p_
            Point3f pSamples(p.x * nx - .5f, p.y * ny - .5f, p.z * nz - .5f);
            Point3i pi = (Point3i) Floor(pSamples);
            Vector3f d = pSamples - (Point3f) pi;

            // Return trilinearly interpolated voxel values
            auto d00 = Lerp(d.x, Lookup(pi), Lookup(pi + Vector3i(1, 0, 0)));
            auto d10 = Lerp(d.x, Lookup(pi + Vector3i(0, 1, 0)), Lookup(pi + Vector3i(1, 1, 0)));
            auto d01 = Lerp(d.x, Lookup(pi + Vector3i(0, 0, 1)), Lookup(pi + Vector3i(1, 0, 1)));
            auto d11 = Lerp(d.x, Lookup(pi + Vector3i(0, 1, 1)), Lookup(pi + Vector3i(1, 1, 1)));
            return Lerp(d.z, Lerp(d.y, d00, d10), Lerp(d.y, d01, d11));
        }

        template <typename F>
        __host__ __device__ auto Lookup(const Point3i& p, F convert) const {
            Bounds3i sampleBounds(Point3i(0, 0, 0), Point3i(nx, ny, nz));
            if (!InsideExclusive(p, sampleBounds)) return convert(T{});
            return convert(values[(p.z * ny + p.y) * nx + p.x]);
        }

        __host__ __device__ T Lookup(const Point3i& p) const {
            Bounds3i sampleBounds(Point3i(0, 0, 0), Point3i(nx, ny, nz));
            if (!InsideExclusive(p, sampleBounds)) return T{};
            return values[(p.z * ny + p.y) * nx + p.x];
        }

        template <typename F>
        auto MaxValue(const Bounds3f& bounds, F convert) const {
            Point3f ps[2] = {Point3f(bounds.pMin.x * nx - .5f, bounds.pMin.y * ny - .5f, bounds.pMin.z * nz - .5f), Point3f(bounds.pMax.x * nx - .5f, bounds.pMax.y * ny - .5f, bounds.pMax.z * nz - .5f)};
            Point3i pi[2] = {Max(Point3i(Floor(ps[0])), Point3i(0, 0, 0)), Min(Point3i(Floor(ps[1])) + Vector3i(1, 1, 1), Point3i(nx - 1, ny - 1, nz - 1))};

            auto maxValue = Lookup(Point3i(pi[0]), convert);
            for (int z = pi[0].z; z <= pi[1].z; ++z)
                for (int y = pi[0].y; y <= pi[1].y; ++y)
                    for (int x = pi[0].x; x <= pi[1].x; ++x) maxValue = std::max(maxValue, Lookup(Point3i(x, y, z), convert));

            return maxValue;
        }

        T MaxValue(const Bounds3f& bounds) const {
            return MaxValue(bounds, [](T value) { return value; });
        }

    private:
        // SampledGrid Private Members
        pstd::vector<T> values;
        int nx, ny, nz;
    };

    // InternCache Definition
    template <typename T, typename Hash = std::hash<T>>
    class InternCache {
    public:
        // InternCache Public Methods
        InternCache(Allocator alloc = {}) : hashTable(256, alloc), bufferResource(alloc.resource()), itemAlloc(&bufferResource) {}

        template <typename F>
        const T* Lookup(const T& item, F create) {
            size_t offset = Hash()(item) % hashTable.size();
            int step      = 1;
            mutex.lock_shared();
            while (true) {
                // Check _hashTable[offset]_ for provided item
                if (!hashTable[offset]) {
                    // Insert item into open hash table entry
                    mutex.unlock_shared();
                    mutex.lock();
                    // Double check that another thread hasn't inserted _item_
                    size_t offset = Hash()(item) % hashTable.size();
                    int step      = 1;
                    while (true) {
                        if (!hashTable[offset])
                            // fine--it's definitely not there
                            break;
                        else if (*hashTable[offset] == item) {
                            // Another thread inserted it
                            const T* ret = hashTable[offset];
                            mutex.unlock();
                            return ret;
                        } else {
                            // collision
                            offset += step;
                            ++step;
                            offset %= hashTable.size();
                        }
                    }

                    // Grow the hash table if needed
                    if (4 * nEntries > hashTable.size()) {
                        pstd::vector<const T*> newHash(2 * hashTable.size(), hashTable.get_allocator());
                        for (const T* ptr : hashTable)
                            if (ptr) Insert(ptr, &newHash);

                        hashTable.swap(newHash);
                    }

                    // Allocate new hash table entry and add it to the hash table
                    ++nEntries;
                    T* newPtr = create(itemAlloc, item);
                    Insert(newPtr, &hashTable);
                    mutex.unlock();
                    return newPtr;
                } else if (*hashTable[offset] == item) {
                    // Return pointer for found _item_ in hash table
                    const T* ret = hashTable[offset];
                    mutex.unlock_shared();
                    return ret;
                } else {
                    // Advance _offset_ after hash table collision
                    offset += step;
                    ++step;
                    offset %= hashTable.size();
                }
            }
        }

        const T* Lookup(const T& item) {
            return Lookup(item, [](Allocator alloc, const T& item) { return alloc.new_object<T>(item); });
        }

        size_t size() const {
            return nEntries;
        }
        size_t capacity() const {
            return hashTable.size();
        }

    private:
        // InternCache Private Methods
        void Insert(const T* ptr, pstd::vector<const T*>* table) {
            size_t offset = Hash()(*ptr) % table->size();
            int step      = 1;
            // Advance _offset_ to next free entry in hash table
            while ((*table)[offset]) {
                offset += step;
                ++step;
                offset %= table->size();
            }

            (*table)[offset] = ptr;
        }

        // InternCache Private Members
        pstd::pmr::monotonic_buffer_resource bufferResource;
        Allocator itemAlloc;
        size_t nEntries = 0;
        pstd::vector<const T*> hashTable;
        std::shared_mutex mutex;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_CONTAINERS_H
