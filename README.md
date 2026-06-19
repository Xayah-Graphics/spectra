![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/14c6599b610e65a7ef42174e6910dac53004cec9/spectra-banner2.png)
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

then drag and drop a `.pbrt` file or plugin `.dll`/`.so` file onto the application window. Plugin libraries open an
empty dynamic project first; the Project popover renders the generic form declared by the plugin descriptor and creates
the dynamic scene only when the plugin-declared open action is pressed. After creation, the same popover displays
plugin-declared project actions, status metrics, and logs.


## Dynamic Scene Plugins Developer Guide

Spectra loads dynamic scenes from platform dynamic libraries. External projects do not need to include Spectra headers,
link Spectra libraries, import Spectra modules, or use Spectra CMake helpers.
Inside Spectra, dynamic plugin/project lifetime is owned by `spectra.scene_runtime`; the rasterizer only supplies the
Vulkan-backed host-service implementation and preview draw passes.

A dynamic scene plugin only needs to:

1. Build a dynamic library.
2. Export `spectra_dynamic_scene_plugin()`.
3. Declare the ABI structs exactly as documented below.

The plugin owns all returned string and array views. Scene metadata is copied into Spectra scene storage during
conversion, but camera visual pixels and viewport voxel grid GPU payloads are borrowed. Camera visual RGBA8 pointers
must stay valid for the plugin instance lifetime, and `revision` must increase when the pixel contents change. Viewport
voxel grid data is borrowed GPU data: the plugin requests a Spectra-owned external Vulkan storage buffer through host
services, imports it into its own GPU runtime, writes the declared source payload, and publishes
`resource_id + source_kind + index_encoding + source_byte_size + revision`.

### Binary Contract

- ABI version: `14`.
- Exported symbol: `spectra_dynamic_scene_plugin`.
- Windows export: `extern "C" __declspec(dllexport)`.
- Other platforms: `extern "C" __attribute__((visibility("default")))`.
- Use the platform default C calling convention.
- Do not use `#pragma pack`, custom alignment, C++ standard library types, C++ exceptions, RTTI objects, allocators, or
  Spectra C++ types across the ABI.

### Required ABI Declarations

Dynamic scene plugins must declare the documented C ABI in the producer project. Do not include Spectra headers, link
Spectra libraries, import Spectra modules, or depend on Spectra CMake targets. The plugin descriptor exposes generic
project/open metadata, an open-options schema, and `get_api`. Spectra renders the open schema in the Project popover,
loads the required scene capability table through `get_api("spectra.dynamic_scene.scene", 1)`, and loads the optional
project-control table through `get_api("spectra.dynamic_scene.project", 1)`.

### Required Export

```cpp
#if defined(_WIN32)
#define SPECTRA_DYNAMIC_SCENE_EXPORT __declspec(dllexport)
#else
#define SPECTRA_DYNAMIC_SCENE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" SPECTRA_DYNAMIC_SCENE_EXPORT const SpectraDynamicScenePlugin* spectra_dynamic_scene_plugin(void);
```

The returned descriptor and every capability table returned by `get_api` must stay valid while the library is loaded.
Set `abi_version` to `14`, set each `struct_size` to the exact matching ABI struct size, and return `OK + null` for
unsupported optional APIs. The scene API is required; missing it is an error.

### Data Rules

- `id`, `title`, material names, light names, camera name, primitive names, and material references must be non-empty.
- `frames_per_second` must be finite and positive.
- `default_coordinate_system` may be empty or one of `SpectraYUp`, `PBRT`, `BlenderZUp`, `OpenGL`, `OpenCV`.
  Unknown names are errors.
- Material `model` values: `lit_surface`, `unlit_surface`, `emissive_surface`, `volume`, `point_sprite`.
- Material `alpha_mode` values: `opaque`, `masked`, `blend`.
- Light `kind` values: `directional`, `point`, `spot`, `area`, `environment`.
- Meshes require non-empty vertices and triangle indices.
- Spheres require positive radius.
- Point radii must be positive.
- Volume dimensions must be positive, and each channel value count must equal `x * y * z`.
- Viewport segment annotations and camera visualization are preview-only. They are consumed by the rasterizer and ignored
  by the pathtracer/PBRT scene.
- Viewport voxel grids are preview-only sparse voxel annotations. They are consumed by the rasterizer and ignored by the
  pathtracer/PBRT scene. `dimensions` and positive `voxel_size` describe the logical grid; `buffer_id` names a
  host-service GPU buffer. `source_kind` values are `0` compacted `uint32_t` index list and `1` dense occupancy
  bitfield. `index_encoding` values are `0` row-major linear `x + dimX * (y + dimY * z)` and `1` Morton/Z-order.
  For `source_kind = 0`, `index_count` is the number of occupied cells in the buffer. For `source_kind = 1`,
  `index_count` must be `0`; Spectra compacts the bitfield in a Vulkan compute pass before drawing. `source_byte_size`
  is the valid source payload byte count, and `revision` must increase when the producer rewrites the buffer contents.
- Viewport width modes: `0` screen-space pixels, `1` world-space units.
- Viewport depth modes: `0` depth tested, `1` always visible.
- Camera projection values: `0` perspective, `1` pinhole intrinsics.
- Camera visual RGBA8 images are borrowed pointers with tightly packed `width * height * 4` bytes.
- `pbrt_template_path` may be empty. If non-empty, it must be relative to the plugin library directory.
- `project_panel_title` and `open_action_label` must be non-empty. `open_action_description` may be empty.
- The project API is optional. If present, `project_actions` may be empty, but every declared action id and label must be
  non-empty and unique. Action option schemas use the same option kind and validation rules as open options.
- If the project API is present, `project_status` phase, headline, metric keys/labels/values, enabled action ids, log
  levels, and log messages must be non-empty. Enabled action ids must refer to declared project actions.
- Open option kinds: `0` text, `1` directory path, `2` file path, `3` choice, `4` bool, `5` float,
  `6` unsigned integer.
- Open option keys must be unique and non-empty. Choice options must declare non-empty unique choices; non-choice
  options must not declare choices. Bool defaults must be `true` or `false`.

### Capability Tables

- `get_api("spectra.dynamic_scene.scene", 1, &api)` must return a non-null `SpectraDynamicSceneSceneApi`.
- `get_api("spectra.dynamic_scene.project", 1, &api)` may return null if the plugin has no project controls.
- Unknown capability names or unsupported capability versions return `OK` with `*api = nullptr`.

### Callback Rules

- Scene API `create` receives a non-null `host_services` pointer in `SpectraDynamicSceneOpenInfo`.
- Host services `request_viewport_voxel_buffer` allocates a Spectra-owned external Vulkan storage buffer and
  returns `resource_id`, `byte_size`, `handle_kind`, an OS external memory handle, and Vulkan device identity. Handle
  kinds are `1` opaque Win32 handle and `2` opaque file descriptor.
- The producer may import that external memory into CUDA or another GPU runtime and write either compacted `uint32_t`
  viewport voxel cell indices or a dense bitfield, according to `source_kind`. The producer owns synchronization in
  v14: GPU writes must be complete before the callback that published the corresponding `ViewportVoxelGrid` returns.
  There is no CPU voxel copy path and no semaphore fallback.
- Host services `release_viewport_voxel_buffer` releases the Spectra resource. Producers must release imported
  GPU mappings and then release every requested resource before instance destruction or reset.
- Scene API `create` returns a plugin-owned instance pointer.
- Scene API `destroy` releases that instance.
- Scene API `reset` resets simulation state and rebuilds internal visualization buffers.
- Scene API `step` advances simulation state by `delta_seconds`.
- Scene API `document` returns static scene data: materials, lights, camera, and static primitives.
- Scene API `frame` returns dynamic primitives for the requested frame cursor.
- Scene API `last_error` returns the most recent instance-local error string; it may be empty. Project API callbacks use
  the same instance error channel.
- Project API `project_update` advances plugin-owned project work once per GUI frame. It is independent from scene
  timeline `step` and should keep long-running work chunked enough for interactive UI.
- Project API `project_action` executes one plugin-declared action with strict key/value options.
- Project API `project_status` returns the current plugin-defined phase, headline, detail, metrics, and currently
  enabled actions.
- Project API `project_logs` returns a plugin-owned log snapshot. Spectra copies log entries for display.

Callbacks must not throw across the ABI. Return `SPECTRA_DYNAMIC_SCENE_RESULT_ERROR` and expose a message through
`last_error`.

Scene URI query strings such as `plugin.dll?dataset=...` are not supported. Open a plugin path directly, then configure
and create the dynamic scene through the Project popover.

## License

Spectra is distributed under the GNU General Public License v2. See [LICENSE](LICENSE).
