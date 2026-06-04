#ifndef SPECTRA_PATHTRACER_UTIL_LOOPSUBDIV_H
#define SPECTRA_PATHTRACER_UTIL_LOOPSUBDIV_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/vecmath.cuh>

namespace spectra {
    class MeshBufferCache;
    class Transform;
    class TriangleMesh;

    // LoopSubdiv Declarations
    TriangleMesh* LoopSubdivide(const Transform* renderFromObject, bool reverseOrientation, int nLevels, pstd::span<const int> vertexIndices, pstd::span<const Point3f> p, MeshBufferCache& bufferCache, Allocator alloc);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_LOOPSUBDIV_H
