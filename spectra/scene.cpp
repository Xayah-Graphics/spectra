#include <cctype>
#include <cstdlib>
#include <format>
#include <mutex>
#include <spectra/pathtracer/base/material.h>
#include <spectra/pathtracer/base/shape.h>
#include <spectra/pathtracer/core/options.h>
#include <spectra/pathtracer/core/paramdict.h>
#include <spectra/pathtracer/gpu/memory.h>
#include <spectra/pathtracer/util/color.h>
#include <spectra/pathtracer/util/colorspace.h>
#include <spectra/pathtracer/util/file.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/mesh.h>
#include <spectra/pathtracer/util/parallel.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/string.h>
#include <spectra/pathtracer/util/transform.h>
#include <spectra/scene.h>
#include <spectra/scene_builtin.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace spectra {
    void ParsedParameter::AddFloat(Float v) {
        SPECTRA_CHECK(ints.empty() && strings.empty() && bools.empty());
        floats.push_back(v);
    }

    void ParsedParameter::AddInt(int i) {
        SPECTRA_CHECK(floats.empty() && strings.empty() && bools.empty());
        ints.push_back(i);
    }

    void ParsedParameter::AddString(std::string_view str) {
        SPECTRA_CHECK(floats.empty() && ints.empty() && bools.empty());
        strings.push_back({str.begin(), str.end()});
    }

    void ParsedParameter::AddBool(bool v) {
        SPECTRA_CHECK(floats.empty() && ints.empty() && strings.empty());
        bools.push_back(v);
    }


} // namespace spectra

namespace spectra::scene {
    InternCache<std::string> SceneEntity::internedStrings(Allocator{});

    static std::string NormalizeSceneOptionName(const std::string& str) {
        std::string ret;
        for (unsigned char c : str) {
            if (c != '_' && c != '-') ret += std::tolower(c);
        }
        return ret;
    }

    [[nodiscard]] ParsedParameter* MakeIntegerParameter(std::string_view name, int value, const FileLoc& location) {
        ParsedParameter* parameter = new ParsedParameter(location);
        parameter->type            = "integer";
        parameter->name            = std::string{name};
        parameter->AddInt(value);
        return parameter;
    }

    [[nodiscard]] ParsedParameterVector ApplyFilmResolutionOverride(ParsedParameterVector parameters, Point2i resolution, const FileLoc& location) {
        if (resolution.x <= 0 || resolution.y <= 0) throw std::runtime_error(spectra::diagnostics::Format(&location, "Spectra interactive film resolution must be positive."));
        for (auto iterator = parameters.begin(); iterator != parameters.end();) {
            ParsedParameter* parameter = *iterator;
            if (parameter == nullptr) throw std::runtime_error(spectra::diagnostics::Format(&location, "Film parameter list contains a null parameter."));
            if (parameter->name == "xresolution" || parameter->name == "yresolution") {
                delete parameter;
                iterator = parameters.erase(iterator);
            } else
                ++iterator;
        }
        parameters.push_back(MakeIntegerParameter("xresolution", resolution.x, location));
        parameters.push_back(MakeIntegerParameter("yresolution", resolution.y, location));
        return parameters;
    }

    SceneBuilder::SceneBuilder(Scene* scene) : scene(scene), transformCache(Allocator(&CUDATrackedMemoryResource::singleton)) {
        camera.name      = SceneEntity::internedStrings.Lookup("perspective");
        sampler.name     = SceneEntity::internedStrings.Lookup("zsobol");
        filter.name      = SceneEntity::internedStrings.Lookup("gaussian");
        integrator.name  = SceneEntity::internedStrings.Lookup("volpath");
        accelerator.name = SceneEntity::internedStrings.Lookup("bvh");

        film.name       = SceneEntity::internedStrings.Lookup("rgb");
        film.parameters = ParameterDictionary({}, RGBColorSpace::sRGB);

        ParameterDictionary dict({}, RGBColorSpace::sRGB);
        graphicsState.currentMaterialIndex = scene->AddMaterial({SceneEntity::internedStrings.Lookup("diffuse"), {}, dict});
    }

    SceneBuilder::SceneBuilder(Scene* scene, Point2i filmResolutionOverride) : SceneBuilder(scene) {
        if (filmResolutionOverride.x <= 0 || filmResolutionOverride.y <= 0) throw std::runtime_error(spectra::diagnostics::Format("Spectra interactive film resolution must be positive."));
        this->filmResolutionOverride = filmResolutionOverride;
    }

    void SceneBuilder::RequireOptions(std::string_view command, const FileLoc& loc) const {
        if (currentPhase == ScenePhase::World) throw std::runtime_error(spectra::diagnostics::Format(&loc, "Options cannot be set inside world block; \"%s\" is not allowed.", std::string(command)));
    }

    void SceneBuilder::RequireWorld(std::string_view command, const FileLoc& loc) const {
        if (currentPhase == ScenePhase::Options) throw std::runtime_error(spectra::diagnostics::Format(&loc, "Scene command must be inside world block; \"%s\" is not allowed.", std::string(command)));
    }

    void SceneBuilder::ReverseOrientation(FileLoc loc) {
        RequireWorld("ReverseOrientation", loc);
        graphicsState.reverseOrientation = !graphicsState.reverseOrientation;
    }

    void SceneBuilder::ColorSpace(const std::string& name, FileLoc loc) {
        if (const RGBColorSpace* cs = RGBColorSpace::GetNamed(name))
            graphicsState.colorSpace = cs;
        else
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: color space unknown", name));
    }

    void SceneBuilder::Identity(FileLoc loc) {
        graphicsState.ForActiveTransforms([](auto t) { return spectra::Transform(); });
    }

    void SceneBuilder::Translate(Float dx, Float dy, Float dz, FileLoc loc) {
        graphicsState.ForActiveTransforms([=](auto t) { return t * spectra::Translate(Vector3f(dx, dy, dz)); });
    }

    void SceneBuilder::CoordinateSystem(const std::string& origName, FileLoc loc) {
        std::string name             = NormalizeUTF8(origName);
        namedCoordinateSystems[name] = graphicsState.ctm;
    }

    void SceneBuilder::CoordSysTransform(const std::string& origName, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);
        if (namedCoordinateSystems.find(name) != namedCoordinateSystems.end())
            graphicsState.ctm = namedCoordinateSystems[name];
        else
            spectra::diagnostics::PrintWarning(&loc, "Couldn't find named coordinate system \"%s\"", name);
    }

    void SceneBuilder::Camera(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);

        RequireOptions("spectra::Camera", loc);

        TransformSet cameraFromWorld     = graphicsState.ctm;
        TransformSet worldFromCamera     = Inverse(graphicsState.ctm);
        namedCoordinateSystems["camera"] = Inverse(cameraFromWorld);

        CameraTransform cameraTransform(AnimatedTransform(worldFromCamera[0], graphicsState.transformStartTime, worldFromCamera[1], graphicsState.transformEndTime));
        renderFromWorld = cameraTransform.RenderFromWorld();

        camera = {{SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)}, cameraTransform, graphicsState.currentOutsideMedium};
    }

    void SceneBuilder::AttributeBegin(FileLoc loc) {
        RequireWorld("AttributeBegin", loc);
        pushedGraphicsStates.push_back(graphicsState);
        pushStack.push_back({ScopeKind::Attribute, loc});
    }

    void SceneBuilder::AttributeEnd(FileLoc loc) {
        RequireWorld("AttributeEnd", loc);
        if (pushedGraphicsStates.empty()) throw std::runtime_error(spectra::diagnostics::Format(&loc, "Unmatched AttributeEnd encountered. Ignoring it."));

        graphicsState = std::move(pushedGraphicsStates.back());
        pushedGraphicsStates.pop_back();

        if (pushStack.back().kind == ScopeKind::Object)
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "Mismatched nesting: open ObjectBegin from %s at AttributeEnd", std::format("{}:{}:{}", pushStack.back().loc.filename, pushStack.back().loc.line, pushStack.back().loc.column)));
        else
            SPECTRA_CHECK(pushStack.back().kind == ScopeKind::Attribute);
        pushStack.pop_back();
    }

    void SceneBuilder::Attribute(const std::string& target, ParsedParameterVector attrib, FileLoc loc) {
        ParsedParameterVector* currentAttributes = nullptr;
        if (target == "shape") {
            currentAttributes = &graphicsState.shapeAttributes;
        } else if (target == "light") {
            currentAttributes = &graphicsState.lightAttributes;
        } else if (target == "material") {
            currentAttributes = &graphicsState.materialAttributes;
        } else if (target == "medium") {
            currentAttributes = &graphicsState.mediumAttributes;
        } else if (target == "texture") {
            currentAttributes = &graphicsState.textureAttributes;
        } else {
            throw std::runtime_error(spectra::diagnostics::Format(&loc,
                "Unknown attribute target \"%s\". Must be \"shape\", \"light\", "
                "\"material\", \"medium\", or \"texture\".",
                target));
        }

        for (ParsedParameter* p : attrib) {
            p->mayBeUnused = true;
            p->colorSpace  = graphicsState.colorSpace;
            currentAttributes->push_back(p);
        }
    }

    void SceneBuilder::Sampler(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        RequireOptions("spectra::Sampler", loc);
        sampler = {SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)};
    }

    void SceneBuilder::WorldBegin(FileLoc loc) {
        RequireOptions("WorldBegin", loc);
        if (filmResolutionOverride.has_value() && !filmSeen) Film("rgb", ParsedParameterVector{}, loc);
        currentPhase = ScenePhase::World;
        for (int i = 0; i < MaxTransforms; ++i) graphicsState.ctm[i] = spectra::Transform();
        graphicsState.activeTransformBits = AllTransformsBits;
        namedCoordinateSystems["world"]   = graphicsState.ctm;

        scene->SetOptions(filter, film, camera, sampler, integrator, accelerator);
    }

    void SceneBuilder::MakeNamedMedium(const std::string& origName, ParsedParameterVector params, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);
        if (mediumNames.find(name) != mediumNames.end()) {
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "Named medium \"%s\" redefined.", name));
        }
        mediumNames.insert(name);

        ParameterDictionary dict(std::move(params), graphicsState.mediumAttributes, graphicsState.colorSpace);
        scene->AddMedium({{{SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)}, RenderFromObject()}});
    }

    void SceneBuilder::LightSource(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        RequireWorld("LightSource", loc);
        ParameterDictionary dict(std::move(params), graphicsState.lightAttributes, graphicsState.colorSpace);
        scene->AddLight({{{SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)}, RenderFromObject()}, graphicsState.currentOutsideMedium});
    }

    void SceneBuilder::Shape(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        RequireWorld("spectra::Shape", loc);

        ParameterDictionary dict(std::move(params), graphicsState.shapeAttributes, graphicsState.colorSpace);

        int areaLightIndex = -1;
        if (!graphicsState.areaLightName.empty()) {
            areaLightIndex = scene->AddAreaLight({SceneEntity::internedStrings.Lookup(graphicsState.areaLightName), graphicsState.areaLightLoc, graphicsState.areaLightParams});
            if (activeInstanceDefinition) spectra::diagnostics::PrintWarning(&loc, "Area lights not supported with object instancing");
        }

        if (CTMIsAnimated()) {
            AnimatedTransform renderFromShape  = RenderFromObject();
            const spectra::Transform* identity = transformCache.Lookup(spectra::Transform());

            AnimatedShapeSceneEntity entity{{{SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)}, renderFromShape}, identity, graphicsState.reverseOrientation, graphicsState.currentMaterialIndex, graphicsState.currentMaterialName, areaLightIndex, graphicsState.currentInsideMedium, graphicsState.currentOutsideMedium};

            if (activeInstanceDefinition)
                activeInstanceDefinition->animatedShapes.push_back(std::move(entity));
            else
                scene->AddAnimatedShape(std::move(entity));
        } else {
            const spectra::Transform* renderFromObject = transformCache.Lookup(RenderFromObject(0));
            const spectra::Transform* objectFromRender = transformCache.Lookup(spectra::Inverse(*renderFromObject));

            ShapeSceneEntity entity{{SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)}, renderFromObject, objectFromRender, graphicsState.reverseOrientation, graphicsState.currentMaterialIndex, graphicsState.currentMaterialName, areaLightIndex, graphicsState.currentInsideMedium, graphicsState.currentOutsideMedium};
            if (activeInstanceDefinition)
                activeInstanceDefinition->shapes.push_back(std::move(entity));
            else
                shapes.push_back(std::move(entity));
        }
    }

    void SceneBuilder::ObjectBegin(const std::string& origName, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);

        RequireWorld("ObjectBegin", loc);
        pushedGraphicsStates.push_back(graphicsState);

        pushStack.push_back({ScopeKind::Object, loc});

        if (activeInstanceDefinition) {
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "ObjectBegin called inside of instance definition"));
        }

        if (instanceNames.find(name) != instanceNames.end()) {
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: trying to redefine an object instance", name));
        }
        instanceNames.insert(name);

        activeInstanceDefinition = InstanceDefinitionSceneEntity{
            .name = SceneEntity::internedStrings.Lookup(name),
            .loc  = loc,
        };
    }

    void SceneBuilder::ObjectEnd(FileLoc loc) {
        RequireWorld("ObjectEnd", loc);
        if (!activeInstanceDefinition) throw std::runtime_error(spectra::diagnostics::Format(&loc, "ObjectEnd called outside of instance definition"));

        graphicsState = std::move(pushedGraphicsStates.back());
        pushedGraphicsStates.pop_back();

        if (pushStack.back().kind == ScopeKind::Attribute)
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "Mismatched nesting: open AttributeBegin from %s at ObjectEnd", std::format("{}:{}:{}", pushStack.back().loc.filename, pushStack.back().loc.line, pushStack.back().loc.column)));
        else
            SPECTRA_CHECK(pushStack.back().kind == ScopeKind::Object);
        pushStack.pop_back();

        scene->AddInstanceDefinition(std::move(*activeInstanceDefinition));
        activeInstanceDefinition.reset();
    }

    void SceneBuilder::ObjectInstance(const std::string& origName, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);
        RequireWorld("ObjectInstance", loc);

        if (activeInstanceDefinition) throw std::runtime_error(spectra::diagnostics::Format(&loc, "ObjectInstance can't be called inside instance definition"));

        spectra::Transform worldFromRender = spectra::Inverse(renderFromWorld);

        if (CTMIsAnimated()) {
            AnimatedTransform animatedRenderFromInstance(RenderFromObject(0) * worldFromRender, graphicsState.transformStartTime, RenderFromObject(1) * worldFromRender, graphicsState.transformEndTime);

            if (animatedRenderFromInstance.IsAnimated()) {
                AnimatedTransform* renderFromInstanceAnim = new AnimatedTransform(animatedRenderFromInstance);
                SPECTRA_CHECK(renderFromInstanceAnim->IsAnimated());
                instanceUses.push_back({
                    .name                   = SceneEntity::internedStrings.Lookup(name),
                    .loc                    = loc,
                    .renderFromInstanceAnim = renderFromInstanceAnim,
                });
                return;
            }
        }

        const spectra::Transform* renderFromInstance = transformCache.Lookup(RenderFromObject(0) * worldFromRender);
        instanceUses.push_back({
            .name               = SceneEntity::internedStrings.Lookup(name),
            .loc                = loc,
            .renderFromInstance = renderFromInstance,
        });
    }

    void SceneBuilder::Finish() {
        if (currentPhase != ScenePhase::World) throw std::runtime_error(spectra::diagnostics::Format("Scene finished before \"WorldBegin\"."));

        while (!pushedGraphicsStates.empty()) {
            throw std::runtime_error(spectra::diagnostics::Format("Missing end to AttributeBegin"));
            pushedGraphicsStates.pop_back();
        }

        if (!shapes.empty()) scene->AddShapes(shapes);
        if (!instanceUses.empty()) scene->AddInstanceUses(instanceUses);
    }

    [[nodiscard]] const SceneInfo& SceneInfoFor(std::string_view name) {
        return BuiltinSceneInfoFor(name);
    }

    [[nodiscard]] BuiltScene BuildScene(std::string_view name, std::optional<Point2i> filmResolutionOverride) {
        BuiltScene built_scene{};
        built_scene.scene = std::make_unique<Scene>();
        if (filmResolutionOverride.has_value())
            built_scene.builder = std::make_unique<SceneBuilder>(built_scene.scene.get(), *filmResolutionOverride);
        else
            built_scene.builder = std::make_unique<SceneBuilder>(built_scene.scene.get());
        BuildBuiltinScene(name, *built_scene.builder);
        return built_scene;
    }

    void SceneBuilder::Option(const std::string& name, const std::string& value, FileLoc loc) {
        std::string nName = NormalizeSceneOptionName(name);

        if (nName == "disablepixeljitter") {
            if (value == "true")
                Options->disablePixelJitter = true;
            else if (value == "false")
                Options->disablePixelJitter = false;
            else
                throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: expected \"true\" or \"false\" for option value", value));
        } else if (nName == "disabletexturefiltering") {
            if (value == "true")
                Options->disableTextureFiltering = true;
            else if (value == "false")
                Options->disableTextureFiltering = false;
            else
                throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: expected \"true\" or \"false\" for option value", value));
        } else if (nName == "disablewavelengthjitter") {
            if (value == "true")
                Options->disableWavelengthJitter = true;
            else if (value == "false")
                Options->disableWavelengthJitter = false;
            else
                throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: expected \"true\" or \"false\" for option value", value));
        } else if (nName == "displacementedgescale") {
            if (!Atof(value, &Options->displacementEdgeScale)) throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: expected floating-point option value", value));
        } else if (nName == "rendercoordsys") {
            if (value.size() < 3 || value.front() != '"' || value.back() != '"') throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: expected quoted string for option value", value));
            std::string renderCoordSys = value.substr(1, value.size() - 2);
            if (renderCoordSys == "camera")
                Options->renderingSpace = RenderingCoordinateSystem::Camera;
            else if (renderCoordSys == "cameraworld")
                Options->renderingSpace = RenderingCoordinateSystem::CameraWorld;
            else if (renderCoordSys == "world")
                Options->renderingSpace = RenderingCoordinateSystem::World;
            else
                throw std::runtime_error(spectra::diagnostics::Format("%s: unknown rendering coordinate system.", renderCoordSys));
        } else if (nName == "seed") {
            Options->seed = std::atoi(value.c_str());
        } else
            throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: unknown option", name));

        CopyOptionsToGPU();
    }

    void SceneBuilder::Transform(Float tr[16], FileLoc loc) {
        graphicsState.ForActiveTransforms([=](auto t) { return Transpose(spectra::Transform(spectra::SquareMatrix<4>(pstd::MakeSpan(tr, 16)))); });
    }

    void SceneBuilder::ConcatTransform(Float tr[16], FileLoc loc) {
        graphicsState.ForActiveTransforms([=](auto t) { return t * Transpose(spectra::Transform(spectra::SquareMatrix<4>(pstd::MakeSpan(tr, 16)))); });
    }

    void SceneBuilder::Rotate(Float angle, Float dx, Float dy, Float dz, FileLoc loc) {
        graphicsState.ForActiveTransforms([=](auto t) { return t * spectra::Rotate(angle, Vector3f(dx, dy, dz)); });
    }

    void SceneBuilder::Scale(Float sx, Float sy, Float sz, FileLoc loc) {
        graphicsState.ForActiveTransforms([=](auto t) { return t * spectra::Scale(sx, sy, sz); });
    }

    void SceneBuilder::LookAt(Float ex, Float ey, Float ez, Float lx, Float ly, Float lz, Float ux, Float uy, Float uz, FileLoc loc) {
        spectra::Transform lookAt = spectra::LookAt(Point3f(ex, ey, ez), Point3f(lx, ly, lz), Vector3f(ux, uy, uz));
        graphicsState.ForActiveTransforms([=](auto t) { return t * lookAt; });
    }

    void SceneBuilder::ActiveTransformAll(FileLoc loc) {
        graphicsState.activeTransformBits = AllTransformsBits;
    }

    void SceneBuilder::ActiveTransformEndTime(FileLoc loc) {
        graphicsState.activeTransformBits = EndTransformBits;
    }

    void SceneBuilder::ActiveTransformStartTime(FileLoc loc) {
        graphicsState.activeTransformBits = StartTransformBits;
    }

    void SceneBuilder::TransformTimes(Float start, Float end, FileLoc loc) {
        RequireOptions("TransformTimes", loc);
        graphicsState.transformStartTime = start;
        graphicsState.transformEndTime   = end;
    }

    void SceneBuilder::PixelFilter(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        RequireOptions("PixelFilter", loc);
        filter = {SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)};
    }

    void SceneBuilder::Film(const std::string& type, ParsedParameterVector params, FileLoc loc) {
        RequireOptions("spectra::Film", loc);
        filmSeen = true;
        if (filmResolutionOverride.has_value()) params = ApplyFilmResolutionOverride(std::move(params), *filmResolutionOverride, loc);
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        film = {SceneEntity::internedStrings.Lookup(type), loc, std::move(dict)};
    }

    void SceneBuilder::Accelerator(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        RequireOptions("Accelerator", loc);
        accelerator = {SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)};
    }

    void SceneBuilder::Integrator(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);

        RequireOptions("Integrator", loc);
        integrator = {SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)};
    }

    void SceneBuilder::MediumInterface(const std::string& origInsideName, const std::string& origOutsideName, FileLoc loc) {
        std::string insideName  = NormalizeUTF8(origInsideName);
        std::string outsideName = NormalizeUTF8(origOutsideName);

        graphicsState.currentInsideMedium  = insideName;
        graphicsState.currentOutsideMedium = outsideName;
    }

    void SceneBuilder::Texture(const std::string& origName, const std::string& type, const std::string& texname, ParsedParameterVector params, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);
        RequireWorld("Texture", loc);

        ParameterDictionary dict(std::move(params), graphicsState.textureAttributes, graphicsState.colorSpace);

        if (type != "float" && type != "spectrum") throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: texture type unknown. Must be \"float\" or \"spectrum\".", type));

        std::set<std::string>& names = (type == "float") ? floatTextureNames : spectrumTextureNames;
        if (names.find(name) != names.end()) throw std::runtime_error(spectra::diagnostics::Format(&loc, "Redefining texture \"%s\".", name));
        names.insert(name);

        if (type == "float")
            scene->AddFloatTexture(name, {{{SceneEntity::internedStrings.Lookup(texname), loc, std::move(dict)}, RenderFromObject()}});
        else
            scene->AddSpectrumTexture(name, {{{SceneEntity::internedStrings.Lookup(texname), loc, std::move(dict)}, RenderFromObject()}});
    }

    void SceneBuilder::Material(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        RequireWorld("spectra::Material", loc);

        ParameterDictionary dict(std::move(params), graphicsState.materialAttributes, graphicsState.colorSpace);

        graphicsState.currentMaterialIndex = scene->AddMaterial({SceneEntity::internedStrings.Lookup(name), loc, std::move(dict)});
        graphicsState.currentMaterialName.clear();
    }

    void SceneBuilder::MakeNamedMaterial(const std::string& origName, ParsedParameterVector params, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);
        RequireWorld("MakeNamedMaterial", loc);

        ParameterDictionary dict(std::move(params), graphicsState.materialAttributes, graphicsState.colorSpace);

        if (namedMaterialNames.find(name) != namedMaterialNames.end()) throw std::runtime_error(spectra::diagnostics::Format(&loc, "%s: named material redefined.", name));
        namedMaterialNames.insert(name);

        scene->AddNamedMaterial(name, {SceneEntity::internedStrings.Lookup(""), loc, std::move(dict)});
    }

    void SceneBuilder::NamedMaterial(const std::string& origName, FileLoc loc) {
        std::string name = NormalizeUTF8(origName);
        RequireWorld("NamedMaterial", loc);
        graphicsState.currentMaterialName  = name;
        graphicsState.currentMaterialIndex = -1;
    }

    void SceneBuilder::AreaLightSource(const std::string& name, ParsedParameterVector params, FileLoc loc) {
        RequireWorld("AreaLightSource", loc);
        graphicsState.areaLightName   = name;
        graphicsState.areaLightParams = ParameterDictionary(std::move(params), graphicsState.lightAttributes, graphicsState.colorSpace);
        graphicsState.areaLightLoc    = loc;
    }

    void Scene::SetOptions(SceneEntity filter, SceneEntity film, CameraSceneEntity camera, SceneEntity sampler, SceneEntity integ, SceneEntity accel) {
        filmColorSpace = film.parameters.ColorSpace();
        integrator     = integ;
        accelerator    = accel;

        Allocator alloc = threadAllocators.Get();
        Filter filt     = Filter::Create(filter.name, filter.parameters, &filter.loc, alloc);

        Float exposureTime = camera.parameters.GetOneFloat("shutterclose", 1.f) - camera.parameters.GetOneFloat("shutteropen", 0.f);
        if (exposureTime <= 0)
            throw std::runtime_error(spectra::diagnostics::Format(&camera.loc, "The specified camera shutter times imply that the shutter "
                                                                               "does not open.  A black image will result."));

        this->film = Film::Create(film.name, film.parameters, exposureTime, camera.cameraTransform, filt, &film.loc, alloc);

        samplerJob = RunAsync([sampler, this]() {
            Allocator alloc = threadAllocators.Get();
            Point2i res     = this->film.FullResolution();
            return Sampler::Create(sampler.name, sampler.parameters, res, &sampler.loc, alloc);
        });

        cameraJob = RunAsync([camera, this]() {
            Allocator alloc     = threadAllocators.Get();
            Medium cameraMedium = GetMedium(camera.medium, &camera.loc);

            Camera c = Camera::Create(camera.name, camera.parameters, cameraMedium, camera.cameraTransform, this->film, &camera.loc, alloc);
            return c;
        });
    }

    Camera Scene::GetCamera() {
        cameraJobMutex.lock();
        while (!camera) {
            pstd::optional<Camera> c = cameraJob->TryGetResult(&cameraJobMutex);
            if (c) camera = *c;
        }
        cameraJobMutex.unlock();
        return camera;
    }

    Sampler Scene::GetSampler() {
        samplerJobMutex.lock();
        while (!sampler) {
            pstd::optional<Sampler> s = samplerJob->TryGetResult(&samplerJobMutex);
            if (s) sampler = *s;
        }
        samplerJobMutex.unlock();
        return sampler;
    }

    void Scene::AddMedium(MediumSceneEntity medium) {
        auto create = [medium, this]() {
            std::string type = medium.parameters.GetOneString("type", "");
            if (type.empty()) throw std::runtime_error(spectra::diagnostics::Format(&medium.loc, "No parameter \"string type\" found for medium."));
            if (medium.renderFromObject.IsAnimated())
                spectra::diagnostics::PrintWarning(&medium.loc, "Animated transformation provided for medium. Only the "
                                                                "start transform will be used.");

            return Medium::Create(type, medium.parameters, medium.renderFromObject.startTransform, &medium.loc, threadAllocators.Get());
        };

        std::lock_guard<std::mutex> lock(mediaMutex);
        mediumJobs[medium.name] = RunAsync(create);
    }

    Medium Scene::GetMedium(const std::string& name, const FileLoc* loc) {
        if (name.empty()) return nullptr;

        mediaMutex.lock();
        while (true) {
            if (auto iter = mediaMap.find(name); iter != mediaMap.end()) {
                Medium m = iter->second;
                mediaMutex.unlock();
                return m;
            } else {
                auto fiter = mediumJobs.find(name);
                if (fiter == mediumJobs.end()) throw std::runtime_error(spectra::diagnostics::Format(loc, "%s: medium is not defined.", name));

                pstd::optional<Medium> m = fiter->second->TryGetResult(&mediaMutex);
                if (m) {
                    mediaMap[name] = *m;
                    mediumJobs.erase(fiter);
                    mediaMutex.unlock();
                    return *m;
                }
            }
        }
    }

    std::map<std::string, Medium> Scene::CreateMedia() {
        mediaMutex.lock();
        if (!mediumJobs.empty()) {
            for (auto& m : mediumJobs) {
                while (mediaMap.find(m.first) == mediaMap.end()) {
                    pstd::optional<Medium> med = m.second->TryGetResult(&mediaMutex);
                    if (med) mediaMap[m.first] = *med;
                }
            }
            mediumJobs.clear();
        }
        mediaMutex.unlock();
        return mediaMap;
    }

    Scene::Scene()
        : threadAllocators([]() {
              pstd::pmr::monotonic_buffer_resource* resource = new pstd::pmr::monotonic_buffer_resource(1024 * 1024, &CUDATrackedMemoryResource::singleton);
              return Allocator(resource);
          }) {}

    void Scene::AddNamedMaterial(std::string name, SceneEntity material) {
        std::lock_guard<std::mutex> lock(materialMutex);
        startLoadingNormalMaps(material.parameters);
        namedMaterials.push_back(std::make_pair(std::move(name), std::move(material)));
    }

    int Scene::AddMaterial(SceneEntity material) {
        std::lock_guard<std::mutex> lock(materialMutex);
        startLoadingNormalMaps(material.parameters);
        materials.push_back(std::move(material));
        return int(materials.size() - 1);
    }

    void Scene::startLoadingNormalMaps(const ParameterDictionary& parameters) {
        std::string filename = ResolveFilename(parameters.GetOneString("normalmap", ""));
        if (filename.empty()) return;

        if (normalMapJobs.find(filename) != normalMapJobs.end()) return;

        auto create = [=, this](std::string filename) {
            Allocator alloc          = threadAllocators.Get();
            ImageAndMetadata immeta  = Image::Read(filename, Allocator(), ColorEncoding::Linear);
            Image& image             = immeta.image;
            ImageChannelDesc rgbDesc = image.GetChannelDesc({"R", "G", "B"});
            if (!rgbDesc) throw std::runtime_error(spectra::diagnostics::Format("%s: normal map image must contain R, G, and B channels", filename));
            Image* normalMap = alloc.new_object<Image>(alloc);
            *normalMap       = image.SelectChannels(rgbDesc);

            return normalMap;
        };
        normalMapJobs[filename] = RunAsync(create, filename);
    }

    void Scene::AddFloatTexture(std::string name, TextureSceneEntity texture) {
        if (texture.renderFromObject.IsAnimated())
            spectra::diagnostics::PrintWarning(&texture.loc, "Animated world to texture transforms are not supported. "
                                                             "Using start transform.");

        std::lock_guard<std::mutex> lock(textureMutex);
        if (texture.name != "imagemap" && texture.name != "ptex") {
            serialFloatTextures.push_back(std::make_pair(std::move(name), std::move(texture)));
            return;
        }

        std::string filename = ResolveFilename(texture.parameters.GetOneString("filename", ""));
        if (filename.empty()) throw std::runtime_error(spectra::diagnostics::Format(&texture.loc, "\"string filename\" not provided for image texture."));
        if (!FileExists(filename)) throw std::runtime_error(spectra::diagnostics::Format(&texture.loc, "%s: file not found.", filename));

        if (loadingTextureFilenames.find(filename) != loadingTextureFilenames.end()) {
            serialFloatTextures.push_back(std::make_pair(std::move(name), std::move(texture)));
            return;
        }
        loadingTextureFilenames.insert(filename);

        auto create = [=, this](TextureSceneEntity texture) {
            Allocator alloc = threadAllocators.Get();

            Transform renderFromTexture = texture.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&texture.parameters, nullptr);
            return FloatTexture::Create(texture.name, renderFromTexture, texDict, &texture.loc, alloc);
        };
        floatTextureJobs[name] = RunAsync(create, texture);
    }

    void Scene::AddSpectrumTexture(std::string name, TextureSceneEntity texture) {
        std::lock_guard<std::mutex> lock(textureMutex);

        if (texture.name != "imagemap" && texture.name != "ptex") {
            serialSpectrumTextures.push_back(std::make_pair(std::move(name), std::move(texture)));
            return;
        }

        std::string filename = ResolveFilename(texture.parameters.GetOneString("filename", ""));
        if (filename.empty()) throw std::runtime_error(spectra::diagnostics::Format(&texture.loc, "\"string filename\" not provided for image texture."));
        if (!FileExists(filename)) throw std::runtime_error(spectra::diagnostics::Format(&texture.loc, "%s: file not found.", filename));

        if (loadingTextureFilenames.find(filename) != loadingTextureFilenames.end()) {
            serialSpectrumTextures.push_back(std::make_pair(std::move(name), std::move(texture)));
            return;
        }
        loadingTextureFilenames.insert(filename);

        asyncSpectrumTextures.push_back(std::make_pair(name, texture));

        auto create = [=, this](TextureSceneEntity texture) {
            Allocator alloc = threadAllocators.Get();

            Transform renderFromTexture = texture.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&texture.parameters, nullptr);
            return SpectrumTexture::Create(texture.name, renderFromTexture, texDict, SpectrumType::Albedo, &texture.loc, alloc);
        };
        spectrumTextureJobs[name] = RunAsync(create, texture);
    }

    void Scene::AddLight(LightSceneEntity light) {
        Medium lightMedium = GetMedium(light.medium, &light.loc);
        std::lock_guard<std::mutex> lock(lightMutex);

        if (light.renderFromObject.IsAnimated()) spectra::diagnostics::PrintWarning(&light.loc, "Animated lights aren't supported. Using the start transform.");

        auto create = [this, light, lightMedium]() { return Light::Create(light.name, light.parameters, light.renderFromObject.startTransform, GetCamera().GetCameraTransform(), lightMedium, &light.loc, threadAllocators.Get()); };
        lightJobs.push_back(RunAsync(create));
    }

    int Scene::AddAreaLight(SceneEntity light) {
        std::lock_guard<std::mutex> lock(areaLightMutex);
        areaLights.push_back(std::move(light));
        return areaLights.size() - 1;
    }

    void Scene::AddShapes(pstd::span<ShapeSceneEntity> s) {
        std::lock_guard<std::mutex> lock(shapeMutex);
        std::move(std::begin(s), std::end(s), std::back_inserter(shapes));
    }

    void Scene::AddAnimatedShape(AnimatedShapeSceneEntity shape) {
        std::lock_guard<std::mutex> lock(animatedShapeMutex);
        animatedShapes.push_back(std::move(shape));
    }

    void Scene::AddInstanceDefinition(InstanceDefinitionSceneEntity instance) {
        InstanceDefinitionSceneEntity* def = new InstanceDefinitionSceneEntity{
            .name           = instance.name,
            .loc            = instance.loc,
            .shapes         = std::move(instance.shapes),
            .animatedShapes = std::move(instance.animatedShapes),
        };

        std::lock_guard<std::mutex> lock(instanceDefinitionMutex);
        instanceDefinitions[def->name] = def;
    }

    void Scene::AddInstanceUses(pstd::span<InstanceSceneEntity> in) {
        std::lock_guard<std::mutex> lock(instanceUseMutex);
        std::move(std::begin(in), std::end(in), std::back_inserter(instances));
    }

    void Scene::CreateMaterials(const NamedTextures& textures, std::map<std::string, Material>* namedMaterialsOut, std::vector<Material>* materialsOut) {
        std::lock_guard<std::mutex> lock(materialMutex);
        for (auto& job : normalMapJobs) {
            SPECTRA_CHECK(normalMaps.find(job.first) == normalMaps.end());
            normalMaps[job.first] = job.second->GetResult();
        }
        normalMapJobs.clear();

        for (const auto& nm : namedMaterials) {
            const std::string& name = nm.first;
            const SceneEntity& mtl  = nm.second;
            Allocator alloc         = threadAllocators.Get();

            if (namedMaterialsOut->find(name) != namedMaterialsOut->end()) {
                throw std::runtime_error(spectra::diagnostics::Format(&mtl.loc, "%s: trying to redefine named material.", name));
            }

            std::string type = mtl.parameters.GetOneString("type", "");
            if (type.empty()) {
                throw std::runtime_error(spectra::diagnostics::Format(&mtl.loc, "%s: \"string type\" not provided in named material's parameters.", name));
            }

            std::string fn   = ResolveFilename(nm.second.parameters.GetOneString("normalmap", ""));
            Image* normalMap = nullptr;
            if (!fn.empty()) {
                SPECTRA_CHECK(normalMaps.find(fn) != normalMaps.end());
                normalMap = normalMaps[fn];
            }

            TextureParameterDictionary texDict(&mtl.parameters, &textures);
            Material m                 = Material::Create(type, texDict, normalMap, *namedMaterialsOut, &mtl.loc, alloc);
            (*namedMaterialsOut)[name] = m;
        }

        materialsOut->reserve(materials.size());
        for (const auto& mtl : materials) {
            Allocator alloc  = threadAllocators.Get();
            std::string fn   = ResolveFilename(mtl.parameters.GetOneString("normalmap", ""));
            Image* normalMap = nullptr;
            if (!fn.empty()) {
                SPECTRA_CHECK(normalMaps.find(fn) != normalMaps.end());
                normalMap = normalMaps[fn];
            }

            TextureParameterDictionary texDict(&mtl.parameters, &textures);
            Material m = Material::Create(mtl.name, texDict, normalMap, *namedMaterialsOut, &mtl.loc, alloc);
            materialsOut->push_back(m);
        }
    }

    NamedTextures Scene::CreateTextures() {
        NamedTextures textures;

        textureMutex.lock();
        for (auto& tex : floatTextureJobs) textures.floatTextures[tex.first] = tex.second->GetResult();
        floatTextureJobs.clear();
        for (auto& tex : spectrumTextureJobs) textures.albedoSpectrumTextures[tex.first] = tex.second->GetResult();
        spectrumTextureJobs.clear();
        textureMutex.unlock();

        Allocator alloc = threadAllocators.Get();
        for (const auto& tex : asyncSpectrumTextures) {
            Transform renderFromTexture = tex.second.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&tex.second.parameters, nullptr);

            SpectrumTexture unboundedTex = SpectrumTexture::Create(tex.second.name, renderFromTexture, texDict, SpectrumType::Unbounded, &tex.second.loc, alloc);
            SpectrumTexture illumTex     = SpectrumTexture::Create(tex.second.name, renderFromTexture, texDict, SpectrumType::Illuminant, &tex.second.loc, alloc);

            textures.unboundedSpectrumTextures[tex.first]  = unboundedTex;
            textures.illuminantSpectrumTextures[tex.first] = illumTex;
        }

        for (auto& tex : serialFloatTextures) {
            Allocator alloc = threadAllocators.Get();

            Transform renderFromTexture = tex.second.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&tex.second.parameters, &textures);
            FloatTexture t                    = FloatTexture::Create(tex.second.name, renderFromTexture, texDict, &tex.second.loc, alloc);
            textures.floatTextures[tex.first] = t;
        }

        for (auto& tex : serialSpectrumTextures) {
            Allocator alloc = threadAllocators.Get();

            if (tex.second.renderFromObject.IsAnimated())
                spectra::diagnostics::PrintWarning(&tex.second.loc, "Animated world to texture transform not supported. "
                                                                    "Using start transform.");

            Transform renderFromTexture = tex.second.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&tex.second.parameters, &textures);
            SpectrumTexture albedoTex    = SpectrumTexture::Create(tex.second.name, renderFromTexture, texDict, SpectrumType::Albedo, &tex.second.loc, alloc);
            SpectrumTexture unboundedTex = SpectrumTexture::Create(tex.second.name, renderFromTexture, texDict, SpectrumType::Unbounded, &tex.second.loc, alloc);
            SpectrumTexture illumTex     = SpectrumTexture::Create(tex.second.name, renderFromTexture, texDict, SpectrumType::Illuminant, &tex.second.loc, alloc);

            textures.albedoSpectrumTextures[tex.first]     = albedoTex;
            textures.unboundedSpectrumTextures[tex.first]  = unboundedTex;
            textures.illuminantSpectrumTextures[tex.first] = illumTex;
        }

        return textures;
    }

    std::vector<Light> Scene::CreateLights(const NamedTextures& textures, std::map<int, pstd::vector<Light>*>* shapeIndexToAreaLights) {
        auto findMedium = [this](const std::string& s, const FileLoc* loc) -> Medium {
            if (s.empty()) return nullptr;

            auto iter = mediaMap.find(s);
            if (iter == mediaMap.end()) throw std::runtime_error(spectra::diagnostics::Format(loc, "%s: medium not defined", s));
            return iter->second;
        };

        Allocator alloc = threadAllocators.Get();

        auto getAlphaTexture = [&](const ParameterDictionary& parameters, const FileLoc* loc) -> FloatTexture {
            std::string alphaTexName = parameters.GetTexture("alpha");
            if (!alphaTexName.empty()) {
                if (auto iter = textures.floatTextures.find(alphaTexName); iter != textures.floatTextures.end()) {
                    if (!BasicTextureEvaluator().CanEvaluate({iter->second}, {})) return nullptr;
                    return iter->second;
                } else
                    throw std::runtime_error(spectra::diagnostics::Format(loc, "%s: couldn't find float texture for \"alpha\" parameter.", alphaTexName));
            } else if (Float alpha = parameters.GetOneFloat("alpha", 1.f); alpha < 1.f)
                return alloc.new_object<FloatConstantTexture>(alpha);
            else
                return nullptr;
        };

        std::vector<Light> lights;
        for (size_t i = 0; i < shapes.size(); ++i) {
            const auto& sh = shapes[i];

            if (sh.lightIndex == -1) continue;

            std::string materialName;
            if (!sh.materialName.empty()) {
                auto iter = std::find_if(namedMaterials.begin(), namedMaterials.end(), [&](auto iter) { return iter.first == sh.materialName; });
                if (iter == namedMaterials.end()) throw std::runtime_error(spectra::diagnostics::Format(&sh.loc, "%s: no named material defined.", sh.materialName));
                SPECTRA_CHECK(iter->second.parameters.GetStringArray("type").size() > 0);
                materialName = iter->second.parameters.GetOneString("type", "");
            } else {
                SPECTRA_CHECK_LT(sh.materialIndex, materials.size());
                materialName = materials[sh.materialIndex].name;
            }
            if (materialName == "interface" || materialName == "none" || materialName == "") {
                spectra::diagnostics::PrintWarning(&sh.loc, "Ignoring area light specification for shape "
                                                            "with \"interface\" material.");
                continue;
            }

            pstd::vector<Shape> shapeObjects = Shape::Create(sh.name, sh.renderFromObject, sh.objectFromRender, sh.reverseOrientation, sh.parameters, textures.floatTextures, &sh.loc, alloc);

            FloatTexture alphaTex = getAlphaTexture(sh.parameters, &sh.loc);

            MediumInterface mi(findMedium(sh.insideMedium, &sh.loc), findMedium(sh.outsideMedium, &sh.loc));

            pstd::vector<Light>* shapeLights = new pstd::vector<Light>(alloc);
            const auto& areaLightEntity      = areaLights[sh.lightIndex];
            for (Shape ps : shapeObjects) {
                Light area = Light::CreateArea(areaLightEntity.name, areaLightEntity.parameters, *sh.renderFromObject, mi, ps, alphaTex, &areaLightEntity.loc, alloc);
                if (area) {
                    lights.push_back(area);
                    shapeLights->push_back(area);
                }
            }

            (*shapeIndexToAreaLights)[i] = shapeLights;
        }


        std::lock_guard<std::mutex> lock(lightMutex);
        for (auto& job : lightJobs) lights.push_back(job->GetResult());

        return lights;
    }
} // namespace spectra::scene
