![Spectra](https://github.com/Xayah-Graphics/imagebed/blob/14c6599b610e65a7ef42174e6910dac53004cec9/spectra-banner2.png)
# Spectra

[![Windows](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/windows.yml)
[![Arch Linux](https://github.com/Xayah-Graphics/spectra/actions/workflows/archlinux.yml/badge.svg)](https://github.com/Xayah-Graphics/spectra/actions/workflows/archlinux.yml)
[![License](https://img.shields.io/github/license/Xayah-Graphics/spectra)](LICENSE)

Spectra is an alpha-stage renderer and visualization workspace for graphics research, graphics scene inspection and dynamic rendering workflows. It provides a Vulkan-based interactive host with two renderer backends:

- **Spectra Rasterizer** for live scene preview.
- **Spectra Pathtracer** for OptiX/CUDA path-traced rendering of the current scene snapshot.

Spectra is designed around a shared scene workspace. Static PBRT scenes and runtime scene plugins are loaded
by the application layer, then consumed by both renderers without renderer-owned file loading. Dynamic plugins are
scene drivers: they publish renderable scene entities, scene parameters, and optional rasterizer-only debug
attachments through the same scene model used by PBRT imports.

## Build

- C++23 compiler with module support.
- CMake 4.3 or newer.
- Vulkan SDK 1.4 or newer.
- CUDA Toolkit 13.0 or newer.
- NVIDIA OptiX SDK.

```bash
cmake -S . -B build -G Ninja -DSPECTRA_OPTIX_PATH=/path/to/OptiX-SDK
cmake --build build --parallel
```

## Usage

You may load PBRT scene files and scene plugins directly from the command line.

```bash
./build/spectra_gui --scene /path/to/scene.pbrt
./build/spectra_gui --scene /path/to/plugin.dll
```

You may also load PBRT scene files and scene plugins through the GUI.

```bash
./build/spectra_gui
```

then drag and drop a `.pbrt` file or plugin `.dll`/`.so` file onto the application window. Plugin libraries open an
empty scene plugin controls workspace first; the Scene popover renders the generic form declared by the plugin
descriptor and creates the dynamic scene only when the plugin-declared open action is pressed. After creation, the same
popover displays plugin-declared control actions and live settings, while runtime phase and metrics appear in the
viewport HUD.


## scene plugins Developer Guide

Spectra loads plugin-driven scenes from platform dynamic libraries. External projects do not need to include Spectra headers,
link Spectra libraries, import Spectra modules, or use Spectra CMake helpers.
Inside Spectra, `spectra.scene` owns the scene model, plugin protocol, host services, DLL loading, timeline update, and active scene lifetime. The rasterizer only supplies the Vulkan-backed host-service implementation and preview draw passes.

A scene plugin only needs to:

1. Build a dynamic library.
2. Export `spectra_scene_plugin_v10()`.
3. Declare the ABI structs exactly as documented below.

ABI strings are UTF-8, NUL-terminated `const char*` values. `nullptr` is treated as empty only for fields documented as
optional; required strings must be non-empty. Numeric categories are `uint32_t` values constrained by the tables below,
not ABI enum types. The plugin owns all returned string and array views. Scene metadata is copied into Spectra scene storage during
conversion, but camera image pixels, viewport voxel grid GPU payloads, and external volume
channel GPU payloads are borrowed.
Camera image RGBA8 pointers must stay valid for the plugin instance lifetime, and `revision` must increase when the
pixel contents change. Viewport voxel grid data is borrowed GPU data: the plugin requests a
Spectra-owned external Vulkan storage buffer through host services, imports it into its own GPU runtime, writes the
declared source payload, and publishes
`resource_id + source_kind + index_encoding + external_device_pointer + source_byte_size + revision`.

### Binary Contract

- ABI version: `10`.
- Exported symbol: `spectra_scene_plugin_v10`.
- Windows export: `extern "C" __declspec(dllexport)`.
- Result codes are `uint32_t`: `0` OK and `1` error. Option kinds, handle kinds, scene item kinds, entity kinds,
  projection kinds, channel kinds, and presentation hints are also `uint32_t` table values rather than ABI enum
  declarations.
- Other platforms: `extern "C" __attribute__((visibility("default")))`.
- Use the platform default C calling convention.
- Do not use `#pragma pack`, custom alignment, C++ standard library types, C++ exceptions, RTTI objects, allocators, or
  Spectra C++ types across the ABI.

### Required ABI Declarations

scene plugins must declare the documented C ABI in the producer. Do not include Spectra headers, link
Spectra libraries, import Spectra modules, or depend on Spectra CMake targets. A producer only needs to declare payload
structs for the scene item kinds and control fields it actually emits. The plugin descriptor directly exposes generic
controls/open metadata, scene callbacks, controls callbacks, open option schemas, control action schemas, and live setting schemas. Spectra renders the
schemas in the Scene popover and calls the descriptor callbacks directly.

### Required Export

```cpp
#if defined(_WIN32)
#define SPECTRA_SCENE_EXPORT __declspec(dllexport)
#else
#define SPECTRA_SCENE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" SPECTRA_SCENE_EXPORT const SpectraScenePlugin* spectra_scene_plugin_v10(void);
```

The returned descriptor must stay valid while the library is loaded. Set `abi_version` to `9` and set each
`struct_size` to the exact matching ABI struct size. The scene callbacks are required; missing callbacks are errors.
Controls callbacks are required; missing callbacks are errors.

### Data Rules

- `id`, `title`, material names, light names, camera name, primitive names, and material references must be non-empty.
- `frames_per_second` must be finite and positive.
- Float options can opt into bounded slider presentation by declaring presentation `1`, `has_numeric_range = 1`, and finite
  `numeric_min < numeric_max` with positive `numeric_step`. Other options must use presentation `0` and no numeric range.
- Scene document and frame payloads are published through named `SpectraSceneItems` spans: `materials`, `lights`,
  `cameras`, `meshes`, `spheres`, `point_clouds`, `volumes`, `viewport_segment_sets`, and
  `viewport_voxel_grids`. Empty spans use `{ nullptr, 0 }`. Non-empty spans must provide non-null `data`.
  Document views may publish every named span. Frame views may publish cameras, renderable entities, and viewport
  attachments; materials and lights are document-only.
- A successfully created dynamic scene must resolve to at least one renderable `Mesh`, `Sphere`, `PointCloud`, or
  `VolumeGrid`. Cameras, lights, control state, and debug attachments do not count as renderable scene entities.
- `default_coordinate_system` may be empty or one of `SpectraSceneYUp`, `PBRT`, `BlenderZUp`, `OpenGL`, `OpenCV`.
  Unknown names are errors.
- Material `model` values: `lit_surface`, `unlit_surface`, `emissive_surface`, `volume`, `point_sprite`.
- Material `alpha_mode` values: `opaque`, `masked`, `blend`.
- Light `kind` values: `directional`, `point`, `spot`, `area`, `environment`.
- Meshes require non-empty vertices and triangle indices.
- Spheres require positive radius.
- Point radii must be positive.
- Volume dimensions must be positive. Volume channels are true render-entity data. A channel named `density` is
  renderable extinction density / `sigma_t` data and may be consumed by PBRT/pathtracer volume rendering. Acceleration
  data such as occupancy grids, masks, empty-space skipping structures, or training sampler grids must not be published
  as `VolumeGrid` density channels; publish them as debug attachments owned by the corresponding renderable volume.
  Renderer backends must consume volume channels through their native volume adapter; a backend that cannot consume a
  channel source must reject the scene explicitly.
  Channel `format` values are `0` `Float32` and `1` `Float32x3`. The `density` and `temperature` channels must use
  `Float32`; an optional `color` channel must use `Float32x3` tightly packed RGB and represents true renderable volume
  color data, not a debug attachment. Channel `source_kind` values are `0` CPU float values and `1` external GPU float
  buffer. Channel `index_encoding` values are `0` row-major linear `x + dimX * (y + dimY * z)` and `1` Morton/Z-order.
  CPU channels must provide exactly `x * y * z * component_count` finite float values and no GPU buffer id; CPU `color`
  values must be non-negative. External GPU channels must provide no CPU values, a non-zero rasterizer `buffer_id`, a
  non-zero borrowed `external_device_pointer` for pathtracer static snapshot materialization,
  `source_byte_size >= x * y * z * component_count * sizeof(float)`, and non-zero `revision`.
- Debug attachments are preview-only data attached to real scene entities. Each viewport segment set and viewport voxel
  grid must name an existing owner entity through `{ kind, name }`. Supported owner kinds are `0` mesh, `1` sphere,
  `2` point cloud, `3` volume grid, `4` camera, and `5` light. Viewport voxel grids must be owned by a volume grid.
  Missing, empty, or type-mismatched owners are errors.
- Viewport segment annotations are preview-only. They are consumed by the rasterizer and ignored by the
  pathtracer/PBRT scene.
- Viewport voxel grids are preview-only sparse voxel annotations. They are consumed by the rasterizer and ignored by the
  pathtracer/PBRT scene. `dimensions` and positive `voxel_size` describe the logical grid; `buffer_id` names a
  host-service GPU buffer. `source_kind` values are `0` compacted `uint32_t` index list and `1` dense occupancy
  bitfield. `index_encoding` values are `0` row-major linear `x + dimX * (y + dimY * z)` and `1` Morton/Z-order.
  For `source_kind = 0`, `index_count` is the number of occupied cells in the buffer. For `source_kind = 1`,
  `index_count` must be `0`; Spectra compacts the bitfield in a Vulkan compute pass before drawing. `source_byte_size`
  is the valid source payload byte count, and `revision` must increase when the producer rewrites the buffer contents.
- Viewport width modes: `0` screen-space pixels, `1` world-space units.
- Viewport depth modes: `0` depth tested, `1` always visible.
- Camera projection values: `0` perspective, `1` pinhole intrinsics.
- Camera RGBA8 images are optional borrowed pointers with tightly packed `width * height * 4` bytes.
- `open_action_label` must be non-empty.
- `sections` declares generic control panel sections. Each section id and label must be non-empty and unique. Section
  declaration order is the display order. Open options, control settings, control actions, and state metrics must each
  reference a declared `section_id`. Within a section, item order is the order of the corresponding descriptor or state span.
- `control_actions` may be empty, but every declared action id and label must be non-empty and unique. Action option
  schemas use the same option kind and validation rules as open options.
- `control_state` and `control_setting_update` are required. Settings are live editable
  controls for active plugin-driven scenes; Spectra submits a changed setting immediately and then checks `scene_revision` to
  refresh changed scene/debug data. Setting schemas are declared once in the plugin descriptor through normal option
  schemas. Setting kinds are restricted to `3` choice, `4` bool, `5` float, and `6` unsigned integer. Text and path
  kinds are only valid for open options or explicit action options. Setting keys and labels must be non-empty and unique
  in the descriptor and must provide non-empty defaults. Setting values are host-owned UI state initialized from those
  descriptor defaults; plugins accept or reject changes through `control_setting_update`.
- `control_state` returns `phase`, `headline`, `detail`, metrics, and action states directly.
  `struct_size` must match `SpectraSceneControlStateView`. Empty optional arrays use `{ nullptr, 0 }`.
- State phase, headline, metric keys/labels/values, and action state ids must be non-empty.
  Each action state id must refer to a declared control action. Every declared control action must have exactly one
  action state. Disabled actions must set `enabled = 0` and provide a non-empty `disabled_reason`; enabled actions must
  set `enabled = 1` and provide an empty `disabled_reason`.
- Control metric display flags are bit flags: `1` Primary. Unknown bits are errors. Metric order is the
  control-state metric span order. `has_color` must be `0` or `1`; if set, `color` must contain finite RGBA values.
  Spectra shows primary metrics as compact viewport HUD KPIs; unmarked metrics are viewport HUD detail rows.
- Option kinds: `0` text, `1` directory path, `2` file path, `3` choice, `4` bool, `5` float,
  `6` unsigned integer.
- Option keys must be unique and non-empty. Choice options must declare non-empty unique choices; non-choice
  options must not declare choices. Bool defaults must be `true` or `false`.

### Callback Rules

- Descriptor `create` receives a non-null `host_services` pointer in `SpectraSceneOpenInfo`.
- Host services `request_gpu_buffer` allocates a Spectra-owned external Vulkan storage buffer and returns `resource_id`,
  `byte_size`, buffer `kind`, `handle_kind`, an OS external memory handle, and Vulkan device identity. Buffer kind values
  are `0` true `VolumeGrid` channel storage and `1` viewport voxel debug storage. Handle kinds are `1` opaque Win32
  handle and `2` opaque file descriptor.
- For viewport voxel debug storage, the producer may import that external memory into CUDA or another GPU runtime and
  write either compacted `uint32_t` viewport voxel cell indices or a dense bitfield, according to `source_kind`. For
  true volume channel storage, the producer may publish a `VolumeChannel` that references the returned `resource_id`
  with `source_kind = 1` and also exposes the producer-owned borrowed device pointer for static pathtracer snapshot
  materialization. The producer owns synchronization: GPU writes must be complete before the callback returning the
  corresponding scene document/frame or debug attachment returns. CPU voxel copies and semaphore synchronization are
  unsupported.
- Host services `release_gpu_buffer` releases a previously requested Spectra GPU resource. Producers must release
  imported GPU mappings and then release every requested resource before instance destruction.
- `SPECTRA_VOLUME_DEBUG=1` enables generic pathtracer volume diagnostics, including density range and approximate
  majorant statistics. This validates renderable volume data only; acceleration/debug data is not accepted there.
- Descriptor `create` returns a plugin-owned instance pointer.
- Descriptor `destroy` releases that instance.
- Descriptor `update` receives a `SpectraSceneUpdateInfo` once per GUI frame. `wall_delta_seconds`
  is the elapsed host UI time. `scene_delta_seconds` is the delta that source-owned scene work may consume; it is `0`
  when the host timeline is paused. `timeline_playing`, `time_seconds`, and `frame_index` describe the host timeline
  state. Long-running source updates must honor `scene_delta_seconds` so Space pause/resume controls stay synchronized
  with source actions and preview availability.
- Descriptor `document` returns static scene data as named spans: materials, lights, cameras, static renderable
  entities, and static debug attachments.
- Cameras are optional scene items. If a plugin provides no cameras, Spectra injects a
  host-owned `Spectra Inspector Camera`. If a plugin provides exactly one camera, `active_camera_name` may be empty and
  that camera becomes active. Plugins that provide multiple cameras must declare `active_camera_name`.
- Descriptor `frame` returns dynamic named spans for the requested frame cursor: dynamic cameras, renderable entities,
  and debug attachments.
- Frame items with the same name as document items override the document item for that frame. A plugin can publish a
  static object in `document`, then publish the same named object with a changed transform in `frame` to express dynamic
  transform animation without a separate transform callback ABI.
- Descriptor `last_error` returns the most recent instance-local error string; it may be empty. Controls callbacks use
  the same instance error channel.
- All descriptor function pointers listed in this section are mandatory for ABI v9. Missing callbacks are rejected
  during plugin inspection/loading.
- Descriptor `scene_revision` returns a non-zero plugin-owned scene data revision. It must increase whenever
  `document` or `frame` would publish changed scene entities or debug attachments. Controls-only UI changes such as
  state metrics must not increment it.
- Descriptor `control_action` executes one plugin-declared action with strict key/value options.
- Descriptor `control_setting_update` applies one changed setting key/value pair; it must validate the value strictly and increase
  `scene_revision` if the change modifies scene entities or debug attachments.
- Descriptor `control_state` returns phase/headline/detail, metrics, and action states as named spans.

Callbacks must not throw across the ABI. Return `SPECTRA_SCENE_RESULT_ERROR` and expose a message through
`last_error`.

## License

Spectra is distributed under the GNU General Public License v2. See [LICENSE](LICENSE).
