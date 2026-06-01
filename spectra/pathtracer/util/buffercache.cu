#include <spectra/pathtracer/util/buffercache.cuh>

namespace spectra {
    // BufferCache Global Definitions
    BufferCache<int>* intBufferCache;
    BufferCache<Point2f>* point2BufferCache;
    BufferCache<Point3f>* point3BufferCache;
    BufferCache<Vector3f>* vector3BufferCache;
    BufferCache<Normal3f>* normal3BufferCache;

    void InitBufferCaches() {
        CHECK(intBufferCache == nullptr);
        intBufferCache     = new BufferCache<int>;
        point2BufferCache  = new BufferCache<Point2f>;
        point3BufferCache  = new BufferCache<Point3f>;
        vector3BufferCache = new BufferCache<Vector3f>;
        normal3BufferCache = new BufferCache<Normal3f>;
    }
} // namespace spectra
