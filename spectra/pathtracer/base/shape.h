#ifndef SPECTRA_PATHTRACER_BASE_SHAPE_H
#define SPECTRA_PATHTRACER_BASE_SHAPE_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/base/texture.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <map>
#include <string>

namespace spectra
{
    class Interaction;
    class ParameterDictionary;
    class Ray;
    class Transform;
    struct FileLoc;

    // Shape Declarations
    class Triangle;
    class BilinearPatch;
    class Curve;
    class Sphere;
    class Cylinder;
    class Disk;

    struct ShapeSample;
    struct ShapeIntersection;
    struct ShapeSampleContext;

    // Shape Definition
    class Shape
        : public TaggedPointer<Sphere, Cylinder, Disk, Triangle, BilinearPatch, Curve>
    {
    public:
        // Shape Interface
        using TaggedPointer::TaggedPointer;

        static pstd::vector<Shape> Create(
            const std::string& name, const Transform* renderFromObject,
            const Transform* objectFromRender, bool reverseOrientation,
            const ParameterDictionary& parameters,
            const std::map<std::string, FloatTexture>& floatTextures, const FileLoc* loc,
            Allocator alloc);

        SPECTRA_CPU_GPU inline Bounds3f Bounds() const;

        SPECTRA_CPU_GPU inline DirectionCone NormalBounds() const;

        SPECTRA_CPU_GPU inline pstd::optional<ShapeIntersection> Intersect(
            const Ray& ray, Float tMax = Infinity) const;

        SPECTRA_CPU_GPU inline bool IntersectP(const Ray& ray, Float tMax = Infinity) const;

        SPECTRA_CPU_GPU inline Float Area() const;

        SPECTRA_CPU_GPU inline pstd::optional<ShapeSample> Sample(Point2f u) const;

        SPECTRA_CPU_GPU inline Float PDF(const Interaction&) const;

        SPECTRA_CPU_GPU inline pstd::optional<ShapeSample> Sample(const ShapeSampleContext& ctx,
                                                               Point2f u) const;

        SPECTRA_CPU_GPU inline Float PDF(const ShapeSampleContext& ctx, Vector3f wi) const;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_SHAPE_H
