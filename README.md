![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/14c6599b610e65a7ef42174e6910dac53004cec9/spectra-banner2.png)

[![arch](https://github.com/Xayah-Graphics/spectra/actions/workflows/arch.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/arch.yml)
[![windows](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml)

# Spectra

Spectra is a Windows graphics workspace for physical simulation and 3D reconstruction. One native `.spectra` scene is shared by an interactive Vulkan rasterizer and a spectral Vulkan ray-tracing path tracer.

The application owns scene loading, editing, presentation and capture. Simulation plugins publish data through a small C ABI and do not depend on the renderer implementation.

## Rendering architecture

- `spectra`: the only Vulkan runtime. It owns the window, instance, device, descriptor heaps, allocator, swapchain and frame synchronization.
- `spectra.scene`: the canonical scene model and native package format.
- `spectra.renderer`: shared GPU assets, acceleration structures, picking, overlays and render outputs.
- `spectra.rasterizer`: Vulkan Mesh Shader interactive preview.
- `spectra.pathtracer`: Slang wavefront spectral path tracing through `VK_KHR_ray_tracing_pipeline`.
- `spectra.editor`: the single active scene, renderer switching, selection, camera control and direct scene editing.
- `spectra.plugin`: Plugin API 4 host for live deformable meshes, topology-changing meshes, particles and simulation state.

The rasterizer deliberately presents complex materials as a clearly labelled interactive PBR approximation. The path tracer is the physical reference for every feature included in the native scene schema.

## Requirements

- Windows 11
- CMake 4.4
- Ninja
- Microsoft Visual C++ with C++23 standard-library module support
- Vulkan SDK 1.4
- A discrete Vulkan 1.4 GPU supporting Descriptor Heap, Shader Untyped Pointers, Shader Object, Mesh Shader and the complete KHR ray-tracing pipeline profile required by `spectra/spectra.cpp`

Slang 2026.14 and the remaining pinned source dependencies are downloaded and hash-verified by CMake.

CUDA and OptiX are not used.

## Native scene suite

Large scenes and image baselines are intentionally kept outside Git. Configuration requires the external suite directory:

```text
C:\Users\xayah\Documents\Desktop\spectra-scenes
```

It contains 15 native scenes, one shared content-addressed asset store, Path Tracer and Rasterizer baselines, and `suite.json`. Together with the repository Cornell Box, the acceptance suite contains 16 cases.

Use `SPECTRA_SCENE_SUITE_DIR` to select another complete suite. Missing scenes, assets, baselines or an incompatible manifest are errors; there is no download or fallback path.

## Build

Run from a Visual Studio developer command prompt:

```powershell
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DSPECTRA_SCENE_SUITE_DIR=C:/Users/xayah/Documents/Desktop/spectra-scenes
cmake --build cmake-build-release --parallel 30
```

Shader compilation, SPIR-V validation and generation of the shared C++/Slang path-tracing ABI are part of the build.

## Run

Open the default Cornell Box:

```powershell
.\cmake-build-release\spectra.exe
```

Open a native scene or a Plugin API 4 library:

```powershell
.\cmake-build-release\spectra.exe --scene C:\path\scene.spectra
.\cmake-build-release\spectra.exe --plugin C:\path\simulation.dll
```

Select the initial renderer or run a fixed number of successfully presented frames:

```powershell
.\cmake-build-release\spectra.exe --renderer pathtracer
.\cmake-build-release\spectra.exe --frames 5
```

The UI supports Rasterizer/Path Tracer switching, scene hierarchy and inspection, viewport navigation, picking, outlines, transform gizmos, sampler and film controls, and PNG or linear EXR capture. A `.spectra` scene or `.dll` plugin can also be dropped onto the window.

## Test

Run all functional and native scene acceptance tests serially:

```powershell
ctest --test-dir cmake-build-release --parallel 1 --output-on-failure
```

Run only the 16-scene Path Tracer and Rasterizer suite:

```powershell
cmake --build cmake-build-release --target spectra_scene_suite_acceptance --parallel 30
```

Small native fixtures and frozen numerical golden data remain in the repository so material, light, sampling, geometry, volume and dynamic-resource failures can be diagnosed independently of the large scene suite.

## Project layout

- `spectra/`: Vulkan runtime and `GpuDevice` façade.
- `scene/`: canonical schema, resources, spectral data, spatial utilities and `.spectra` serialization.
- `renderer/`: shared GPU scene, workspace, presentation, picking, overlays, UI renderer and image I/O.
- `rasterizer/`: interactive raster backend.
- `pathtracer/`: spectral wavefront path tracer, Slang shaders and immutable sampling tables.
- `plugin/`: zero-renderer-dependency simulation contract and host.
- `app/`: desktop application and workspace UI.
- `scenes/`: repository Cornell Box only.
- `tests/`: native micro-scenes, frozen baselines and regression tests.
- `docs/`: current architecture and migration certificate documentation.

## License

Spectra is distributed under the GNU General Public License v2. See [LICENSE](LICENSE). Third-party algorithm and data notices are retained beside the affected assets and test fixtures.
