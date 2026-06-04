#ifndef SPECTRA_PATHTRACER_UTIL_MIPMAP_H
#define SPECTRA_PATHTRACER_UTIL_MIPMAP_H

#include <memory>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/image.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <string>
#include <vector>

namespace spectra {
    // FilterFunction Definition
    enum class FilterFunction { Point, Bilinear, Trilinear, EWA };

    inline pstd::optional<FilterFunction> ParseFilter(const std::string& f) {
        if (f == "ewa" || f == "EWA")
            return FilterFunction::EWA;
        else if (f == "trilinear")
            return FilterFunction::Trilinear;
        else if (f == "bilinear")
            return FilterFunction::Bilinear;
        else if (f == "point")
            return FilterFunction::Point;
        else
            return {};
    }

    // MIPMapFilterOptions Definition
    struct MIPMapFilterOptions {
        FilterFunction filter = FilterFunction::EWA;
        Float maxAnisotropy   = 8.f;

        bool operator<(MIPMapFilterOptions o) const {
            return std::tie(filter, maxAnisotropy) < std::tie(o.filter, o.maxAnisotropy);
        }
    };

    // MIPMap Definition
    class MIPMap {
    public:
        // MIPMap Public Methods
        MIPMap(Image image, const RGBColorSpace* colorSpace, WrapMode wrapMode, Allocator alloc, const MIPMapFilterOptions& options);
        static MIPMap* CreateFromFile(const std::string& filename, const MIPMapFilterOptions& options, WrapMode wrapMode, ColorEncoding encoding, Allocator alloc);

        template <typename T>
        T Filter(Point2f st, Vector2f dstdx, Vector2f dstdy) const;


        Point2i LevelResolution(int level) const {
            CHECK(level >= 0 && level < pyramid.size());
            return pyramid[level].Resolution();
        }

        int Levels() const {
            return int(pyramid.size());
        }
        const RGBColorSpace* GetRGBColorSpace() const {
            return colorSpace;
        }
        const Image& GetLevel(int level) const {
            return pyramid[level];
        }

    private:
        // MIPMap Private Methods
        template <typename T>
        T Texel(int level, Point2i st) const;
        template <typename T>
        T Bilerp(int level, Point2f st) const;
        template <typename T>
        T EWA(int level, Point2f st, Vector2f dst0, Vector2f dst1) const;

        // MIPMap Private Members
        pstd::vector<Image> pyramid;
        const RGBColorSpace* colorSpace;
        WrapMode wrapMode;
        MIPMapFilterOptions options;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_MIPMAP_H
