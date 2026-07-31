module;

#include <plugin/api.h>
#include <Windows.h>

module spectra.plugin;

import std;

namespace spectra::plugin {
    struct PluginHost::State {
        explicit State(const std::filesystem::path& path, scene::Scene& scene)
            : writer(scene),
              scene_writer{
                  &this->writer,
                  &State::create_diffuse_material,
                  &State::create_diffuse_area_light,
                  &State::create_triangle_mesh,
                  &State::create_prototype,
                  &State::create_particle_set,
                  &State::create_instance,
                  &State::define_perspective_camera,
                  &State::define_rgb_film,
                  &State::define_sampler,
                  &State::update_triangle_mesh,
                  &State::update_particle_set,
                  &State::update_transform,
                  &State::update_diffuse_material,
              } {
            this->library = LoadLibraryW(path.c_str());
            if (this->library == nullptr) throw std::runtime_error(std::format("{}: failed to load Plugin API 5 library, Win32 error {}", path.string(), GetLastError()));
            try {
                const FARPROC symbol = GetProcAddress(this->library, SPECTRA_PLUGIN_ENTRY_NAME);
                if (symbol == nullptr) throw std::runtime_error(std::format("{}: missing Plugin API 5 entry \"{}\"", path.string(), SPECTRA_PLUGIN_ENTRY_NAME));
                const SpectraPluginApi* (*entry)() = reinterpret_cast<const SpectraPluginApi* (*)()>(symbol);
                this->api = entry();
                if (this->api == nullptr) throw std::runtime_error(std::format("{}: Plugin API 5 entry returned null", path.string()));
                if (this->api->version != SPECTRA_PLUGIN_API_VERSION || this->api->size != sizeof(SpectraPluginApi))
                    throw std::runtime_error(std::format("{}: requires Plugin API 5 exactly", path.string()));
                if (this->api->name == nullptr ||
                    this->api->load == nullptr ||
                    this->api->unload == nullptr ||
                    this->api->start == nullptr ||
                    this->api->stop == nullptr ||
                    this->api->advance == nullptr ||
                    this->api->controls == nullptr ||
                    this->api->timeline == nullptr)
                    throw std::runtime_error(std::format("{}: Plugin API 5 contract is incomplete", path.string()));
                this->plugin_name = this->api->name;
                this->instance = this->api->load(&this->scene_writer);
                if (this->instance == nullptr) throw std::runtime_error(std::format("{}: Plugin API 5 load returned null", path.string()));
            } catch (...) {
                FreeLibrary(this->library);
                this->library = nullptr;
                throw;
            }
        }

        ~State() {
            if (this->instance != nullptr) this->api->unload(this->instance);
            if (this->library != nullptr) FreeLibrary(this->library);
        }

        static std::uint64_t create_diffuse_material(
            void* state,
            const SpectraPluginFloat3 reflectance) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            return writer.create_diffuse_material(
                {reflectance.x, reflectance.y, reflectance.z}).value;
        }

        static std::uint64_t create_diffuse_area_light(
            void* state,
            const SpectraPluginFloat3 radiance,
            const SpectraPluginEmissionSidedness sidedness) {
            scene::SceneWriter& writer =
                *static_cast<scene::SceneWriter*>(state);
            return writer.create_diffuse_area_light(
                {radiance.x, radiance.y, radiance.z},
                sidedness == SpectraPluginEmissionSidedness::Both
                    ? scene::EmissionSidedness::Both
                    : scene::EmissionSidedness::Front).value;
        }

        static std::uint64_t create_triangle_mesh(
            void* state,
            const SpectraPluginFloat3* positions,
            const std::uint64_t position_count,
            const SpectraPluginFloat3* normals,
            const std::uint64_t normal_count,
            const SpectraPluginFloat3* tangents,
            const std::uint64_t tangent_count,
            const SpectraPluginFloat2*
                texture_coordinates,
            const std::uint64_t
                texture_coordinate_count,
            const std::uint32_t* indices,
            const std::uint64_t index_count,
            const SpectraPluginMeshUpdateMode update_mode) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            std::vector<scene::Float3> scene_positions(position_count);
            for (std::uint64_t index = 0; index < position_count; ++index)
                scene_positions[index] = {positions[index].x, positions[index].y, positions[index].z};
            std::vector<scene::Float3> scene_normals(normal_count);
            for (std::uint64_t index = 0; index < normal_count; ++index)
                scene_normals[index] = {normals[index].x, normals[index].y, normals[index].z};
            std::vector<scene::Float3>
                scene_tangents(tangent_count);
            for (
                std::uint64_t index = 0;
                index < tangent_count;
                ++index)
                scene_tangents[index] = {
                    tangents[index].x,
                    tangents[index].y,
                    tangents[index].z};
            std::vector<scene::Float2>
                scene_texture_coordinates(
                    texture_coordinate_count);
            for (
                std::uint64_t index = 0;
                index <
                texture_coordinate_count;
                ++index)
                scene_texture_coordinates[index] = {
                    texture_coordinates[index].x,
                    texture_coordinates[index].y};
            return writer.create_triangle_mesh(
                scene_positions,
                scene_normals,
                scene_tangents,
                scene_texture_coordinates,
                std::span<const std::uint32_t>{indices, index_count},
                update_mode == SpectraPluginMeshUpdateMode::Deformable
                    ? scene::GeometryUpdateMode::Deformable
                    : update_mode == SpectraPluginMeshUpdateMode::TopologyChanging
                        ? scene::GeometryUpdateMode::TopologyChanging
                        : scene::GeometryUpdateMode::Static).value;
        }

        static std::uint64_t create_prototype(
            void* state,
            const SpectraPluginPrimitiveKind kind,
            const std::uint64_t resource,
            const std::uint64_t material,
            const std::uint64_t area_light,
            const SpectraPluginTransform transform) {
            scene::SceneWriter& writer =
                *static_cast<scene::SceneWriter*>(state);
            scene::Transform scene_transform{};
            std::ranges::copy(
                transform.matrix,
                scene_transform.matrix.begin());
            return writer.create_prototype(
                scene::Primitive{
                    .geometry =
                        kind ==
                                SpectraPluginPrimitiveKind::
                                    Geometry
                            ? scene::GeometryId{
                                  resource}
                            : scene::GeometryId{},
                    .particles =
                        kind ==
                                SpectraPluginPrimitiveKind::
                                    ParticleSet
                            ? scene::ParticleSetId{
                                  resource}
                            : scene::ParticleSetId{},
                    .material = {material},
                    .area_light = {area_light},
                    .transform = scene_transform,
                }).value;
        }

        static std::uint64_t create_particle_set(
            void* state,
            const SpectraPluginFloat3* positions,
            const std::uint64_t position_count,
            const float* radii,
            const SpectraPluginFloat3* velocities,
            const std::uint64_t velocity_count,
            const SpectraPluginFloat3* colors,
            const std::uint64_t color_count,
            const float* temperatures,
            const std::uint64_t temperature_count,
            const std::uint64_t material,
            const std::uint64_t* particle_materials,
            const std::uint64_t
                particle_material_count,
            const SpectraPluginMeshUpdateMode
                update_mode) {
            scene::SceneWriter& writer =
                *static_cast<
                    scene::SceneWriter*>(state);
            std::vector<scene::Float3>
                scene_positions(position_count);
            for (std::uint64_t index = 0;
                 index != position_count;
                 ++index)
                scene_positions[index] = {
                    positions[index].x,
                    positions[index].y,
                    positions[index].z};
            std::vector<scene::Float3>
                scene_velocities(velocity_count);
            for (std::uint64_t index = 0;
                 index != velocity_count;
                 ++index)
                scene_velocities[index] = {
                    velocities[index].x,
                    velocities[index].y,
                    velocities[index].z};
            std::vector<scene::Float3>
                scene_colors(color_count);
            for (std::uint64_t index = 0;
                 index != color_count;
                 ++index)
                scene_colors[index] = {
                    colors[index].x,
                    colors[index].y,
                    colors[index].z};
            std::vector<scene::MaterialId>
                scene_materials(
                    particle_material_count);
            for (std::uint64_t index = 0;
                 index !=
                     particle_material_count;
                 ++index)
                scene_materials[index] =
                    scene::MaterialId{
                        particle_materials[
                            index]};
            return writer.create_particle_set(
                scene_positions,
                std::span<const float>{
                    radii,
                    position_count},
                scene_velocities,
                scene_colors,
                std::span<const float>{
                    temperatures,
                    temperature_count},
                scene::MaterialId{material},
                scene_materials,
                update_mode ==
                        SpectraPluginMeshUpdateMode::
                            Deformable
                    ? scene::GeometryUpdateMode::
                          Deformable
                    : update_mode ==
                              SpectraPluginMeshUpdateMode::
                                  TopologyChanging
                          ? scene::GeometryUpdateMode::
                                TopologyChanging
                          : scene::GeometryUpdateMode::
                                Static)
                .value;
        }

        static std::uint64_t create_instance(
            void* state,
            const std::uint64_t prototype,
            const SpectraPluginTransform transform) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            scene::Transform scene_transform{};
            std::ranges::copy(transform.matrix, scene_transform.matrix.begin());
            return writer.create_instance(
                scene::PrototypeId{prototype},
                scene_transform).value;
        }

        static std::uint64_t define_perspective_camera(
            void* state,
            const SpectraPluginTransform transform,
            const float vertical_fov,
            const float near_plane,
            const float far_plane) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            scene::Transform scene_transform{};
            std::ranges::copy(transform.matrix, scene_transform.matrix.begin());
            return writer.define_perspective_camera(
                scene_transform,
                vertical_fov,
                near_plane,
                far_plane).value;
        }

        static std::uint64_t define_rgb_film(
            void* state,
            const std::uint32_t width,
            const std::uint32_t height,
            const SpectraPluginFilter filter) {
            scene::SceneWriter& writer =
                *static_cast<scene::SceneWriter*>(
                    state);
            return writer.define_rgb_film(
                {width, height},
                scene::Filter{
                    .kind =
                        filter.kind ==
                                SpectraPluginFilterKind::
                                    Gaussian
                            ? scene::FilterKind::Gaussian
                            : filter.kind ==
                                      SpectraPluginFilterKind::
                                          Mitchell
                                  ? scene::FilterKind::Mitchell
                                  : filter.kind ==
                                            SpectraPluginFilterKind::
                                                Sinc
                                        ? scene::FilterKind::Sinc
                                        : filter.kind ==
                                                  SpectraPluginFilterKind::
                                                      Triangle
                                              ? scene::FilterKind::
                                                    Triangle
                                              : scene::FilterKind::Box,
                    .radius = {
                        filter.radius.x,
                        filter.radius.y},
                    .sigma = filter.sigma,
                    .b = filter.b,
                    .c = filter.c,
                    .tau = filter.tau,
                }).value;
        }

        static std::uint64_t define_sampler(
            void* state,
            const SpectraPluginSamplerKind kind,
            const std::uint32_t samples_per_pixel,
            const std::uint32_t seed) {
            scene::SceneWriter& writer =
                *static_cast<scene::SceneWriter*>(
                    state);
            return writer.define_sampler(
                kind ==
                        SpectraPluginSamplerKind::
                            Stratified
                    ? scene::SamplerKind::Stratified
                    : kind ==
                              SpectraPluginSamplerKind::
                                  Halton
                          ? scene::SamplerKind::Halton
                          : kind ==
                                    SpectraPluginSamplerKind::
                                        Sobol
                                ? scene::SamplerKind::Sobol
                                : kind ==
                                          SpectraPluginSamplerKind::
                                              PaddedSobol
                                      ? scene::SamplerKind::
                                            PaddedSobol
                                      : kind ==
                                                SpectraPluginSamplerKind::
                                                    ZSobol
                                            ? scene::SamplerKind::
                                                  ZSobol
                                            : kind ==
                                                      SpectraPluginSamplerKind::
                                                          Pmj02bn
                                                  ? scene::SamplerKind::
                                                        Pmj02bn
                                                  : scene::SamplerKind::
                                                        Independent,
                samples_per_pixel,
                seed).value;
        }

        static void update_triangle_mesh(
            void* state,
            const std::uint64_t mesh,
            const SpectraPluginFloat3* positions,
            const std::uint64_t position_count,
            const SpectraPluginFloat3* normals,
            const std::uint64_t normal_count,
            const SpectraPluginFloat3* tangents,
            const std::uint64_t tangent_count,
            const SpectraPluginFloat2*
                texture_coordinates,
            const std::uint64_t
                texture_coordinate_count,
            const std::uint32_t* indices,
            const std::uint64_t index_count) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            std::vector<scene::Float3> scene_positions(position_count);
            for (std::uint64_t index = 0; index < position_count; ++index)
                scene_positions[index] = {positions[index].x, positions[index].y, positions[index].z};
            std::vector<scene::Float3>
                scene_normals(normal_count);
            for (
                std::uint64_t index = 0;
                index < normal_count;
                ++index)
                scene_normals[index] = {
                    normals[index].x,
                    normals[index].y,
                    normals[index].z};
            std::vector<scene::Float3>
                scene_tangents(tangent_count);
            for (
                std::uint64_t index = 0;
                index < tangent_count;
                ++index)
                scene_tangents[index] = {
                    tangents[index].x,
                    tangents[index].y,
                    tangents[index].z};
            std::vector<scene::Float2>
                scene_texture_coordinates(
                    texture_coordinate_count);
            for (
                std::uint64_t index = 0;
                index <
                texture_coordinate_count;
                ++index)
                scene_texture_coordinates[index] = {
                    texture_coordinates[index].x,
                    texture_coordinates[index].y};
            writer.update_triangle_mesh(
                scene::GeometryId{mesh},
                scene_positions,
                scene_normals,
                scene_tangents,
                scene_texture_coordinates,
                std::span<const std::uint32_t>{
                    indices,
                    index_count});
        }

        static void update_particle_set(
            void* state,
            const std::uint64_t particles,
            const SpectraPluginFloat3* positions,
            const std::uint64_t position_count,
            const float* radii,
            const SpectraPluginFloat3* velocities,
            const std::uint64_t velocity_count,
            const SpectraPluginFloat3* colors,
            const std::uint64_t color_count,
            const float* temperatures,
            const std::uint64_t temperature_count,
            const std::uint64_t* particle_materials,
            const std::uint64_t
                particle_material_count) {
            scene::SceneWriter& writer =
                *static_cast<
                    scene::SceneWriter*>(state);
            std::vector<scene::Float3>
                scene_positions(position_count);
            for (std::uint64_t index = 0;
                 index != position_count;
                 ++index)
                scene_positions[index] = {
                    positions[index].x,
                    positions[index].y,
                    positions[index].z};
            std::vector<scene::Float3>
                scene_velocities(velocity_count);
            for (std::uint64_t index = 0;
                 index != velocity_count;
                 ++index)
                scene_velocities[index] = {
                    velocities[index].x,
                    velocities[index].y,
                    velocities[index].z};
            std::vector<scene::Float3>
                scene_colors(color_count);
            for (std::uint64_t index = 0;
                 index != color_count;
                 ++index)
                scene_colors[index] = {
                    colors[index].x,
                    colors[index].y,
                    colors[index].z};
            std::vector<scene::MaterialId>
                scene_materials(
                    particle_material_count);
            for (std::uint64_t index = 0;
                 index !=
                     particle_material_count;
                 ++index)
                scene_materials[index] =
                    scene::MaterialId{
                        particle_materials[
                            index]};
            writer.update_particle_set(
                scene::ParticleSetId{particles},
                scene_positions,
                std::span<const float>{
                    radii,
                    position_count},
                scene_velocities,
                scene_colors,
                std::span<const float>{
                    temperatures,
                    temperature_count},
                scene_materials);
        }

        static void update_transform(
            void* state,
            const std::uint64_t instance,
            const SpectraPluginTransform transform) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            scene::Transform scene_transform{};
            std::ranges::copy(transform.matrix, scene_transform.matrix.begin());
            writer.update_transform(scene::InstanceId{instance}, scene_transform);
        }

        static void update_diffuse_material(
            void* state,
            const std::uint64_t material,
            const SpectraPluginFloat3 reflectance) {
            scene::SceneWriter& writer = *static_cast<scene::SceneWriter*>(state);
            writer.update_diffuse_material(
                scene::MaterialId{material},
                {reflectance.x, reflectance.y, reflectance.z});
        }

        scene::SceneWriter writer;
        SpectraPluginSceneWriter scene_writer{};
        HMODULE library{};
        const SpectraPluginApi* api{};
        void* instance{};
        std::string plugin_name{};
    };

    PluginHost::PluginHost(const std::filesystem::path& path, scene::Scene& scene)
        : state(std::make_unique<State>(path, scene)), name(this->state->plugin_name) {}

    PluginHost::~PluginHost() = default;

    void PluginHost::start() {
        this->state->api->start(this->state->instance);
    }

    void PluginHost::stop() {
        this->state->api->stop(this->state->instance);
    }

    void PluginHost::advance(const double seconds) {
        this->state->api->advance(this->state->instance, seconds);
    }

    Controls PluginHost::controls() const {
        const SpectraPluginControls controls = this->state->api->controls(this->state->instance);
        return {
            controls.running != 0,
            controls.can_start != 0,
            controls.can_stop != 0,
            controls.can_advance != 0,
        };
    }

    Timeline PluginHost::timeline() const {
        const SpectraPluginTimeline timeline = this->state->api->timeline(this->state->instance);
        return {timeline.seconds, timeline.frame};
    }

} // namespace spectra::plugin
