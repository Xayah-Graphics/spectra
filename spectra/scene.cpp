module;

#ifndef SPECTRA_PROJECT_SCENE_ROOT
#error "SPECTRA_PROJECT_SCENE_ROOT must point to the project-local scene directory."
#endif

#include <zlib.h>

module spectra.scene;

import spectra.util.math;
import std;

extern "C++" {
namespace spectra::scene {
    void SceneEditBuilder::replaceSnapshot(SceneSnapshot snapshot, const SceneDirtyFlags dirty) {
        if (dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Scene snapshot replacement must use snapshot dirty state");
        this->replacement = std::move(snapshot);
        this->dirty       = dirty;
    }

    SceneWorkspace::SceneWorkspace(SceneSnapshot snapshot) {
        if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(snapshot));
    }

    [[nodiscard]] bool SceneWorkspace::loaded() const {
        return this->currentSnapshot != nullptr;
    }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> SceneWorkspace::snapshot() const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded snapshot");
        return this->currentSnapshot;
    }

    [[nodiscard]] SceneEditBatch SceneWorkspace::commit(SceneEditBuilder edit) {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded scene workspace");
        if (!edit.replacement.has_value()) throw std::runtime_error("Cannot commit an empty scene edit");
        if (edit.dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Scene edit commit must use snapshot dirty state");

        SceneSnapshot next                 = std::move(*edit.replacement);
        const SceneRevision beforeRevision = this->currentSnapshot->revision;
        next.revision                      = SceneRevision{beforeRevision.value + 1};
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(next));

        SceneEditBatch batch = this->fullEdit(beforeRevision);
        batch.dirty          = edit.dirty;
        this->lastEdit       = batch;
        return batch;
    }

    [[nodiscard]] SceneEditBatch SceneWorkspace::changes_since(const SceneRevision revision) const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot query scene changes from an unloaded workspace");
        if (revision == this->currentSnapshot->revision) {
            return SceneEditBatch{
                .beforeRevision = revision,
                .afterRevision  = revision,
                .dirty          = SceneDirtyFlags::None,
            };
        }
        if (revision.value == 0) return this->fullEdit(revision);
        if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
        throw std::runtime_error("Scene edit history for the requested revision is unavailable");
    }

    [[nodiscard]] SceneEditBatch SceneWorkspace::fullEdit(const SceneRevision before) const {
        return SceneEditBatch{
            .beforeRevision = before,
            .afterRevision  = this->currentSnapshot->revision,
            .dirty          = SceneDirtyFlags::Snapshot,
        };
    }
} // namespace spectra::scene

namespace spectra::scene {
    namespace {
        constexpr char DefaultMaterialName[] = "__pbrt_default_material";

        enum class TokenKind { Word, QuotedString, LeftBracket, RightBracket };
        enum class EntityUse { Generic, Film, Camera, Texture, Material, Medium, Light, AreaLight, Shape };

        struct Token {
            TokenKind kind{TokenKind::Word};
            std::string text{};
            SceneSourceLocation source{};
        };

        [[nodiscard]] std::string Lowercase(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] std::string SourceString(const SceneSourceLocation& source) {
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[nodiscard]] std::runtime_error ParseError(const SceneSourceLocation& source, const std::string_view message) {
            return std::runtime_error(std::format("{}: {}", SourceString(source), message));
        }

        [[nodiscard]] bool HasExtension(const std::filesystem::path& path, const std::string_view extension) {
            return Lowercase(path.extension().string()) == Lowercase(std::string(extension));
        }

        [[nodiscard]] bool IsPbrtSceneFile(const std::filesystem::path& path) {
            if (HasExtension(path, ".pbrt")) return true;
            if (!HasExtension(path, ".gz")) return false;
            return HasExtension(path.stem(), ".pbrt");
        }

        [[nodiscard]] bool IsAbsolutePathString(const std::string& value) {
            if (value.empty()) return false;
            if (std::filesystem::path(value).is_absolute()) return true;
            return value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':' && (value[2] == '/' || value[2] == '\\');
        }

        [[nodiscard]] bool IsPathLike(const std::string& value) {
            if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos) return true;
            const std::filesystem::path path(value);
            return !path.extension().empty();
        }

        [[nodiscard]] std::string ReadPlainFile(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error(std::format("{}: unable to open PBRT scene file", path.string()));
            std::ostringstream stream;
            stream << input.rdbuf();
            return stream.str();
        }

        [[nodiscard]] std::string ReadGzipFile(const std::filesystem::path& path) {
            gzFile file = gzopen(path.string().c_str(), "rb");
            if (file == nullptr) throw std::runtime_error(std::format("{}: unable to open gzip PBRT scene file", path.string()));

            std::string result;
            std::array<char, 1 << 15> buffer{};
            while (true) {
                const int count = gzread(file, buffer.data(), static_cast<unsigned int>(buffer.size()));
                if (count < 0) {
                    int errorNumber = 0;
                    const char* message = gzerror(file, &errorNumber);
                    gzclose(file);
                    throw std::runtime_error(std::format("{}: gzip read failed: {}", path.string(), message == nullptr ? "unknown zlib error" : message));
                }
                if (count == 0) break;
                result.append(buffer.data(), static_cast<std::size_t>(count));
            }

            const int closeStatus = gzclose(file);
            if (closeStatus != Z_OK) throw std::runtime_error(std::format("{}: gzip close failed", path.string()));
            return result;
        }

        [[nodiscard]] std::string ReadSceneFile(const std::filesystem::path& path) {
            if (HasExtension(path, ".gz")) return ReadGzipFile(path);
            return ReadPlainFile(path);
        }

        class PbrtTokenStream {
        public:
            explicit PbrtTokenStream(std::filesystem::path filename) {
                this->PushFile(std::move(filename));
            }

            [[nodiscard]] std::optional<Token> Next() {
                if (this->pushedToken.has_value()) return std::exchange(this->pushedToken, {});

                while (!this->fileStack.empty()) {
                    PbrtTokenFile& file = this->fileStack.back();
                    this->SkipIgnored(&file);
                    if (file.offset >= file.content.size()) {
                        this->fileStack.pop_back();
                        continue;
                    }

                    const SceneSourceLocation source{
                        .filename = file.filename.string(),
                        .line     = file.line,
                        .column   = file.column,
                    };

                    const char character = file.content[file.offset];
                    if (character == '[') {
                        this->Advance(&file);
                        return Token{.kind = TokenKind::LeftBracket, .text = "[", .source = source};
                    }
                    if (character == ']') {
                        this->Advance(&file);
                        return Token{.kind = TokenKind::RightBracket, .text = "]", .source = source};
                    }
                    if (character == '"') return this->ReadString(&file, source);
                    return this->ReadWord(&file, source);
                }

                return {};
            }

            void PushBack(Token token) {
                if (this->pushedToken.has_value()) throw std::runtime_error("PBRT parser internal error: token pushback overflow");
                this->pushedToken = std::move(token);
            }

            void PushFile(std::filesystem::path path) {
                if (!std::filesystem::exists(path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", path.string()));
                std::string content = ReadSceneFile(path);
                this->fileStack.push_back(PbrtTokenFile{
                    .filename = std::move(path),
                    .content  = std::move(content),
                });
            }

        private:
            struct PbrtTokenFile {
                std::filesystem::path filename;
                std::string content;
                std::size_t offset{};
                int line{1};
                int column{1};
            };

            void Advance(PbrtTokenFile* file) {
                if (file->offset >= file->content.size()) return;
                if (file->content[file->offset] == '\n') {
                    ++file->line;
                    file->column = 1;
                } else {
                    ++file->column;
                }
                ++file->offset;
            }

            void SkipIgnored(PbrtTokenFile* file) {
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (std::isspace(static_cast<unsigned char>(character))) {
                        this->Advance(file);
                        continue;
                    }
                    if (character == '#') {
                        while (file->offset < file->content.size() && file->content[file->offset] != '\n') this->Advance(file);
                        continue;
                    }
                    return;
                }
            }

            [[nodiscard]] Token ReadString(PbrtTokenFile* file, const SceneSourceLocation& source) {
                this->Advance(file);
                std::string text;
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (character == '"') {
                        this->Advance(file);
                        return Token{.kind = TokenKind::QuotedString, .text = std::move(text), .source = source};
                    }
                    if (character == '\\') {
                        this->Advance(file);
                        if (file->offset >= file->content.size()) throw ParseError(source, "unterminated escape sequence in quoted string");
                        text.push_back(file->content[file->offset]);
                        this->Advance(file);
                        continue;
                    }
                    text.push_back(character);
                    this->Advance(file);
                }
                throw ParseError(source, "unterminated quoted string");
            }

            [[nodiscard]] Token ReadWord(PbrtTokenFile* file, const SceneSourceLocation& source) {
                std::string text;
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (std::isspace(static_cast<unsigned char>(character)) || character == '[' || character == ']' || character == '#') break;
                    text.push_back(character);
                    this->Advance(file);
                }
                if (text.empty()) throw ParseError(source, "unexpected character in PBRT scene file");
                return Token{.kind = TokenKind::Word, .text = std::move(text), .source = source};
            }

            std::vector<PbrtTokenFile> fileStack{};
            std::optional<Token> pushedToken{};
        };

        [[nodiscard]] Token RequireToken(PbrtTokenStream& stream, const std::string_view context) {
            std::optional<Token> token = stream.Next();
            if (!token.has_value()) throw std::runtime_error(std::format("Unexpected end of PBRT scene file while parsing {}", context));
            return std::move(*token);
        }

        [[nodiscard]] std::string RequireStringToken(PbrtTokenStream& stream, const std::string_view context) {
            Token token = RequireToken(stream, context);
            if (token.kind != TokenKind::QuotedString) throw ParseError(token.source, std::format("{} expects a quoted string", context));
            return std::move(token.text);
        }

        [[nodiscard]] float ParseFloatToken(const Token& token) {
            const char* begin = token.text.c_str();
            char* end         = nullptr;
            const float value = std::strtof(begin, &end);
            if (end == begin || *end != '\0') throw ParseError(token.source, std::format("\"{}\" is not a floating-point value", token.text));
            return value;
        }

        [[nodiscard]] int ParseIntegerToken(const Token& token) {
            const char* begin = token.text.c_str();
            char* end         = nullptr;
            const long value  = std::strtol(begin, &end, 10);
            if (end == begin || *end != '\0') throw ParseError(token.source, std::format("\"{}\" is not an integer value", token.text));
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) throw ParseError(token.source, std::format("\"{}\" is outside integer range", token.text));
            return static_cast<int>(value);
        }

        [[nodiscard]] std::uint8_t ParseBoolToken(const Token& token) {
            if (token.text == "true") return 1;
            if (token.text == "false") return 0;
            throw ParseError(token.source, std::format("\"{}\" is not a Boolean value", token.text));
        }

        [[nodiscard]] std::array<float, 16> TransposeMatrix(const std::array<float, 16>& matrix) {
            return {
                matrix[0],
                matrix[4],
                matrix[8],
                matrix[12],
                matrix[1],
                matrix[5],
                matrix[9],
                matrix[13],
                matrix[2],
                matrix[6],
                matrix[10],
                matrix[14],
                matrix[3],
                matrix[7],
                matrix[11],
                matrix[15],
            };
        }

        [[nodiscard]] std::array<float, 16> InverseMatrix(const std::array<float, 16>& matrix, const SceneSourceLocation& source) {
            std::array<std::array<double, 8>, 4> augmented{};
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = matrix[static_cast<std::size_t>(row * 4 + column)];
                augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(4 + row)] = 1.0;
            }

            for (int column = 0; column < 4; ++column) {
                int pivotRow = column;
                double pivot = std::abs(augmented[static_cast<std::size_t>(pivotRow)][static_cast<std::size_t>(column)]);
                for (int row = column + 1; row < 4; ++row) {
                    const double candidate = std::abs(augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
                    if (candidate > pivot) {
                        pivot    = candidate;
                        pivotRow = row;
                    }
                }
                if (!(pivot > 0.0)) throw ParseError(source, "Transform matrix is singular");
                if (pivotRow != column) std::swap(augmented[static_cast<std::size_t>(pivotRow)], augmented[static_cast<std::size_t>(column)]);

                const double denominator = augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(column)];
                for (int index = 0; index < 8; ++index) augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(index)] /= denominator;

                for (int row = 0; row < 4; ++row) {
                    if (row == column) continue;
                    const double factor = augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                    for (int index = 0; index < 8; ++index) augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(index)] -= factor * augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(index)];
                }
            }

            std::array<float, 16> inverse{};
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) inverse[static_cast<std::size_t>(row * 4 + column)] = static_cast<float>(augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(4 + column)]);
            return inverse;
        }

        [[nodiscard]] math::Transform TransformFromPbrtMatrix(const std::array<float, 16>& pbrtMatrix, const SceneSourceLocation& source) {
            const std::array<float, 16> matrix = TransposeMatrix(pbrtMatrix);
            return math::Transform{
                .matrix  = matrix,
                .inverse = InverseMatrix(matrix, source),
            };
        }

        [[nodiscard]] bool TransformDiffers(const math::Transform& left, const math::Transform& right) {
            return left.matrix != right.matrix || left.inverse != right.inverse;
        }

        void RefreshAnimatedFlag(SceneTransformSet* transform) {
            transform->animated = TransformDiffers(transform->start, transform->end);
        }

        void ApplyTransform(SceneTransformSet* transform, const math::Transform& value, const bool startActive, const bool endActive) {
            if (startActive) transform->start = math::Multiply(transform->start, value);
            if (endActive) transform->end = math::Multiply(transform->end, value);
            RefreshAnimatedFlag(transform);
        }

        void SetTransform(SceneTransformSet* transform, const math::Transform& value, const bool startActive, const bool endActive) {
            if (startActive) transform->start = value;
            if (endActive) transform->end = value;
            RefreshAnimatedFlag(transform);
        }

        [[nodiscard]] ColorSpace ParseColorSpaceName(const std::string& name, const SceneSourceLocation& source) {
            const std::string lower = Lowercase(name);
            if (lower == "srgb") return ColorSpace::sRGB;
            if (lower == "dci-p3") return ColorSpace::DCI_P3;
            if (lower == "rec2020") return ColorSpace::Rec2020;
            if (lower == "aces2065-1") return ColorSpace::ACES2065_1;
            throw ParseError(source, std::format("\"{}\" is not a supported PBRT color space", name));
        }

        [[nodiscard]] std::vector<std::string>* ParameterStringValues(SceneParameter* parameter) {
            return std::get_if<std::vector<std::string>>(&parameter->values);
        }

        [[nodiscard]] const std::vector<std::string>* ParameterStringValues(const SceneParameter& parameter) {
            return std::get_if<std::vector<std::string>>(&parameter.values);
        }

        [[nodiscard]] const std::vector<float>* ParameterFloatValues(const SceneParameter& parameter) {
            return std::get_if<std::vector<float>>(&parameter.values);
        }

        [[nodiscard]] std::string OneStringParameter(const std::vector<SceneParameter>& parameters, const std::string& name, std::string fallback) {
            for (const SceneParameter& parameter : parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                const std::vector<std::string>* values = ParameterStringValues(parameter);
                if (values == nullptr || values->size() != 1) return fallback;
                return values->front();
            }
            return fallback;
        }

        [[nodiscard]] float OneFloatParameter(const std::vector<SceneParameter>& parameters, const std::string& name, const float fallback) {
            for (const SceneParameter& parameter : parameters) {
                if (parameter.name != name) continue;
                const std::vector<float>* values = ParameterFloatValues(parameter);
                if (values == nullptr || values->size() != 1) return fallback;
                return values->front();
            }
            return fallback;
        }

        [[nodiscard]] bool IsBuiltInApertureName(const std::string& value) {
            return value == "gaussian" || value == "square" || value == "pentagon" || value == "star";
        }

        struct GraphicsState {
            SceneTransformSet transform{};
            bool activeStart{true};
            bool activeEnd{true};
            ColorSpace colorSpace{ColorSpace::sRGB};
            std::string currentMaterialName{DefaultMaterialName};
            std::optional<SceneAreaLight> areaLight{};
            SceneMediumInterface mediumInterface{};
            bool reverseOrientation{false};
            std::vector<SceneParameter> shapeAttributes{};
            std::vector<SceneParameter> lightAttributes{};
            std::vector<SceneParameter> materialAttributes{};
            std::vector<SceneParameter> mediumAttributes{};
            std::vector<SceneParameter> textureAttributes{};
        };

        class PbrtSceneBuilder {
        public:
            explicit PbrtSceneBuilder(std::filesystem::path inputFile) : inputFile(std::filesystem::absolute(std::move(inputFile)).lexically_normal()), searchDirectory(this->inputFile.parent_path()) {
                this->scene.name   = this->inputFile.stem().string();
                this->scene.title  = this->inputFile.stem().string();
                this->scene.source = this->inputFile.string();

                const SceneSourceLocation source{.filename = this->inputFile.string(), .line = 1, .column = 1};
                this->SetDefaultEntitySources(source);
                this->scene.materials.push_back(SceneMaterial{
                    .name   = DefaultMaterialName,
                    .entity = SceneEntity{.type = "diffuse", .colorSpace = ColorSpace::sRGB, .source = source},
                });
                this->materialNames.insert(DefaultMaterialName);
                this->namedCoordinateSystems["world"] = this->graphicsState.transform;
            }

            [[nodiscard]] SceneSnapshot Parse() {
                this->ParseFile(this->inputFile);
                this->Finish();
                return std::move(this->scene);
            }

        private:
            enum class BlockState { Options, World };

            void SetDefaultEntitySources(const SceneSourceLocation& source) {
                this->scene.renderSettings.filter.source      = source;
                this->scene.renderSettings.film.source        = source;
                this->scene.renderSettings.camera.source      = source;
                this->scene.renderSettings.sampler.source     = source;
                this->scene.renderSettings.integrator.source  = source;
                this->scene.renderSettings.accelerator.source = source;
            }

            [[nodiscard]] std::string ResolveResourcePath(const std::string& value) const {
                if (value.empty() || IsAbsolutePathString(value)) return value;
                return (this->searchDirectory / std::filesystem::path(value)).lexically_normal().string();
            }

            [[nodiscard]] std::filesystem::path ResolveIncludePath(const std::string& value, const SceneSourceLocation& source) const {
                if (value.empty()) throw ParseError(source, "Include filename must not be empty");
                const std::filesystem::path path = IsAbsolutePathString(value) ? std::filesystem::path(value) : this->searchDirectory / std::filesystem::path(value);
                return std::filesystem::absolute(path).lexically_normal();
            }

            void ResolveParameterPaths(std::vector<SceneParameter>* parameters, const EntityUse entityUse) const {
                for (SceneParameter& parameter : *parameters) {
                    std::vector<std::string>* values = ParameterStringValues(&parameter);
                    if (values == nullptr) continue;
                    if (entityUse == EntityUse::Film && parameter.name == "filename") continue;

                    const bool directFileParameter = parameter.name == "filename" || parameter.name == "normalmap" || parameter.name == "lensfile" || parameter.name == "emissionfilename";
                    const bool apertureParameter   = parameter.name == "aperture";
                    const bool spectrumParameter   = parameter.type == "spectrum";
                    if (!directFileParameter && !apertureParameter && !spectrumParameter) continue;

                    for (std::string& value : *values) {
                        if (value.empty()) continue;
                        if (apertureParameter && IsBuiltInApertureName(value)) continue;
                        if (spectrumParameter && !IsPathLike(value) && !std::filesystem::exists(this->searchDirectory / std::filesystem::path(value))) continue;
                        value = this->ResolveResourcePath(value);
                    }
                }
            }

            [[nodiscard]] std::vector<SceneParameter> MergeParameters(const std::vector<SceneParameter>& attributes, std::vector<SceneParameter> parameters, const EntityUse entityUse) const {
                std::vector<SceneParameter> merged;
                merged.reserve(attributes.size() + parameters.size());
                for (SceneParameter parameter : attributes) {
                    parameter.mayBeUnused = true;
                    merged.push_back(std::move(parameter));
                }
                for (SceneParameter& parameter : parameters) merged.push_back(std::move(parameter));
                this->ResolveParameterPaths(&merged, entityUse);
                return merged;
            }

            [[nodiscard]] SceneEntity Entity(std::string type, std::vector<SceneParameter> parameters, const EntityUse entityUse, const SceneSourceLocation& source, const ColorSpace colorSpace) const {
                this->ResolveParameterPaths(&parameters, entityUse);
                return SceneEntity{
                    .type       = std::move(type),
                    .parameters = std::move(parameters),
                    .colorSpace = colorSpace,
                    .source     = source,
                };
            }

            [[nodiscard]] SceneEntity EntityWithAttributes(std::string type, std::vector<SceneParameter> parameters, const std::vector<SceneParameter>& attributes, const EntityUse entityUse, const SceneSourceLocation& source, const ColorSpace colorSpace) const {
                return SceneEntity{
                    .type       = std::move(type),
                    .parameters = this->MergeParameters(attributes, std::move(parameters), entityUse),
                    .colorSpace = colorSpace,
                    .source     = source,
                };
            }

            void ParseFile(const std::filesystem::path& path) {
                PbrtTokenStream stream(path);
                while (std::optional<Token> directive = stream.Next()) this->ParseDirective(stream, *directive);
            }

            [[nodiscard]] std::vector<SceneParameter> ParseParameters(PbrtTokenStream& stream) {
                std::vector<SceneParameter> parameters;
                while (true) {
                    std::optional<Token> declaration = stream.Next();
                    if (!declaration.has_value()) return parameters;
                    if (declaration->kind != TokenKind::QuotedString) {
                        stream.PushBack(std::move(*declaration));
                        return parameters;
                    }

                    SceneParameter parameter = this->ParseParameterDeclaration(*declaration);
                    Token value              = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                    if (value.kind == TokenKind::LeftBracket) {
                        while (true) {
                            Token element = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                            if (element.kind == TokenKind::RightBracket) break;
                            this->AppendParameterValue(&parameter, element);
                        }
                    } else {
                        this->AppendParameterValue(&parameter, value);
                    }
                    parameters.push_back(std::move(parameter));
                }
            }

            [[nodiscard]] SceneParameter ParseParameterDeclaration(const Token& declaration) const {
                const std::string& text = declaration.text;
                std::size_t typeBegin = 0;
                while (typeBegin < text.size() && std::isspace(static_cast<unsigned char>(text[typeBegin]))) ++typeBegin;
                if (typeBegin == text.size()) throw ParseError(declaration.source, "PBRT parameter declaration does not contain a type");

                std::size_t typeEnd = typeBegin;
                while (typeEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[typeEnd]))) ++typeEnd;

                std::size_t nameBegin = typeEnd;
                while (nameBegin < text.size() && std::isspace(static_cast<unsigned char>(text[nameBegin]))) ++nameBegin;
                if (nameBegin == text.size()) throw ParseError(declaration.source, std::format("\"{}\" does not contain a parameter name", text));

                std::size_t nameEnd = nameBegin;
                while (nameEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[nameEnd]))) ++nameEnd;

                return SceneParameter{
                    .type       = text.substr(typeBegin, typeEnd - typeBegin),
                    .name       = text.substr(nameBegin, nameEnd - nameBegin),
                    .colorSpace = this->graphicsState.colorSpace,
                    .source     = declaration.source,
                };
            }

            void AppendParameterValue(SceneParameter* parameter, const Token& value) const {
                if (parameter->type == "integer") {
                    if (value.kind == TokenKind::QuotedString) throw ParseError(value.source, std::format("\"integer {}\" expects numeric values", parameter->name));
                    if (!std::holds_alternative<std::vector<int>>(parameter->values)) parameter->values = std::vector<int>{};
                    std::get<std::vector<int>>(parameter->values).push_back(ParseIntegerToken(value));
                    return;
                }
                if (parameter->type == "bool") {
                    if (!std::holds_alternative<std::vector<std::uint8_t>>(parameter->values)) parameter->values = std::vector<std::uint8_t>{};
                    std::get<std::vector<std::uint8_t>>(parameter->values).push_back(ParseBoolToken(value));
                    return;
                }
                if (parameter->type == "string" || parameter->type == "texture" || value.kind == TokenKind::QuotedString) {
                    if (value.kind != TokenKind::QuotedString) throw ParseError(value.source, std::format("\"{} {}\" expects quoted string values", parameter->type, parameter->name));
                    if (!std::holds_alternative<std::vector<std::string>>(parameter->values)) parameter->values = std::vector<std::string>{};
                    std::get<std::vector<std::string>>(parameter->values).push_back(value.text);
                    return;
                }

                if (!std::holds_alternative<std::vector<float>>(parameter->values)) parameter->values = std::vector<float>{};
                std::get<std::vector<float>>(parameter->values).push_back(ParseFloatToken(value));
            }

            void ParseDirective(PbrtTokenStream& stream, const Token& directive) {
                if (directive.kind != TokenKind::Word) throw ParseError(directive.source, "PBRT directive must be an unquoted identifier");

                if (directive.text == "AttributeBegin" || directive.text == "TransformBegin") {
                    this->RequireWorld(directive, directive.text);
                    this->stateStack.push_back(this->graphicsState);
                    this->stackKinds.push_back('a');
                    return;
                }
                if (directive.text == "AttributeEnd" || directive.text == "TransformEnd") {
                    this->RequireWorld(directive, directive.text);
                    this->PopGraphicsState(directive);
                    return;
                }
                if (directive.text == "ActiveTransform") {
                    this->ActiveTransform(RequireToken(stream, "ActiveTransform"), directive.source);
                    return;
                }
                if (directive.text == "AreaLightSource") {
                    this->RequireWorld(directive, "AreaLightSource");
                    const std::string type = RequireStringToken(stream, "AreaLightSource");
                    this->graphicsState.areaLight = SceneAreaLight{.entity = this->EntityWithAttributes(type, this->ParseParameters(stream), this->graphicsState.lightAttributes, EntityUse::AreaLight, directive.source, this->graphicsState.colorSpace)};
                    return;
                }
                if (directive.text == "Accelerator") {
                    this->RequireOptions(directive, "Accelerator");
                    const std::string type = RequireStringToken(stream, "Accelerator");
                    this->scene.renderSettings.accelerator = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "Attribute") {
                    std::string target = RequireStringToken(stream, "Attribute");
                    std::vector<SceneParameter> parameters = this->ParseParameters(stream);
                    this->Attribute(std::move(target), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Camera") {
                    this->RequireOptions(directive, "Camera");
                    const std::string type = RequireStringToken(stream, "Camera");
                    this->scene.renderSettings.camera          = this->Entity(type, this->ParseParameters(stream), EntityUse::Camera, directive.source, this->graphicsState.colorSpace);
                    this->scene.renderSettings.cameraTransform = this->WorldFromCameraTransform();
                    this->scene.renderSettings.cameraMedium    = this->graphicsState.mediumInterface.outside;
                    this->namedCoordinateSystems["camera"]     = this->scene.renderSettings.cameraTransform;
                    return;
                }
                if (directive.text == "ColorSpace") {
                    this->graphicsState.colorSpace = ParseColorSpaceName(RequireStringToken(stream, "ColorSpace"), directive.source);
                    return;
                }
                if (directive.text == "ConcatTransform") {
                    this->ConcatTransform(stream, directive.source);
                    return;
                }
                if (directive.text == "CoordinateSystem") {
                    this->namedCoordinateSystems[RequireStringToken(stream, "CoordinateSystem")] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "CoordSysTransform") {
                    const std::string name = RequireStringToken(stream, "CoordSysTransform");
                    const std::map<std::string, SceneTransformSet>::const_iterator iter = this->namedCoordinateSystems.find(name);
                    if (iter == this->namedCoordinateSystems.end()) throw ParseError(directive.source, std::format("Unknown coordinate system \"{}\"", name));
                    this->graphicsState.transform = iter->second;
                    return;
                }
                if (directive.text == "Film") {
                    this->RequireOptions(directive, "Film");
                    const std::string type = RequireStringToken(stream, "Film");
                    this->scene.renderSettings.film = this->Entity(type, this->ParseParameters(stream), EntityUse::Film, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "Identity") {
                    this->SetActiveTransform(math::Transform{});
                    return;
                }
                if (directive.text == "Import") {
                    this->RequireWorld(directive, "Import");
                    this->Import(stream, directive.source);
                    return;
                }
                if (directive.text == "Include") {
                    stream.PushFile(this->ResolveIncludePath(RequireStringToken(stream, "Include"), directive.source));
                    return;
                }
                if (directive.text == "Integrator") {
                    this->RequireOptions(directive, "Integrator");
                    const std::string type = RequireStringToken(stream, "Integrator");
                    this->scene.renderSettings.integrator = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "LightSource") {
                    this->RequireWorld(directive, "LightSource");
                    this->LightSource(stream, directive.source);
                    return;
                }
                if (directive.text == "LookAt") {
                    std::array<float, 9> values{};
                    for (float& value : values) value = ParseFloatToken(RequireToken(stream, "LookAt"));
                    this->ApplyActiveTransform(math::LookAt(math::Point3{values[0], values[1], values[2]}, math::Point3{values[3], values[4], values[5]}, math::Vector3{values[6], values[7], values[8]}));
                    return;
                }
                if (directive.text == "MakeNamedMaterial") {
                    this->RequireWorld(directive, "MakeNamedMaterial");
                    std::string name = RequireStringToken(stream, "MakeNamedMaterial");
                    std::vector<SceneParameter> parameters = this->ParseParameters(stream);
                    this->MakeNamedMaterial(std::move(name), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "MakeNamedMedium") {
                    std::string name = RequireStringToken(stream, "MakeNamedMedium");
                    std::vector<SceneParameter> parameters = this->ParseParameters(stream);
                    this->MakeNamedMedium(std::move(name), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Material") {
                    this->RequireWorld(directive, "Material");
                    std::string type = RequireStringToken(stream, "Material");
                    std::vector<SceneParameter> parameters = this->ParseParameters(stream);
                    this->Material(std::move(type), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "MediumInterface") {
                    this->MediumInterface(stream);
                    return;
                }
                if (directive.text == "NamedMaterial") {
                    this->RequireWorld(directive, "NamedMaterial");
                    this->graphicsState.currentMaterialName = RequireStringToken(stream, "NamedMaterial");
                    return;
                }
                if (directive.text == "ObjectBegin") {
                    this->ObjectBegin(RequireStringToken(stream, "ObjectBegin"), directive.source);
                    return;
                }
                if (directive.text == "ObjectEnd") {
                    this->ObjectEnd(directive.source);
                    return;
                }
                if (directive.text == "ObjectInstance") {
                    this->ObjectInstance(RequireStringToken(stream, "ObjectInstance"), directive.source);
                    return;
                }
                if (directive.text == "Option") {
                    std::string name = RequireStringToken(stream, "Option");
                    Token value = RequireToken(stream, "Option");
                    this->Option(std::move(name), std::move(value.text), directive.source);
                    return;
                }
                if (directive.text == "PixelFilter") {
                    this->RequireOptions(directive, "PixelFilter");
                    const std::string type = RequireStringToken(stream, "PixelFilter");
                    this->scene.renderSettings.filter = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "ReverseOrientation") {
                    this->RequireWorld(directive, "ReverseOrientation");
                    this->graphicsState.reverseOrientation = !this->graphicsState.reverseOrientation;
                    return;
                }
                if (directive.text == "Rotate") {
                    const float angle = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float x     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float y     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float z     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    this->ApplyActiveTransform(math::Rotate(angle, math::Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "Sampler") {
                    this->RequireOptions(directive, "Sampler");
                    const std::string type = RequireStringToken(stream, "Sampler");
                    this->scene.renderSettings.sampler = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "Scale") {
                    const float x = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float y = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float z = ParseFloatToken(RequireToken(stream, "Scale"));
                    this->ApplyActiveTransform(math::Scale(x, y, z));
                    return;
                }
                if (directive.text == "Shape") {
                    this->RequireWorld(directive, "Shape");
                    std::string type = RequireStringToken(stream, "Shape");
                    std::vector<SceneParameter> parameters = this->ParseParameters(stream);
                    this->Shape(std::move(type), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Texture") {
                    this->RequireWorld(directive, "Texture");
                    this->Texture(stream, directive.source);
                    return;
                }
                if (directive.text == "Transform") {
                    this->Transform(stream, directive.source);
                    return;
                }
                if (directive.text == "TransformTimes") {
                    this->RequireOptions(directive, "TransformTimes");
                    this->graphicsState.transform.startTime = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    this->graphicsState.transform.endTime   = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    return;
                }
                if (directive.text == "Translate") {
                    const float x = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float y = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float z = ParseFloatToken(RequireToken(stream, "Translate"));
                    this->ApplyActiveTransform(math::Translate(math::Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "WorldBegin") {
                    this->RequireOptions(directive, "WorldBegin");
                    const float startTime = this->graphicsState.transform.startTime;
                    const float endTime   = this->graphicsState.transform.endTime;
                    this->currentBlock = BlockState::World;
                    this->graphicsState.transform   = SceneTransformSet{.startTime = startTime, .endTime = endTime};
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = true;
                    this->namedCoordinateSystems["world"] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "WorldEnd") throw ParseError(directive.source, "WorldEnd is not used by PBRT v4 scene files");
                throw ParseError(directive.source, std::format("Unknown PBRT directive \"{}\"", directive.text));
            }

            void RequireOptions(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::Options) throw ParseError(directive.source, std::format("{} is only valid before WorldBegin", name));
            }

            void RequireWorld(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::World) throw ParseError(directive.source, std::format("{} is only valid after WorldBegin", name));
            }

            void RequireWorld(const SceneSourceLocation& source, const std::string_view name) const {
                if (this->currentBlock != BlockState::World) throw ParseError(source, std::format("{} is only valid after WorldBegin", name));
            }

            void ApplyActiveTransform(const math::Transform& transform) {
                ApplyTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void SetActiveTransform(const math::Transform& transform) {
                SetTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void ActiveTransform(const Token& token, const SceneSourceLocation& source) {
                if (token.kind != TokenKind::Word) throw ParseError(token.source, "ActiveTransform expects StartTime, EndTime, or All");
                if (token.text == "StartTime") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = false;
                    return;
                }
                if (token.text == "EndTime") {
                    this->graphicsState.activeStart = false;
                    this->graphicsState.activeEnd   = true;
                    return;
                }
                if (token.text == "All") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = true;
                    return;
                }
                throw ParseError(source, std::format("Unknown ActiveTransform target \"{}\"", token.text));
            }

            [[nodiscard]] SceneTransformSet WorldFromCameraTransform() const {
                SceneTransformSet result{
                    .start     = math::Inverse(this->graphicsState.transform.start),
                    .end       = math::Inverse(this->graphicsState.transform.end),
                    .startTime = this->graphicsState.transform.startTime,
                    .endTime   = this->graphicsState.transform.endTime,
                };
                RefreshAnimatedFlag(&result);
                return result;
            }

            void ReadBracketedMatrix(PbrtTokenStream& stream, const std::string_view context, std::array<float, 16>* values) const {
                Token open = RequireToken(stream, context);
                if (open.kind != TokenKind::LeftBracket) throw ParseError(open.source, std::format("{} expects '['", context));
                for (float& value : *values) value = ParseFloatToken(RequireToken(stream, context));
                Token close = RequireToken(stream, context);
                if (close.kind != TokenKind::RightBracket) throw ParseError(close.source, std::format("{} expects ']'", context));
            }

            void Transform(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "Transform", &values);
                this->SetActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void ConcatTransform(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "ConcatTransform", &values);
                this->ApplyActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void PopGraphicsState(const Token& directive) {
                if (this->stateStack.empty()) throw ParseError(directive.source, std::format("Unmatched {}", directive.text));
                if (this->stackKinds.empty() || this->stackKinds.back() != 'a') throw ParseError(directive.source, std::format("{} does not match the current graphics state stack", directive.text));
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
            }

            void Attribute(std::string target, std::vector<SceneParameter> parameters, const SceneSourceLocation& source) {
                std::vector<SceneParameter>* currentAttributes = nullptr;
                if (target == "shape")
                    currentAttributes = &this->graphicsState.shapeAttributes;
                else if (target == "light")
                    currentAttributes = &this->graphicsState.lightAttributes;
                else if (target == "material")
                    currentAttributes = &this->graphicsState.materialAttributes;
                else if (target == "medium")
                    currentAttributes = &this->graphicsState.mediumAttributes;
                else if (target == "texture")
                    currentAttributes = &this->graphicsState.textureAttributes;
                else
                    throw ParseError(source, std::format("Unknown Attribute target \"{}\"", target));

                for (SceneParameter& parameter : parameters) {
                    parameter.mayBeUnused = true;
                    parameter.colorSpace  = this->graphicsState.colorSpace;
                    currentAttributes->push_back(std::move(parameter));
                }
            }

            void Option(std::string name, std::string value, const SceneSourceLocation& source) {
                this->scene.renderSettings.options.push_back(SceneOption{
                    .name   = std::move(name),
                    .value  = std::move(value),
                    .source = source,
                });
            }

            void MediumInterface(PbrtTokenStream& stream) {
                const std::string inside = RequireStringToken(stream, "MediumInterface");
                std::optional<Token> outsideToken = stream.Next();
                if (!outsideToken.has_value()) {
                    this->graphicsState.mediumInterface = SceneMediumInterface{.inside = inside, .outside = inside};
                    return;
                }
                if (outsideToken->kind == TokenKind::QuotedString) {
                    this->graphicsState.mediumInterface = SceneMediumInterface{.inside = inside, .outside = std::move(outsideToken->text)};
                    return;
                }
                stream.PushBack(std::move(*outsideToken));
                this->graphicsState.mediumInterface = SceneMediumInterface{.inside = inside, .outside = inside};
            }

            void MakeNamedMedium(std::string name, std::vector<SceneParameter> parameters, const SceneSourceLocation& source) {
                this->RequireUniqueName(this->mediumNames, "medium", name, source);
                SceneEntity entity = this->EntityWithAttributes("", std::move(parameters), this->graphicsState.mediumAttributes, EntityUse::Medium, source, this->graphicsState.colorSpace);
                const std::string type = OneStringParameter(entity.parameters, "type", "");
                if (type.empty()) throw ParseError(source, std::format("MakeNamedMedium \"{}\" requires \"string type\"", name));
                entity.type = type;
                this->mediumNames.insert(name);
                this->scene.media.push_back(SceneMedium{
                    .name      = std::move(name),
                    .entity    = std::move(entity),
                    .transform = this->graphicsState.transform,
                });
            }

            void LightSource(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                const std::string type = RequireStringToken(stream, "LightSource");
                this->scene.lights.push_back(SceneLight{
                    .name      = std::format("__light_{}", this->scene.lights.size()),
                    .entity    = this->EntityWithAttributes(type, this->ParseParameters(stream), this->graphicsState.lightAttributes, EntityUse::Light, source, this->graphicsState.colorSpace),
                    .transform = this->graphicsState.transform,
                    .medium    = this->graphicsState.mediumInterface.outside,
                });
            }

            void Material(std::string type, std::vector<SceneParameter> parameters, const SceneSourceLocation& source) {
                if (type.empty()) type = "interface";
                const std::string name = std::format("__inline_material_{}", this->inlineMaterialCount);
                ++this->inlineMaterialCount;
                this->scene.materials.push_back(SceneMaterial{
                    .name   = name,
                    .entity = this->EntityWithAttributes(std::move(type), std::move(parameters), this->graphicsState.materialAttributes, EntityUse::Material, source, this->graphicsState.colorSpace),
                });
                this->materialNames.insert(name);
                this->graphicsState.currentMaterialName = name;
            }

            void MakeNamedMaterial(std::string name, std::vector<SceneParameter> parameters, const SceneSourceLocation& source) {
                this->RequireUniqueName(this->materialNames, "material", name, source);
                SceneEntity entity = this->EntityWithAttributes("", std::move(parameters), this->graphicsState.materialAttributes, EntityUse::Material, source, this->graphicsState.colorSpace);
                const std::string type = OneStringParameter(entity.parameters, "type", "");
                if (type.empty()) throw ParseError(source, std::format("MakeNamedMaterial \"{}\" requires \"string type\"", name));
                entity.type = type;
                this->materialNames.insert(name);
                this->scene.materials.push_back(SceneMaterial{
                    .name   = std::move(name),
                    .entity = std::move(entity),
                });
            }

            void Texture(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::string name = RequireStringToken(stream, "Texture");
                std::string kind = RequireStringToken(stream, "Texture");
                std::string type = RequireStringToken(stream, "Texture");
                if (kind != "float" && kind != "spectrum") throw ParseError(source, std::format("Texture \"{}\" has unsupported value type \"{}\"", name, kind));
                this->RequireUniqueName(kind == "float" ? this->floatTextureNames : this->spectrumTextureNames, "texture", name, source);
                if (kind == "float")
                    this->floatTextureNames.insert(name);
                else
                    this->spectrumTextureNames.insert(name);
                this->scene.textures.push_back(SceneTexture{
                    .name      = std::move(name),
                    .kind      = std::move(kind),
                    .entity    = this->EntityWithAttributes(std::move(type), this->ParseParameters(stream), this->graphicsState.textureAttributes, EntityUse::Texture, source, this->graphicsState.colorSpace),
                    .transform = this->graphicsState.transform,
                });
            }

            void Shape(std::string type, std::vector<SceneParameter> parameters, const SceneSourceLocation& source) {
                SceneShape shape{
                    .name               = std::format("__shape_{}", this->shapeCount),
                    .entity             = this->EntityWithAttributes(std::move(type), std::move(parameters), this->graphicsState.shapeAttributes, EntityUse::Shape, source, this->graphicsState.colorSpace),
                    .transform          = this->graphicsState.transform,
                    .reverseOrientation = this->graphicsState.reverseOrientation,
                    .materialName       = this->graphicsState.currentMaterialName,
                    .areaLight          = this->graphicsState.areaLight,
                    .mediumInterface    = this->graphicsState.mediumInterface,
                };
                ++this->shapeCount;

                if (this->activeObjectDefinition.has_value())
                    this->activeObjectDefinition->shapes.push_back(std::move(shape));
                else
                    this->scene.shapes.push_back(std::move(shape));
            }

            void ObjectBegin(std::string name, const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectBegin");
                if (this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectBegin cannot be nested inside another ObjectBegin");
                this->RequireUniqueName(this->objectDefinitionNames, "object definition", name, source);
                this->stateStack.push_back(this->graphicsState);
                this->stackKinds.push_back('o');
                this->objectDefinitionNames.insert(name);
                this->activeObjectDefinition = SceneObjectDefinition{.name = std::move(name), .source = source};
            }

            void ObjectEnd(const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectEnd");
                if (!this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectEnd without ObjectBegin");
                if (this->stateStack.empty() || this->stackKinds.empty() || this->stackKinds.back() != 'o') throw ParseError(source, "ObjectEnd does not match the current graphics state stack");
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
                this->scene.objectDefinitions.push_back(std::move(*this->activeObjectDefinition));
                this->activeObjectDefinition.reset();
            }

            void ObjectInstance(std::string name, const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectInstance");
                if (this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectInstance cannot be used inside ObjectBegin");
                this->scene.objectInstances.push_back(SceneObjectInstance{
                    .name           = std::format("__instance_{}", this->scene.objectInstances.size()),
                    .definitionName = std::move(name),
                    .transform      = this->graphicsState.transform,
                    .source         = source,
                });
            }

            void Import(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                const std::filesystem::path importPath = this->ResolveIncludePath(RequireStringToken(stream, "Import"), source);
                const GraphicsState savedState         = this->graphicsState;
                this->ParseFile(importPath);
                this->graphicsState = savedState;
            }

            void RequireUniqueName(std::set<std::string>& names, const std::string_view kind, const std::string& name, const SceneSourceLocation& source) {
                if (name.empty()) throw ParseError(source, std::format("PBRT {} name must not be empty", kind));
                if (!names.insert(name).second) throw ParseError(source, std::format("PBRT {} \"{}\" is already defined", kind, name));
            }

            void Finish() const {
                if (!this->stateStack.empty()) throw std::runtime_error(std::format("{}: missing AttributeEnd/ObjectEnd for scene parser stack", this->scene.source));
                if (this->activeObjectDefinition.has_value()) throw std::runtime_error(std::format("{}: missing ObjectEnd", this->scene.source));
            }

            SceneSnapshot scene{};
            std::filesystem::path inputFile;
            std::filesystem::path searchDirectory;
            GraphicsState graphicsState{};
            BlockState currentBlock{BlockState::Options};
            std::vector<GraphicsState> stateStack{};
            std::vector<char> stackKinds{};
            std::map<std::string, SceneTransformSet> namedCoordinateSystems{};
            std::optional<SceneObjectDefinition> activeObjectDefinition{};
            std::set<std::string> materialNames{};
            std::set<std::string> mediumNames{};
            std::set<std::string> floatTextureNames{};
            std::set<std::string> spectrumTextureNames{};
            std::set<std::string> objectDefinitionNames{};
            std::size_t inlineMaterialCount{};
            std::size_t shapeCount{};
        };

        [[nodiscard]] std::filesystem::path SceneRoot() {
            return std::filesystem::absolute(std::filesystem::path(SPECTRA_PROJECT_SCENE_ROOT)).lexically_normal();
        }

        [[nodiscard]] std::string PathId(std::filesystem::path path) {
            return path.generic_string();
        }

        [[nodiscard]] std::filesystem::path PbrtSceneFilenameStem(const std::filesystem::path& path) {
            std::filesystem::path filename = path.filename();
            if (HasExtension(filename, ".gz")) filename = filename.stem();
            if (HasExtension(filename, ".pbrt")) filename = filename.stem();
            return filename;
        }

        [[nodiscard]] std::string SceneDisplayName(const std::filesystem::path& relativePath) {
            return PbrtSceneFilenameStem(relativePath).string();
        }

        [[nodiscard]] std::string SceneGroupName(const std::filesystem::path& relativePath) {
            if (relativePath.empty()) return "scene";
            const std::filesystem::path::iterator begin = relativePath.begin();
            if (begin == relativePath.end()) return "scene";
            return begin->string();
        }

        [[nodiscard]] bool ContainsWorldBegin(const std::filesystem::path& path) {
            PbrtTokenStream stream(path);
            while (std::optional<Token> token = stream.Next()) {
                if (token->kind == TokenKind::Word && token->text == "WorldBegin") return true;
            }
            return false;
        }

        void RefreshCatalogCounts(SceneCatalog* catalog) {
            catalog->pending_count     = 0;
            catalog->ready_count       = 0;
            catalog->invalid_count     = 0;
            for (const SceneCatalogEntry& entry : catalog->entries) {
                switch (entry.state) {
                case SceneCatalogEntryState::Pending: ++catalog->pending_count; break;
                case SceneCatalogEntryState::Ready: ++catalog->ready_count; break;
                case SceneCatalogEntryState::Invalid: ++catalog->invalid_count; break;
                }
            }
        }

        [[nodiscard]] std::filesystem::path ResolveScenePathByUniqueStem(const std::filesystem::path& root, const std::string& name) {
            std::optional<std::filesystem::path> match;
            if (!std::filesystem::exists(root)) throw std::runtime_error(std::format("{}: scene root does not exist", root.string()));
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                const std::filesystem::path path = entry.path();
                if (!IsPbrtSceneFile(path)) continue;
                if (PbrtSceneFilenameStem(path).string() != name) continue;
                if (match.has_value()) throw std::runtime_error(std::format("Scene alias \"{}\" is ambiguous; pass a scene-root-relative .pbrt path", name));
                match = path;
            }
            if (match.has_value()) return std::filesystem::absolute(*match).lexically_normal();
            return {};
        }

        [[nodiscard]] std::filesystem::path ResolveScenePath(const std::string_view requestedName) {
            const std::string requested(requestedName);
            const std::filesystem::path root = SceneRoot();
            if (requested == "default") return (root / "pbrt-book" / "book.pbrt").lexically_normal();

            const std::filesystem::path asPath(requested);
            if (std::filesystem::is_regular_file(asPath)) return std::filesystem::absolute(asPath).lexically_normal();
            if (std::filesystem::is_regular_file(root / asPath)) return std::filesystem::absolute(root / asPath).lexically_normal();
            if (std::filesystem::is_regular_file(root / (requested + ".pbrt"))) return std::filesystem::absolute(root / (requested + ".pbrt")).lexically_normal();
            if (std::filesystem::is_regular_file(root / requested / (requested + ".pbrt"))) return std::filesystem::absolute(root / requested / (requested + ".pbrt")).lexically_normal();

            const std::filesystem::path uniqueStem = ResolveScenePathByUniqueStem(root, requested);
            if (!uniqueStem.empty()) return uniqueStem;

            throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", requested));
        }

    } // namespace

    SceneInfo DescribeScene(const SceneSnapshot& scene) {
        std::size_t definitionShapeCount     = 0;
        std::size_t definitionAreaLightCount = 0;
        for (const SceneObjectDefinition& definition : scene.objectDefinitions) {
            definitionShapeCount += definition.shapes.size();
            for (const SceneShape& shape : definition.shapes)
                if (shape.areaLight.has_value()) ++definitionAreaLightCount;
        }

        std::size_t areaLightCount = definitionAreaLightCount;
        for (const SceneShape& shape : scene.shapes)
            if (shape.areaLight.has_value()) ++areaLightCount;

        std::size_t infiniteLightCount = 0;
        for (const SceneLight& light : scene.lights)
            if (light.entity.type == "infinite") ++infiniteLightCount;

        float cameraFov = OneFloatParameter(scene.renderSettings.camera.parameters, "fov", scene.renderSettings.camera.type == "perspective" ? 90.0f : 45.0f);
        if (!(cameraFov > 0.0f && cameraFov < 180.0f)) cameraFov = 45.0f;

        return SceneInfo{
            .name                    = scene.name,
            .title                   = scene.title,
            .camera                  = scene.renderSettings.camera.type,
            .sampler                 = scene.renderSettings.sampler.type,
            .integrator              = scene.renderSettings.integrator.type,
            .accelerator             = scene.renderSettings.accelerator.type,
            .shape_count             = scene.shapes.size() + definitionShapeCount,
            .material_count          = scene.materials.size(),
            .texture_count           = scene.textures.size(),
            .medium_count            = scene.media.size(),
            .light_count             = scene.lights.size(),
            .area_light_count        = areaLightCount,
            .infinite_light_count    = infiniteLightCount,
            .object_definition_count = scene.objectDefinitions.size(),
            .object_instance_count   = scene.objectInstances.size(),
            .camera_fov_degrees      = cameraFov,
        };
    }

    SceneCatalog DiscoverSceneCatalog() {
        SceneCatalog catalog{.root = SceneRoot()};
        if (!std::filesystem::exists(catalog.root)) throw std::runtime_error(std::format("{}: scene root does not exist", catalog.root.string()));
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(catalog.root)) {
            if (!entry.is_regular_file()) continue;
            const std::filesystem::path sourcePath = std::filesystem::absolute(entry.path()).lexically_normal();
            if (!IsPbrtSceneFile(sourcePath)) continue;
            std::optional<std::string> scanError{};
            try {
                if (!ContainsWorldBegin(sourcePath)) continue;
            } catch (const std::exception& error) {
                scanError = error.what();
            }

            const std::filesystem::path relativePath = sourcePath.lexically_relative(catalog.root);
            SceneCatalogEntry catalogEntry{
                .id          = PathId(relativePath),
                .displayName = SceneDisplayName(relativePath),
                .group       = SceneGroupName(relativePath),
                .relativePath = relativePath,
                .sourcePath  = sourcePath,
                .state       = scanError.has_value() ? SceneCatalogEntryState::Invalid : SceneCatalogEntryState::Pending,
            };
            if (scanError.has_value()) {
                catalogEntry.issues.push_back(SceneDiagnostic{
                    .source  = SceneSourceLocation{.filename = sourcePath.string(), .line = 1, .column = 1},
                    .message = *scanError,
                });
            }
            catalog.entries.push_back(std::move(catalogEntry));
        }
        std::ranges::sort(catalog.entries, {}, &SceneCatalogEntry::id);
        RefreshCatalogCounts(&catalog);
        return catalog;
    }

    void ValidateSceneCatalogEntry(SceneCatalogEntry& entry) {
        entry.state = SceneCatalogEntryState::Pending;
        entry.document.reset();
        entry.info.reset();
        entry.issues.clear();
        try {
            PbrtSceneBuilder builder(entry.sourcePath);
            SceneSnapshot scene = builder.Parse();
            scene.name          = entry.id;
            scene.title         = entry.displayName;
            if (scene.revision.value == 0) scene.revision = SceneRevision{1};

            entry.document = std::make_shared<SceneSnapshot>(std::move(scene));
            entry.info     = DescribeScene(*entry.document);
            entry.state    = SceneCatalogEntryState::Ready;
        } catch (const std::exception& error) {
            entry.state = SceneCatalogEntryState::Invalid;
            entry.issues.push_back(SceneDiagnostic{
                .source  = SceneSourceLocation{.filename = entry.sourcePath.string(), .line = 1, .column = 1},
                .message = error.what(),
            });
        }
    }

    SceneWorkspace BuildScene(const SceneCatalogEntry& entry) {
        if (entry.state != SceneCatalogEntryState::Ready) throw std::runtime_error(std::format("Cannot build disabled Spectra scene \"{}\"", entry.id));
        if (entry.document != nullptr) return SceneWorkspace{*entry.document};
        PbrtSceneBuilder builder(entry.sourcePath);
        SceneSnapshot scene = builder.Parse();
        scene.name          = entry.id;
        scene.title         = entry.displayName;
        return SceneWorkspace{std::move(scene)};
    }

    SceneWorkspace BuildScene(const std::string_view name) {
        const std::filesystem::path scenePath = ResolveScenePath(name);
        PbrtSceneBuilder builder(scenePath);
        SceneSnapshot scene = builder.Parse();
        if (name == "default") scene.name = "default";
        return SceneWorkspace{std::move(scene)};
    }
} // namespace spectra::scene
}
