#ifndef SPECTRA_PATHTRACER_UTIL_LOOPSUBDIV_H
#define SPECTRA_PATHTRACER_UTIL_LOOPSUBDIV_H

#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>

namespace spectra {
    class MeshBufferCache;
    class Transform;
    class TriangleMesh;

    // LoopSubdiv Declarations
    TriangleMesh* LoopSubdivide(const Transform* renderFromObject, bool reverseOrientation, int nLevels, pstd::span<const int> vertexIndices, pstd::span<const Point3f> p, MeshBufferCache& bufferCache, Allocator alloc);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_LOOPSUBDIV_H
