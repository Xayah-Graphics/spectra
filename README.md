![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/14c6599b610e65a7ef42174e6910dac53004cec9/spectra-banner2.png)

# Spectra

[![Windows](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml)
[![Arch Linux](https://github.com/Xayah-Graphics/spectra/actions/workflows/archlinux.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/archlinux.yml)
[![License](https://img.shields.io/github/license/Xayah-Graphics/spectra)](LICENSE)

Spectra is a C++23 graphics research workspace for inspecting static PBRT scenes and live plugin-driven scenes.
It owns one shared `spectra.scene` workspace and lets multiple renderer backends consume the same active scene.

| Pathtracing Rendering                                                                                                                       | Physical Simulation                                                                                                                                   |
|---------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|
| ![Cornell Box](https://github.com/Xayah-Graphics/imagebed/blob/883141b90fd655fa7e4b227d8a54842ee137392c/spectra-pathtracer-cornell-box.png) | ![Cloth Simulation](https://github.com/Xayah-Graphics/imagebed/blob/0b600f42860a713b7bad36e350fbb55f29a4d97c/spectra-pathtracer-cloth-simulation.png) |

## Highlights

- **Shared scene model**: PBRT files and runtime scene plugins resolve into the same `spectra.scene` data model.
- **Live rasterizer**: Vulkan renderer for interactive preview, debug overlays, camera visuals, volumes, point clouds,
  and scene attachments.
- **Path tracer**: CUDA/OptiX renderer for path-traced snapshots of the active scene.
- **Scene plugins**: External dynamic libraries can publish dynamic meshes, cameras, volumes, debug attachments,
  controls, and live HUD state without including Spectra headers.
- **Zero renderer-owned loading**: renderers receive the active scene; file/plugin loading stays in the application
  scene layer.

## Requirements

- CMake 4.3+
- C++23 compiler with modules support
- Vulkan SDK 1.4+
- CUDA Toolkit 13+
- NVIDIA OptiX SDK 9+
- Ninja is recommended

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPECTRA_OPTIX_PATH=/path/to/OptiX-SDK
cmake --build build --parallel
```

The main desktop executable target is `spectra`.

## Run

Open an empty workspace:

```bash
./build/spectra
```

Open a PBRT scene or scene plugin directly:

```bash
./build/spectra --scene /path/to/scene.pbrt
./build/spectra --scene /path/to/plugin.dll
```

You can also drag a `.pbrt`, `.pbrt.gz`, `.dll`, or `.so` into the viewport. Plugin libraries first open their declared
setup form in the Scene panel; after opening, controls stay in the Scene panel and runtime status appears in the
viewport HUD.

## Scene Plugins

Spectra scene plugins are platform dynamic libraries. They do not include Spectra headers, link Spectra libraries,
import Spectra modules, or use Spectra CMake targets.

The current binary entry point is:

```cpp
extern "C" SPECTRA_SCENE_EXPORT const SpectraScenePlugin* spectra_scene_plugin_v10(void);
```

Plugin boundaries:

- The plugin owns its descriptor and returned ABI views while the library or instance is alive.
- Spectra copies scene metadata, but borrows camera image pixels and external GPU buffer payloads.
- Controls are descriptor-driven: open options, runtime actions, settings, and HUD metrics are declared by the plugin.
- Runtime is live-only: the host timeline provides play/pause state, time, frame index, and delta time.
- Invalid ABI versions, missing required callbacks, unknown enum values, duplicate ids, and malformed scene data are
  hard errors.

## Project Layout

- `scene/`: scene model, PBRT import, scene plugin host, control/HUD data.
- `rasterizer/`: Vulkan preview renderer and debug visualization.
- `pathtracer/`: CUDA/OptiX path tracing backend.
- `spectra.ixx` / `spectra.cpp`: application shell and Vulkan lifecycle core.
- `main.cpp`: desktop executable entry point.

## License

Spectra is distributed under the GNU General Public License v2. See [LICENSE](LICENSE).
