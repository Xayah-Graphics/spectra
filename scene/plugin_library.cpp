module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

module spectra.scene.plugin_library;

import std;
import spectra.scene;
import spectra.scene.plugin_abi;
import spectra.scene.plugin_codec;
import spectra.scene.plugin_library;

namespace spectra::scene {
    namespace {
        class NativeLibrary final {
        public:
            explicit NativeLibrary(std::filesystem::path path) : path(std::move(path)) {
#if defined(_WIN32)
                this->handle = static_cast<void*>(::LoadLibraryW(this->path.wstring().c_str()));
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load Scene plugin, Win32 error {}", this->path.string(), ::GetLastError()));
#else
                this->handle = ::dlopen(this->path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load Scene plugin: {}", this->path.string(), ::dlerror()));
#endif
            }

            NativeLibrary(const NativeLibrary& other) = delete;
            NativeLibrary(NativeLibrary&& other) = delete;
            NativeLibrary& operator=(const NativeLibrary& other) = delete;
            NativeLibrary& operator=(NativeLibrary&& other) = delete;

            ~NativeLibrary() noexcept {
#if defined(_WIN32)
                if (this->handle != nullptr) static_cast<void>(::FreeLibrary(static_cast<HMODULE>(this->handle)));
#else
                if (this->handle != nullptr) static_cast<void>(::dlclose(this->handle));
#endif
            }

            [[nodiscard]] void* symbol(const char* name) const {
#if defined(_WIN32)
                void* symbol_address = reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(this->handle), name));
                if (symbol_address == nullptr) throw std::runtime_error(std::format("{}: Scene plugin is missing export \"{}\", Win32 error {}", this->path.string(), name, ::GetLastError()));
                return symbol_address;
#else
                ::dlerror();
                void* symbol_address = ::dlsym(this->handle, name);
                const char* error = ::dlerror();
                if (error != nullptr) throw std::runtime_error(std::format("{}: Scene plugin is missing export \"{}\": {}", this->path.string(), name, error));
                return symbol_address;
#endif
            }

        private:
            std::filesystem::path path{};
            void* handle{};
        };

        struct PluginOptionStorage {
            std::string key{};
            std::string value{};
        };

        struct PluginOpenRequestStorage {
            std::filesystem::path plugin_path{};
            std::vector<PluginOptionStorage> options{};
            std::vector<SpectraSceneOption> option_views{};
            std::shared_ptr<HostServices> host{};
            SpectraSceneHostServices host_view{};
            std::string scene_id{};
        };

        thread_local std::string scene_host_service_callback_error{};

        [[nodiscard]] SpectraSceneResult request_gpu_buffer(void* user_data, const SpectraSceneGpuBufferRequest* request, SpectraSceneGpuBufferAllocation* allocation) noexcept {
            try {
                scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Scene host services user data pointer is null");
                if (request == nullptr) throw std::runtime_error("Scene GPU buffer request pointer is null");
                if (allocation == nullptr) throw std::runtime_error("Scene GPU buffer allocation pointer is null");
                const PluginAbiCodec codec{};
                HostServices& host = *static_cast<HostServices*>(user_data);
                const GpuBufferAllocation allocated = host.request_gpu_buffer(codec.decode_gpu_buffer_request(*request, "Scene host services"));
                *allocation = codec.encode_gpu_buffer_allocation(allocated);
                return SPECTRA_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                scene_host_service_callback_error = error.what();
                return SPECTRA_SCENE_RESULT_ERROR;
            } catch (...) {
                scene_host_service_callback_error = "unknown scene host service error";
                return SPECTRA_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraSceneResult release_gpu_buffer(void* user_data, const std::uint64_t resource_id) noexcept {
            try {
                scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Scene host services user data pointer is null");
                HostServices& host = *static_cast<HostServices*>(user_data);
                host.release_gpu_buffer(resource_id);
                return SPECTRA_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                scene_host_service_callback_error = error.what();
                return SPECTRA_SCENE_RESULT_ERROR;
            } catch (...) {
                scene_host_service_callback_error = "unknown scene host service error";
                return SPECTRA_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] const char* scene_host_last_error(void* user_data) noexcept {
            if (user_data == nullptr) return scene_host_service_callback_error.c_str();
            HostServices& host = *static_cast<HostServices*>(user_data);
            const std::string_view service_error = host.last_error();
            thread_local std::string host_service_error_text{};
            if (!service_error.empty()) {
                host_service_error_text = service_error;
                return host_service_error_text.c_str();
            }
            return scene_host_service_callback_error.c_str();
        }

        [[nodiscard]] SpectraSceneHostServices make_host_view(HostServices& host) {
            return SpectraSceneHostServices{
                .struct_size = sizeof(SpectraSceneHostServices),
                .user_data = &host,
                .request_gpu_buffer = request_gpu_buffer,
                .release_gpu_buffer = release_gpu_buffer,
                .last_error = scene_host_last_error,
            };
        }

        [[nodiscard]] scene::Scene::Camera make_host_inspection_camera() {
            return scene::Scene::Camera{
                .name = "Spectra Inspector Camera",
                .view = scene::camera_view_from_look_at(
                    scene::Vector3{0.0f, 1.0f, 5.0f},
                    scene::Vector3{0.0f, 0.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::CameraProjection{
                        .kind = scene::CameraProjectionKind::Perspective,
                        .vertical_fov_degrees = 45.0f,
                        .near_plane = 0.01f,
                        .far_plane = 200.0f,
                    }
                ),
            };
        }

        void ensure_scene_camera(scene::Scene::Document& document, const std::string_view plugin_id) {
            if (document.cameras.empty()) {
                document.cameras.push_back(make_host_inspection_camera());
                document.active_camera_name = document.cameras.back().name;
                return;
            }
            if (!document.active_camera_name.empty()) return;
            if (document.cameras.size() != 1u) throw std::runtime_error(std::format("Scene plugin \"{}\" provided {} cameras but no active camera name", plugin_id, document.cameras.size()));
            document.active_camera_name = document.cameras.front().name;
        }

        [[nodiscard]] std::filesystem::path normalized_scene_plugin_path(const std::filesystem::path& plugin_path) {
            if (plugin_path.empty()) throw std::runtime_error("Scene plugin path must not be empty");
            const std::filesystem::path absolute_path = std::filesystem::absolute(plugin_path).lexically_normal();
            if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a Scene plugin library, not a folder");
            if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: Scene plugin file does not exist", absolute_path.string()));
            const PluginAbiCodec codec{};
            if (!codec.accepts_plugin_path(absolute_path)) throw std::runtime_error(std::format("{}: Scene plugin file extension is not supported on this platform", absolute_path.string()));
            return absolute_path;
        }

        [[nodiscard]] std::uint64_t fnv1a64_append(std::uint64_t hash, const std::string_view value) {
            for (const char character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] std::string make_plugin_scene_id(const std::filesystem::path& plugin_path, const std::vector<PluginOptionStorage>& options) {
            std::vector<PluginOptionStorage> sorted_options = options;
            std::ranges::sort(sorted_options, {}, &PluginOptionStorage::key);
            std::uint64_t hash = 14695981039346656037ull;
            hash = fnv1a64_append(hash, plugin_path.string());
            for (const PluginOptionStorage& option : sorted_options) {
                hash = fnv1a64_append(hash, "\n");
                hash = fnv1a64_append(hash, option.key);
                hash = fnv1a64_append(hash, "=");
                hash = fnv1a64_append(hash, option.value);
            }
            return std::format("{}#plugin-open-{:016x}", plugin_path.string(), hash);
        }

        [[nodiscard]] PluginOpenRequestStorage make_plugin_open_request_storage(std::filesystem::path plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host) {
            PluginOpenRequestStorage storage{
                .plugin_path = normalized_scene_plugin_path(plugin_path),
            };
            if (host == nullptr) throw std::runtime_error("Scene open request requires host services");
            storage.host = std::move(host);
            storage.host_view = make_host_view(*storage.host);
            std::set<std::string> option_keys{};
            storage.options.reserve(options.size());
            for (ControlOption& option : options) {
                if (option.key.empty()) throw std::runtime_error("Scene open option key must not be empty");
                if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Scene open option '{}' is duplicated", option.key));
                storage.options.push_back(PluginOptionStorage{
                    .key = std::move(option.key),
                    .value = std::move(option.value),
                });
            }
            storage.scene_id = make_plugin_scene_id(storage.plugin_path, storage.options);
            storage.option_views.reserve(storage.options.size());
            for (const PluginOptionStorage& option : storage.options) {
                storage.option_views.push_back(SpectraSceneOption{
                    .key = option.key.c_str(),
                    .value = option.value.c_str(),
                });
            }
            return storage;
        }


        [[nodiscard]] PluginOpenRequestStorage make_plugin_inspect_request_storage(std::filesystem::path plugin_path) {
            PluginOpenRequestStorage storage{
                .plugin_path = normalized_scene_plugin_path(plugin_path),
            };
            storage.scene_id = make_plugin_scene_id(storage.plugin_path, storage.options);
            return storage;
        }
    } // namespace

    struct PluginLibrary::Impl final {
            explicit Impl(PluginOpenRequestStorage open_request) : open_request(std::move(open_request)), plugin_directory(this->open_request.plugin_path.parent_path()), native(this->open_request.plugin_path) {
                void* entry_address = this->native.symbol("spectra_scene_plugin_v1");
                const SpectraScenePluginEntryFn entry = reinterpret_cast<SpectraScenePluginEntryFn>(entry_address);
                this->plugin = entry();
                if (this->plugin == nullptr) throw std::runtime_error(std::format("{}: Scene plugin entry returned null", this->open_request.plugin_path.string()));
                this->validate_plugin_descriptor();
                this->descriptor = this->codec.decode_descriptor(*this->plugin);
                this->validate_scene_api();
                this->validate_controls_api();
            }

            Impl(const Impl& other) = delete;
            Impl(Impl&& other) = delete;
            Impl& operator=(const Impl& other) = delete;
            Impl& operator=(Impl&& other) = delete;
            ~Impl() noexcept = default;

            [[nodiscard]] std::string id() const {
                return this->descriptor.id;
            }

            [[nodiscard]] std::string title() const {
                return this->descriptor.title;
            }

            [[nodiscard]] std::string controls_panel_title() const {
                return this->descriptor.controls_panel_title;
            }

            [[nodiscard]] std::string open_action_label() const {
                return this->descriptor.open_action_label;
            }

            [[nodiscard]] std::string open_action_description() const {
                return this->descriptor.open_action_description;
            }

            [[nodiscard]] std::string scene_id() const {
                return this->open_request.scene_id;
            }

            [[nodiscard]] const std::filesystem::path& path() const {
                return this->open_request.plugin_path;
            }

            [[nodiscard]] std::vector<ControlOptionSchema> open_options() const {
                return this->descriptor.open_options;
            }

            [[nodiscard]] std::vector<ControlAction> control_actions() const {
                return this->descriptor.control_actions;
            }

            [[nodiscard]] std::vector<ControlOptionSchema> control_settings() const {
                return this->descriptor.control_settings;
            }

            [[nodiscard]] double frames_per_second() const {
                return this->descriptor.frames_per_second;
            }

            [[nodiscard]] scene::Scene::Document make_base_document() const {
                const std::string& base_path_text = this->descriptor.base_pbrt_path;
                if (base_path_text.empty()) {
                    return scene::Scene::Document{
                        .revision = scene::Scene::Revision{1},
                        .name = this->id(),
                        .title = this->title(),
                        .source = this->scene_id(),
                        .frames_per_second = this->frames_per_second(),
                        .timeline_enabled = true,
                    };
                }
                const std::filesystem::path base_relative_path{base_path_text};
                if (base_relative_path.is_absolute()) throw std::runtime_error(std::format("{}: Scene base PBRT path must be relative to the plugin directory", base_path_text));
                const std::filesystem::path base_path = (this->plugin_directory / base_relative_path).lexically_normal();
                if (!std::filesystem::is_regular_file(base_path)) throw std::runtime_error(std::format("{}: Scene base PBRT file does not exist", base_path.string()));
                scene::Scene base_scene = scene::Scene::parse_pbrt_file(base_path);
                scene::Scene::Document document = *base_scene.document();
                document.revision = scene::Scene::Revision{1};
                document.name = this->id();
                document.title = this->title();
                document.source = this->scene_id();
                document.frames_per_second = this->frames_per_second();
                document.timeline_enabled = true;
                return document;
            }

            void check_result(const SpectraSceneResult result, SpectraSceneInstance* instance, const std::string_view action) const {
                if (result == SPECTRA_SCENE_RESULT_OK) return;
                if (result != SPECTRA_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{} returned an unknown result code {}", action, static_cast<int>(result)));
                std::string error = this->codec.decode_last_error(*this->plugin, instance, action);
                if (error.empty()) error = "unknown plugin error";
                throw std::runtime_error(std::format("{} failed: {}", action, error));
            }

            [[nodiscard]] SpectraSceneInstance* create_instance() const {
                if (this->open_request.host == nullptr) throw std::runtime_error("Scene plugin instance creation requires host services");
                SpectraSceneInstance* instance{};
                const std::string plugin_path_text = this->open_request.plugin_path.string();
                const SpectraSceneOpenInfo open_info{
                    .struct_size = sizeof(SpectraSceneOpenInfo),
                    .plugin_path = plugin_path_text.c_str(),
                    .options = SpectraSceneOptionSpan{
                        .data = this->open_request.option_views.empty() ? nullptr : this->open_request.option_views.data(),
                        .count = static_cast<std::uint64_t>(this->open_request.option_views.size()),
                    },
                    .host_services = &this->open_request.host_view,
                };
                this->check_result(this->plugin->create(&open_info, &instance), nullptr, "Scene plugin create");
                if (instance == nullptr) throw std::runtime_error("Scene plugin create returned a null instance");
                return instance;
            }

            void destroy_instance(SpectraSceneInstance* instance) const noexcept {
                if (instance != nullptr) this->plugin->destroy(instance);
            }

            void reset(SpectraSceneInstance* instance) const {
                this->check_result(this->plugin->reset(instance), instance, "Scene plugin reset");
            }

            void update(SpectraSceneInstance* instance, const UpdateInfo& update) const {
                const SpectraSceneUpdateInfo update_info{
                    .struct_size = sizeof(SpectraSceneUpdateInfo),
                    .wall_delta_seconds = update.wall_delta_seconds,
                    .scene_delta_seconds = update.scene_delta_seconds,
                    .time_seconds = update.time_seconds,
                    .frame_index = update.frame_index,
                    .timeline_mode = static_cast<std::uint32_t>(update.timeline_mode),
                    .timeline_playing = update.timeline_playing ? 1u : 0u,
                };
                this->check_result(this->plugin->update(instance, &update_info), instance, "Scene plugin update");
            }

            [[nodiscard]] std::uint64_t scene_revision(SpectraSceneInstance* instance) const {
                std::uint64_t revision{};
                this->check_result(this->plugin->scene_revision(instance, &revision), instance, "Scene plugin controls scene revision");
                if (revision == 0u) throw std::runtime_error("Scene plugin controls scene revision must not be zero");
                return revision;
            }

            void control_action(SpectraSceneInstance* instance, const std::string_view action_id, const std::span<const ControlOption> options) const {
                if (action_id.empty()) throw std::runtime_error("Scene plugin controls action id must not be empty");
                const std::string action_id_text{action_id};
                std::vector<PluginOptionStorage> option_storage{};
                std::vector<SpectraSceneOption> option_views{};
                std::set<std::string> option_keys{};
                option_storage.reserve(options.size());
                option_views.reserve(options.size());
                for (const ControlOption& option : options) {
                    if (option.key.empty()) throw std::runtime_error("Scene controls action option key must not be empty");
                    if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Scene controls action option '{}' is duplicated", option.key));
                    option_storage.push_back(PluginOptionStorage{
                        .key = option.key,
                        .value = option.value,
                    });
                }
                for (const PluginOptionStorage& option : option_storage) {
                    option_views.push_back(SpectraSceneOption{
                        .key = option.key.c_str(),
                        .value = option.value.c_str(),
                    });
                }
                this->check_result(
                    this->plugin->control_action(
                        instance,
                        action_id_text.c_str(),
                        SpectraSceneOptionSpan{
                            .data = option_views.empty() ? nullptr : option_views.data(),
                            .count = static_cast<std::uint64_t>(option_views.size()),
                        }),
                    instance,
                    std::format("Scene plugin controls action '{}'", action_id));
            }

            void control_setting_update(SpectraSceneInstance* instance, const std::string_view key, const std::string_view value) const {
                if (key.empty()) throw std::runtime_error("Scene plugin controls setting key must not be empty");
                const std::string key_text{key};
                const std::string value_text{value};
                this->check_result(this->plugin->control_setting_update(instance, key_text.c_str(), value_text.c_str()), instance, std::format("Scene plugin controls setting '{}'", key));
            }

            [[nodiscard]] ControlSnapshot control_snapshot(SpectraSceneInstance* instance) const {
                SpectraSceneControlSnapshotView view{};
                this->check_result(this->plugin->control_snapshot(instance, &view), instance, "Scene plugin controls snapshot");
                return this->codec.decode_control_snapshot(view, this->descriptor.control_actions, this->descriptor.control_settings, "Scene plugin controls snapshot");
            }

            [[nodiscard]] SpectraSceneDocumentView document(SpectraSceneInstance* instance) const {
                SpectraSceneDocumentView view{};
                this->check_result(this->plugin->document(instance, &view), instance, "Scene plugin document");
                return view;
            }

            [[nodiscard]] SpectraSceneFrameView frame(SpectraSceneInstance* instance, const scene::Scene::FrameInfo& frame_info) const {
                SpectraSceneFrameView view{};
                this->check_result(this->plugin->frame(instance, SpectraSceneFrameInfo{.delta_seconds = frame_info.delta_seconds, .time_seconds = frame_info.time_seconds, .frame_index = frame_info.frame_index}, &view), instance, "Scene plugin frame");
                return view;
            }

        private:
            void validate_plugin_descriptor() const {
                if (this->plugin->abi_version != plugin_abi_version) throw std::runtime_error(std::format("{}: scene plugin ABI version {} does not match host ABI version {}", this->open_request.plugin_path.string(), this->plugin->abi_version, plugin_abi_version));
                if (this->plugin->struct_size != sizeof(SpectraScenePlugin)) throw std::runtime_error(std::format("{}: Scene plugin descriptor size mismatch", this->open_request.plugin_path.string()));
            }

            void validate_scene_api() const {
                const double fps = this->frames_per_second();
                if (fps <= 0.0) throw std::runtime_error(std::format("{}: Scene plugin frame rate must be positive", this->open_request.plugin_path.string()));
                if (this->plugin->create == nullptr) throw std::runtime_error(std::format("{}: Scene plugin create function is null", this->open_request.plugin_path.string()));
                if (this->plugin->destroy == nullptr) throw std::runtime_error(std::format("{}: Scene plugin destroy function is null", this->open_request.plugin_path.string()));
                if (this->plugin->reset == nullptr) throw std::runtime_error(std::format("{}: Scene plugin reset function is null", this->open_request.plugin_path.string()));
                if (this->plugin->update == nullptr) throw std::runtime_error(std::format("{}: Scene plugin update function is null", this->open_request.plugin_path.string()));
                if (this->plugin->document == nullptr) throw std::runtime_error(std::format("{}: Scene plugin document function is null", this->open_request.plugin_path.string()));
                if (this->plugin->frame == nullptr) throw std::runtime_error(std::format("{}: Scene plugin frame function is null", this->open_request.plugin_path.string()));
                if (this->plugin->last_error == nullptr) throw std::runtime_error(std::format("{}: Scene plugin last_error function is null", this->open_request.plugin_path.string()));
            }

            void validate_controls_api() const {
                if (this->plugin->scene_revision == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls scene_revision function is null", this->open_request.plugin_path.string()));
                if (this->plugin->control_action == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls control_action function is null", this->open_request.plugin_path.string()));
                if (this->plugin->control_setting_update == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls control_setting_update function is null", this->open_request.plugin_path.string()));
                if (this->plugin->control_snapshot == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls control_snapshot function is null", this->open_request.plugin_path.string()));
            }

            PluginOpenRequestStorage open_request{};
            std::filesystem::path plugin_directory{};
            PluginAbiCodec codec{};
            PluginAbiCodec::Descriptor descriptor{};
            NativeLibrary native;
            const SpectraScenePlugin* plugin{};
        };

        class PluginSceneDriver final : public SceneDriver {
        public:
            explicit PluginSceneDriver(std::shared_ptr<PluginLibrary> plugin) : plugin(std::move(plugin)) {
                if (this->plugin == nullptr) throw std::runtime_error("Scene plugin source requires a plugin library");
                this->instance = this->plugin->impl->create_instance();
            }

            PluginSceneDriver(const PluginSceneDriver& other) = delete;
            PluginSceneDriver(PluginSceneDriver&& other) = delete;
            PluginSceneDriver& operator=(const PluginSceneDriver& other) = delete;
            PluginSceneDriver& operator=(PluginSceneDriver&& other) = delete;

            ~PluginSceneDriver() noexcept override {
                this->plugin->impl->destroy_instance(this->instance);
                this->instance = nullptr;
            }

            void reset() override {
                this->plugin->impl->reset(this->instance);
            }

            void update(const UpdateInfo& update) override {
                this->plugin->impl->update(this->instance, update);
            }

            [[nodiscard]] std::uint64_t scene_revision() const override {
                return this->plugin->impl->scene_revision(this->instance);
            }

            void execute_control_action(const std::string_view action_id, const std::span<const ControlOption> options) override {
                this->plugin->impl->control_action(this->instance, action_id, options);
            }

            void update_control_setting(const std::string_view key, const std::string_view value) override {
                this->plugin->impl->control_setting_update(this->instance, key, value);
            }

            [[nodiscard]] ControlSnapshot control_snapshot() const override {
                return this->plugin->impl->control_snapshot(this->instance);
            }

            [[nodiscard]] scene::Scene::Document create_scene_document() const override {
                scene::Scene::Document document = this->plugin->impl->make_base_document();
                const PluginAbiCodec codec{};
                PluginAbiCodec::SceneSymbols symbols = codec.collect_scene_symbols(document);
                codec.append_document(document, this->plugin->impl->document(this->instance), symbols);
                ensure_scene_camera(document, this->plugin->impl->id());
                document.timeline_enabled = true;
                document.frames_per_second = this->plugin->impl->frames_per_second();
                this->scene_symbols = std::move(symbols);
                this->document_validated = true;
                return document;
            }

            [[nodiscard]] scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const override {
                if (!this->document_validated) throw std::runtime_error("Scene plugin frame was requested before document material validation");
                const PluginAbiCodec codec{};
                return codec.decode_frame(this->plugin->impl->frame(this->instance, frame), frame, this->scene_symbols);
            }

        private:
            std::shared_ptr<PluginLibrary> plugin{};
            SpectraSceneInstance* instance{};
            mutable PluginAbiCodec::SceneSymbols scene_symbols{};
            mutable bool document_validated{};
        };
    PluginLibrary::PluginLibrary(std::filesystem::path plugin_path) : impl(std::make_unique<Impl>(make_plugin_inspect_request_storage(std::move(plugin_path)))) {}

    PluginLibrary::PluginLibrary(std::filesystem::path plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host) : impl(std::make_unique<Impl>(make_plugin_open_request_storage(std::move(plugin_path), std::move(options), std::move(host)))) {}

    PluginLibrary::~PluginLibrary() noexcept = default;

    std::string PluginLibrary::id() const {
        return this->impl->id();
    }

    std::string PluginLibrary::title() const {
        return this->impl->title();
    }

    std::string PluginLibrary::controls_panel_title() const {
        return this->impl->controls_panel_title();
    }

    std::string PluginLibrary::open_action_label() const {
        return this->impl->open_action_label();
    }

    std::string PluginLibrary::open_action_description() const {
        return this->impl->open_action_description();
    }

    std::string PluginLibrary::scene_id() const {
        return this->impl->scene_id();
    }

    const std::filesystem::path& PluginLibrary::path() const {
        return this->impl->path();
    }

    std::vector<ControlOptionSchema> PluginLibrary::open_options() const {
        return this->impl->open_options();
    }

    std::vector<ControlAction> PluginLibrary::control_actions() const {
        return this->impl->control_actions();
    }

    std::vector<ControlOptionSchema> PluginLibrary::control_settings() const {
        return this->impl->control_settings();
    }

    std::unique_ptr<SceneDriver> make_plugin_driver(std::shared_ptr<PluginLibrary> plugin) {
        return std::make_unique<PluginSceneDriver>(std::move(plugin));
    }
} // namespace spectra::scene
