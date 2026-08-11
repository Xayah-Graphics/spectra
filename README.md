![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/7c523b3de96e287ba2e14c54bd7bf1a0c25c9cec/spectra2-banner2.png)

# Spectra

[![Windows](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml)
[![Arch Linux](https://github.com/Xayah-Graphics/spectra/actions/workflows/arch.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/arch.yml)
[![Docker](https://github.com/Xayah-Graphics/spectra/actions/workflows/docker.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/docker.yml)
[![License](https://img.shields.io/github/license/Xayah-Graphics/spectra)](LICENSE)

Spectra is a C++23 Vulkan 1.4 graphics research workspace for native `.spectra` scenes and live simulation providers.
One shared scene is consumed by an interactive rasterizer and a spectral wavefront path tracer. The Windows build
includes the editor, while the same executable also supports headless rendering on Windows and Linux.

## Gallery

|                                                              **Cornell Box**                                                               |                                                               **Geometry Mapping Garden**                                                                |                                                            **Lighting Pavilion**                                                             |
|:------------------------------------------------------------------------------------------------------------------------------------------:|:--------------------------------------------------------------------------------------------------------------------------------------------------------:|:--------------------------------------------------------------------------------------------------------------------------------------------:|
|      ![Cornell Box](https://github.com/Xayah-Graphics/imagebed/blob/58258914e0a8b7772eeefd52a6bf08c5e44c2cdf/cornell-box-4096spp.png)      | ![Geometry Mapping Garden](https://github.com/Xayah-Graphics/imagebed/blob/58258914e0a8b7772eeefd52a6bf08c5e44c2cdf/geometry-mapping-garden-4096spp.png) | ![Lighting Pavilion](https://github.com/Xayah-Graphics/imagebed/blob/58258914e0a8b7772eeefd52a6bf08c5e44c2cdf/lighting-pavilion-4096spp.png) |
|                                                            **Material Atelier**                                                            |                                                                      **PBRT Book**                                                                       |                                                             **Volume Chambers**                                                              |
| ![Material Atelier](https://github.com/Xayah-Graphics/imagebed/blob/58258914e0a8b7772eeefd52a6bf08c5e44c2cdf/material-atelier-4096spp.png) |               ![PBRT Book](https://github.com/Xayah-Graphics/imagebed/blob/58258914e0a8b7772eeefd52a6bf08c5e44c2cdf/pbrt-book-4096spp.png)               |   ![Volume Chambers](https://github.com/Xayah-Graphics/imagebed/blob/58258914e0a8b7772eeefd52a6bf08c5e44c2cdf/volume-chambers-4096spp.png)   |

## Build Instruction

### Requirements

- CMake 4.4
- C++23 compiler with standard-library module support
- Vulkan SDK 1.4 + Slang

### Build

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
```

The main executable target is `spectra`. `SPECTRA_BUILD_UI` defaults to `ON` on Windows and `OFF` elsewhere.

Build the Linux headless image locally with Docker:

```bash
docker build -t spectra .
```

Tagged releases publish `ghcr.io/xayah-graphics/spectra:latest`. Rendering inside the container still requires access to
a compatible host Vulkan device.

## Usage

Render a scene headlessly with either renderer:

```bash
spectra /path/to/scene.spectra --renderer rasterizer
spectra /path/to/scene.spectra --renderer pathtracer
```

Open a scene in the editor:

```bash
spectra /path/to/scene.spectra --gui
```
