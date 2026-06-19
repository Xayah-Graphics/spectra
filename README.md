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
the dynamic scene only when the plugin-declared open action is pressed.


## Dynamic Scene Plugins Developer Guide

Spectra loads dynamic scenes from platform dynamic libraries. External projects do not need to include Spectra headers,
link Spectra libraries, import Spectra modules, or use Spectra CMake helpers.

A dynamic scene plugin only needs to:

1. Build a dynamic library.
2. Export `spectra_dynamic_scene_plugin()`.
3. Declare the ABI structs exactly as documented below.

The plugin owns all returned string and array views. Non-image view data is copied into Spectra scene storage during
conversion. Camera visual RGBA8 pointers are borrowed; the pointed image memory must stay valid for the plugin instance
lifetime, and `revision` must increase when the pixel contents change.

### Binary Contract

- ABI version: `10`.
- Exported symbol: `spectra_dynamic_scene_plugin`.
- Windows export: `extern "C" __declspec(dllexport)`.
- Other platforms: `extern "C" __attribute__((visibility("default")))`.
- Use the platform default C calling convention.
- Do not use `#pragma pack`, custom alignment, C++ standard library types, C++ exceptions, RTTI objects, allocators, or
  Spectra C++ types across the ABI.

### Required ABI Declarations

Dynamic scene plugins must declare the documented C ABI in the producer project. Do not include Spectra headers, link
Spectra libraries, import Spectra modules, or depend on Spectra CMake targets. The plugin descriptor may expose a
generic project/open metadata and an open-options schema. Spectra renders that schema in the Project popover and passes
strict key/value options to `create` through `SpectraDynamicSceneOpenInfo`, together with the resolved plugin path.


### Required Export

```cpp
#if defined(_WIN32)
#define SPECTRA_DYNAMIC_SCENE_EXPORT __declspec(dllexport)
#else
#define SPECTRA_DYNAMIC_SCENE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" SPECTRA_DYNAMIC_SCENE_EXPORT const SpectraDynamicScenePlugin* spectra_dynamic_scene_plugin(void);
```

The returned descriptor must stay valid while the library is loaded. Set `abi_version` to `10`, set `struct_size` to
`sizeof(SpectraDynamicScenePlugin)`, and provide every function pointer.

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
- Viewport segment annotations and camera visualization are preview-only. They are loaded by the rasterizer and ignored
  by the pathtracer/PBRT scene.
- Viewport width modes: `0` screen-space pixels, `1` world-space units.
- Viewport depth modes: `0` depth tested, `1` always visible.
- Camera projection values: `0` perspective, `1` pinhole intrinsics.
- Camera visual RGBA8 images are borrowed pointers with tightly packed `width * height * 4` bytes.
- `pbrt_template_path` may be empty. If non-empty, it must be relative to the plugin library directory.
- `project_panel_title` and `open_action_label` must be non-empty. `open_action_description` may be empty.
- Open option kinds: `0` text, `1` directory path, `2` file path, `3` choice, `4` bool, `5` float,
  `6` unsigned integer.
- Open option keys must be unique and non-empty. Choice options must declare non-empty unique choices; non-choice
  options must not declare choices. Bool defaults must be `true` or `false`.

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

Scene URI query strings such as `plugin.dll?dataset=...` are not supported. Open a plugin path directly, then configure
and create the dynamic scene through the Project popover.

## License

Spectra is distributed under the GNU General Public License v2. See [LICENSE](LICENSE).
