module spectra.scene_runtime.plugin_library;

import std;
import spectra.scene;
import spectra.scene_runtime.plugin_c_abi;
import spectra.scene_runtime.plugin_conversion;
import spectra.scene_runtime.plugin_native_library;
import spectra.scene_runtime.host_services;

namespace spectra::scene_runtime {
    namespace {
        struct DynamicScenePluginOptionStorage {
            std::string key{};
            std::string value{};
        };

        struct DynamicScenePluginOpenRequestStorage {
            std::filesystem::path plugin_path{};
            std::vector<DynamicScenePluginOptionStorage> options{};
            std::vector<SpectraDynamicSceneOption> option_views{};
            std::shared_ptr<DynamicSceneHostServices> host_services{};
            SpectraDynamicSceneHostServices host_services_view{};
            std::string source_id{};
        };

        [[nodiscard]] std::uint32_t abi_gpu_resource_handle_kind(const DynamicSceneGpuResourceHandleKind kind) {
            switch (kind) {
            case DynamicSceneGpuResourceHandleKind::OpaqueWin32: return 1u;
            case DynamicSceneGpuResourceHandleKind::OpaqueFileDescriptor: return 2u;
            }
            throw std::runtime_error("Dynamic scene GPU resource handle kind is invalid");
        }

        [[nodiscard]] SpectraDynamicSceneGpuDeviceIdentity abi_gpu_device_identity(const DynamicSceneGpuDeviceIdentity& identity) {
            SpectraDynamicSceneGpuDeviceIdentity view{
                .vendor_id = identity.vendor_id,
                .device_id = identity.device_id,
                .device_node_mask = identity.device_node_mask,
            };
            for (std::size_t index = 0u; index < identity.device_uuid.size(); ++index) view.device_uuid[index] = identity.device_uuid[index];
            for (std::size_t index = 0u; index < identity.device_luid.size(); ++index) view.device_luid[index] = identity.device_luid[index];
            return view;
        }

        [[nodiscard]] std::uint32_t dynamic_scene_gpu_buffer_kind_from_abi(const std::uint32_t kind, const std::string_view context) {
            switch (kind) {
            case SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VOLUME_CHANNEL: return DynamicSceneGpuBufferKindVolumeChannel;
            case SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID: return DynamicSceneGpuBufferKindViewportVoxelGrid;
            default: throw std::runtime_error(std::format("{} GPU buffer kind {} is unknown", context, kind));
            }
        }

        [[nodiscard]] std::uint32_t abi_gpu_buffer_kind(const std::uint32_t kind) {
            switch (kind) {
            case DynamicSceneGpuBufferKindVolumeChannel: return SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VOLUME_CHANNEL;
            case DynamicSceneGpuBufferKindViewportVoxelGrid: return SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID;
            default: throw std::runtime_error(std::format("Dynamic scene GPU buffer kind {} is invalid", kind));
            }
        }

        thread_local std::string dynamic_scene_host_service_callback_error{};

        [[nodiscard]] SpectraDynamicSceneResult request_gpu_buffer(void* user_data, const SpectraDynamicSceneGpuBufferRequest* request, SpectraDynamicSceneGpuBufferAllocation* allocation) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                if (request == nullptr) throw std::runtime_error("Dynamic scene GPU buffer request pointer is null");
                if (allocation == nullptr) throw std::runtime_error("Dynamic scene GPU buffer allocation pointer is null");
                if (request->struct_size != sizeof(SpectraDynamicSceneGpuBufferRequest)) throw std::runtime_error("Dynamic scene GPU buffer request ABI size mismatch");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                const DynamicSceneGpuBufferAllocation allocated = host_services.request_gpu_buffer(DynamicSceneGpuBufferRequest{
                    .kind = dynamic_scene_gpu_buffer_kind_from_abi(request->kind, "Dynamic scene host services"),
                    .byte_size = request->byte_size,
                    .debug_name = abi_string(request->debug_name, "Dynamic scene GPU buffer debug name", true),
                });
                *allocation = SpectraDynamicSceneGpuBufferAllocation{
                    .struct_size = sizeof(SpectraDynamicSceneGpuBufferAllocation),
                    .resource_id = allocated.resource_id,
                    .byte_size = allocated.byte_size,
                    .kind = abi_gpu_buffer_kind(allocated.kind),
                    .handle_kind = abi_gpu_resource_handle_kind(allocated.handle_kind),
                    .handle = allocated.handle,
                    .device_identity = abi_gpu_device_identity(allocated.device_identity),
                };
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraDynamicSceneResult release_gpu_buffer(void* user_data, const std::uint64_t resource_id) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                host_services.release_gpu_buffer(resource_id);
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] const char* dynamic_scene_host_services_last_error(void* user_data) noexcept {
            if (user_data == nullptr) return dynamic_scene_host_service_callback_error.c_str();
            DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
            const std::string_view service_error = host_services.last_error();
            thread_local std::string host_service_error_text{};
            if (!service_error.empty()) {
                host_service_error_text = service_error;
                return host_service_error_text.c_str();
            }
            return dynamic_scene_host_service_callback_error.c_str();
        }

        [[nodiscard]] SpectraDynamicSceneHostServices make_host_services_view(DynamicSceneHostServices& host_services) {
            return SpectraDynamicSceneHostServices{
                .struct_size = sizeof(SpectraDynamicSceneHostServices),
                .user_data = &host_services,
                .request_gpu_buffer = request_gpu_buffer,
                .release_gpu_buffer = release_gpu_buffer,
                .last_error = dynamic_scene_host_services_last_error,
            };
        }



        [[nodiscard]] bool dynamic_scene_plugin_path_extension_supported(const std::filesystem::path& path) {
#if defined(_WIN32)
            return path_extension_is(path, ".dll");
#elif defined(__APPLE__)
            return path_extension_is(path, ".dylib");
#else
            return path_extension_is(path, ".so");
#endif
        }

        [[nodiscard]] std::filesystem::path normalized_dynamic_scene_plugin_path(const std::filesystem::path& plugin_path) {
            if (plugin_path.empty()) throw std::runtime_error("Dynamic scene plugin path must not be empty");
            const std::string path_text = plugin_path.string();
            if (path_text.find('?') != std::string::npos) throw std::runtime_error("Dynamic scene plugin Scene URI query is not supported; open the plugin path and configure it in the Scene popover");
            const std::filesystem::path absolute_path = std::filesystem::absolute(plugin_path).lexically_normal();
            if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a dynamic scene plugin library, not a folder");
            if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file does not exist", absolute_path.string()));
            if (!dynamic_scene_plugin_path_extension_supported(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file extension is not supported on this platform", absolute_path.string()));
            return absolute_path;
        }

        [[nodiscard]] std::uint64_t fnv1a64_append(std::uint64_t hash, const std::string_view value) {
            for (const char character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] std::string make_dynamic_scene_source_id(const std::filesystem::path& plugin_path, const std::vector<DynamicScenePluginOptionStorage>& options) {
            std::vector<DynamicScenePluginOptionStorage> sorted_options = options;
            std::ranges::sort(sorted_options, {}, &DynamicScenePluginOptionStorage::key);
            std::uint64_t hash = 14695981039346656037ull;
            hash = fnv1a64_append(hash, plugin_path.string());
            for (const DynamicScenePluginOptionStorage& option : sorted_options) {
                hash = fnv1a64_append(hash, "\n");
                hash = fnv1a64_append(hash, option.key);
                hash = fnv1a64_append(hash, "=");
                hash = fnv1a64_append(hash, option.value);
            }
            return std::format("{}#dynamic-open-{:016x}", plugin_path.string(), hash);
        }

        [[nodiscard]] DynamicScenePluginOpenRequestStorage make_plugin_open_request_storage(std::filesystem::path plugin_path, std::vector<DynamicSceneOption> options, std::shared_ptr<DynamicSceneHostServices> host_services) {
            DynamicScenePluginOpenRequestStorage storage{
                .plugin_path = normalized_dynamic_scene_plugin_path(plugin_path),
            };
            if (host_services == nullptr) throw std::runtime_error("Dynamic scene open request requires host services");
            storage.host_services = std::move(host_services);
            storage.host_services_view = make_host_services_view(*storage.host_services);
            std::set<std::string> option_keys{};
            storage.options.reserve(options.size());
            for (DynamicSceneOption& option : options) {
                if (option.key.empty()) throw std::runtime_error("Dynamic scene open option key must not be empty");
                if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Dynamic scene open option '{}' is duplicated", option.key));
                storage.options.push_back(DynamicScenePluginOptionStorage{
                    .key = std::move(option.key),
                    .value = std::move(option.value),
                });
            }
            storage.source_id = make_dynamic_scene_source_id(storage.plugin_path, storage.options);
            storage.option_views.reserve(storage.options.size());
            for (const DynamicScenePluginOptionStorage& option : storage.options) {
                storage.option_views.push_back(SpectraDynamicSceneOption{
                    .key = option.key.c_str(),
                    .value = option.value.c_str(),
                });
            }
            return storage;
        }


        [[nodiscard]] DynamicScenePluginOpenRequestStorage make_plugin_inspect_request_storage(std::filesystem::path plugin_path) {
            DynamicScenePluginOpenRequestStorage storage{
                .plugin_path = normalized_dynamic_scene_plugin_path(plugin_path),
            };
            storage.source_id = make_dynamic_scene_source_id(storage.plugin_path, storage.options);
            return storage;
        }
    } // namespace

    struct DynamicScenePluginLibrary::Impl final {
            explicit Impl(DynamicScenePluginOpenRequestStorage open_request) : open_request(std::move(open_request)), plugin_directory(this->open_request.plugin_path.parent_path()), native(this->open_request.plugin_path) {
                void* entry_address = this->native.symbol("spectra_dynamic_scene_plugin");
                const SpectraDynamicScenePluginEntryFn entry = reinterpret_cast<SpectraDynamicScenePluginEntryFn>(entry_address);
                this->plugin = entry();
                if (this->plugin == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin entry returned null", this->open_request.plugin_path.string()));
                this->validate_plugin_descriptor();
                this->validate_scene_api();
                this->validate_controls_api();
            }

            Impl(const Impl& other) = delete;
            Impl(Impl&& other) = delete;
            Impl& operator=(const Impl& other) = delete;
            Impl& operator=(Impl&& other) = delete;
            ~Impl() noexcept = default;

            [[nodiscard]] std::string id() const {
                return abi_string(this->plugin->id, "Dynamic scene plugin id", false);
            }

            [[nodiscard]] std::string title() const {
                return abi_string(this->plugin->title, "Dynamic scene plugin title", false);
            }

            [[nodiscard]] std::string controls_panel_title() const {
                return abi_string(this->plugin->controls_panel_title, "Dynamic scene plugin controls panel title", false);
            }

            [[nodiscard]] std::string open_action_label() const {
                return abi_string(this->plugin->open_action_label, "Dynamic scene plugin open action label", false);
            }

            [[nodiscard]] std::string open_action_description() const {
                return abi_string(this->plugin->open_action_description, "Dynamic scene plugin open action description", true);
            }

            [[nodiscard]] std::string source_id() const {
                return this->open_request.source_id;
            }

            [[nodiscard]] const std::filesystem::path& path() const {
                return this->open_request.plugin_path;
            }

            [[nodiscard]] const std::filesystem::path& directory() const {
                return this->plugin_directory;
            }

            [[nodiscard]] std::vector<DynamicSceneOptionSchema> open_options() const {
                return make_open_option_schemas(this->plugin->open_options, "Dynamic scene plugin open option schema");
            }

            [[nodiscard]] std::vector<DynamicSceneControlAction> control_actions() const {
                return make_control_actions(this->plugin->control_actions, "Dynamic scene plugin controls action");
            }

            [[nodiscard]] std::vector<DynamicSceneOptionSchema> control_settings() const {
                return make_control_setting_schemas(this->plugin->control_settings, "Dynamic scene plugin controls setting schema");
            }

            [[nodiscard]] double frames_per_second() const {
                return finite_double(this->plugin->frames_per_second, "Dynamic scene plugin frame rate");
            }

            [[nodiscard]] scene::Scene::Document make_base_document() const {
                const std::string base_path_text = abi_string(this->plugin->base_pbrt_path, "Dynamic scene plugin base PBRT path", true);
                if (base_path_text.empty()) {
                    return scene::Scene::Document{
                        .revision = scene::Scene::Revision{1},
                        .name = this->id(),
                        .title = this->title(),
                        .source = this->source_id(),
                        .frames_per_second = this->frames_per_second(),
                        .timeline_enabled = true,
                    };
                }
                const std::filesystem::path base_relative_path{base_path_text};
                if (base_relative_path.is_absolute()) throw std::runtime_error(std::format("{}: dynamic scene base PBRT path must be relative to the plugin directory", base_path_text));
                const std::filesystem::path base_path = (this->plugin_directory / base_relative_path).lexically_normal();
                if (!std::filesystem::is_regular_file(base_path)) throw std::runtime_error(std::format("{}: dynamic scene base PBRT file does not exist", base_path.string()));
                scene::Scene base_scene = scene::Scene::parse_pbrt_file(base_path);
                scene::Scene::Document document = *base_scene.document();
                document.revision = scene::Scene::Revision{1};
                document.name = this->id();
                document.title = this->title();
                document.source = this->source_id();
                document.frames_per_second = this->frames_per_second();
                document.timeline_enabled = true;
                return document;
            }

            void check_result(const SpectraDynamicSceneResult result, SpectraDynamicSceneInstance* instance, const std::string_view action) const {
                if (result == SPECTRA_DYNAMIC_SCENE_RESULT_OK) return;
                if (result != SPECTRA_DYNAMIC_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{} returned an unknown result code {}", action, static_cast<int>(result)));
                std::string error = abi_string(this->plugin->last_error(instance), std::format("{} error message", action), true);
                if (error.empty()) error = "unknown plugin error";
                throw std::runtime_error(std::format("{} failed: {}", action, error));
            }

            [[nodiscard]] SpectraDynamicSceneInstance* create_instance() const {
                if (this->open_request.host_services == nullptr) throw std::runtime_error("Dynamic scene plugin instance creation requires host services");
                SpectraDynamicSceneInstance* instance{};
                const std::string plugin_path_text = this->open_request.plugin_path.string();
                const SpectraDynamicSceneOpenInfo open_info{
                    .struct_size = sizeof(SpectraDynamicSceneOpenInfo),
                    .plugin_path = plugin_path_text.c_str(),
                    .options = SpectraDynamicSceneOptionSpan{
                        .data = this->open_request.option_views.empty() ? nullptr : this->open_request.option_views.data(),
                        .count = static_cast<std::uint64_t>(this->open_request.option_views.size()),
                    },
                    .host_services = &this->open_request.host_services_view,
                };
                this->check_result(this->plugin->create(&open_info, &instance), nullptr, "Dynamic scene plugin create");
                if (instance == nullptr) throw std::runtime_error("Dynamic scene plugin create returned a null instance");
                return instance;
            }

            void destroy_instance(SpectraDynamicSceneInstance* instance) const noexcept {
                if (instance != nullptr) this->plugin->destroy(instance);
            }

            void reset(SpectraDynamicSceneInstance* instance) const {
                this->check_result(this->plugin->reset(instance), instance, "Dynamic scene plugin reset");
            }

            void update(SpectraDynamicSceneInstance* instance, const DynamicSceneUpdateInfo& update) const {
                const SpectraDynamicSceneUpdateInfo update_info{
                    .struct_size = sizeof(SpectraDynamicSceneUpdateInfo),
                    .wall_delta_seconds = update.wall_delta_seconds,
                    .scene_delta_seconds = update.scene_delta_seconds,
                    .time_seconds = update.time_seconds,
                    .frame_index = update.frame_index,
                    .timeline_mode = static_cast<std::uint32_t>(update.timeline_mode),
                    .timeline_playing = update.timeline_playing ? 1u : 0u,
                };
                this->check_result(this->plugin->update(instance, &update_info), instance, "Dynamic scene plugin update");
            }

            [[nodiscard]] std::uint64_t scene_revision(SpectraDynamicSceneInstance* instance) const {
                if (!this->has_controls()) return 0u;
                std::uint64_t revision{};
                this->check_result(this->plugin->scene_revision(instance, &revision), instance, "Dynamic scene plugin controls scene revision");
                if (revision == 0u) throw std::runtime_error("Dynamic scene plugin controls scene revision must not be zero");
                return revision;
            }

            void control_action(SpectraDynamicSceneInstance* instance, const std::string_view action_id, const std::span<const DynamicSceneOption> options) const {
                if (!this->has_controls()) throw std::runtime_error("Dynamic scene plugin does not expose controls");
                if (action_id.empty()) throw std::runtime_error("Dynamic scene plugin controls action id must not be empty");
                const std::string action_id_text{action_id};
                std::vector<DynamicScenePluginOptionStorage> option_storage{};
                std::vector<SpectraDynamicSceneOption> option_views{};
                std::set<std::string> option_keys{};
                option_storage.reserve(options.size());
                option_views.reserve(options.size());
                for (const DynamicSceneOption& option : options) {
                    if (option.key.empty()) throw std::runtime_error("Dynamic scene controls action option key must not be empty");
                    if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Dynamic scene controls action option '{}' is duplicated", option.key));
                    option_storage.push_back(DynamicScenePluginOptionStorage{
                        .key = option.key,
                        .value = option.value,
                    });
                }
                for (const DynamicScenePluginOptionStorage& option : option_storage) {
                    option_views.push_back(SpectraDynamicSceneOption{
                        .key = option.key.c_str(),
                        .value = option.value.c_str(),
                    });
                }
                this->check_result(
                    this->plugin->control_action(
                        instance,
                        action_id_text.c_str(),
                        SpectraDynamicSceneOptionSpan{
                            .data = option_views.empty() ? nullptr : option_views.data(),
                            .count = static_cast<std::uint64_t>(option_views.size()),
                        }),
                    instance,
                    std::format("Dynamic scene plugin controls action '{}'", action_id));
            }

            void control_setting_update(SpectraDynamicSceneInstance* instance, const std::string_view key, const std::string_view value) const {
                if (!this->has_controls()) throw std::runtime_error("Dynamic scene plugin does not expose controls");
                if (key.empty()) throw std::runtime_error("Dynamic scene plugin controls setting key must not be empty");
                const std::string key_text{key};
                const std::string value_text{value};
                this->check_result(this->plugin->control_setting_update(instance, key_text.c_str(), value_text.c_str()), instance, std::format("Dynamic scene plugin controls setting '{}'", key));
            }

            [[nodiscard]] DynamicSceneControlSnapshot control_snapshot(SpectraDynamicSceneInstance* instance) const {
                if (!this->has_controls()) {
                    return DynamicSceneControlSnapshot{
                        .status = DynamicSceneControlStatus{
                            .phase = "Active",
                            .headline = "Dynamic scene active",
                        },
                    };
                }
                SpectraDynamicSceneControlSnapshotView view{};
                this->check_result(this->plugin->control_snapshot(instance, &view), instance, "Dynamic scene plugin controls snapshot");
                const std::vector<DynamicSceneControlAction> actions = this->control_actions();
                const std::vector<DynamicSceneOptionSchema> settings = this->control_settings();
                return make_control_snapshot(view, actions, settings, "Dynamic scene plugin controls snapshot");
            }

            [[nodiscard]] SpectraDynamicSceneDocumentView document(SpectraDynamicSceneInstance* instance) const {
                SpectraDynamicSceneDocumentView view{};
                this->check_result(this->plugin->document(instance, &view), instance, "Dynamic scene plugin document");
                return view;
            }

            [[nodiscard]] SpectraDynamicSceneFrameView frame(SpectraDynamicSceneInstance* instance, const scene::Scene::FrameInfo& frame_info) const {
                SpectraDynamicSceneFrameView view{};
                this->check_result(this->plugin->frame(instance, SpectraDynamicSceneFrameInfo{.delta_seconds = frame_info.delta_seconds, .time_seconds = frame_info.time_seconds, .frame_index = frame_info.frame_index}, &view), instance, "Dynamic scene plugin frame");
                return view;
            }

        private:
            [[nodiscard]] bool has_controls() const {
                return this->plugin->scene_revision != nullptr || this->plugin->control_action != nullptr || this->plugin->control_setting_update != nullptr || this->plugin->control_snapshot != nullptr || this->plugin->control_actions.count != 0u || this->plugin->control_settings.count != 0u;
            }

            void validate_plugin_descriptor() const {
                if (this->plugin->abi_version != plugin_abi_version) throw std::runtime_error(std::format("{}: dynamic scene plugin ABI version {} does not match host ABI version {}", this->open_request.plugin_path.string(), this->plugin->abi_version, plugin_abi_version));
                if (this->plugin->struct_size != sizeof(SpectraDynamicScenePlugin)) throw std::runtime_error(std::format("{}: dynamic scene plugin descriptor size mismatch", this->open_request.plugin_path.string()));
                static_cast<void>(this->id());
                static_cast<void>(this->title());
                static_cast<void>(this->controls_panel_title());
                static_cast<void>(this->open_action_label());
                static_cast<void>(this->open_action_description());
                static_cast<void>(this->open_options());
            }

            void validate_scene_api() const {
                const double fps = this->frames_per_second();
                if (fps <= 0.0) throw std::runtime_error(std::format("{}: dynamic scene plugin frame rate must be positive", this->open_request.plugin_path.string()));
                if (this->plugin->create == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin create function is null", this->open_request.plugin_path.string()));
                if (this->plugin->destroy == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin destroy function is null", this->open_request.plugin_path.string()));
                if (this->plugin->reset == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin reset function is null", this->open_request.plugin_path.string()));
                if (this->plugin->update == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin update function is null", this->open_request.plugin_path.string()));
                if (this->plugin->document == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin document function is null", this->open_request.plugin_path.string()));
                if (this->plugin->frame == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin frame function is null", this->open_request.plugin_path.string()));
                if (this->plugin->last_error == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin last_error function is null", this->open_request.plugin_path.string()));
            }

            void validate_controls_api() const {
                if (!this->has_controls()) return;
                if (this->plugin->scene_revision == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin controls scene_revision function is null", this->open_request.plugin_path.string()));
                if (this->plugin->control_action == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin controls control_action function is null", this->open_request.plugin_path.string()));
                if (this->plugin->control_setting_update == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin controls control_setting_update function is null", this->open_request.plugin_path.string()));
                if (this->plugin->control_snapshot == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin controls control_snapshot function is null", this->open_request.plugin_path.string()));
                static_cast<void>(this->control_actions());
                static_cast<void>(this->control_settings());
            }

            DynamicScenePluginOpenRequestStorage open_request{};
            std::filesystem::path plugin_directory{};
            NativeLibrary native;
            const SpectraDynamicScenePlugin* plugin{};
        };

        class DynamicScenePluginSourceInstance final : public DynamicSceneSourceInstance {
        public:
            explicit DynamicScenePluginSourceInstance(std::shared_ptr<DynamicScenePluginLibrary> plugin) : plugin(std::move(plugin)) {
                if (this->plugin == nullptr) throw std::runtime_error("Dynamic scene plugin source requires a plugin library");
                this->instance = this->plugin->impl->create_instance();
            }

            DynamicScenePluginSourceInstance(const DynamicScenePluginSourceInstance& other) = delete;
            DynamicScenePluginSourceInstance(DynamicScenePluginSourceInstance&& other) = delete;
            DynamicScenePluginSourceInstance& operator=(const DynamicScenePluginSourceInstance& other) = delete;
            DynamicScenePluginSourceInstance& operator=(DynamicScenePluginSourceInstance&& other) = delete;

            ~DynamicScenePluginSourceInstance() noexcept override {
                this->plugin->impl->destroy_instance(this->instance);
                this->instance = nullptr;
            }

            void reset() override {
                this->plugin->impl->reset(this->instance);
            }

            void update(const DynamicSceneUpdateInfo& update) override {
                this->plugin->impl->update(this->instance, update);
            }

            [[nodiscard]] std::uint64_t scene_revision() const override {
                return this->plugin->impl->scene_revision(this->instance);
            }

            void execute_control_action(const std::string_view action_id, const std::span<const DynamicSceneOption> options) override {
                this->plugin->impl->control_action(this->instance, action_id, options);
            }

            void update_control_setting(const std::string_view key, const std::string_view value) override {
                this->plugin->impl->control_setting_update(this->instance, key, value);
            }

            [[nodiscard]] DynamicSceneControlSnapshot control_snapshot() const override {
                return this->plugin->impl->control_snapshot(this->instance);
            }

            [[nodiscard]] scene::Scene::Document create_scene_document() const override {
                scene::Scene::Document document = this->plugin->impl->make_base_document();
                std::set<std::string> material_names = collect_material_names(document);
                std::set<std::string> light_names = collect_light_names(document);
                append_document_view(document, this->plugin->impl->document(this->instance), material_names, light_names);
                if (document.active_camera_name.empty()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide an active camera name", this->plugin->impl->id()));
                if (document.cameras.empty()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide a camera or base PBRT camera", this->plugin->impl->id()));
                document.timeline_enabled = true;
                document.frames_per_second = this->plugin->impl->frames_per_second();
                this->material_names = std::move(material_names);
                this->document_validated = true;
                return document;
            }

            [[nodiscard]] scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const override {
                if (!this->document_validated) throw std::runtime_error("Dynamic scene plugin frame was requested before document material validation");
                return make_frame_snapshot(this->plugin->impl->frame(this->instance, frame), frame, this->material_names);
            }

        private:
            std::shared_ptr<DynamicScenePluginLibrary> plugin{};
            SpectraDynamicSceneInstance* instance{};
            mutable std::set<std::string> material_names{};
            mutable bool document_validated{};
        };
    DynamicScenePluginLibrary::DynamicScenePluginLibrary(std::filesystem::path plugin_path) : impl(std::make_unique<Impl>(make_plugin_inspect_request_storage(std::move(plugin_path)))) {}

    DynamicScenePluginLibrary::DynamicScenePluginLibrary(std::filesystem::path plugin_path, std::vector<DynamicSceneOption> options, std::shared_ptr<DynamicSceneHostServices> host_services) : impl(std::make_unique<Impl>(make_plugin_open_request_storage(std::move(plugin_path), std::move(options), std::move(host_services)))) {}

    DynamicScenePluginLibrary::~DynamicScenePluginLibrary() noexcept = default;

    std::string DynamicScenePluginLibrary::id() const {
        return this->impl->id();
    }

    std::string DynamicScenePluginLibrary::title() const {
        return this->impl->title();
    }

    std::string DynamicScenePluginLibrary::controls_panel_title() const {
        return this->impl->controls_panel_title();
    }

    std::string DynamicScenePluginLibrary::open_action_label() const {
        return this->impl->open_action_label();
    }

    std::string DynamicScenePluginLibrary::open_action_description() const {
        return this->impl->open_action_description();
    }

    std::string DynamicScenePluginLibrary::source_id() const {
        return this->impl->source_id();
    }

    const std::filesystem::path& DynamicScenePluginLibrary::path() const {
        return this->impl->path();
    }

    std::vector<DynamicSceneOptionSchema> DynamicScenePluginLibrary::open_options() const {
        return this->impl->open_options();
    }

    std::vector<DynamicSceneControlAction> DynamicScenePluginLibrary::control_actions() const {
        return this->impl->control_actions();
    }

    std::vector<DynamicSceneOptionSchema> DynamicScenePluginLibrary::control_settings() const {
        return this->impl->control_settings();
    }

    std::unique_ptr<DynamicSceneSourceInstance> make_dynamic_scene_plugin_source_instance(std::shared_ptr<DynamicScenePluginLibrary> plugin) {
        return std::make_unique<DynamicScenePluginSourceInstance>(std::move(plugin));
    }
} // namespace spectra::scene_runtime
