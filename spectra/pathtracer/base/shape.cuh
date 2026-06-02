#ifndef SPECTRA_PATHTRACER_BASE_SHAPE_H
#define SPECTRA_PATHTRACER_BASE_SHAPE_H

#include <map>
#include <spectra/pathtracer/base/texture.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    class Interaction;
    class MeshBufferCache;
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

    namespace pathtracer {
        struct RenderConfig;
    } // namespace pathtracer

    // Shape Definition
    class Shape : public TaggedPointer<Sphere, Cylinder, Disk, Triangle, BilinearPatch, Curve> {
    public:
        // Shape Interface
        using TaggedPointer::TaggedPointer;

        static pstd::vector<Shape> Create(const std::string& name, const Transform* renderFromObject, const Transform* objectFromRender, bool reverseOrientation, const ParameterDictionary& parameters, const std::map<std::string, FloatTexture>& floatTextures, const pathtracer::RenderConfig& config, const FileLoc* loc, MeshBufferCache& bufferCache, Allocator alloc);

        __host__ __device__ inline Bounds3f Bounds() const;

        __host__ __device__ inline DirectionCone NormalBounds() const;

        __host__ __device__ inline pstd::optional<ShapeIntersection> Intersect(const Ray& ray, Float tMax = Infinity) const;

        __host__ __device__ inline bool IntersectP(const Ray& ray, Float tMax = Infinity) const;

        __host__ __device__ inline Float Area() const;

        __host__ __device__ inline pstd::optional<ShapeSample> Sample(Point2f u) const;

        __host__ __device__ inline Float PDF(const Interaction&) const;

        __host__ __device__ inline pstd::optional<ShapeSample> Sample(const ShapeSampleContext& ctx, Point2f u) const;

        __host__ __device__ inline Float PDF(const ShapeSampleContext& ctx, Vector3f wi) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_SHAPE_H
