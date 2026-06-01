#ifndef SPECTRA_PATHTRACER_CORE_PARAMDICT_H
#define SPECTRA_PATHTRACER_CORE_PARAMDICT_H

#include <limits>
#include <map>
#include <memory>
#include <spectra/pathtracer/base/texture.h>
#include <spectra/pathtracer/core/diagnostics.h>
#include <spectra/pathtracer/util/containers.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/vecmath.h>
#include <string>
#include <vector>

namespace spectra {
    // ParsedParameter Definition
    class ParsedParameter {
    public:
        // ParsedParameter Public Methods
        ParsedParameter(FileLoc loc) : loc(loc) {}

        void AddFloat(Float v);
        void AddInt(int i);
        void AddString(std::string_view str);
        void AddBool(bool v);


        // ParsedParameter Public Members
        std::string type, name;
        FileLoc loc;
        pstd::vector<Float> floats;
        pstd::vector<int> ints;
        pstd::vector<std::string> strings;
        pstd::vector<uint8_t> bools;
        mutable bool lookedUp                   = false;
        mutable const RGBColorSpace* colorSpace = nullptr;
        bool mayBeUnused                        = false;
    };

    // ParsedParameterVector Definition
    using ParsedParameterVector = InlinedVector<ParsedParameter*, 8>;

    // ParameterType Definition
    enum class ParameterType { Boolean, Float, Integer, Point2f, Vector2f, Point3f, Vector3f, Normal3f, Spectrum, String, Texture };

    // SpectrumType Definition
    enum class SpectrumType { Illuminant, Albedo, Unbounded };

    inline std::string ToString(SpectrumType t) {
        switch (t) {
        case SpectrumType::Albedo: return "Albedo";
        case SpectrumType::Unbounded: return "Unbounded";
        case SpectrumType::Illuminant: return "Illuminant";
        default: SPECTRA_FATAL("Unhandled SpectrumType");
        }
    }

    // NamedTextures Definition
    struct NamedTextures {
        std::map<std::string, FloatTexture> floatTextures;
        std::map<std::string, SpectrumTexture> albedoSpectrumTextures;
        std::map<std::string, SpectrumTexture> unboundedSpectrumTextures;
        std::map<std::string, SpectrumTexture> illuminantSpectrumTextures;
    };

    template <ParameterType PT>
    struct ParameterTypeTraits {};

    // ParameterDictionary Definition
    class ParameterDictionary {
    public:
        // ParameterDictionary Public Methods
        ParameterDictionary() = default;
        ParameterDictionary(ParsedParameterVector params, const RGBColorSpace* colorSpace);

        ParameterDictionary(ParsedParameterVector params0, const ParsedParameterVector& params1, const RGBColorSpace* colorSpace);

        std::string GetTexture(const std::string& name) const;

        std::vector<RGB> GetRGBArray(const std::string& name) const;

        const RGBColorSpace* ColorSpace() const {
            return colorSpace;
        }


        const FileLoc* loc(const std::string&) const;

        const ParsedParameterVector& GetParameterVector() const {
            return params;
        }

        void FreeParameters();

        Float GetOneFloat(const std::string& name, Float def) const;
        int GetOneInt(const std::string& name, int def) const;
        bool GetOneBool(const std::string& name, bool def) const;
        std::string GetOneString(const std::string& name, const std::string& def) const;

        Point2f GetOnePoint2f(const std::string& name, Point2f def) const;
        Vector2f GetOneVector2f(const std::string& name, Vector2f def) const;
        Point3f GetOnePoint3f(const std::string& name, Point3f def) const;
        Vector3f GetOneVector3f(const std::string& name, Vector3f def) const;
        Normal3f GetOneNormal3f(const std::string& name, Normal3f def) const;

        Spectrum GetOneSpectrum(const std::string& name, Spectrum def, SpectrumType spectrumType, Allocator alloc) const;

        std::vector<Float> GetFloatArray(const std::string& name) const;
        std::vector<int> GetIntArray(const std::string& name) const;
        std::vector<uint8_t> GetBoolArray(const std::string& name) const;

        std::vector<Point2f> GetPoint2fArray(const std::string& name) const;
        std::vector<Vector2f> GetVector2fArray(const std::string& name) const;
        std::vector<Point3f> GetPoint3fArray(const std::string& name) const;
        std::vector<Vector3f> GetVector3fArray(const std::string& name) const;
        std::vector<Normal3f> GetNormal3fArray(const std::string& name) const;
        std::vector<Spectrum> GetSpectrumArray(const std::string& name, SpectrumType spectrumType, Allocator alloc) const;
        std::vector<std::string> GetStringArray(const std::string& name) const;

        void ReportUnused() const;

    private:
        friend class TextureParameterDictionary;
        // ParameterDictionary Private Methods
        template <ParameterType PT>
        typename ParameterTypeTraits<PT>::ReturnType lookupSingle(const std::string& name, typename ParameterTypeTraits<PT>::ReturnType defaultValue) const;

        template <ParameterType PT>
        std::vector<typename ParameterTypeTraits<PT>::ReturnType> lookupArray(const std::string& name) const;

        template <typename ReturnType, typename G, typename C>
        std::vector<ReturnType> lookupArray(const std::string& name, ParameterType type, const char* typeName, int nPerItem, G getValues, C convert) const;

        std::vector<Spectrum> extractSpectrumArray(const ParsedParameter& param, SpectrumType spectrumType, Allocator alloc) const;

        void checkParameterTypes();

        // ParameterDictionary Private Members
        ParsedParameterVector params;
        const RGBColorSpace* colorSpace = nullptr;
        int nOwnedParams;
    };

    // TextureParameterDictionary Definition
    class TextureParameterDictionary {
    public:
        // TextureParameterDictionary Public Methods
        TextureParameterDictionary(const ParameterDictionary* dict, const NamedTextures* textures);

        operator const ParameterDictionary&() const {
            return *dict;
        }

        Float GetOneFloat(const std::string& name, Float def) const;
        int GetOneInt(const std::string& name, int def) const;
        bool GetOneBool(const std::string& name, bool def) const;
        Point2f GetOnePoint2f(const std::string& name, Point2f def) const;
        Vector2f GetOneVector2f(const std::string& name, Vector2f def) const;
        Point3f GetOnePoint3f(const std::string& name, Point3f def) const;
        Vector3f GetOneVector3f(const std::string& name, Vector3f def) const;
        Normal3f GetOneNormal3f(const std::string& name, Normal3f def) const;
        Spectrum GetOneSpectrum(const std::string& name, Spectrum def, SpectrumType spectrumType, Allocator alloc) const;
        std::string GetOneString(const std::string& name, const std::string& def) const;

        std::vector<Float> GetFloatArray(const std::string& name) const;
        std::vector<int> GetIntArray(const std::string& name) const;
        std::vector<uint8_t> GetBoolArray(const std::string& name) const;
        std::vector<Point2f> GetPoint2fArray(const std::string& name) const;
        std::vector<Vector2f> GetVector2fArray(const std::string& name) const;
        std::vector<Point3f> GetPoint3fArray(const std::string& name) const;
        std::vector<Vector3f> GetVector3fArray(const std::string& name) const;
        std::vector<Normal3f> GetNormal3fArray(const std::string& name) const;
        std::vector<Spectrum> GetSpectrumArray(const std::string& name, SpectrumType spectrumType, Allocator alloc) const;
        std::vector<std::string> GetStringArray(const std::string& name) const;

        FloatTexture GetFloatTexture(const std::string& name, Float defaultValue, Allocator alloc) const;
        FloatTexture GetFloatTextureOrNull(const std::string& name, Allocator alloc) const;

        void ReportUnused() const;

        SpectrumTexture GetSpectrumTexture(std::string name, Spectrum defaultValue, SpectrumType spectrumType, Allocator alloc) const;
        SpectrumTexture GetSpectrumTextureOrNull(std::string name, SpectrumType spectrumType, Allocator alloc) const;

    private:
        // TextureParameterDictionary Private Members
        const ParameterDictionary* dict;
        const NamedTextures* textures;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_PARAMDICT_H
