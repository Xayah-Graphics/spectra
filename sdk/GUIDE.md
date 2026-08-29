# Spectra SDK 2.0

Spectra SDK lets a CUDA simulation publish typed GPU resources without implementing Spectra's binary ABI, Vulkan external-memory import, or semaphore protocol.

Spectra SDK CUDA Providers are supported only on Windows. The SDK is disabled in the Spectra root build by default, so the main Spectra build has no CUDA dependency. Configure and install the SDK from its own source directory before building a Provider:

```text
cmake -S sdk -B sdk/cmake-build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=sdk/install
cmake --build sdk/cmake-build-release --parallel 30
cmake --install sdk/cmake-build-release
```

## Provider

A Provider is one exported C++23 struct with public `settings`, a compile-time `description`, and four lifecycle functions:

```cpp
module;

#include <spectra/sdk/cuda_types.h>

export module project.visualization;

import spectra.sdk;
import spectra.sdk.cuda;
import std;

export namespace project {
    struct Settings {
        float gravity{-9.81F};
    };

    struct Provider {
        Settings settings{};

        static constexpr auto description = spectra::sdk::describe(
            "project.cloth",
            spectra::sdk::parameter<"gravity", &Settings::gravity>(
                "Gravity", "m/s²",
                {.minimum = -20.0, .maximum = 0.0, .step = 0.1, .application = spectra::sdk::ParameterApplication::Reset}
            ),
            spectra::sdk::mesh<"surface">({.attributes = spectra::sdk::MeshAttribute::Normal | spectra::sdk::MeshAttribute::TextureCoordinate}),
            spectra::sdk::lines<"springs">(),
            spectra::sdk::metric<"energy", float>("Energy", "J", {}, true)
        );

        Provider(Settings settings, const std::filesystem::path& assets);
        void setup(spectra::sdk::cuda::Setup& setup);
        void reset(std::uint64_t seed);
        void step(double seconds);
        void publish(spectra::sdk::cuda::Output& output, spectra::sdk::PresentationFrame presentation);
    };
}
```

`Settings` member initializers are the defaults. `Live` parameters update `settings` immediately, `Reset` parameters take effect when the simulation resets, and `Recreate` parameters rebuild the Provider because they change its resource layout.

The SDK uses ABI 7. Providers built against an older ABI are rejected; there is no compatibility entry point or fallback.

## Presentation Sequence

A Provider instance can optionally expose a display sequence whose frame axis is independent of simulation or optimization progress:

```cpp
void Provider::setup(spectra::sdk::cuda::Setup& setup) {
    setup.presentation_sequence({frame_count, start_seconds, frame_seconds});
    // Allocate the fixed output layout shared by every presentation frame.
}

void Provider::publish(spectra::sdk::cuda::Output& output, const spectra::sdk::PresentationFrame presentation) {
    auto frame = output.begin(simulation.stream());
    publish_slice(presentation.index, frame);
    frame.commit();
}
```

`PresentationSequence` describes uniformly spaced, 0-based frames. `frame_count` is at least one, `start_seconds` is finite, and `frame_seconds` is finite and strictly positive. The resulting frame timestamps must remain finite. The sequence is declared from the dataset loaded by the Provider instance during `setup`, not from the compile-time Provider description or scene file. `PresentationFrame` contains the selected `index` and its corresponding `seconds`. Spectra does not ask the Provider to step, rewind, or retain history when this selection changes; `publish` writes the selected slice of the Provider's current result into the normal frames-in-flight slot. Mesh topology, Volume resolution, Image extent, Neural Field layout, and all capacities therefore remain fixed for the Provider instance. Multiple systems may declare a sequence only when all three descriptor values are identical, because the scene has one global presentation coordinate.

## GPU outputs

Declare only the resources a project publishes. SDK 2.0 provides `mesh`, `spheres`, `volume`, `instances`, `particles`, `lines`, `vectors`, `image`, `hash_grid_radiance_field`, and `cameras`. A Volume declares its fields explicitly:

```cpp
spectra::sdk::volume<"smoke">(
    spectra::sdk::field<"density", float>("Density"),
    spectra::sdk::field<"temperature", float>("Temperature", "K"),
    spectra::sdk::volume_field<"velocity", spectra::sdk::MacFloat3>(
        "Velocity", "m/s",
        {.sampling = spectra::sdk::VolumeFieldSampling::Cell, .vector_space = spectra::sdk::VolumeVectorSpace::World}
    )
)
```

`float`, `Float3`, and `std::uint32_t` fields contain `nx * ny * nz` values. Cell sampling places them at cell centers; vertex sampling spans the Volume bounds without changing the published element count. `MacFloat3` exposes staggered `x`, `y`, and `z` spans with resolutions `(nx + 1, ny, nz)`, `(nx, ny + 1, nz)`, and `(nx, ny, nz + 1)`. Spectra owns field selection, derived maps, slicing, ray marching, isosurfaces, glyphs, streamlines, LIC, and categorical-cell inspection; a Provider publishes only simulation data and field metadata.

A particle simulation publishes positions plus explicitly named SoA fields. The uniform physical radius belongs to the ParticleSet setup rather than each particle:

```cpp
spectra::sdk::particles<"fluid">(
    spectra::sdk::field<"velocity", spectra::sdk::Float3>("Velocity", "m/s"),
    spectra::sdk::field<"density", float>("Density", "kg/m³"),
    spectra::sdk::field<"phase", std::uint32_t>("Phase")
)

setup.particles<"fluid">(particle_capacity, particle_radius);

auto fluid = frame.particles<"fluid">(particle_count);
launch_publish(stream,
    fluid.positions.data(),
    fluid.field<"velocity", spectra::sdk::Float3>().data(),
    fluid.field<"density", float>().data(),
    fluid.field<"phase", std::uint32_t>().data());
```

`particles` is a first-class scene resource. Spectra owns its point/disc/sphere display, field maps, selection, bounds, and vector diagnostics. Use `spheres` only when every element is actual sphere geometry with its own radius.

`cameras` publishes a fixed set of dataset Cameras and their reference images during `setup`:

```cpp
spectra::sdk::cameras<"training">()

std::array cameras{spectra::sdk::Camera{
    .right = {1.0F, 0.0F, 0.0F},
    .down = {0.0F, -1.0F, 0.0F},
    .forward = {0.0F, 0.0F, -1.0F},
    .position = {0.0F, 0.0F, 2.0F},
    .focal = {800.0F, 800.0F},
    .principal = {400.0F, 300.0F},
}};
auto training = setup.cameras<"training">(cameras, 800u, 600u);
cudaMemcpy(training.images.data(), rgba_pixels, training.images.size_bytes(), cudaMemcpyHostToDevice);
```

`right`, `down`, and `forward` are unit world-space Camera axes, and `position` is the world-space optical center. `focal = {fx, fy}` and `principal = {cx, cy}` use pixel units with a top-left image origin. Every Camera in one output shares the supplied resolution. Images are laid out as `[camera][y][x]` in `Rgba8` nonlinear sRGB with straight alpha. Spectra turns each entry into an ordinary runtime Camera with a static GT reference image. Cameras have no frame accessor and are never republished from `publish`.

`hash_grid_radiance_field` is the fixed canonical v1 radiance-field layout: a unit AABB, eight F16 Hash Grid levels with four features per level, a `32 → 64 → 16` density network, and a `32 → 64 → 64 → 16` RGB network. It exposes seven typed spans:

```cpp
auto field = frame.hash_grid_radiance_field<"field">();
cudaMemcpyAsync(field.hash_grid.data(), source_hash_grid, field.hash_grid.size_bytes(), cudaMemcpyDeviceToDevice, stream);
cudaMemcpyAsync(field.density_input.data(), source_density_input, field.density_input.size_bytes(), cudaMemcpyDeviceToDevice, stream);
cudaMemcpyAsync(field.density_output.data(), source_density_output, field.density_output.size_bytes(), cudaMemcpyDeviceToDevice, stream);
cudaMemcpyAsync(field.rgb_input.data(), source_rgb_input, field.rgb_input.size_bytes(), cudaMemcpyDeviceToDevice, stream);
cudaMemcpyAsync(field.rgb_hidden.data(), source_rgb_hidden, field.rgb_hidden.size_bytes(), cudaMemcpyDeviceToDevice, stream);
cudaMemcpyAsync(field.rgb_output.data(), source_rgb_output, field.rgb_output.size_bytes(), cudaMemcpyDeviceToDevice, stream);
cudaMemcpyAsync(field.occupancy.data(), source_occupancy, field.occupancy.size_bytes(), cudaMemcpyDeviceToDevice, stream);
```

The seven spans may share internal allocations, but their layouts and lifetimes are independent of Provider code. `occupancy` is the canonical 128³ Morton-order bitfield stored as 32-bit words. Spectra performs Vulkan-side matrix conversion, inference, and scene-linear composition.

Allocate fixed capacities once in `setup`:

```cpp
void Provider::setup(spectra::sdk::cuda::Setup& setup) {
    auto surface = setup.mesh<"surface">(vertex_count, triangle_count);
    upload_topology(surface.triangles.data());
    upload_texture_coordinates(surface.texture_coordinates.data());
    setup.lines<"springs">(spring_capacity);
}
```

The SDK completes the setup stream before Spectra starts rendering. Capacities remain fixed for the Provider instance.

Publish one atomic frame on the simulation's CUDA stream:

```cpp
void Provider::publish(spectra::sdk::cuda::Output& output, spectra::sdk::PresentationFrame) {
    auto frame = output.begin(simulation.stream());
    auto surface = frame.mesh<"surface">();
    auto springs = frame.lines<"springs">(active_springs);

    launch_surface(simulation.stream(), surface.positions.data(), surface.normals.data());
    launch_springs(simulation.stream(), springs.data());
    frame.metric<"energy">().upload(simulation.energy());
    frame.commit();
}
```

`begin` queues the Vulkan-to-CUDA wait. `commit` queues the CUDA-to-Vulkan signal. Do not synchronize the stream around them.

CUDA translation units include `<spectra/sdk/cuda_types.h>` and write the exact SDK element types directly. The header contains only standard-layout POD declarations and does not expose frame management or depend on the CUDA Runtime.

## CMake

```cmake
find_package(SpectraSDK 2.0.6 CONFIG REQUIRED)

spectra_add_provider(
        cloth-provider
        MODULE project.visualization
        TYPE project::Provider
)

target_sources(
        cloth-provider
        PRIVATE
        FILE_SET provider_module TYPE CXX_MODULES
        FILES project.cpp
)

target_link_libraries(cloth-provider PRIVATE project-simulation)
```

The target produces `cloth-provider.spectra-provider.dll` on Windows. `spectra_add_provider` generates the ABI entry and compiles the internal bridge; project code must not include SDK internal files.

Place the Provider DLL beside the `.spectra` scene. Spectra scans that directory for `*.spectra-provider.dll` and matches the scene's Provider ID against the DLL's `description` ID, so the CMake target name does not need to duplicate the Provider ID.

The complete ten-output compile example is in `sdk/example` and can be enabled while configuring the SDK with `-DSPECTRA_SDK_BUILD_EXAMPLE=ON`.
