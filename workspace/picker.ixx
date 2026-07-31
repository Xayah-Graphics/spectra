export module spectra.workspace.picker;

import spectra;
import spectra.rasterizer;
import std;
import vulkan;

namespace spectra::workspace {
    export struct PickRequest {
        float x{};
        float y{};
        bool select{};
        bool additive{};
    };

    export struct PickResult {
        bool available{};
        std::optional<std::uint32_t> instance_index{};
        bool select{};
        bool additive{};
    };

    export struct Picker {
        Picker(GpuDevice& gpu, const rasterizer::RasterScene& scene, const std::filesystem::path& shader_directory, std::uint32_t frames_in_flight);
        ~Picker();

        Picker(const Picker&) = delete;
        Picker(Picker&&) = delete;
        Picker& operator=(const Picker&) = delete;
        Picker& operator=(Picker&&) = delete;

        void request(PickRequest request) noexcept;
        [[nodiscard]] PickResult consume(std::uint32_t frame_index) noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index);

    private:
        struct Slot {
            GpuBuffer result{};
            DescriptorHandle descriptor{};
            std::optional<PickRequest> submitted{};
        };

        GpuDevice* gpu{};
        const rasterizer::RasterScene* scene{};
        vk::raii::ShaderEXT shader{nullptr};
        std::vector<Slot> slots{};
        std::optional<PickRequest> pending{};
    };
}
