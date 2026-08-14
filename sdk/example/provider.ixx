module;

#include <cuda_runtime_api.h>
#include <spectra/sdk/cuda_types.h>

export module spectra.sdk.example;

import spectra.sdk;
import spectra.sdk.cuda;
import std;

export namespace spectra::sdk::example {
    struct Settings {
        float scale{1.0F};
        bool visible{true};
    };

    export struct Provider {
        Settings settings{};

        static constexpr auto description = spectra::sdk::describe(
            "spectra.sdk.example",
            spectra::sdk::parameter<"scale", &Settings::scale>("Scale", {}, {.minimum = 0.0, .maximum = 10.0, .step = 0.1}),
            spectra::sdk::parameter<"visible", &Settings::visible>("Visible"),
            spectra::sdk::mesh<"mesh">({.attributes = spectra::sdk::MeshAttribute::Normal | spectra::sdk::MeshAttribute::TextureCoordinate}),
            spectra::sdk::spheres<"spheres">(),
            spectra::sdk::volume<"volume">(
                spectra::sdk::field<"density", float>("Density"),
                spectra::sdk::field<"velocity", spectra::sdk::Float3>("Velocity")
            ),
            spectra::sdk::instances<"instances">(),
            spectra::sdk::points<"points">(),
            spectra::sdk::lines<"lines">(),
            spectra::sdk::vectors<"vectors">(),
            spectra::sdk::image<"image">(),
            spectra::sdk::metric<"time", float>("Time", "s", {}, true)
        );

        Provider(Settings source, const std::filesystem::path&) : settings(source) {}

        void setup(spectra::sdk::cuda::Setup& setup) {
            auto mesh = setup.mesh<"mesh">(3u, 1u);
            constexpr std::array triangles{0u, 1u, 2u};
            constexpr std::array texture_coordinates{spectra::sdk::Float2{}, spectra::sdk::Float2{1.0F, 0.0F}, spectra::sdk::Float2{0.0F, 1.0F}};
            if (cudaMemcpy(mesh.triangles.data(), triangles.data(), sizeof(triangles), cudaMemcpyHostToDevice) != cudaSuccess) throw std::runtime_error("CUDA topology upload failed");
            if (cudaMemcpy(mesh.texture_coordinates.data(), texture_coordinates.data(), sizeof(texture_coordinates), cudaMemcpyHostToDevice) != cudaSuccess) throw std::runtime_error("CUDA texture-coordinate upload failed");
            setup.spheres<"spheres">(1u);
            setup.volume<"volume">({2u, 2u, 2u});
            setup.instances<"instances">(1u);
            setup.points<"points">(1u);
            setup.lines<"lines">(1u);
            setup.vectors<"vectors">(1u);
            setup.image<"image">(2u, 2u);
        }

        void reset(std::uint64_t) {}
        void step(double seconds) { time += static_cast<float>(seconds); }

        void publish(spectra::sdk::cuda::Output& output) {
            auto frame = output.begin(nullptr);
            auto mesh = frame.mesh<"mesh">();
            mesh.vertex_count = 3u;
            mesh.triangle_count = 1u;
            auto spheres = frame.spheres<"spheres">(1u);
            auto volume = frame.volume<"volume">();
            auto instances = frame.instances<"instances">(1u);
            auto points = frame.points<"points">(1u);
            auto lines = frame.lines<"lines">(1u);
            auto vectors = frame.vectors<"vectors">(1u);
            auto image = frame.image<"image">();
            static_cast<void>(mesh.positions.data());
            static_cast<void>(spheres.data());
            static_cast<void>(volume.field<"density", float>().data());
            static_cast<void>(volume.field<"velocity", spectra::sdk::Float3>().data());
            static_cast<void>(instances.data());
            static_cast<void>(points.data());
            static_cast<void>(lines.data());
            static_cast<void>(vectors.data());
            static_cast<void>(image.pixels.data());
            frame.metric<"time">().upload(time);
            frame.commit();
        }

    private:
        float time{};
    };
}
