export module spectra.rasterizer.volume;

export import spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export enum class SceneVolumeKind {
        LiquidLevelSet,
        GasDensity,
        GasTemperature,
        GasVelocity,
        SignedDistanceField,
        TruncatedSignedDistanceField,
    };

    export enum class SceneVolumeChannelLayout {
        CellCentered,
        FaceX,
        FaceY,
        FaceZ,
    };

    export struct SceneVolumeChannel {
        std::string name{};
        SceneVolumeChannelLayout layout{SceneVolumeChannelLayout::CellCentered};
        std::array<std::uint32_t, 3> dimensions{};
        std::vector<float> values{};
    };

    export struct SceneVolumeGrid {
        std::string name{};
        SceneVolumeKind kind{SceneVolumeKind::GasDensity};
        std::array<std::uint32_t, 3> dimensions{};
        SceneVector3 origin{};
        SceneVector3 voxelSize{1.0f, 1.0f, 1.0f};
        std::vector<std::string> channelNames{};
        std::vector<SceneVolumeChannel> channels{};
        std::string materialName{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };
} // namespace spectra::rasterizer
