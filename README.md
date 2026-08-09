![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/14c6599b610e65a7ef42174e6910dac53004cec9/spectra-banner2.png)

# Spectra v2.0.0

[![Windows](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml)
[![Arch Linux](https://github.com/Xayah-Graphics/spectra/actions/workflows/arch.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/arch.yml)
[![Docker](https://github.com/Xayah-Graphics/spectra/actions/workflows/docker.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/docker.yml)
[![License](https://img.shields.io/github/license/Xayah-Graphics/spectra)](LICENSE)

Spectra is a C++23 Vulkan 1.4 graphics research workspace for native `.spectra` scenes and live simulation providers.
One shared scene is consumed by an interactive rasterizer and a spectral wavefront path tracer. The Windows build
includes the editor, while the same executable also supports headless rendering on Windows and Linux.

| Path Tracing                                                                                                                                | Dynamic Scene Providers                                                                                                                               | Interactive Rasterization                                                                                                                                 |
|---------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|
| ![Cornell Box](https://github.com/Xayah-Graphics/imagebed/blob/883141b90fd655fa7e4b227d8a54842ee137392c/spectra-pathtracer-cornell-box.png) | ![Dynamic Scene](https://github.com/Xayah-Graphics/imagebed/blob/0b600f42860a713b7bad36e350fbb55f29a4d97c/spectra-pathtracer-cloth-simulation.png)      | ![Interactive Rasterization](https://github.com/Xayah-Graphics/imagebed/blob/eae3d3fd1d4073f8876f69cc38927ee88448df21/spectra-rasterizer-instant-ngp.png) |

## Build Instruction

### Requirements

- CMake 4.4
- A C++23 compiler with standard-library module support
- Vulkan SDK 1.4 + Slang

### Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 30
```

The main executable target is `spectra`. `SPECTRA_BUILD_UI` defaults to `ON` on Windows and `OFF` elsewhere.

Build the Linux headless image locally with Docker:

```bash
docker build -t spectra .
```

Tagged releases publish `ghcr.io/xayah-graphics/spectra:latest`. Rendering inside the container still requires access to
a compatible host Vulkan device.

## Usage

Open an empty editor workspace:

```bash
spectra --gui
```

Open a native scene in the editor:

```bash
spectra /path/to/scene.spectra --gui
```

Render a scene headlessly with either renderer:

```bash
spectra /path/to/scene.spectra --renderer rasterizer
spectra /path/to/scene.spectra --renderer pathtracer
```

Headless rendering writes display PNG and linear EXR outputs under `output/renders` by default. Use `--output` to select
another output basename and `--gbuffer-output` to request a Path Tracer GBuffer EXR. A dynamic `.spectra` scene loads
its declared Provider libraries from the scene directory. The editor also accepts one dropped `.spectra` scene.
