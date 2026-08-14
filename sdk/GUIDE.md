# Spectra SDK 1.0

Spectra SDK lets a CUDA simulation publish typed GPU resources without implementing Spectra's binary ABI, Vulkan external-memory import, or semaphore protocol.

Spectra and Spectra SDK are separate CMake projects. The Spectra root build never configures CUDA or builds the SDK. Configure and install the SDK from its own source directory before building a Provider:

```text
cmake -S sdk -B sdk/cmake-build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=sdk/install
cmake --build sdk/cmake-build-release --parallel 30
cmake --install sdk/cmake-build-release
```

## Provider

A Provider is one exported C++23 struct with public `settings`, a compile-time `description`, and four lifecycle functions:

```cpp
export module project.visualization;

import spectra.sdk;
import spectra.sdk.cuda;
import std;

export namespace project {
    struct Settings {
        float gravity{-9.81F};
    };

    export struct Provider {
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
        void publish(spectra::sdk::cuda::Output& output);
    };
}
```

`Settings` member initializers are the defaults. `Live` parameters update `settings` immediately, `Reset` parameters take effect when the simulation resets, and `Recreate` parameters rebuild the Provider because they change its resource layout.

## GPU outputs

Declare only the resources a project publishes. SDK 1.0 provides `mesh`, `spheres`, `volume`, `instances`, `points`, `lines`, `vectors`, and `image`. A Volume declares its channels explicitly:

```cpp
spectra::sdk::volume<"smoke">(
    spectra::sdk::channel<"density", float>(),
    spectra::sdk::channel<"velocity", spectra::sdk::Float3>()
)
```

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
void Provider::publish(spectra::sdk::cuda::Output& output) {
    auto frame = output.begin(simulation.stream());
    auto surface = frame.mesh<"surface">();
    auto springs = frame.lines<"springs">(active_springs);

    launch_surface(simulation.stream(), surface.positions.data(), surface.normals.data());
    launch_springs(simulation.stream(), springs.data());
    surface.vertex_count = vertex_count;
    surface.triangle_count = triangle_count;
    frame.metric<"energy">().upload(simulation.energy());
    frame.commit();
}
```

`begin` queues the Vulkan-to-CUDA wait. `commit` queues the CUDA-to-Vulkan signal. Do not synchronize the stream around them.

## CMake

```cmake
find_package(SpectraSDK 1 CONFIG REQUIRED)

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

The target produces `cloth-provider.spectra-provider.dll` on Windows or `cloth-provider.spectra-provider.so` on Linux. `spectra_add_provider` generates the ABI entry and compiles the internal bridge; project code must not include SDK internal files.

Place the Provider DLL beside the `.spectra` scene. Spectra scans that directory for `*.spectra-provider.dll` or `*.spectra-provider.so` and matches the scene's Provider ID against the DLL's `description` ID, so the CMake target name does not need to duplicate the Provider ID.

The complete eight-output compile example is in `sdk/example` and can be enabled while configuring the SDK with `-DSPECTRA_SDK_BUILD_EXAMPLE=ON`.
