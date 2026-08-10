# Spectra 功能完整性整改方案与实施记录

## 1. 文档目的

本文档记录本轮发布前功能审计的最终架构方案、实施边界和验证标准。整改只服务于个人 GPU 研究工作流，不增加兼容层、CPU 回退、冗余 validation 或与实际研究无关的发布设施。

最终目标如下：

1. Rasterizer 与 Path Tracer 是同一层级的两个 Renderer，消费同一个 Scene、Camera、Film 和动态 GPU 场景状态。
2. Renderer 原始输出只包含场景渲染结果。Diagnostics、Dynamic Visualization 和 Editor Overlay 不属于 Rasterizer 或 Path Tracer。
3. 在 Renderer 输出之外提供独立的 Composed 输出，把显示变换、Diagnostics、Visualization 和按需启用的 Overlay 合成为最终视口图像。
4. Headless 与 GUI 使用相同的 Renderer 和组合管线；同一个冻结动态帧、同一相机和同一输出层应得到相同结果。
5. 动态模拟、优化、重建、场景资源、Bounds、Debug 数据和 Telemetry 默认且允许始终驻留 GPU，不为纯 CPU Provider 设计回退路径。
6. 动态 GPU 场景不通过完整资源的 CPU 回读维持 Scene 镜像。Host 只允许读取导航或 UI 真正需要的少量归约结果。
7. Path Tracer 算法修改必须极其谨慎，只修复有直接证据的问题，不以结构重构为由改写 BSDF、MIS、介质积分或光谱采样算法。

## 2. Renderer 与最终输出分层

### 2.1 Renderer 是同一抽象层级

`Rasterizer` 与 `PathTracer` 都实现公共 Renderer 契约：

```text
Scene + Dynamic Scene State + Camera + Film
                    |
          +---------+---------+
          |                   |
          v                   v
     Rasterizer          Path Tracer
          |                   |
          +---------+---------+
                    |
                    v
        scene-linear Renderer output
```

两者输出相同 working color space 下的 scene-linear 图像。差异只来自渲染算法和明确声明的实时预览能力，不来自不同的场景模型、曝光策略或输出层级。

Diagnostics 不属于 Rasterizer。把 Diagnostics 绘制在 Raster 视口上只是一种最终组合方式，不代表它是 Rasterizer 的渲染结果。Path Tracer 也不负责 Diagnostics。

### 2.2 三种输出层

公共 `RenderOutputLayer` 提供三种明确输出：

- `RendererLinear`：Renderer 的 scene-linear working-space 输出，用于 EXR、研究数据和 GBuffer 对照。
- `RendererDisplay`：Renderer 结果经过统一显示变换后的 display-referred 图像，不包含 Diagnostics、Visualization 或 Overlay。
- `ComposedDisplay`：Renderer 显示结果与所选 Diagnostics、Visualization、选中轮廓、坐标轴等内容合成后的最终图像。

组合管线为：

```text
Renderer scene-linear output
            |
            v
 Display transform / tone mapping
            |
            +---- Scene Diagnostics
            +---- Dynamic Visualization
            +---- Optional editor overlays
            |
            v
   Composed display-referred output
```

Headless 可通过 `--output-layer` 和 `--composition` 选择与 GUI 相同的输出层，而不是把 GUI 截图逻辑复制到另一套代码中。

## 3. 动态 GPU 场景模型

### 3.1 GPU 常驻原则

所有动态 Provider 均按 GPU 项目设计：

- 模拟、优化和重建状态驻留 Provider GPU 显存。
- Scene geometry、Instance transform、Volume、Bounds 和 Debug Visualization 通过 Vulkan/CUDA external memory 发布。
- Host 不回读完整顶点、粒子、线段、体素或图像以维护 CPU Scene 副本。
- 不提供 CPU Dataset、CPU LineSet 或 CPU Volume 回退。
- CUDA device 必须与 Vulkan UUID/LUID 完全匹配，不匹配直接报错。

### 3.2 DynamicFrame 原子发布

一次动态帧是一个完整事务：

```text
DynamicFrame
├── SceneState
│   ├── TriangleMesh
│   ├── SphereSet
│   ├── InstanceTransformSet
│   ├── Volume Field
│   └── SceneBoundsSet
├── VisualizationState
│   ├── Point / Segment / Curve / Vector
│   ├── Surface / Field slice
│   └── Image / Camera observation
├── TelemetryState
│   ├── typed metrics
│   └── phase / headline / message
└── PresentationState
    ├── simulation step and time
    └── presentation frame and time
```

发布过程：

1. Host 建立临时 publication transaction。
2. Provider 获取当前 frame slot 的 external buffer、timeline semaphore 及 release/signal 值。
3. Provider 在 CUDA stream 等待 Host release，写入 GPU 数据并 signal。
4. Dataset commit 只进入临时事务，不覆盖当前可见帧。
5. 任一 Dataset grow、配置失败或 Provider 错误都会 abort 整个事务。
6. Scene、Visualization、Telemetry 和 Presentation 全部成功后一次性切换为新帧。

同一 Dataset 在一个 transaction 中只允许 commit 一次。Reconfigure 完整成功后才交换新旧资源；旧资源按 frame serial 延迟销毁。

### 3.3 强类型 Dataset

删除同时容纳所有字段的历史 mega-struct，使用明确类型：

- `TriangleMesh`：顶点/索引容量和 attribute mask。
- `SphereSet`：球心、半径及可选属性。
- `InstanceTransformSet`：实例变换。
- `SceneBoundsSet`：局部或世界 Bounds。
- `PointSet`、`SegmentSet`、`CurveSet`、`VectorSet`：Debug Visualization 基础数据。
- `Field`：分辨率、通道、origin、basis/spacing 和物理 domain。
- `Image`：extent、format、primaries 和 transfer function。
- `CameraObservationSet`：相机观测及其图像、内外参和畸变信息。
- `TransformSet`：Visualization 使用的通用 GPU 变换。

Scene binding 与 Visualization binding 在类型上分开。真实场景对象只能绑定 Scene Dataset；PointSet 等 Debug 数据不会被伪装成场景几何。

## 4. 动态 Bounds 方案

CPU Scene bounds 不应通过回读动态几何来更新。动态 Bounds 使用专用 `SceneBoundsSet`：

```text
Dynamic Mesh / SphereSet / Instance / Volume
                    |
                    | Provider GPU reduction
                    v
       SceneBoundsSet (Local or World)
                    |
                    v
 Renderer + Diagnostics + Navigation + Picking
```

具体原则：

- Provider 在 GPU 上归约动态资源 Bounds。
- Bounds 与对应几何在同一个 DynamicFrame transaction 中发布，因此 Renderer 和 Diagnostics 不会消费不同 revision。
- `Local` Bounds 搭配同帧 transform 使用；`World` Bounds 直接表示最终空间范围。
- Renderer、Diagnostics、Picker 和动态光源构建直接消费 GPU Bounds。
- GUI 导航若必须获得数值，只异步读取最终几个 `Bounds3`，不回读完整动态资源。
- Frozen snapshot 物化 Bounds 与对应资源，使 Headless 不依赖再次运行 Provider 才能复现范围。

因此，这一方案与“所有动态研究资源都在 GPU”完全一致；它修复的是 Bounds 的数据通路，而不是引入 CPU 场景镜像。

## 5. Plugin SDK 19

SDK 最终版本为 19：

- CMake：`find_package(SpectraPluginSDK 19 CONFIG REQUIRED)`。
- C ABI 入口：`spectra_plugin_api_19`。
- 所有跨 ABI 回调均为 `noexcept`，错误通过 `SpectraPluginResult` 返回。
- Provider `destroy` 返回前必须同步完成所有仍访问 Host external resource 的 GPU work。
- Linux CUDA opaque FD 在成功导入后由 CUDA 接管；Windows handle 按 Vulkan/CUDA 平台所有权规则持有。
- 每个 Dataset 按 Vulkan frames-in-flight 配置独立 external buffer 和 timeline semaphore。
- `SpectraPluginSegment` 固定 48 字节并保留逐段 RGBA、像素宽度和 flags。

参数元数据支持 section、description、step、Live 和 ResetRequired。Telemetry 支持整数、浮点、向量、文本、单位、分组和历史绘图标记。

`tick_presentation(provider, elapsed_seconds)` 独立于全局仿真暂停，用于缓存轨迹播放、循环和 FPS 控制。Live 参数应用后立即重新发布当前 simulation step，使暂停状态下的显示参数即时生效。

## 6. GPU 资源所有权与生命周期

### 6.1 Descriptor

`DescriptorHandle` 仅表示 GPU ABI index，不承担所有权。Host 使用 move-only `DescriptorLease`：

- 默认无效。
- 析构自动归还 descriptor。
- 可转入 frame retirement。
- allocator 只在成功分配后推进 index。
- GpuScene、Renderer、Diagnostics、Visualization、Picker、Display 和 ImGui 统一持有 lease。

### 6.2 Frame serial

资源销毁依据已提交/已完成 frame serial，而不是仅依据循环 frame slot：

- in-flight descriptor、buffer、image、pipeline 和 external interop resource 在 GPU 完成前不会释放。
- resize、scene replace、Renderer 切换和 Provider reconfigure 使用 deferred retirement。
- Runtime shutdown 统一等待设备；析构路径不允许 Vulkan RAII 异常覆盖原始错误。

### 6.3 异常安全构造

GpuScene、Renderer scene session 和 Dataset configuration 均先在临时 state 中完整创建，成功后 swap。禁止先污染当前 state 再尝试恢复。

## 7. Scene 与 Renderer 功能修复

### 7.1 Scene/GpuScene

- 修复 empty Bounds、transform 和 include 语义。
- standalone Volume 进入 Scene bounds 并成为稳定 Scene entity。
- 删除历史 `Primitive::volume` 双重对象模型。
- 动态 TriangleMesh 可只更新顶点而保持静态 topology。
- Texture 按 ID/revision 更新并延迟回收旧 GPU 资源。
- 动态 emissive geometry 更新时同步刷新面积、功率、CDF/BVH 和 MIS PDF 数据。
- RGBGrid 的 emission 与 sigma-a/s 独立。
- Film crop 完整贯穿 Renderer、Display、Capture 和 GBuffer。

### 7.2 Rasterizer

- Rasterizer 不再构造或依赖 `PathTracerResources`。
- 使用公共 sampling resources 和公共场景 GPU 状态。
- 修复 RGBGrid 缺省字段、颜色空间、PortalInfinite 背景、SphereSet 非均匀变换、正交相机方向、alpha、动态 transform、动态 emissive light 和 volume revision 更新。
- Point/Sphere debug glyph、宽线 clip、scalar style、field physical domain 和 image composition domain 保持统一。
- 体积步长由 voxel spacing、volume transform 和有效 ray interval 决定。
- Procedural cloud 使用完整参数。
- 体积直接光照保留表面 ray-query 遮挡，但不在每个主射线采样点再次执行完整体积阴影 march，避免二次方步进导致 Windows GPU TDR。
- camera medium、homogeneous boundary medium 和 NanoVDB 是 Path-only 能力，Rasterizer 明确报错，不静默忽略或伪装等价。

### 7.3 Path Tracer 谨慎修改边界

本轮没有重写 Path Tracer 积分器。修改仅限已经证明存在问题的外围一致性：

- Renderer scene-linear 输出与统一曝光契约。
- dynamic emissive light table。
- SphereSet area emitter。
- Film crop 和 pixel-count 宽度。
- shader loading。
- preparation future 生命周期及异常传播。
- Descriptor、GpuScene 和 in-flight resource RAII。
- 与动态场景 revision 一致的资源更新。

未因本轮架构整改改写：

- BSDF 采样和求值。
- MIS 权重算法。
- spectral wavelength sampling。
- homogeneous/heterogeneous medium stack 和积分逻辑。
- reconstruction filter。
- wavefront path integration 主算法。

以后这些算法只有在具备独立复现场景、对照结果和明确错误证据时才允许修改。

## 8. Diagnostics、Visualization 与 Overlay

三者均位于 Renderer 之外：

- Diagnostics：Scene bounds、Camera frustum、不同 Light 图标、Area Emitter、Volume bounds、medium boundary 等 Scene 诊断。
- Dynamic Visualization：Point、Segment、Curve、Vector、Surface、Field slice、velocity field、Image 和 Camera observation。
- Editor Overlay：选中轮廓、操纵器、坐标轴和编辑器专属内容。

Camera、Light、Volume、Instance 和 Area Emitter 使用稳定 entity ID。Active camera 通过 ID 判断，不比较 transform。Volume bounds 可拾取；SphereSet 的 emitter 和 medium-boundary diagnostics 与 Mesh 使用一致语义。

ParticleSet/SphereSet 的归类依据用途而不是底层 record：

- 参与遮挡、材质、光照、Path Tracing 或真实 Scene bounds 时属于 Scene `SphereSet`。
- 仅用于研究观察的粒子点、采样点或轨迹节点属于 Visualization `PointSet`。
- 两者可以来自同一份 GPU 研究数据，但必须通过不同的强类型 binding 明确表达语义。

## 9. Headless、GUI 与 Frozen Snapshot

Headless 与 GUI 共享：

- Scene loader。
- Dynamic Provider runtime。
- GpuScene。
- RenderEngine。
- Diagnostics/Visualization composition。
- Capture 编码。

Headless 请求可指定：

- Renderer 类型。
- Raster display mode。
- `RendererLinear`、`RendererDisplay` 或 `ComposedDisplay`。
- composition 内容。
- simulation target step/time。
- presentation target frame/time。
- Diagnostics、Visualization、axes 和 selected outline。
- GBuffer 与 Telemetry 输出路径。

Frozen snapshot 保存完整 DynamicFrame，并将 GPU Scene 数据、Bounds、Visualization、Telemetry 和 Presentation timeline 物化为可独立载入的 bundle。冻结后 Headless 不需要原 Provider DLL，也不需要重新运行模拟即可复现对应帧。

这里的“Headless 与 GUI 等价”不是要求 Renderer 原始输出包含 Diagnostics，而是要求两端在选择相同输出层时走同一套管线：

- 选择 Renderer 输出时，两端都只得到 Renderer 场景结果。
- 选择 Composed 输出时，两端都能得到包含 Diagnostics/Visualization/Overlay 的最终结果。

## 10. diff-solver 迁移

五个外部动态场景均迁移到 API 19：

| 场景 | GPU 动态输出 | 固定时钟 |
|---|---|---|
| cloth/forward | Scene TriangleMesh + Debug Segment/Vector | 1/240 s |
| cloth/stretch_stiffness_inverse | Debug SegmentSet | 1/60 s |
| cloth/wind_trajectory_optimization | Debug SegmentSet + presentation trajectory | 1/60 s |
| smoke/forward | Density Field + emitter visualization | 1/120 s |
| smoke/wind_trajectory_optimization | RGB Field + wind visualization + presentation trajectory | 1/60 s |

迁移要求：

- 删除旧根级 `plugin.ixx`、v21 SceneBuilder、ControlBuilder、HostServices 和旧 CUDA event ABI。
- Provider 在 GPU/interop 端口完成配置后、`reset` 阶段才构造 CUDA Simulation/Task。
- 保留五个 Provider ID，并输出精确的 `<provider-id>.spectra-plugin.dll`。
- `.spectra` 静态声明 Scene、Camera、Material、Dataset binding 和 Clock。
- Provider 只负责计算、参数、Telemetry 和 GPU 数据发布。
- smoke wind 保留独立 trajectory 播放、循环、FPS 和手动修改帧后暂停自动播放的行为。

## 11. 构建与安装

- C++ 23 modules。
- CMake 4.4。
- Vulkan 1.4 RAII。
- Slang shaders。
- Release 构建使用 Ninja 和 30 线程。
- 默认安装到项目根目录的 `install`，不污染系统目录。
- 安装内容包含 `spectra.exe`、全部运行时 shader/spectral/sampling 资源和 Plugin SDK CMake package。
- 不安装 `share/doc`。

外部项目使用：

```cmake
find_package(SpectraPluginSDK 19 CONFIG REQUIRED)
target_link_libraries(my_plugin PRIVATE SpectraPluginSDK::contract)
```

并通过 `CMAKE_PREFIX_PATH` 或 `SpectraPluginSDK_DIR` 指向 `install`。

## 12. 最终验证标准与结果

必须满足：

1. `git diff --check` 无错误。
2. 不存在 API 14–18、旧入口、旧 Dataset enum、`render_common` 或 Rasterizer/PathTracerResources 耦合残留。
3. Spectra 使用 Release、30 线程完整构建并安装成功。
4. 独立外部 CMake consumer 仅通过安装 prefix 成功执行 `find_package(SpectraPluginSDK 19 CONFIG REQUIRED)` 并构建。
5. 静态 Raster、静态 Path、Renderer PNG、linear EXR 和 GBuffer 输出成功。
6. 五个 diff-solver bundle 全部构建，动态起始帧、指定 simulation step、presentation frame、Composed 输出和 Telemetry 成功。
7. cloth 动态 Scene 使用 Path Tracer 成功。
8. Dense/Procedural Volume 使用 Raster 成功且不触发 device lost/TDR。
9. 含 homogeneous boundary medium 的场景由 Raster 明确拒绝，并由 Path Tracer 正常渲染。
10. 14 个 spectra-showcases 场景均使用格式 28。
11. 重复 Reset、Step、Play、Provider destroy 和程序退出不出现 CUDA/Vulkan timeline 或 in-flight lifetime 错误。
12. README 不因本轮整改发生修改。

当前实施结果已通过以上构建和 Headless 端到端验证。GUI 与 Headless 共用的渲染和组合代码已覆盖；人工 GUI 交互仍用于视觉操作体验检查，不作为另一套功能实现。

## 13. 有意保留的能力边界

以下不是未完成兼容项，而是明确设计边界：

- Rasterizer 是实时预览 Renderer，不复制 Path Tracer 的完整介质积分器。
- Camera medium、homogeneous boundary medium 和 NanoVDB 必须使用 Path Tracer。
- Raster volume direct lighting 使用实时单次散射近似；完整多次散射和体积自阴影由 Path Tracer 负责。
- 不支持 CPU Provider、CPU visualization fallback 或跨 GPU 隐式复制。
- 不保留 Plugin API 14–18、旧 ABI 或旧 scene format 兼容层。

任何超出这些边界的输入都应明确报错，不以近似成功或静默忽略掩盖能力缺口。
