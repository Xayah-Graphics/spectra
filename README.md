![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/01ee4171e91dde40477d7d0e0d01d581ca86b463/spectra-banner.png)
# Spectra

[![Windows](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml)
[![Arch Linux](https://github.com/Xayah-Graphics/spectra/actions/workflows/archlinux.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/archlinux.yml)
[![License](https://img.shields.io/github/license/Xayah-Graphics/spectra)](LICENSE)

Spectra is an alpha-stage renderer and visualization workspace for graphics research, physical simulation, and 3D
reconstruction experiments. It provides a Vulkan-based interactive host with two renderer backends:

- **Spectra Rasterizer** for live scene and simulation preview.
- **Spectra Pathtracer** for OptiX/CUDA path-traced rendering of the current scene snapshot.

The project is designed around a shared scene workspace. Static PBRT scenes and runtime dynamic scene plugins are loaded
by the application layer, then consumed by both renderers without renderer-owned file loading.

| Pathtracing Rendering                                                                                                                       | Physical Simulation                                                                                                                                   |
|---------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|
| ![Cornell Box](https://github.com/Xayah-Graphics/imagebed/blob/883141b90fd655fa7e4b227d8a54842ee137392c/spectra-pathtracer-cornell-box.png) | ![Cloth Simulation](https://github.com/Xayah-Graphics/imagebed/blob/0b600f42860a713b7bad36e350fbb55f29a4d97c/spectra-pathtracer-cloth-simulation.png) |

## Build

- C++23 compiler with module support.
- CMake 4.3 or newer.
- Vulkan SDK 1.4 or newer.
- CUDA Toolkit 13.0 or newer.
- NVIDIA OptiX SDK.

```bash
cmake -S . -B build -G Ninja -DSPECTRA_OPTIX_PATH=/path/to/OptiX-SDK
cmake --build build --parallel
```

## Usage

You may load PBRT scenes and dynamic scene plugins directly from the command line.

```bash
./build/spectra_gui --scene /path/to/scene.pbrt
./build/spectra_gui --scene /path/to/plugin.dll
```

You may also load PBRT scenes and dynamic scene plugins through the GUI.

```bash
./build/spectra_gui
```

then drag and drop a `.pbrt` file or plugin `.dll`/`.so` file onto the application window.


## Dynamic Scene Plugins Developer Guide

Spectra loads dynamic scenes from platform dynamic libraries. External projects do not need to include Spectra headers,
link Spectra libraries, import Spectra modules, or use Spectra CMake helpers.

A dynamic scene plugin only needs to:

1. Build a dynamic library.
2. Export `spectra_dynamic_scene_plugin()`.
3. Declare the ABI structs exactly as documented below.

The plugin owns all returned string and array views. Spectra copies view data immediately. Returned views must stay
valid until the next ABI call on the same instance or until `destroy`.

### Binary Contract

- ABI version: `1`.
- Exported symbol: `spectra_dynamic_scene_plugin`.
- Windows export: `extern "C" __declspec(dllexport)`.
- Other platforms: `extern "C" __attribute__((visibility("default")))`.
- Use the platform default C calling convention.
- Do not use `#pragma pack`, custom alignment, C++ standard library types, C++ exceptions, RTTI objects, allocators, or
  Spectra C++ types across the ABI.

### Required ABI Declarations

<details>
<summary>Complete C ABI declarations</summary>

```cpp
#include <stdint.h>

#define SPECTRA_DYNAMIC_SCENE_ABI_VERSION 1u

struct SpectraDynamicSceneInstance;

enum SpectraDynamicSceneResult {
    SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0,
    SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1,
};

struct SpectraDynamicSceneString {
    const char* data;
    uint64_t size;
};

struct SpectraDynamicSceneTransform {
    float position[3];
    float rotation[4];
    float scale[3];
};

struct SpectraDynamicSceneMaterial {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneString model;
    SpectraDynamicSceneString alpha_mode;
    float base_color[4];
    float emission_color[3];
    float emission_strength;
    float roughness;
    float metallic;
    float alpha_cutoff;
    float volume_density_scale;
    float volume_temperature_scale;
};

struct SpectraDynamicSceneMaterialSpan {
    const SpectraDynamicSceneMaterial* data;
    uint64_t count;
};

struct SpectraDynamicSceneLight {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneString kind;
    SpectraDynamicSceneTransform transform;
    float color[3];
    float intensity;
    float cone_angle_degrees;
};

struct SpectraDynamicSceneLightSpan {
    const SpectraDynamicSceneLight* data;
    uint64_t count;
};

struct SpectraDynamicSceneCamera {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneTransform transform;
    float target[3];
    float up[3];
    float vertical_fov_degrees;
    float near_plane;
    float far_plane;
};

struct SpectraDynamicSceneMeshVertex {
    float position[3];
    float normal[3];
};

struct SpectraDynamicSceneMeshVertexSpan {
    const SpectraDynamicSceneMeshVertex* data;
    uint64_t count;
};

struct SpectraDynamicSceneUInt32Span {
    const uint32_t* data;
    uint64_t count;
};

struct SpectraDynamicSceneMesh {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneMeshVertexSpan vertices;
    SpectraDynamicSceneUInt32Span indices;
    SpectraDynamicSceneString material_name;
    SpectraDynamicSceneTransform transform;
};

struct SpectraDynamicSceneMeshSpan {
    const SpectraDynamicSceneMesh* data;
    uint64_t count;
};

struct SpectraDynamicSceneSphere {
    SpectraDynamicSceneString name;
    float radius;
    SpectraDynamicSceneString material_name;
    SpectraDynamicSceneTransform transform;
};

struct SpectraDynamicSceneSphereSpan {
    const SpectraDynamicSceneSphere* data;
    uint64_t count;
};

struct SpectraDynamicScenePoint {
    float position[3];
    float normal[3];
    float color[4];
    float radius;
};

struct SpectraDynamicScenePointSpan {
    const SpectraDynamicScenePoint* data;
    uint64_t count;
};

struct SpectraDynamicScenePointCloud {
    SpectraDynamicSceneString name;
    SpectraDynamicScenePointSpan points;
    SpectraDynamicSceneString material_name;
    SpectraDynamicSceneTransform transform;
};

struct SpectraDynamicScenePointCloudSpan {
    const SpectraDynamicScenePointCloud* data;
    uint64_t count;
};

struct SpectraDynamicSceneFloatSpan {
    const float* data;
    uint64_t count;
};

struct SpectraDynamicSceneVolumeChannel {
    SpectraDynamicSceneString name;
    uint32_t dimensions[3];
    SpectraDynamicSceneFloatSpan values;
};

struct SpectraDynamicSceneVolumeChannelSpan {
    const SpectraDynamicSceneVolumeChannel* data;
    uint64_t count;
};

struct SpectraDynamicSceneVolume {
    SpectraDynamicSceneString name;
    uint32_t dimensions[3];
    float origin[3];
    float voxel_size[3];
    SpectraDynamicSceneVolumeChannelSpan channels;
    SpectraDynamicSceneString material_name;
};

struct SpectraDynamicSceneVolumeSpan {
    const SpectraDynamicSceneVolume* data;
    uint64_t count;
};

struct SpectraDynamicSceneDocumentView {
    uint64_t struct_size;
    uint32_t has_camera;
    SpectraDynamicSceneCamera camera;
    SpectraDynamicSceneMaterialSpan materials;
    SpectraDynamicSceneLightSpan lights;
    SpectraDynamicSceneMeshSpan meshes;
    SpectraDynamicSceneSphereSpan spheres;
    SpectraDynamicScenePointCloudSpan point_clouds;
    SpectraDynamicSceneVolumeSpan volumes;
};

struct SpectraDynamicSceneFrameInfo {
    double delta_seconds;
    double time_seconds;
    uint64_t frame_index;
};

struct SpectraDynamicSceneFrameView {
    uint64_t struct_size;
    SpectraDynamicSceneMeshSpan meshes;
    SpectraDynamicSceneSphereSpan spheres;
    SpectraDynamicScenePointCloudSpan point_clouds;
    SpectraDynamicSceneVolumeSpan volumes;
};

typedef SpectraDynamicSceneResult (*SpectraDynamicSceneCreateFn)(SpectraDynamicSceneInstance** instance);
typedef void (*SpectraDynamicSceneDestroyFn)(SpectraDynamicSceneInstance* instance);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneResetFn)(SpectraDynamicSceneInstance* instance);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneStepFn)(SpectraDynamicSceneInstance* instance, float delta_seconds);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneDocumentFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneDocumentView* document);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneFrameFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneFrameInfo frame, SpectraDynamicSceneFrameView* snapshot);
typedef SpectraDynamicSceneString (*SpectraDynamicSceneLastErrorFn)(SpectraDynamicSceneInstance* instance);

struct SpectraDynamicScenePlugin {
    uint32_t abi_version;
    uint64_t struct_size;
    SpectraDynamicSceneString id;
    SpectraDynamicSceneString title;
    SpectraDynamicSceneString pbrt_template_path;
    double frames_per_second;
    SpectraDynamicSceneCreateFn create;
    SpectraDynamicSceneDestroyFn destroy;
    SpectraDynamicSceneResetFn reset;
    SpectraDynamicSceneStepFn step;
    SpectraDynamicSceneDocumentFn document;
    SpectraDynamicSceneFrameFn frame;
    SpectraDynamicSceneLastErrorFn last_error;
};
```

</details>

### Required Export

```cpp
#if defined(_WIN32)
#define SPECTRA_DYNAMIC_SCENE_EXPORT __declspec(dllexport)
#else
#define SPECTRA_DYNAMIC_SCENE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" SPECTRA_DYNAMIC_SCENE_EXPORT const SpectraDynamicScenePlugin* spectra_dynamic_scene_plugin(void);
```

The returned descriptor must stay valid while the library is loaded. Set `abi_version` to
`SPECTRA_DYNAMIC_SCENE_ABI_VERSION`, set `struct_size` to `sizeof(SpectraDynamicScenePlugin)`, and provide every
function pointer.

### Data Rules

- `id`, `title`, material names, light names, camera name, primitive names, and material references must be non-empty.
- `frames_per_second` must be finite and positive.
- Material `model` values: `lit_surface`, `unlit_surface`, `emissive_surface`, `volume`, `point_sprite`.
- Material `alpha_mode` values: `opaque`, `masked`, `blend`.
- Light `kind` values: `directional`, `point`, `spot`, `area`, `environment`.
- Meshes require non-empty vertices and triangle indices.
- Spheres require positive radius.
- Point radii must be positive.
- Volume dimensions must be positive, and each channel value count must equal `x * y * z`.
- `pbrt_template_path` may be empty. If non-empty, it must be relative to the plugin library directory.

### Callback Rules

- `create` returns a plugin-owned instance pointer.
- `destroy` releases that instance.
- `reset` resets simulation state and rebuilds internal visualization buffers.
- `step` advances simulation state by `delta_seconds`.
- `document` returns static scene data: materials, lights, camera, and static primitives.
- `frame` returns dynamic primitives for the requested frame cursor.
- `last_error` returns the most recent instance-local error string; it may be empty.

Callbacks must not throw across the ABI. Return `SPECTRA_DYNAMIC_SCENE_RESULT_ERROR` and expose a message through
`last_error`.

## License

Spectra is distributed under the GNU General Public License v2. See [LICENSE](LICENSE).
