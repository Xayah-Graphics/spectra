#ifndef SPECTRA_OPTIX_AGGREGATE_H
#define SPECTRA_OPTIX_AGGREGATE_H

#include <cstring>
#include <cuda.h>
#include <cuda_runtime.h>
#include <map>
#include <optix.h>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/optix/optix.cuh>
#include <pathtracer/util/containers.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/soa.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <pathtracer/wavefront/workitems.cuh>
#include <string>
#include <vector>

namespace spectra {
    class MeshBufferCache;
} // namespace spectra

namespace spectra::pathtracer {
    struct RenderConfig;
    class CompiledScene;
    struct ShapeEntity;
} // namespace spectra::pathtracer

namespace spectra::optix {
    class SpectraOptiXAggregate {
    public:
        SpectraOptiXAggregate(pathtracer::CompiledScene& scene, const pathtracer::RenderConfig& config, pathtracer::PathtracerMemoryScope* memoryScope);
        ~SpectraOptiXAggregate();

        Bounds3f Bounds() const {
            return bounds;
        }

        void IntersectClosest(int maxRays, const RayQueue* rayQueue, EscapedRayQueue* escapedRayQueue, HitAreaLightQueue* hitAreaLightQueue, MaterialEvalQueue* basicEvalMaterialQueue, MaterialEvalQueue* universalEvalMaterialQueue, MediumSampleQueue* mediumSampleQueue, RayQueue* nextRayQueue) const;

        void IntersectShadow(int maxRays, ShadowRayQueue* shadowRayQueue, SOA<PixelSampleState>* pixelSampleState) const;

        void IntersectShadowTr(int maxRays, ShadowRayQueue* shadowRayQueue, SOA<PixelSampleState>* pixelSampleState) const;

        void IntersectOneRandom(int maxRays, SubsurfaceScatterQueue* subsurfaceScatterQueue) const;

        // WAR: The enclosing parent function ("PreparePLYMeshes") for an
        // extended __device__ lambda cannot have private or protected access
        // within its class, so it's public...
        static std::map<int, TriQuadMesh> PreparePLYMeshes(const std::vector<pathtracer::ShapeEntity>& shapes, const std::map<std::string, FloatTexture>& floatTextures, Float displacementEdgeScale);

    private:
        struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) HitgroupRecord {
            HitgroupRecord() {}

            HitgroupRecord(const HitgroupRecord& r) {
                std::memcpy(this, &r, sizeof(HitgroupRecord));
            }

            HitgroupRecord& operator=(const HitgroupRecord& r) {
                if (this != &r) std::memcpy(this, &r, sizeof(HitgroupRecord));
                return *this;
            }

            alignas(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];

            union {
                TriangleMeshRecord triRec;
                BilinearMeshRecord bilinearRec;
                QuadricRecord quadricRec;
            };
        };

        struct BVH {
            BVH() = default;
            BVH(size_t size);

            BVH(const BVH&)            = delete;
            BVH& operator=(const BVH&) = delete;
            BVH(BVH&&);
            BVH& operator=(BVH&&);
            ~BVH();

            OptixTraversableHandle traversableHandle = {};
            pathtracer::PathtracerDeviceBuffer accelBuffer;
            std::vector<HitgroupRecord> intersectHGRecords;
            std::vector<HitgroupRecord> shadowHGRecords;
            std::vector<HitgroupRecord> randomHitHGRecords;
            Bounds3f bounds;
        };

        struct Accel {
            OptixTraversableHandle traversableHandle = {};
            pathtracer::PathtracerDeviceBuffer buffer;
        };

        static BVH buildBVHForTriangles(const std::vector<pathtracer::ShapeEntity>& shapes, const std::map<int, TriQuadMesh>& plyMeshes, OptixDeviceContext optixContext, const OptixProgramGroup& intersectPG, const OptixProgramGroup& shadowPG, const OptixProgramGroup& randomHitPG, const std::map<std::string, FloatTexture>& floatTextures, const std::map<std::string, Material>& materials, const std::map<std::string, Medium>& media, const std::map<int, pstd::vector<Light>*>& shapeIndexToAreaLights, MeshBufferCache& meshBufferCache, ThreadLocal<Allocator>& threadAllocators, ThreadLocal<pathtracer::PathtracerCudaStream>& threadCUDAStreams);

        static BilinearPatchMesh* diceCurveToBLP(const pathtracer::ShapeEntity& shape, int nDiceU, int nDiceV, MeshBufferCache& meshBufferCache, Allocator alloc);

        static BVH buildBVHForBLPs(const std::vector<pathtracer::ShapeEntity>& shapes, OptixDeviceContext optixContext, const OptixProgramGroup& intersectPG, const OptixProgramGroup& shadowPG, const OptixProgramGroup& randomHitPG, const std::map<std::string, FloatTexture>& floatTextures, const std::map<std::string, Material>& materials, const std::map<std::string, Medium>& media, const std::map<int, pstd::vector<Light>*>& shapeIndexToAreaLights, const pathtracer::RenderConfig& config, MeshBufferCache& meshBufferCache, ThreadLocal<Allocator>& threadAllocators, ThreadLocal<pathtracer::PathtracerCudaStream>& threadCUDAStreams);

        static BVH buildBVHForQuadrics(const std::vector<pathtracer::ShapeEntity>& shapes, OptixDeviceContext optixContext, const OptixProgramGroup& intersectPG, const OptixProgramGroup& shadowPG, const OptixProgramGroup& randomHitPG, const std::map<std::string, FloatTexture>& floatTextures, const std::map<std::string, Material>& materials, const std::map<std::string, Medium>& media, const std::map<int, pstd::vector<Light>*>& shapeIndexToAreaLights, const pathtracer::RenderConfig& config, MeshBufferCache& meshBufferCache, ThreadLocal<Allocator>& threadAllocators, ThreadLocal<pathtracer::PathtracerCudaStream>& threadCUDAStreams);

        int addHGRecords(BVH& bvh);

        static OptixModule createOptiXModule(OptixDeviceContext optixContext, const char* input, size_t inputSize);
        static OptixPipelineCompileOptions getPipelineCompileOptions();

        OptixProgramGroup createRaygenPG(const char* entrypoint);
        OptixProgramGroup createMissPG(const char* entrypoint);
        OptixProgramGroup createIntersectionPG(const char* closest, const char* any, const char* intersect);

        static Accel buildOptixBVH(OptixDeviceContext optixContext, const std::vector<OptixBuildInput>& buildInputs, ThreadLocal<pathtracer::PathtracerCudaStream>& threadCUDAStreams);

        pathtracer::PathtracerMemoryScope* memoryScope;
        std::mutex boundsMutex;
        Bounds3f bounds;
        CUstream cudaStream             = nullptr;
        OptixDeviceContext optixContext = nullptr;
        OptixModule optixModule         = nullptr;
        OptixPipeline optixPipeline     = nullptr;
        std::vector<OptixProgramGroup> programGroups;
        std::vector<pathtracer::PathtracerDeviceBuffer> accelBuffers;
        std::vector<pathtracer::PathtracerDeviceBuffer> sbtBuffers;

        struct ParamBufferState {
            bool used = false;
            pathtracer::PathtracerCudaEvent finishedEvent;
            pathtracer::PathtracerDeviceBuffer deviceBuffer;
            pathtracer::PathtracerPinnedHostBuffer hostBuffer;
        };

        mutable std::vector<ParamBufferState> paramsPool;
        mutable size_t nextParamOffset = 0;

        ParamBufferState& getParamBuffer(const RayIntersectParameters&) const;

        pstd::vector<HitgroupRecord> intersectHGRecords;
        pstd::vector<HitgroupRecord> shadowHGRecords;
        pstd::vector<HitgroupRecord> randomHitHGRecords;
        OptixShaderBindingTable intersectSBT = {}, shadowSBT = {}, shadowTrSBT = {};
        OptixShaderBindingTable randomHitSBT   = {};
        OptixTraversableHandle rootTraversable = {};
    };
} // namespace spectra::optix

#endif // SPECTRA_OPTIX_AGGREGATE_H
