#ifndef SPECTRA_PATHTRACER_UTIL_TAGGEDPTR_H
#define SPECTRA_PATHTRACER_UTIL_TAGGEDPTR_H

#include <algorithm>
#include <spectra/pathtracer/util/check.h>
#include <spectra/pathtracer/util/containers.h>
#include <spectra/pathtracer/util/float.h>
#include <string>
#include <type_traits>
#include <utility>

namespace spectra {
    namespace detail {
        template <typename F, typename R, typename T>
        SPECTRA_CPU_GPU R Dispatch(F&& func, const void* ptr, int index) {
            DCHECK_EQ(0, index);
            return func(reinterpret_cast<const T*>(ptr));
        }

        template <typename F, typename R, typename T>
        SPECTRA_CPU_GPU R Dispatch(F&& func, void* ptr, int index) {
            DCHECK_EQ(0, index);
            return func(reinterpret_cast<T*>(ptr));
        }

        template <typename F, typename R, typename T, typename Next, typename... Rest>
        SPECTRA_CPU_GPU R Dispatch(F&& func, const void* ptr, int index) {
            DCHECK_GE(index, 0);
            if (index == 0) return func(reinterpret_cast<const T*>(ptr));
            return Dispatch<F, R, Next, Rest...>(std::forward<F>(func), ptr, index - 1);
        }

        template <typename F, typename R, typename T, typename Next, typename... Rest>
        SPECTRA_CPU_GPU R Dispatch(F&& func, void* ptr, int index) {
            DCHECK_GE(index, 0);
            if (index == 0) return func(reinterpret_cast<T*>(ptr));
            return Dispatch<F, R, Next, Rest...>(std::forward<F>(func), ptr, index - 1);
        }

        template <typename F, typename R, typename T>
        R DispatchHost(F&& func, const void* ptr, int index) {
            DCHECK_EQ(0, index);
            return func(reinterpret_cast<const T*>(ptr));
        }

        template <typename F, typename R, typename T>
        R DispatchHost(F&& func, void* ptr, int index) {
            DCHECK_EQ(0, index);
            return func(reinterpret_cast<T*>(ptr));
        }

        template <typename F, typename R, typename T, typename Next, typename... Rest>
        R DispatchHost(F&& func, const void* ptr, int index) {
            DCHECK_GE(index, 0);
            if (index == 0) return func(reinterpret_cast<const T*>(ptr));
            return DispatchHost<F, R, Next, Rest...>(std::forward<F>(func), ptr, index - 1);
        }

        template <typename F, typename R, typename T, typename Next, typename... Rest>
        R DispatchHost(F&& func, void* ptr, int index) {
            DCHECK_GE(index, 0);
            if (index == 0) return func(reinterpret_cast<T*>(ptr));
            return DispatchHost<F, R, Next, Rest...>(std::forward<F>(func), ptr, index - 1);
        }

        template <typename... Ts>
        struct IsSameType;

        template <>
        struct IsSameType<> {
            static constexpr bool value = true;
        };

        template <typename T>
        struct IsSameType<T> {
            static constexpr bool value = true;
        };

        template <typename T, typename U, typename... Ts>
        struct IsSameType<T, U, Ts...> {
            static constexpr bool value = (std::is_same_v<T, U> && IsSameType<U, Ts...>::value);
        };

        template <typename... Ts>
        struct SameType;

        template <typename T, typename... Ts>
        struct SameType<T, Ts...> {
            using type = T;
            static_assert(IsSameType<T, Ts...>::value, "Not all types in pack are the same");
        };

        template <typename F, typename... Ts>
        struct ReturnType {
            using type = SameType<std::invoke_result_t<F, Ts*>...>::type;
        };

        template <typename F, typename... Ts>
        struct ReturnTypeConst {
            using type = SameType<std::invoke_result_t<F, const Ts*>...>::type;
        };
    } // namespace detail

    // TaggedPointer Definition
    template <typename... Ts>
    class TaggedPointer {
    public:
        // TaggedPointer Public Types
        using Types = TypePack<Ts...>;

        // TaggedPointer Public Methods
        TaggedPointer() = default;

        template <typename T>
        SPECTRA_CPU_GPU TaggedPointer(T* ptr) {
            uint64_t iptr = reinterpret_cast<uint64_t>(ptr);
            DCHECK_EQ(iptr & ptrMask, iptr);
            constexpr unsigned int type = TypeIndex<T>();
            bits                        = iptr | ((uint64_t) type << tagShift);
        }

        SPECTRA_CPU_GPU
        TaggedPointer(std::nullptr_t np) {}

        SPECTRA_CPU_GPU
        TaggedPointer(const TaggedPointer& t) {
            bits = t.bits;
        }
        SPECTRA_CPU_GPU
        TaggedPointer& operator=(const TaggedPointer& t) {
            bits = t.bits;
            return *this;
        }

        template <typename T>
        SPECTRA_CPU_GPU static constexpr unsigned int TypeIndex() {
            using Tp = std::remove_cv_t<T>;
            if constexpr (std::is_same_v<Tp, std::nullptr_t>)
                return 0;
            else
                return 1 + IndexOf<Tp, Types>::count;
        }

        SPECTRA_CPU_GPU
        unsigned int Tag() const {
            return ((bits & tagMask) >> tagShift);
        }

        template <typename T>
        SPECTRA_CPU_GPU bool Is() const {
            return Tag() == TypeIndex<T>();
        }

        SPECTRA_CPU_GPU
        static constexpr unsigned int MaxTag() {
            return sizeof...(Ts);
        }
        SPECTRA_CPU_GPU
        static constexpr unsigned int NumTags() {
            return MaxTag() + 1;
        }

        SPECTRA_CPU_GPU
        explicit operator bool() const {
            return (bits & ptrMask) != 0;
        }

        SPECTRA_CPU_GPU
        bool operator<(const TaggedPointer& tp) const {
            return bits < tp.bits;
        }

        template <typename T>
        SPECTRA_CPU_GPU T* Cast() {
            DCHECK(Is<T>());
            return reinterpret_cast<T*>(ptr());
        }

        template <typename T>
        SPECTRA_CPU_GPU const T* Cast() const {
            DCHECK(Is<T>());
            return reinterpret_cast<const T*>(ptr());
        }

        template <typename T>
        SPECTRA_CPU_GPU T* CastOrNullptr() {
            if (Is<T>())
                return reinterpret_cast<T*>(ptr());
            else
                return nullptr;
        }

        template <typename T>
        SPECTRA_CPU_GPU const T* CastOrNullptr() const {
            if (Is<T>())
                return reinterpret_cast<const T*>(ptr());
            else
                return nullptr;
        }


        SPECTRA_CPU_GPU
        bool operator==(const TaggedPointer& tp) const {
            return bits == tp.bits;
        }
        SPECTRA_CPU_GPU
        bool operator!=(const TaggedPointer& tp) const {
            return bits != tp.bits;
        }

        SPECTRA_CPU_GPU
        void* ptr() {
            return reinterpret_cast<void*>(bits & ptrMask);
        }

        SPECTRA_CPU_GPU
        const void* ptr() const {
            return reinterpret_cast<const void*>(bits & ptrMask);
        }

        template <typename F>
        SPECTRA_CPU_GPU decltype(auto) Dispatch(F&& func) {
            DCHECK(ptr());
            using R = detail::ReturnType<F, Ts...>::type;
            return detail::Dispatch<F, R, Ts...>(std::forward<F>(func), ptr(), Tag() - 1);
        }

        template <typename F>
        SPECTRA_CPU_GPU decltype(auto) Dispatch(F&& func) const {
            DCHECK(ptr());
            using R = detail::ReturnTypeConst<F, Ts...>::type;
            return detail::Dispatch<F, R, Ts...>(std::forward<F>(func), ptr(), Tag() - 1);
        }

        template <typename F>
        decltype(auto) DispatchCPU(F&& func) {
            DCHECK(ptr());
            using R = detail::ReturnType<F, Ts...>::type;
            return detail::DispatchHost<F, R, Ts...>(std::forward<F>(func), ptr(), Tag() - 1);
        }

        template <typename F>
        decltype(auto) DispatchCPU(F&& func) const {
            DCHECK(ptr());
            using R = detail::ReturnTypeConst<F, Ts...>::type;
            return detail::DispatchHost<F, R, Ts...>(std::forward<F>(func), ptr(), Tag() - 1);
        }

    private:
        static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "Expected pointer size to be <= 64 bits");
        // TaggedPointer Private Members
        static constexpr int tagShift     = 57;
        static constexpr int tagBits      = 64 - tagShift;
        static constexpr uint64_t tagMask = ((1ull << tagBits) - 1) << tagShift;
        static constexpr uint64_t ptrMask = ~tagMask;
        uint64_t bits                     = 0;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_TAGGEDPTR_H
