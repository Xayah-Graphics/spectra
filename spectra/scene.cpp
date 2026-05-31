#include <spectra/scene.h>

#include <spectra/pathtracer/gpu/memory.h>
#include <spectra/pathtracer/core/materials.h>
#include <spectra/pathtracer/core/options.h>
#include <spectra/pathtracer/core/paramdict.h>
#include <spectra/pathtracer/core/shapes.h>
#include <spectra/pathtracer/util/color.h>
#include <spectra/pathtracer/util/colorspace.h>
#include <spectra/pathtracer/util/file.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/mesh.h>
#include <spectra/pathtracer/util/parallel.h>
#include <spectra/pathtracer/util/print.h>
#include <spectra/pathtracer/util/progressreporter.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/string.h>
#include <spectra/pathtracer/util/transform.h>

#include <double-conversion/double-conversion.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef SPECTRA_HAVE_MMAP
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(SPECTRA_IS_WINDOWS)
#include <windows.h>
#endif
#include <functional>
#include <limits>
#include <mutex>
#include <utility>

namespace spectra
{
    ///////////////////////////////////////////////////////////////////////////
    // spectra::ParsedParameter

    void ParsedParameter::AddFloat(Float v)
    {
        SPECTRA_CHECK(ints.empty() && strings.empty() && bools.empty());
        floats.push_back(v);
    }

    void ParsedParameter::AddInt(int i)
    {
        SPECTRA_CHECK(floats.empty() && strings.empty() && bools.empty());
        ints.push_back(i);
    }

    void ParsedParameter::AddString(std::string_view str)
    {
        SPECTRA_CHECK(floats.empty() && ints.empty() && bools.empty());
        strings.push_back({str.begin(), str.end()});
    }

    void ParsedParameter::AddBool(bool v)
    {
        SPECTRA_CHECK(floats.empty() && ints.empty() && strings.empty());
        bools.push_back(v);
    }


} // namespace spectra

namespace spectra::scene
{
    struct Token
    {
        Token() = default;

        Token(std::string_view token, FileLoc loc) : token(token), loc(loc)
        {
        }


        std::string_view token;
        FileLoc loc;
    };

    class Tokenizer
    {
    public:
        Tokenizer(std::string str, std::string filename,
                  std::function<void(const char*, const FileLoc*)> errorCallback);
#if defined(SPECTRA_HAVE_MMAP) || defined(SPECTRA_IS_WINDOWS)
        Tokenizer(void* ptr, size_t len, std::string filename,
                  std::function<void(const char*, const FileLoc*)> errorCallback);
#endif
        ~Tokenizer();

        static std::unique_ptr<Tokenizer> CreateFromFile(
            const std::string& filename,
            std::function<void(const char*, const FileLoc*)> errorCallback);
        static std::unique_ptr<Tokenizer> CreateFromString(
            std::string str,
            std::function<void(const char*, const FileLoc*)> errorCallback);

        pstd::optional<Token> Next();

        FileLoc loc;

    private:
        void CheckUTF(const void* ptr, int len) const;

        int getChar()
        {
            if (pos == end) return EOF;
            int ch = *pos++;
            if (ch == '\n')
            {
                ++loc.line;
                loc.column = 0;
            }
            else
                ++loc.column;
            return ch;
        }

        void ungetChar()
        {
            --pos;
            if (*pos == '\n') --loc.line;
        }

        std::function<void(const char*, const FileLoc*)> errorCallback;

#if defined(SPECTRA_HAVE_MMAP) || defined(SPECTRA_IS_WINDOWS)
        void* unmapPtr = nullptr;
        size_t unmapLength = 0;
#endif

        std::string contents;
        const char* pos = nullptr;
        const char* end = nullptr;
        std::string sEscaped;
    };

    static std::string toString(std::string_view s)
    {
        return std::string(s.data(), s.size());
    }


    // Tokenizer Implementation
    static char decodeEscaped(int ch, const FileLoc& loc)
    {
        switch (ch)
        {
        case EOF:
            ErrorExit(&loc, "premature EOF after character escape '\\'");
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case '\\':
            return '\\';
        case '\'':
            return '\'';
        case '\"':
            return '\"';
        default:
            ErrorExit(&loc, "unexpected escaped character \"%c\"", ch);
        }
        return 0; // NOTREACHED
    }

    static double_conversion::StringToDoubleConverter floatParser(
        double_conversion::StringToDoubleConverter::ALLOW_HEX, 0. /* empty string value */,
        0. /* junk string value */, nullptr /* infinity symbol */, nullptr /* NaN symbol */);

    std::unique_ptr<Tokenizer> Tokenizer::CreateFromFile(
        const std::string& filename,
        std::function<void(const char*, const FileLoc*)> errorCallback)
    {
        if (filename == "-")
        {
            // Handle stdin by slurping everything into a string.
            std::string str;
            int ch;
            while ((ch = getchar()) != EOF)
                str.push_back((char)ch);
            return std::make_unique<Tokenizer>(std::move(str), "<stdin>",
                                               std::move(errorCallback));
        }

        if (HasExtension(filename, ".gz"))
        {
            std::string str = ReadDecompressedFileContents(filename);
            return std::make_unique<Tokenizer>(std::move(str), filename,
                                               std::move(errorCallback));
        }

#ifdef SPECTRA_HAVE_MMAP
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1)
        {
            errorCallback(spectra::StringPrintf("%s: %s", filename, spectra::ErrorString()).c_str(), nullptr);
            return nullptr;
        }

        struct stat stat;
        if (fstat(fd, &stat) != 0)
        {
            errorCallback(spectra::StringPrintf("%s: %s", filename, spectra::ErrorString()).c_str(), nullptr);
            return nullptr;
        }

        size_t len = stat.st_size;
        if (len < 16 * 1024 * 1024)
        {
            close(fd);

            std::string str = spectra::ReadFileContents(filename);
            return std::make_unique<Tokenizer>(std::move(str), filename,
                                               std::move(errorCallback));
        }

        void* ptr = mmap(nullptr, len, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, fd, 0);
        if (ptr == MAP_FAILED)
            errorCallback(spectra::StringPrintf("%s: %s", filename, spectra::ErrorString()).c_str(), nullptr);

        if (close(fd) != 0)
        {
            errorCallback(spectra::StringPrintf("%s: %s", filename, spectra::ErrorString()).c_str(), nullptr);
            return nullptr;
        }

        return std::make_unique<Tokenizer>(ptr, len, filename, std::move(errorCallback));
#elif defined(SPECTRA_IS_WINDOWS)
        auto errorReportLambda = [&errorCallback, &filename]() -> std::unique_ptr<Tokenizer>
        {
            LPSTR messageBuffer = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                           (LPSTR)&messageBuffer, 0, NULL);

            errorCallback(StringPrintf("%s: %s", filename, messageBuffer).c_str(), nullptr);

            LocalFree(messageBuffer);
            return nullptr;
        };

        HANDLE fileHandle =
            CreateFileW(WStringFromUTF8(filename).c_str(), GENERIC_READ,
                        FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (fileHandle == INVALID_HANDLE_VALUE)
            return errorReportLambda();

        size_t len = GetFileSize(fileHandle, 0);

        HANDLE mapping = CreateFileMapping(fileHandle, 0, PAGE_READONLY, 0, 0, 0);
        CloseHandle(fileHandle);
        if (mapping == 0)
            return errorReportLambda();

        LPVOID ptr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);
        if (!ptr)
            return errorReportLambda();

        return std::make_unique<Tokenizer>(ptr, len, filename, std::move(errorCallback));
#else
        std::string str = spectra::ReadFileContents(filename);
        return std::make_unique<Tokenizer>(std::move(str), filename,
                                           std::move(errorCallback));
#endif
    }

    std::unique_ptr<Tokenizer> Tokenizer::CreateFromString(
        std::string str, std::function<void(const char*, const FileLoc*)> errorCallback)
    {
        return std::make_unique<Tokenizer>(std::move(str), "<stdin>",
                                           std::move(errorCallback));
    }

    Tokenizer::Tokenizer(std::string str, std::string filename,
                         std::function<void(const char*, const FileLoc*)> errorCallback)
        : errorCallback(std::move(errorCallback)), contents(std::move(str))
    {
        loc = FileLoc(*new std::string(filename));
        pos = contents.data();
        end = pos + contents.size();
        CheckUTF(contents.data(), contents.size());
    }

#if defined(SPECTRA_HAVE_MMAP) || defined(SPECTRA_IS_WINDOWS)
    Tokenizer::Tokenizer(void* ptr, size_t len, std::string filename,
                         std::function<void(const char*, const FileLoc*)> errorCallback)
        : errorCallback(std::move(errorCallback)), unmapPtr(ptr), unmapLength(len)
    {
        // This is disgusting and leaks memory, but it ensures that the
        // filename in FileLocs returned by the Tokenizer remain valid even
        // after it has been destroyed.
        loc = FileLoc(*new std::string(filename));
        pos = (const char*)ptr;
        end = pos + len;
        CheckUTF(ptr, len);
    }
#endif

    Tokenizer::~Tokenizer()
    {
#ifdef SPECTRA_HAVE_MMAP
        if (unmapPtr && unmapLength > 0)
            if (munmap(unmapPtr, unmapLength) != 0)
                errorCallback(spectra::StringPrintf("munmap: %s", spectra::ErrorString()).c_str(), nullptr);
#elif defined(SPECTRA_IS_WINDOWS)
        if (unmapPtr && UnmapViewOfFile(unmapPtr) == 0)
            errorCallback(StringPrintf("UnmapViewOfFile: %s", ErrorString()).c_str(),
                          nullptr);
#endif
    }

    void Tokenizer::CheckUTF(const void* ptr, int len) const
    {
        const unsigned char* c = (const unsigned char*)ptr;
        // https://en.wikipedia.org/wiki/Byte_order_mark
        if (len >= 2 && ((c[0] == 0xfe && c[1] == 0xff) || (c[0] == 0xff && c[1] == 0xfe)))
            errorCallback("File is encoded with UTF-16, which is not currently "
                          "supported by pbrt (https://github.com/mmp/pbrt-v4/issues/136).",
                          &loc);
    }

    pstd::optional<Token> Tokenizer::Next()
    {
        while (true)
        {
            const char* tokenStart = pos;
            FileLoc startLoc = loc;

            int ch = getChar();
            if (ch == EOF)
                return {};
            else if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r')
            {
                // nothing
            }
            else if (ch == '"')
            {
                // scan to closing quote
                bool haveEscaped = false;
                while ((ch = getChar()) != '"')
                {
                    if (ch == EOF)
                    {
                        errorCallback("premature EOF", &startLoc);
                        return {};
                    }
                    else if (ch == '\n')
                    {
                        errorCallback("unterminated string", &startLoc);
                        return {};
                    }
                    else if (ch == '\\')
                    {
                        haveEscaped = true;
                        // Grab the next character
                        if ((ch = getChar()) == EOF)
                        {
                            errorCallback("premature EOF", &startLoc);
                            return {};
                        }
                    }
                }

                if (!haveEscaped)
                    return Token({tokenStart, size_t(pos - tokenStart)}, startLoc);
                else
                {
                    sEscaped.clear();
                    for (const char* p = tokenStart; p < pos; ++p)
                    {
                        if (*p != '\\')
                            sEscaped.push_back(*p);
                        else
                        {
                            ++p;
                            SPECTRA_CHECK_LT(p, pos);
                            sEscaped.push_back(decodeEscaped(*p, startLoc));
                        }
                    }
                    return Token({sEscaped.data(), sEscaped.size()}, startLoc);
                }
            }
            else if (ch == '[' || ch == ']')
            {
                return Token({tokenStart, size_t(1)}, startLoc);
            }
            else if (ch == '#')
            {
                // comment: scan to EOL (or EOF)
                while ((ch = getChar()) != EOF)
                {
                    if (ch == '\n' || ch == '\r')
                    {
                        ungetChar();
                        break;
                    }
                }

                return Token({tokenStart, size_t(pos - tokenStart)}, startLoc);
            }
            else
            {
                // Regular statement or numeric token; scan until we hit a
                // space, opening quote, or bracket.
                while ((ch = getChar()) != EOF)
                {
                    if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '"' ||
                        ch == '[' || ch == ']')
                    {
                        ungetChar();
                        break;
                    }
                }
                return Token({tokenStart, size_t(pos - tokenStart)}, startLoc);
            }
        }
    }

    static int parseInt(const Token& t)
    {
        bool negate = t.token[0] == '-';

        int index = 0;
        if (t.token[0] == '+' || t.token[0] == '-')
            ++index;

        int64_t value = 0;
        while (index < t.token.size())
        {
            if (!(t.token[index] >= '0' && t.token[index] <= '9'))
                ErrorExit(&t.loc, "\"%c\": expected a number", t.token[index]);
            value = 10 * value + (t.token[index] - '0');
            ++index;

            if (value > std::numeric_limits<int>::max())
                ErrorExit(&t.loc,
                          "Numeric value too large to represent as a 32-bit integer.");
            else if (value < std::numeric_limits<int>::lowest())
                Warning(&t.loc, "Numeric value %d too low to represent as a 32-bit integer.");
        }

        return negate ? -value : value;
    }

    static double parseFloat(const Token& t)
    {
        // Fast path for a single digit
        if (t.token.size() == 1)
        {
            if (!(t.token[0] >= '0' && t.token[0] <= '9'))
                ErrorExit(&t.loc, "\"%c\": expected a number", t.token[0]);
            return t.token[0] - '0';
        }

        // strto[idf]() need a NUL-terminated buffer.
        std::string token(t.token);
        char* bufp = token.data();

        // Can we just use strtol?
        auto isInteger = [](std::string_view str)
        {
            for (char ch : str)
                if (!(ch >= '0' && ch <= '9'))
                    return false;
            return true;
        };

        int length = 0;
        double val;
        if (isInteger(t.token))
        {
            char* endptr;
            val = double(strtol(bufp, &endptr, 10));
            length = endptr - bufp;
        }
        else if (sizeof(Float) == sizeof(float))
            val = floatParser.StringToFloat(bufp, t.token.size(), &length);
        else
            val = floatParser.StringToDouble(bufp, t.token.size(), &length);

        if (length == 0)
            ErrorExit(&t.loc, "%s: expected a number", toString(t.token));

        return val;
    }

    inline bool isQuotedString(std::string_view str)
    {
        return str.size() >= 2 && str[0] == '"' && str.back() == '"';
    }

    static std::string_view dequoteString(const Token& t)
    {
        if (!isQuotedString(t.token))
            ErrorExit(&t.loc, "\"%s\": expected quoted string", toString(t.token));

        std::string_view str = t.token;
        str.remove_prefix(1);
        str.remove_suffix(1);
        return str;
    }

    constexpr int TokenOptional = 0;
    constexpr int TokenRequired = 1;

    template <typename Next, typename Unget>
    static ParsedParameterVector parseParameters(
        Next nextToken, Unget ungetToken,
        const std::function<void(const Token& token, const char*)>& errorCallback)
    {
        ParsedParameterVector parameterVector;

        while (true)
        {
            pstd::optional<Token> t = nextToken(TokenOptional);
            if (!t.has_value())
                return parameterVector;

            if (!isQuotedString(t->token))
            {
                ungetToken(*t);
                return parameterVector;
            }

            ParsedParameter* param = new ParsedParameter(t->loc);

            std::string_view decl = dequoteString(*t);

            auto skipSpace = [&decl](std::string_view::const_iterator iter)
            {
                while (iter != decl.end() && (*iter == ' ' || *iter == '\t'))
                    ++iter;
                return iter;
            };
            // Skip to the next whitespace character (or the end of the string).
            auto skipToSpace = [&decl](std::string_view::const_iterator iter)
            {
                while (iter != decl.end() && *iter != ' ' && *iter != '\t')
                    ++iter;
                return iter;
            };

            auto typeBegin = skipSpace(decl.begin());
            if (typeBegin == decl.end())
                ErrorExit(&t->loc, "Parameter \"%s\" doesn't have a type declaration?!",
                          std::string(decl.begin(), decl.end()));

            // Find end of type declaration
            auto typeEnd = skipToSpace(typeBegin);
            param->type.assign(typeBegin, typeEnd);

            auto nameBegin = skipSpace(typeEnd);
            if (nameBegin == decl.end())
                ErrorExit(&t->loc, "Unable to find parameter name from \"%s\"",
                          std::string(decl.begin(), decl.end()));

            auto nameEnd = skipToSpace(nameBegin);
            param->name.assign(nameBegin, nameEnd);

            enum ValType { Unknown, String, Bool, FloatValue, Int } valType = Unknown;

            if (param->type == "integer")
                valType = Int;

            auto addVal = [&](const Token& t)
            {
                if (isQuotedString(t.token))
                {
                    switch (valType)
                    {
                    case Unknown:
                        valType = String;
                        break;
                    case String:
                        break;
                    case FloatValue:
                        errorCallback(t, "expected floating-point value");
                    case Int:
                        errorCallback(t, "expected integer value");
                    case Bool:
                        errorCallback(t, "expected Boolean value");
                    }

                    param->AddString(dequoteString(t));
                }
                else if (t.token[0] == 't' && t.token == "true")
                {
                    switch (valType)
                    {
                    case Unknown:
                        valType = Bool;
                        break;
                    case String:
                        errorCallback(t, "expected string value");
                    case FloatValue:
                        errorCallback(t, "expected floating-point value");
                    case Int:
                        errorCallback(t, "expected integer value");
                    case Bool:
                        break;
                    }

                    param->AddBool(true);
                }
                else if (t.token[0] == 'f' && t.token == "false")
                {
                    switch (valType)
                    {
                    case Unknown:
                        valType = Bool;
                        break;
                    case String:
                        errorCallback(t, "expected string value");
                    case FloatValue:
                        errorCallback(t, "expected floating-point value");
                    case Int:
                        errorCallback(t, "expected integer value");
                    case Bool:
                        break;
                    }

                    param->AddBool(false);
                }
                else
                {
                    switch (valType)
                    {
                    case Unknown:
                        valType = FloatValue;
                        break;
                    case String:
                        errorCallback(t, "expected string value");
                    case FloatValue:
                        break;
                    case Int:
                        break;
                    case Bool:
                        errorCallback(t, "expected Boolean value");
                    }

                    if (valType == Int)
                        param->AddInt(parseInt(t));
                    else
                        param->AddFloat(parseFloat(t));
                }
            };

            Token val = *nextToken(TokenRequired);

            if (val.token == "[")
            {
                while (true)
                {
                    val = *nextToken(TokenRequired);
                    if (val.token == "]")
                        break;
                    addVal(val);
                }
            }
            else
            {
                addVal(val);
            }

            parameterVector.push_back(param);
        }

        return parameterVector;
    }

    template <typename Target>
    void parse(Target* target, std::unique_ptr<Tokenizer> t)
    {
        static std::atomic<bool> warnedTransformBeginEndDeprecated{false};

        std::vector<std::pair<AsyncJob<int>*, std::unique_ptr<Target>>> imports;

        std::vector<std::unique_ptr<Tokenizer>> fileStack;
        fileStack.push_back(std::move(t));

        pstd::optional<Token> ungetToken;

        auto parseError = [&](const char* msg, const FileLoc* loc)
        {
            ErrorExit(loc, "%s", msg);
        };

        // nextToken is a little helper function that handles the file stack,
        // returning the next token from the current file until reaching EOF,
        // at which point it switches to the next file (if any).
        std::function<pstd::optional<Token>(int)> nextToken;
        nextToken = [&](int flags) -> pstd::optional<Token>
        {
            if (ungetToken.has_value())
                return std::exchange(ungetToken, {});

            if (fileStack.empty())
            {
                if ((flags & TokenRequired) != 0)
                {
                    ErrorExit("premature end of file");
                }
                return {};
            }

            pstd::optional<Token> tok = fileStack.back()->Next();

            if (!tok)
            {
                // We've reached EOF in the current file. Anything more to parse?
                fileStack.pop_back();
                return nextToken(flags);
            }
            else if (tok->token[0] == '#')
            {
                // Swallow comments.
                return nextToken(flags);
            }
            else
            // Regular token; success.
                return tok;
        };

        auto unget = [&](Token t)
        {
            SPECTRA_CHECK(!ungetToken.has_value());
            ungetToken = t;
        };

        // Helper function for pbrt API entrypoints that take a single string
        // parameter and a ParameterVector (e.g. pbrtShape()).
        auto basicParamListEntrypoint =
            [&](void (Target::*apiFunc)(const std::string&, ParsedParameterVector,
                                         FileLoc),
                FileLoc loc)
        {
            Token t = *nextToken(TokenRequired);
            std::string_view dequoted = dequoteString(t);
            std::string n = toString(dequoted);
            ParsedParameterVector parameterVector = parseParameters(
                nextToken, unget, [&](const Token& t, const char* msg)
                {
                    std::string token = toString(t.token);
                    std::string str = StringPrintf("%s: %s", token, msg);
                    parseError(str.c_str(), &t.loc);
                });
            (target->*apiFunc)(n, std::move(parameterVector), loc);
        };

        auto syntaxError = [&](const Token& t)
        {
            ErrorExit(&t.loc, "Unknown directive: %s", toString(t.token));
        };

        pstd::optional<Token> tok;

        while (true)
        {
            tok = nextToken(TokenOptional);
            if (!tok.has_value())
                break;

            switch (tok->token[0])
            {
            case 'A':
                if (tok->token == "AttributeBegin")
                    target->AttributeBegin(tok->loc);
                else if (tok->token == "AttributeEnd")
                    target->AttributeEnd(tok->loc);
                else if (tok->token == "Attribute")
                    basicParamListEntrypoint(&Target::Attribute, tok->loc);
                else if (tok->token == "ActiveTransform")
                {
                    Token a = *nextToken(TokenRequired);
                    if (a.token == "All")
                        target->ActiveTransformAll(tok->loc);
                    else if (a.token == "EndTime")
                        target->ActiveTransformEndTime(tok->loc);
                    else if (a.token == "StartTime")
                        target->ActiveTransformStartTime(tok->loc);
                    else
                        syntaxError(*tok);
                }
                else if (tok->token == "AreaLightSource")
                    basicParamListEntrypoint(&Target::AreaLightSource, tok->loc);
                else if (tok->token == "Accelerator")
                    basicParamListEntrypoint(&Target::Accelerator, tok->loc);
                else
                    syntaxError(*tok);
                break;

            case 'C':
                if (tok->token == "ConcatTransform")
                {
                    if (nextToken(TokenRequired)->token != "[")
                        syntaxError(*tok);
                    Float m[16];
                    for (int i = 0; i < 16; ++i)
                        m[i] = parseFloat(*nextToken(TokenRequired));
                    if (nextToken(TokenRequired)->token != "]")
                        syntaxError(*tok);
                    target->ConcatTransform(m, tok->loc);
                }
                else if (tok->token == "CoordinateSystem")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    target->CoordinateSystem(toString(n), tok->loc);
                }
                else if (tok->token == "CoordSysTransform")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    target->CoordSysTransform(toString(n), tok->loc);
                }
                else if (tok->token == "ColorSpace")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    target->ColorSpace(toString(n), tok->loc);
                }
                else if (tok->token == "Camera")
                    basicParamListEntrypoint(&Target::Camera, tok->loc);
                else
                    syntaxError(*tok);
                break;

            case 'F':
                if (tok->token == "Film")
                    basicParamListEntrypoint(&Target::Film, tok->loc);
                else
                    syntaxError(*tok);
                break;

            case 'I':
                if (tok->token == "Integrator")
                    basicParamListEntrypoint(&Target::Integrator, tok->loc);
                else if (tok->token == "Include")
                {
                    Token filenameToken = *nextToken(TokenRequired);
                    std::string filename = toString(dequoteString(filenameToken));
                    filename = ResolveFilename(filename);
                    std::unique_ptr<Tokenizer> tinc =
                        Tokenizer::CreateFromFile(filename, parseError);
                    if (tinc)
                    {
                        fileStack.push_back(std::move(tinc));
                    }
                }
                else if (tok->token == "Import")
                {
                    Token filenameToken = *nextToken(TokenRequired);
                    std::string filename = toString(dequoteString(filenameToken));
                    if (!target->IsImportAllowed())
                        ErrorExit(&tok->loc, "Import statement only allowed inside world "
                                  "definition block.");

                    filename = ResolveFilename(filename);
                    std::unique_ptr<Target> importBuilder = target->CopyForImport();
                    Target* importTarget = importBuilder.get();

                    if (RunningThreads() == 1)
                    {
                        std::unique_ptr<Tokenizer> timport =
                            Tokenizer::CreateFromFile(filename, parseError);
                        if (timport)
                            parse(importTarget, std::move(timport));
                        target->MergeImported(std::move(importBuilder));
                    }
                    else
                    {
                        auto job = [=](std::string filename)
                        {
                            Timer timer;
                            std::unique_ptr<Tokenizer> timport =
                                Tokenizer::CreateFromFile(filename, parseError);
                            if (timport)
                                parse(importTarget, std::move(timport));
                            return 0;
                        };
                        AsyncJob<int>* jobFinished = spectra::RunAsync(job, filename);
                        imports.push_back(std::make_pair(jobFinished, std::move(importBuilder)));
                    }
                }
                else if (tok->token == "Identity")
                    target->Identity(tok->loc);
                else
                    syntaxError(*tok);
                break;

            case 'L':
                if (tok->token == "LightSource")
                    basicParamListEntrypoint(&Target::LightSource, tok->loc);
                else if (tok->token == "LookAt")
                {
                    Float v[9];
                    for (int i = 0; i < 9; ++i)
                        v[i] = parseFloat(*nextToken(TokenRequired));
                    target->LookAt(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8],
                                   tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'M':
                if (tok->token == "MakeNamedMaterial")
                    basicParamListEntrypoint(&Target::MakeNamedMaterial, tok->loc);
                else if (tok->token == "MakeNamedMedium")
                    basicParamListEntrypoint(&Target::MakeNamedMedium, tok->loc);
                else if (tok->token == "Material")
                    basicParamListEntrypoint(&Target::Material, tok->loc);
                else if (tok->token == "MediumInterface")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    std::string names[2];
                    names[0] = toString(n);

                    // Check for optional second parameter
                    pstd::optional<Token> second = nextToken(TokenOptional);
                    if (second.has_value())
                    {
                        if (isQuotedString(second->token))
                            names[1] = toString(dequoteString(*second));
                        else
                        {
                            unget(*second);
                            names[1] = names[0];
                        }
                    }
                    else
                        names[1] = names[0];

                    target->MediumInterface(names[0], names[1], tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'N':
                if (tok->token == "NamedMaterial")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    target->NamedMaterial(toString(n), tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'O':
                if (tok->token == "ObjectBegin")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    target->ObjectBegin(toString(n), tok->loc);
                }
                else if (tok->token == "ObjectEnd")
                    target->ObjectEnd(tok->loc);
                else if (tok->token == "ObjectInstance")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    target->ObjectInstance(toString(n), tok->loc);
                }
                else if (tok->token == "Option")
                {
                    std::string name = toString(dequoteString(*nextToken(TokenRequired)));
                    std::string value = toString(nextToken(TokenRequired)->token);
                    target->Option(name, value, tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'P':
                if (tok->token == "PixelFilter")
                    basicParamListEntrypoint(&Target::PixelFilter, tok->loc);
                else
                    syntaxError(*tok);
                break;

            case 'R':
                if (tok->token == "ReverseOrientation")
                    target->ReverseOrientation(tok->loc);
                else if (tok->token == "Rotate")
                {
                    Float v[4];
                    for (int i = 0; i < 4; ++i)
                        v[i] = parseFloat(*nextToken(TokenRequired));
                    target->Rotate(v[0], v[1], v[2], v[3], tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'S':
                if (tok->token == "Shape")
                    basicParamListEntrypoint(&Target::Shape, tok->loc);
                else if (tok->token == "Sampler")
                    basicParamListEntrypoint(&Target::Sampler, tok->loc);
                else if (tok->token == "Scale")
                {
                    Float v[3];
                    for (int i = 0; i < 3; ++i)
                        v[i] = parseFloat(*nextToken(TokenRequired));
                    target->Scale(v[0], v[1], v[2], tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'T':
                if (tok->token == "TransformBegin")
                {
                    if (!warnedTransformBeginEndDeprecated)
                    {
                        Warning(&tok->loc, "TransformBegin/End are deprecated and should "
                                "be replaced with AttributeBegin/End");
                        warnedTransformBeginEndDeprecated = true;
                    }
                    target->AttributeBegin(tok->loc);
                }
                else if (tok->token == "TransformEnd")
                {
                    target->AttributeEnd(tok->loc);
                }
                else if (tok->token == "Transform")
                {
                    if (nextToken(TokenRequired)->token != "[")
                        syntaxError(*tok);
                    Float m[16];
                    for (int i = 0; i < 16; ++i)
                        m[i] = parseFloat(*nextToken(TokenRequired));
                    if (nextToken(TokenRequired)->token != "]")
                        syntaxError(*tok);
                    target->Transform(m, tok->loc);
                }
                else if (tok->token == "Translate")
                {
                    Float v[3];
                    for (int i = 0; i < 3; ++i)
                        v[i] = parseFloat(*nextToken(TokenRequired));
                    target->Translate(v[0], v[1], v[2], tok->loc);
                }
                else if (tok->token == "TransformTimes")
                {
                    Float v[2];
                    for (int i = 0; i < 2; ++i)
                        v[i] = parseFloat(*nextToken(TokenRequired));
                    target->TransformTimes(v[0], v[1], tok->loc);
                }
                else if (tok->token == "Texture")
                {
                    std::string_view n = dequoteString(*nextToken(TokenRequired));
                    std::string name = toString(n);
                    n = dequoteString(*nextToken(TokenRequired));
                    std::string type = toString(n);

                    Token t = *nextToken(TokenRequired);
                    std::string_view dequoted = dequoteString(t);
                    std::string texName = toString(dequoted);
                    ParsedParameterVector params = parseParameters(
                        nextToken, unget, [&](const Token& t, const char* msg)
                        {
                            std::string token = toString(t.token);
                            std::string str = StringPrintf("%s: %s", token, msg);
                            parseError(str.c_str(), &t.loc);
                        });

                    target->Texture(name, type, texName, std::move(params), tok->loc);
                }
                else
                    syntaxError(*tok);
                break;

            case 'W':
                if (tok->token == "WorldBegin")
                    target->WorldBegin(tok->loc);
                else
                    syntaxError(*tok);
                break;

            default:
                syntaxError(*tok);
            }
        }

        for (auto& import : imports)
        {
            import.first->Wait();

            target->MergeImported(std::move(import.second));
        }
    }

    void ParseFiles(SceneBuilder* target, pstd::span<const std::string> filenames)
    {
        auto tokError = [](const char* msg, const FileLoc* loc)
        {
            ErrorExit(loc, "%s", msg);
        };

        // Process scene description
        if (filenames.empty())
        {
            // Parse scene from standard input
            std::unique_ptr<Tokenizer> t = Tokenizer::CreateFromFile("-", tokError);
            if (t)
                parse(target, std::move(t));
        }
        else
        {
            // Parse scene from input files
            for (const std::string& fn : filenames)
            {
                if (fn != "-")
                    SetSearchDirectory(fn);

                std::unique_ptr<Tokenizer> t = Tokenizer::CreateFromFile(fn, tokError);
                if (t)
                    parse(target, std::move(t));
            }
        }

        target->EndOfFiles();
    }

    void ParseString(SceneBuilder* target, std::string str)
    {
        auto tokError = [](const char* msg, const FileLoc* loc)
        {
            ErrorExit(loc, "%s", msg);
        };
        std::unique_ptr<Tokenizer> t = Tokenizer::CreateFromString(std::move(str), tokError);
        if (!t)
            return;
        parse(target, std::move(t));

        target->EndOfFiles();
    }

    void ParseFiles(SceneDescriptionBuilder* target, pstd::span<const std::string> filenames)
    {
        auto tokError = [](const char* msg, const FileLoc* loc)
        {
            ErrorExit(loc, "%s", msg);
        };

        if (filenames.empty())
        {
            std::unique_ptr<Tokenizer> t = Tokenizer::CreateFromFile("-", tokError);
            if (t)
                parse(target, std::move(t));
        }
        else
        {
            for (const std::string& fn : filenames)
            {
                if (fn != "-")
                    SetSearchDirectory(fn);

                std::unique_ptr<Tokenizer> t = Tokenizer::CreateFromFile(fn, tokError);
                if (t)
                    parse(target, std::move(t));
            }
        }

        target->EndOfFiles();
    }

    void ParseString(SceneDescriptionBuilder* target, std::string str)
    {
        auto tokError = [](const char* msg, const FileLoc* loc)
        {
            ErrorExit(loc, "%s", msg);
        };
        std::unique_ptr<Tokenizer> t = Tokenizer::CreateFromString(std::move(str), tokError);
        if (!t)
            return;
        parse(target, std::move(t));

        target->EndOfFiles();
    }
} // namespace spectra::scene


namespace spectra::scene
{
    InternCache<std::string> SceneEntity::internedStrings(Allocator{});

    static std::string NormalizeSceneOptionName(const std::string& str)
    {
        std::string ret;
        for (unsigned char c : str)
        {
            if (c != '_' && c != '-')
                ret += std::tolower(c);
        }
        return ret;
    }


    SceneBuilder::GraphicsState::GraphicsState()
    {
        currentMaterialIndex = 0;
    }

    // API State Macros
#define VERIFY_OPTIONS(func)                                   \
    if (currentBlock == BlockState::WorldBlock) {              \
        spectra::ErrorExit(&loc,                                        \
                  "Options cannot be set inside world block; " \
                  "\"%s\" is not allowed.",                    \
                  func);                                       \
        return;                                                \
    } else /* swallow trailing semicolon */
#define VERIFY_WORLD(func)                                         \
    if (currentBlock == BlockState::OptionsBlock) {                \
        spectra::ErrorExit(&loc,                                            \
                  "Scene description must be inside world block; " \
                  "\"%s\" is not allowed.",                        \
                  func);                                           \
        return;                                                    \
    } else /* swallow trailing semicolon */

    [[nodiscard]] ParsedParameter* MakeIntegerParameter(std::string_view name, int value, const FileLoc& location)
    {
        ParsedParameter* parameter = new ParsedParameter(location);
        parameter->type = "integer";
        parameter->name = std::string{name};
        parameter->AddInt(value);
        return parameter;
    }

    [[nodiscard]] ParsedParameterVector ApplyFilmResolutionOverride(ParsedParameterVector parameters, Point2i resolution, const FileLoc& location)
    {
        if (resolution.x <= 0 || resolution.y <= 0)
            ErrorExit(&location, "Spectra interactive film resolution must be positive.");
        for (auto iterator = parameters.begin(); iterator != parameters.end();)
        {
            ParsedParameter* parameter = *iterator;
            if (parameter == nullptr)
                ErrorExit(&location, "Film parameter list contains a null parameter.");
            if (parameter->name == "xresolution" || parameter->name == "yresolution")
            {
                delete parameter;
                iterator = parameters.erase(iterator);
            }
            else
                ++iterator;
        }
        parameters.push_back(MakeIntegerParameter("xresolution", resolution.x, location));
        parameters.push_back(MakeIntegerParameter("yresolution", resolution.y, location));
        return parameters;
    }

    // SceneBuilder Method Definitions
    SceneBuilder::SceneBuilder(Scene* scene)
        : scene(scene),
          transformCache(Allocator(&CUDATrackedMemoryResource::singleton))
    {
        // Set scene defaults
        camera.name = SceneEntity::internedStrings.Lookup("perspective");
        sampler.name = SceneEntity::internedStrings.Lookup("zsobol");
        filter.name = SceneEntity::internedStrings.Lookup("gaussian");
        integrator.name = SceneEntity::internedStrings.Lookup("volpath");
        accelerator.name = SceneEntity::internedStrings.Lookup("bvh");

        film.name = SceneEntity::internedStrings.Lookup("rgb");
        film.parameters = ParameterDictionary({}, RGBColorSpace::sRGB);

        ParameterDictionary dict({}, RGBColorSpace::sRGB);
        currentMaterialIndex = scene->AddMaterial(SceneEntity("diffuse", dict, {}));
    }

    SceneBuilder::SceneBuilder(Scene* scene, Point2i filmResolutionOverride)
        : SceneBuilder(scene)
    {
        if (filmResolutionOverride.x <= 0 || filmResolutionOverride.y <= 0)
            ErrorExit("Spectra interactive film resolution must be positive.");
        this->filmResolutionOverride = filmResolutionOverride;
    }

    void SceneBuilder::ReverseOrientation(FileLoc loc)
    {
        VERIFY_WORLD("ReverseOrientation");
        graphicsState.reverseOrientation = !graphicsState.reverseOrientation;
    }

    void SceneBuilder::ColorSpace(const std::string& name, FileLoc loc)
    {
        if (const RGBColorSpace* cs = RGBColorSpace::GetNamed(name))
            graphicsState.colorSpace = cs;
        else
            Error(&loc, "%s: color space unknown", name);
    }

    void SceneBuilder::Identity(FileLoc loc)
    {
        graphicsState.ForActiveTransforms([](auto t) { return spectra::Transform(); });
    }

    void SceneBuilder::Translate(Float dx, Float dy, Float dz, FileLoc loc)
    {
        graphicsState.ForActiveTransforms(
            [=](auto t) { return t * spectra::Translate(Vector3f(dx, dy, dz)); });
    }

    void SceneBuilder::CoordinateSystem(const std::string& origName, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        namedCoordinateSystems[name] = graphicsState.ctm;
    }

    void SceneBuilder::CoordSysTransform(const std::string& origName, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        if (namedCoordinateSystems.find(name) != namedCoordinateSystems.end())
            graphicsState.ctm = namedCoordinateSystems[name];
        else
            Warning(&loc, "Couldn't find named coordinate system \"%s\"", name);
    }

    void SceneBuilder::Camera(const std::string& name, ParsedParameterVector params,
                                   FileLoc loc)
    {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);

        VERIFY_OPTIONS("spectra::Camera");

        TransformSet cameraFromWorld = graphicsState.ctm;
        TransformSet worldFromCamera = Inverse(graphicsState.ctm);
        namedCoordinateSystems["camera"] = Inverse(cameraFromWorld);

        CameraTransform cameraTransform(
            AnimatedTransform(worldFromCamera[0], graphicsState.transformStartTime,
                              worldFromCamera[1], graphicsState.transformEndTime));
        renderFromWorld = cameraTransform.RenderFromWorld();

        camera = CameraSceneEntity(name, std::move(dict), loc, cameraTransform,
                                   graphicsState.currentOutsideMedium);
    }

    void SceneBuilder::AttributeBegin(FileLoc loc)
    {
        VERIFY_WORLD("AttributeBegin");
        pushedGraphicsStates.push_back(graphicsState);
        pushStack.push_back(std::make_pair('a', loc));
    }

    void SceneBuilder::AttributeEnd(FileLoc loc)
    {
        VERIFY_WORLD("AttributeEnd");
        // Issue error on unmatched _AttributeEnd_
        if (pushedGraphicsStates.empty())
        {
            Error(&loc, "Unmatched AttributeEnd encountered. Ignoring it.");
            return;
        }

        // NOTE: must keep the following consistent with code in ObjectEnd
        graphicsState = std::move(pushedGraphicsStates.back());
        pushedGraphicsStates.pop_back();

        if (pushStack.back().first == 'o')
            ErrorExitDeferred(&loc,
                              "Mismatched nesting: open ObjectBegin from %s at AttributeEnd",
                              pushStack.back().second.ToString());
        else
            SPECTRA_CHECK_EQ(pushStack.back().first, 'a');
        pushStack.pop_back();
    }

    void SceneBuilder::Attribute(const std::string& target, ParsedParameterVector attrib,
                                      FileLoc loc)
    {
        ParsedParameterVector* currentAttributes = nullptr;
        if (target == "shape")
        {
            currentAttributes = &graphicsState.shapeAttributes;
        }
        else if (target == "light")
        {
            currentAttributes = &graphicsState.lightAttributes;
        }
        else if (target == "material")
        {
            currentAttributes = &graphicsState.materialAttributes;
        }
        else if (target == "medium")
        {
            currentAttributes = &graphicsState.mediumAttributes;
        }
        else if (target == "texture")
        {
            currentAttributes = &graphicsState.textureAttributes;
        }
        else
        {
            ErrorExitDeferred(
                &loc,
                "Unknown attribute target \"%s\". Must be \"shape\", \"light\", "
                "\"material\", \"medium\", or \"texture\".",
                target);
            return;
        }

        // Note that we hold on to the current color space and associate it
        // with the parameters...
        for (ParsedParameter* p : attrib)
        {
            p->mayBeUnused = true;
            p->colorSpace = graphicsState.colorSpace;
            currentAttributes->push_back(p);
        }
    }

    void SceneBuilder::Sampler(const std::string& name, ParsedParameterVector params,
                                    FileLoc loc)
    {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        VERIFY_OPTIONS("spectra::Sampler");
        sampler = SceneEntity(name, std::move(dict), loc);
    }

    void SceneBuilder::WorldBegin(FileLoc loc)
    {
        VERIFY_OPTIONS("WorldBegin");
        if (filmResolutionOverride.has_value() && !filmSeen)
            Film("rgb", ParsedParameterVector{}, loc);
        // Reset graphics state for _WorldBegin_
        currentBlock = BlockState::WorldBlock;
        for (int i = 0; i < MaxTransforms; ++i)
            graphicsState.ctm[i] = spectra::Transform();
        graphicsState.activeTransformBits = AllTransformsBits;
        namedCoordinateSystems["world"] = graphicsState.ctm;

        // Pass pre-_WorldBegin_ entities to _scene_
        scene->SetOptions(filter, film, camera, sampler, integrator, accelerator);
    }

    void SceneBuilder::MakeNamedMedium(const std::string& origName,
                                            ParsedParameterVector params, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        // Issue error if medium _name_ is multiply defined
        if (mediumNames.find(name) != mediumNames.end())
        {
            ErrorExitDeferred(&loc, "Named medium \"%s\" redefined.", name);
            return;
        }
        mediumNames.insert(name);

        // Create _ParameterDictionary_ for medium and call _AddMedium()_
        ParameterDictionary dict(std::move(params), graphicsState.mediumAttributes,
                                 graphicsState.colorSpace);
        scene->AddMedium(MediumSceneEntity(name, std::move(dict), loc, RenderFromObject()));
    }

    void SceneBuilder::LightSource(const std::string& name, ParsedParameterVector params,
                                        FileLoc loc)
    {
        VERIFY_WORLD("LightSource");
        ParameterDictionary dict(std::move(params), graphicsState.lightAttributes,
                                 graphicsState.colorSpace);
        scene->AddLight(LightSceneEntity(name, std::move(dict), loc, RenderFromObject(),
                                         graphicsState.currentOutsideMedium));
    }

    void SceneBuilder::Shape(const std::string& name, ParsedParameterVector params,
                                  FileLoc loc)
    {
        VERIFY_WORLD("spectra::Shape");

        ParameterDictionary dict(std::move(params), graphicsState.shapeAttributes,
                                 graphicsState.colorSpace);

        int areaLightIndex = -1;
        if (!graphicsState.areaLightName.empty())
        {
            areaLightIndex = scene->AddAreaLight(SceneEntity(graphicsState.areaLightName,
                                                             graphicsState.areaLightParams,
                                                             graphicsState.areaLightLoc));
            if (activeInstanceDefinition)
                Warning(&loc, "Area lights not supported with object instancing");
        }

        if (CTMIsAnimated())
        {
            AnimatedTransform renderFromShape = RenderFromObject();
            const spectra::Transform* identity = transformCache.Lookup(spectra::Transform());

            AnimatedShapeSceneEntity entity(
                {
                    name, std::move(dict), loc, renderFromShape, identity,
                    graphicsState.reverseOrientation, graphicsState.currentMaterialIndex,
                    graphicsState.currentMaterialName, areaLightIndex,
                    graphicsState.currentInsideMedium, graphicsState.currentOutsideMedium
                });

            if (activeInstanceDefinition)
                activeInstanceDefinition->entity.animatedShapes.push_back(std::move(entity));
            else
                scene->AddAnimatedShape(std::move(entity));
        }
        else
        {
            const spectra::Transform* renderFromObject =
                transformCache.Lookup(RenderFromObject(0));
            const spectra::Transform* objectFromRender =
                transformCache.Lookup(spectra::Inverse(*renderFromObject));

            ShapeSceneEntity entity(
                {
                    name, std::move(dict), loc, renderFromObject, objectFromRender,
                    graphicsState.reverseOrientation, graphicsState.currentMaterialIndex,
                    graphicsState.currentMaterialName, areaLightIndex,
                    graphicsState.currentInsideMedium, graphicsState.currentOutsideMedium
                });
            if (activeInstanceDefinition)
                activeInstanceDefinition->entity.shapes.push_back(std::move(entity));
            else
                shapes.push_back(std::move(entity));
        }
    }

    void SceneBuilder::ObjectBegin(const std::string& origName, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);

        VERIFY_WORLD("ObjectBegin");
        pushedGraphicsStates.push_back(graphicsState);

        pushStack.push_back(std::make_pair('o', loc));

        if (activeInstanceDefinition)
        {
            ErrorExitDeferred(&loc, "ObjectBegin called inside of instance definition");
            return;
        }

        if (instanceNames.find(name) != instanceNames.end())
        {
            ErrorExitDeferred(&loc, "%s: trying to redefine an object instance", name);
            return;
        }
        instanceNames.insert(name);

        activeInstanceDefinition = new ActiveInstanceDefinition(name, loc);
    }

    void SceneBuilder::ObjectEnd(FileLoc loc)
    {
        VERIFY_WORLD("ObjectEnd");
        if (!activeInstanceDefinition)
        {
            ErrorExitDeferred(&loc, "ObjectEnd called outside of instance definition");
            return;
        }
        if (activeInstanceDefinition->parent)
        {
            ErrorExitDeferred(&loc, "ObjectEnd called inside Import for instance definition");
            return;
        }

        // NOTE: Must keep the following consistent with AttributeEnd
        graphicsState = std::move(pushedGraphicsStates.back());
        pushedGraphicsStates.pop_back();

        if (pushStack.back().first == 'a')
            ErrorExitDeferred(&loc,
                              "Mismatched nesting: open AttributeBegin from %s at ObjectEnd",
                              pushStack.back().second.ToString());
        else
            SPECTRA_CHECK_EQ(pushStack.back().first, 'o');
        pushStack.pop_back();

        // Otherwise it will be taken care of in MergeImported()
        if (--activeInstanceDefinition->activeImports == 0)
        {
            scene->AddInstanceDefinition(std::move(activeInstanceDefinition->entity));
            delete activeInstanceDefinition;
        }

        activeInstanceDefinition = nullptr;
    }

    void SceneBuilder::ObjectInstance(const std::string& origName, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        VERIFY_WORLD("ObjectInstance");

        if (activeInstanceDefinition)
        {
            ErrorExitDeferred(&loc,
                              "ObjectInstance can't be called inside instance definition");
            return;
        }

        spectra::Transform worldFromRender = spectra::Inverse(renderFromWorld);

        if (CTMIsAnimated())
        {
            AnimatedTransform animatedRenderFromInstance(
                RenderFromObject(0) * worldFromRender, graphicsState.transformStartTime,
                RenderFromObject(1) * worldFromRender, graphicsState.transformEndTime);

            // For very small changes, animatedRenderFromInstance may have both
            // xforms equal even if CTMIsAnimated() has returned true. Fall
            // through to create a regular non-animated instance in that case.
            if (animatedRenderFromInstance.IsAnimated())
            {
                instanceUses.push_back(
                    InstanceSceneEntity(name, loc, animatedRenderFromInstance));
                return;
            }
        }

        const spectra::Transform* renderFromInstance =
            transformCache.Lookup(RenderFromObject(0) * worldFromRender);
        instanceUses.push_back(InstanceSceneEntity(name, loc, renderFromInstance));
    }

    void SceneBuilder::EndOfFiles()
    {
        if (currentBlock != BlockState::WorldBlock)
            ErrorExitDeferred("End of files before \"WorldBegin\".");

        // Ensure there are no pushed graphics states
        while (!pushedGraphicsStates.empty())
        {
            ErrorExitDeferred("Missing end to AttributeBegin");
            pushedGraphicsStates.pop_back();
        }

        if (errorExit)
            ErrorExit("Fatal errors during scene construction");

        if (!shapes.empty())
            scene->AddShapes(shapes);
        if (!instanceUses.empty())
            scene->AddInstanceUses(instanceUses);

    }

    std::unique_ptr<SceneBuilder> SceneBuilder::CopyForImport()
    {
        std::unique_ptr<SceneBuilder> importBuilder = std::make_unique<SceneBuilder>(scene);
        importBuilder->renderFromWorld = renderFromWorld;
        importBuilder->graphicsState = graphicsState;
        importBuilder->currentBlock = currentBlock;
        importBuilder->filmResolutionOverride = filmResolutionOverride;
        importBuilder->filmSeen = filmSeen;
        if (activeInstanceDefinition)
        {
            importBuilder->activeInstanceDefinition = new ActiveInstanceDefinition(
                activeInstanceDefinition->entity.name, activeInstanceDefinition->entity.loc);

            // In case of nested imports, go up to the true root parent since
            // that's where we need to merge our shapes and that's where the
            // refcount is.
            ActiveInstanceDefinition* parent = activeInstanceDefinition;
            while (parent->parent)
                parent = parent->parent;
            importBuilder->activeInstanceDefinition->parent = parent;
            ++parent->activeImports;
        }
        return importBuilder;
    }

    bool SceneBuilder::IsImportAllowed() const
    {
        return currentBlock == BlockState::WorldBlock;
    }

    void SceneBuilder::MergeImported(std::unique_ptr<SceneBuilder> imported)
    {
        MergeImported(imported.get());
        importedBuilders.push_back(std::move(imported));
    }

    void SceneBuilder::MergeImported(SceneBuilder* imported)
    {
        while (!imported->pushedGraphicsStates.empty())
        {
            ErrorExitDeferred("Missing end to AttributeBegin");
            imported->pushedGraphicsStates.pop_back();
        }

        errorExit |= imported->errorExit;

        if (!imported->shapes.empty())
            scene->AddShapes(imported->shapes);
        if (!imported->instanceUses.empty())
            scene->AddInstanceUses(imported->instanceUses);

        auto mergeVector = [](auto& base, auto& imported)
        {
            if (base.empty())
                base = std::move(imported);
            else
            {
                base.reserve(base.size() + imported.size());
                std::move(std::begin(imported), std::end(imported), std::back_inserter(base));
                imported.clear();
                imported.shrink_to_fit();
            }
        };
        if (imported->activeInstanceDefinition)
        {
            ActiveInstanceDefinition* current = imported->activeInstanceDefinition;
            ActiveInstanceDefinition* parent = current->parent;
            SPECTRA_CHECK(parent != nullptr);

            std::lock_guard<std::mutex> lock(parent->mutex);
            mergeVector(parent->entity.shapes, current->entity.shapes);
            mergeVector(parent->entity.animatedShapes, current->entity.animatedShapes);

            delete current;

            if (--parent->activeImports == 0)
                scene->AddInstanceDefinition(std::move(parent->entity));
        }

        auto mergeSet = [this](auto& base, auto& imported, const char* name)
        {
            for (const auto& item : imported)
            {
                if (base.find(item) != base.end())
                    ErrorExitDeferred("%s: multiply defined %s.", item, name);
                base.insert(std::move(item));
            }
            imported.clear();
        };
        mergeSet(namedMaterialNames, imported->namedMaterialNames, "named material");
        mergeSet(floatTextureNames, imported->floatTextureNames, "texture");
        mergeSet(spectrumTextureNames, imported->spectrumTextureNames, "texture");
    }

    void SceneDescription::Clear()
    {
        pixelFilter = {};
        film = {};
        sampler = {};
        accelerator = {};
        integrator = {};
        camera = {};
        textures.clear();
        materials.clear();
        mediums.clear();
        mediumBindings.clear();
        lights.clear();
        shapes.clear();
        objectDefinitions.clear();
        objectInstances.clear();
    }

    [[nodiscard]] SceneDescriptionFileLocation CopySceneDescriptionFileLocation(const FileLoc& location)
    {
        return {std::string{location.filename}, location.line, location.column};
    }

    void DeleteParsedParameters(ParsedParameterVector& parameters)
    {
        for (ParsedParameter* parameter : parameters)
            delete parameter;
        parameters.clear();
    }

    [[nodiscard]] std::vector<SceneDescriptionParameter> CopySceneDescriptionParameters(ParsedParameterVector parameters)
    {
        std::vector<SceneDescriptionParameter> copiedParameters{};
        copiedParameters.reserve(parameters.size());
        for (const ParsedParameter* parameter : parameters)
        {
            if (parameter == nullptr)
                ErrorExit("Scene description parser produced a null parameter.");
            SceneDescriptionParameter copiedParameter{};
            copiedParameter.type = parameter->type;
            copiedParameter.name = parameter->name;
            copiedParameter.location = CopySceneDescriptionFileLocation(parameter->loc);
            copiedParameter.mayBeUnused = parameter->mayBeUnused;
            copiedParameter.floats.reserve(parameter->floats.size());
            copiedParameter.ints.reserve(parameter->ints.size());
            copiedParameter.strings.reserve(parameter->strings.size());
            copiedParameter.bools.reserve(parameter->bools.size());
            for (const Float value : parameter->floats)
                copiedParameter.floats.push_back(static_cast<float>(value));
            for (const int value : parameter->ints)
                copiedParameter.ints.push_back(value);
            for (const std::string& value : parameter->strings)
                copiedParameter.strings.push_back(value);
            for (const std::uint8_t value : parameter->bools)
                copiedParameter.bools.push_back(value);
            copiedParameters.push_back(std::move(copiedParameter));
        }
        DeleteParsedParameters(parameters);
        return copiedParameters;
    }

    [[nodiscard]] Transform SceneDescriptionTransformFromParserMatrix(const Float transform[16])
    {
        return Transpose(Transform{spectra::SquareMatrix<4>{pstd::MakeSpan(transform, 16)}});
    }

    [[nodiscard]] std::string FirstSceneDescriptionStringParameterValue(const std::vector<SceneDescriptionParameter>& parameters, const std::string& parameterName)
    {
        for (const SceneDescriptionParameter& parameter : parameters)
            if (parameter.name == parameterName && !parameter.strings.empty())
                return parameter.strings.front();
        return {};
    }

    template <typename Value>
    void AppendSceneDescriptionVector(std::vector<Value>& destination, std::vector<Value>& source)
    {
        destination.reserve(destination.size() + source.size());
        for (Value& value : source)
            destination.push_back(std::move(value));
        source.clear();
    }

    void MergeSceneDescriptionSetting(SceneDescriptionRenderSetting& destination, SceneDescriptionRenderSetting& source)
    {
        if (source.present)
            destination = std::move(source);
        source = {};
    }

    std::size_t FindOrCreateSceneDescriptionObjectDefinition(std::vector<SceneDescriptionObjectDefinition>& objectDefinitions, const std::string& name, const SceneDescriptionFileLocation& location)
    {
        for (std::size_t index = 0; index < objectDefinitions.size(); ++index)
            if (objectDefinitions[index].name == name)
                return index;
        SceneDescriptionObjectDefinition objectDefinition{};
        objectDefinition.name = name;
        objectDefinition.location = location;
        objectDefinitions.push_back(std::move(objectDefinition));
        return objectDefinitions.size() - 1;
    }

    struct SceneDescriptionBuildChunk
    {
        SceneDescriptionRenderSetting pixelFilter{};
        SceneDescriptionRenderSetting film{};
        SceneDescriptionRenderSetting sampler{};
        SceneDescriptionRenderSetting accelerator{};
        SceneDescriptionRenderSetting integrator{};
        SceneDescriptionRenderSetting camera{};
        std::vector<SceneDescriptionTexture> textures{};
        std::vector<SceneDescriptionMaterial> materials{};
        std::vector<SceneDescriptionMedium> mediums{};
        std::vector<SceneDescriptionMediumBinding> mediumBindings{};
        std::vector<SceneDescriptionLight> lights{};
        std::vector<SceneDescriptionShape> shapes{};
        std::vector<SceneDescriptionObjectDefinition> objectDefinitions{};
        std::vector<SceneDescriptionObjectInstance> objectInstances{};
    };

    void AppendSceneDescriptionBuildChunk(SceneDescription& description, SceneDescriptionBuildChunk chunk)
    {
        MergeSceneDescriptionSetting(description.pixelFilter, chunk.pixelFilter);
        MergeSceneDescriptionSetting(description.film, chunk.film);
        MergeSceneDescriptionSetting(description.sampler, chunk.sampler);
        MergeSceneDescriptionSetting(description.accelerator, chunk.accelerator);
        MergeSceneDescriptionSetting(description.integrator, chunk.integrator);
        MergeSceneDescriptionSetting(description.camera, chunk.camera);
        AppendSceneDescriptionVector(description.textures, chunk.textures);
        AppendSceneDescriptionVector(description.materials, chunk.materials);
        AppendSceneDescriptionVector(description.mediums, chunk.mediums);
        AppendSceneDescriptionVector(description.mediumBindings, chunk.mediumBindings);
        AppendSceneDescriptionVector(description.lights, chunk.lights);
        AppendSceneDescriptionVector(description.objectDefinitions, chunk.objectDefinitions);
        for (SceneDescriptionShape& shape : chunk.shapes)
        {
            const std::string objectDefinitionName = shape.objectDefinitionName;
            const SceneDescriptionFileLocation shapeLocation = shape.location;
            const std::size_t shapeIndex = description.shapes.size();
            description.shapes.push_back(std::move(shape));
            if (!objectDefinitionName.empty())
            {
                const std::size_t objectDefinitionIndex = FindOrCreateSceneDescriptionObjectDefinition(description.objectDefinitions, objectDefinitionName, shapeLocation);
                description.objectDefinitions[objectDefinitionIndex].shapeIndices.push_back(shapeIndex);
            }
        }
        chunk.shapes.clear();
        AppendSceneDescriptionVector(description.objectInstances, chunk.objectInstances);
    }

    struct SceneDescriptionBuilderGraphicsState
    {
        static constexpr std::uint32_t startTransformBit = 1u << 0u;
        static constexpr std::uint32_t endTransformBit = 1u << 1u;
        static constexpr std::uint32_t allTransformBits = startTransformBit | endTransformBit;

        std::string currentInsideMedium{};
        std::string currentOutsideMedium{};
        std::string currentMaterialName{};
        int currentMaterialIndex = -1;
        std::string areaLightType{};
        SceneDescriptionFileLocation areaLightLocation{};
        std::vector<SceneDescriptionParameter> areaLightParameters{};
        bool reverseOrientation = false;
        TransformSet ctm{};
        std::uint32_t activeTransformBits = allTransformBits;
        Float transformStartTime = 0.0f;
        Float transformEndTime = 1.0f;
    };

    struct SceneDescriptionBuilderState
    {
        SceneDescription* description = nullptr;
        SceneDescriptionBuildChunk chunk{};
        SceneDescriptionBuilderGraphicsState graphicsState{};
        std::vector<SceneDescriptionBuilderGraphicsState> pushedGraphicsStates{};
        std::map<std::string, TransformSet> namedCoordinateSystems{};
        std::vector<std::unique_ptr<SceneDescriptionBuilder>> importedBuilders{};
        std::string activeObjectDefinitionName{};
        std::size_t materialIndexBase = 0;
        bool rootBuilder = true;
        bool worldBegun = false;

        explicit SceneDescriptionBuilderState(SceneDescription* description)
            : description(description)
        {
        }

        SceneDescriptionBuilderState(SceneDescription* description, const SceneDescriptionBuilderGraphicsState& parentGraphicsState,
                                     const std::vector<SceneDescriptionBuilderGraphicsState>& parentPushedGraphicsStates,
                                     const std::map<std::string, TransformSet>& parentNamedCoordinateSystems,
                                     const std::string& parentActiveObjectDefinitionName, std::size_t parentMaterialIndexBase,
                                     bool parentWorldBegun)
            : description(description),
              graphicsState(parentGraphicsState),
              pushedGraphicsStates(parentPushedGraphicsStates),
              namedCoordinateSystems(parentNamedCoordinateSystems),
              activeObjectDefinitionName(parentActiveObjectDefinitionName),
              materialIndexBase(parentMaterialIndexBase),
              rootBuilder(false),
              worldBegun(parentWorldBegun)
        {
        }

        void MergeChunk(SceneDescriptionBuildChunk& importedChunk)
        {
            MergeSceneDescriptionSetting(chunk.pixelFilter, importedChunk.pixelFilter);
            MergeSceneDescriptionSetting(chunk.film, importedChunk.film);
            MergeSceneDescriptionSetting(chunk.sampler, importedChunk.sampler);
            MergeSceneDescriptionSetting(chunk.accelerator, importedChunk.accelerator);
            MergeSceneDescriptionSetting(chunk.integrator, importedChunk.integrator);
            MergeSceneDescriptionSetting(chunk.camera, importedChunk.camera);
            AppendSceneDescriptionVector(chunk.textures, importedChunk.textures);
            AppendSceneDescriptionVector(chunk.materials, importedChunk.materials);
            AppendSceneDescriptionVector(chunk.mediums, importedChunk.mediums);
            AppendSceneDescriptionVector(chunk.mediumBindings, importedChunk.mediumBindings);
            AppendSceneDescriptionVector(chunk.lights, importedChunk.lights);
            AppendSceneDescriptionVector(chunk.shapes, importedChunk.shapes);
            AppendSceneDescriptionVector(chunk.objectDefinitions, importedChunk.objectDefinitions);
            AppendSceneDescriptionVector(chunk.objectInstances, importedChunk.objectInstances);
        }

        [[nodiscard]] bool CurrentTransformIsAnimated() const
        {
            return graphicsState.ctm.IsAnimated();
        }

        [[nodiscard]] Transform CurrentTransform() const
        {
            return graphicsState.ctm[0];
        }

        [[nodiscard]] SceneDescriptionRenderSetting MakeRenderSetting(const std::string& type, const std::string& name, const std::vector<SceneDescriptionParameter>& parameters, const FileLoc& location, bool includeTransform) const
        {
            SceneDescriptionRenderSetting setting{};
            setting.present = true;
            setting.type = type;
            setting.name = name;
            setting.location = CopySceneDescriptionFileLocation(location);
            setting.transform = includeTransform ? CurrentTransform() : Transform{};
            setting.parameters = parameters;
            return setting;
        }

        void ApplyTransformToActive(const Transform& transform)
        {
            for (int index = 0; index < MaxTransforms; ++index)
            {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((graphicsState.activeTransformBits & bit) != 0u)
                    graphicsState.ctm[index] = graphicsState.ctm[index] * transform;
            }
        }

        void ReplaceActiveTransform(const Transform& transform)
        {
            for (int index = 0; index < MaxTransforms; ++index)
            {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((graphicsState.activeTransformBits & bit) != 0u)
                    graphicsState.ctm[index] = transform;
            }
        }
    };

    SceneDescriptionBuilder::SceneDescriptionBuilder(SceneDescription* description)
        : state(std::make_unique<SceneDescriptionBuilderState>(description))
    {
        if (description == nullptr)
            ErrorExit("SceneDescriptionBuilder requires a non-null SceneDescription.");
    }

    SceneDescriptionBuilder::~SceneDescriptionBuilder() = default;

    void SceneDescriptionBuilder::Scale(Float sx, Float sy, Float sz, FileLoc loc)
    {
        (void)loc;
        state->ApplyTransformToActive(spectra::Scale(sx, sy, sz));
    }

    void SceneDescriptionBuilder::Shape(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        SceneDescriptionShape shape{};
        shape.type = name;
        shape.materialName = state->graphicsState.currentMaterialName;
        shape.materialIndex = state->graphicsState.currentMaterialIndex;
        shape.insideMedium = state->graphicsState.currentInsideMedium;
        shape.outsideMedium = state->graphicsState.currentOutsideMedium;
        shape.objectDefinitionName = state->activeObjectDefinitionName;
        shape.areaLightType = state->graphicsState.areaLightType;
        shape.reverseOrientation = state->graphicsState.reverseOrientation;
        shape.animatedTransform = state->CurrentTransformIsAnimated();
        shape.location = CopySceneDescriptionFileLocation(loc);
        shape.transform = state->CurrentTransform();
        shape.parameters = copiedParameters;
        state->chunk.shapes.push_back(std::move(shape));

        if (!state->graphicsState.areaLightType.empty())
        {
            SceneDescriptionLight light{};
            light.type = state->graphicsState.areaLightType;
            light.area = true;
            light.outsideMedium = state->graphicsState.currentOutsideMedium;
            light.location = state->graphicsState.areaLightLocation;
            light.transform = state->CurrentTransform();
            light.parameters = state->graphicsState.areaLightParameters;
            state->chunk.lights.push_back(std::move(light));
        }
    }

    void SceneDescriptionBuilder::Option(const std::string& name, const std::string& value, FileLoc loc)
    {
        (void)name;
        (void)value;
        (void)loc;
    }

    void SceneDescriptionBuilder::Identity(FileLoc loc)
    {
        (void)loc;
        state->ReplaceActiveTransform(spectra::Transform{});
    }

    void SceneDescriptionBuilder::Translate(Float dx, Float dy, Float dz, FileLoc loc)
    {
        (void)loc;
        state->ApplyTransformToActive(spectra::Translate(Vector3f{dx, dy, dz}));
    }

    void SceneDescriptionBuilder::Rotate(Float angle, Float ax, Float ay, Float az, FileLoc loc)
    {
        (void)loc;
        state->ApplyTransformToActive(spectra::Rotate(angle, Vector3f{ax, ay, az}));
    }

    void SceneDescriptionBuilder::LookAt(Float ex, Float ey, Float ez, Float lx, Float ly, Float lz, Float ux, Float uy, Float uz, FileLoc loc)
    {
        (void)loc;
        state->ApplyTransformToActive(spectra::LookAt(Point3f{ex, ey, ez}, Point3f{lx, ly, lz}, Vector3f{ux, uy, uz}));
    }

    void SceneDescriptionBuilder::ConcatTransform(Float transform[16], FileLoc loc)
    {
        (void)loc;
        state->ApplyTransformToActive(SceneDescriptionTransformFromParserMatrix(transform));
    }

    void SceneDescriptionBuilder::Transform(Float transform[16], FileLoc loc)
    {
        (void)loc;
        state->ReplaceActiveTransform(SceneDescriptionTransformFromParserMatrix(transform));
    }

    void SceneDescriptionBuilder::CoordinateSystem(const std::string& name, FileLoc loc)
    {
        (void)loc;
        state->namedCoordinateSystems[name] = state->graphicsState.ctm;
    }

    void SceneDescriptionBuilder::CoordSysTransform(const std::string& name, FileLoc loc)
    {
        (void)loc;
        const auto found = state->namedCoordinateSystems.find(name);
        if (found != state->namedCoordinateSystems.end())
            state->graphicsState.ctm = found->second;
    }

    void SceneDescriptionBuilder::ActiveTransformAll(FileLoc loc)
    {
        (void)loc;
        state->graphicsState.activeTransformBits = SceneDescriptionBuilderGraphicsState::allTransformBits;
    }

    void SceneDescriptionBuilder::ActiveTransformEndTime(FileLoc loc)
    {
        (void)loc;
        state->graphicsState.activeTransformBits = SceneDescriptionBuilderGraphicsState::endTransformBit;
    }

    void SceneDescriptionBuilder::ActiveTransformStartTime(FileLoc loc)
    {
        (void)loc;
        state->graphicsState.activeTransformBits = SceneDescriptionBuilderGraphicsState::startTransformBit;
    }

    void SceneDescriptionBuilder::TransformTimes(Float start, Float end, FileLoc loc)
    {
        (void)loc;
        state->graphicsState.transformStartTime = start;
        state->graphicsState.transformEndTime = end;
    }

    void SceneDescriptionBuilder::ColorSpace(const std::string& name, FileLoc loc)
    {
        (void)name;
        (void)loc;
    }

    void SceneDescriptionBuilder::PixelFilter(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        state->chunk.pixelFilter = state->MakeRenderSetting(name, name, copiedParameters, loc, false);
    }

    void SceneDescriptionBuilder::Film(const std::string& type, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        state->chunk.film = state->MakeRenderSetting(type, {}, copiedParameters, loc, false);
    }

    void SceneDescriptionBuilder::Accelerator(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        state->chunk.accelerator = state->MakeRenderSetting(name, name, copiedParameters, loc, false);
    }

    void SceneDescriptionBuilder::Integrator(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        state->chunk.integrator = state->MakeRenderSetting(name, name, copiedParameters, loc, false);
    }

    void SceneDescriptionBuilder::Camera(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        state->chunk.camera = state->MakeRenderSetting(name, name, copiedParameters, loc, true);
        state->namedCoordinateSystems["camera"] = Inverse(state->graphicsState.ctm);
    }

    void SceneDescriptionBuilder::MakeNamedMedium(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        SceneDescriptionMedium medium{};
        medium.name = name;
        medium.type = FirstSceneDescriptionStringParameterValue(copiedParameters, "type");
        medium.location = CopySceneDescriptionFileLocation(loc);
        medium.transform = state->CurrentTransform();
        medium.parameters = copiedParameters;
        state->chunk.mediums.push_back(std::move(medium));
    }

    void SceneDescriptionBuilder::MediumInterface(const std::string& insideName, const std::string& outsideName, FileLoc loc)
    {
        state->graphicsState.currentInsideMedium = insideName;
        state->graphicsState.currentOutsideMedium = outsideName;
        SceneDescriptionMediumBinding binding{};
        binding.inside = insideName;
        binding.outside = outsideName;
        binding.location = CopySceneDescriptionFileLocation(loc);
        state->chunk.mediumBindings.push_back(std::move(binding));
    }

    void SceneDescriptionBuilder::Sampler(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        state->chunk.sampler = state->MakeRenderSetting(name, name, copiedParameters, loc, false);
    }

    void SceneDescriptionBuilder::WorldBegin(FileLoc loc)
    {
        (void)loc;
        state->graphicsState = {};
        state->pushedGraphicsStates.clear();
        state->activeObjectDefinitionName.clear();
        state->namedCoordinateSystems["world"] = state->graphicsState.ctm;
        state->worldBegun = true;
    }

    void SceneDescriptionBuilder::AttributeBegin(FileLoc loc)
    {
        (void)loc;
        state->pushedGraphicsStates.push_back(state->graphicsState);
    }

    void SceneDescriptionBuilder::AttributeEnd(FileLoc loc)
    {
        (void)loc;
        if (!state->pushedGraphicsStates.empty())
        {
            state->graphicsState = state->pushedGraphicsStates.back();
            state->pushedGraphicsStates.pop_back();
        }
    }

    void SceneDescriptionBuilder::Attribute(const std::string& target, ParsedParameterVector parameters, FileLoc loc)
    {
        (void)target;
        (void)loc;
        DeleteParsedParameters(parameters);
    }

    void SceneDescriptionBuilder::Texture(const std::string& name, const std::string& type, const std::string& textureName, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        SceneDescriptionTexture texture{};
        texture.name = name;
        texture.valueType = type == "float" ? SceneDescriptionTextureValueType::Float : type == "spectrum" ? SceneDescriptionTextureValueType::Spectrum : SceneDescriptionTextureValueType::Unknown;
        texture.implementation = textureName;
        texture.location = CopySceneDescriptionFileLocation(loc);
        texture.transform = state->CurrentTransform();
        texture.parameters = copiedParameters;
        state->chunk.textures.push_back(std::move(texture));
    }

    void SceneDescriptionBuilder::Material(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        SceneDescriptionMaterial material{};
        material.type = name;
        material.named = false;
        material.location = CopySceneDescriptionFileLocation(loc);
        material.parameters = copiedParameters;
        state->chunk.materials.push_back(std::move(material));
        state->graphicsState.currentMaterialIndex = static_cast<int>(state->materialIndexBase + state->chunk.materials.size() - 1);
        state->graphicsState.currentMaterialName.clear();
    }

    void SceneDescriptionBuilder::MakeNamedMaterial(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        SceneDescriptionMaterial material{};
        material.name = name;
        material.type = FirstSceneDescriptionStringParameterValue(copiedParameters, "type");
        material.named = true;
        material.location = CopySceneDescriptionFileLocation(loc);
        material.parameters = copiedParameters;
        state->chunk.materials.push_back(std::move(material));
    }

    void SceneDescriptionBuilder::NamedMaterial(const std::string& name, FileLoc loc)
    {
        (void)loc;
        state->graphicsState.currentMaterialName = name;
        state->graphicsState.currentMaterialIndex = -1;
    }

    void SceneDescriptionBuilder::LightSource(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        const std::vector<SceneDescriptionParameter> copiedParameters = CopySceneDescriptionParameters(std::move(parameters));
        SceneDescriptionLight light{};
        light.type = name;
        light.area = false;
        light.outsideMedium = state->graphicsState.currentOutsideMedium;
        light.location = CopySceneDescriptionFileLocation(loc);
        light.transform = state->CurrentTransform();
        light.parameters = copiedParameters;
        state->chunk.lights.push_back(std::move(light));
    }

    void SceneDescriptionBuilder::AreaLightSource(const std::string& name, ParsedParameterVector parameters, FileLoc loc)
    {
        state->graphicsState.areaLightType = name;
        state->graphicsState.areaLightLocation = CopySceneDescriptionFileLocation(loc);
        state->graphicsState.areaLightParameters = CopySceneDescriptionParameters(std::move(parameters));
    }

    void SceneDescriptionBuilder::ReverseOrientation(FileLoc loc)
    {
        (void)loc;
        state->graphicsState.reverseOrientation = !state->graphicsState.reverseOrientation;
    }

    void SceneDescriptionBuilder::ObjectBegin(const std::string& name, FileLoc loc)
    {
        state->pushedGraphicsStates.push_back(state->graphicsState);
        state->activeObjectDefinitionName = name;
        FindOrCreateSceneDescriptionObjectDefinition(state->chunk.objectDefinitions, name, CopySceneDescriptionFileLocation(loc));
    }

    void SceneDescriptionBuilder::ObjectEnd(FileLoc loc)
    {
        (void)loc;
        if (!state->pushedGraphicsStates.empty())
        {
            state->graphicsState = state->pushedGraphicsStates.back();
            state->pushedGraphicsStates.pop_back();
        }
        state->activeObjectDefinitionName.clear();
    }

    void SceneDescriptionBuilder::ObjectInstance(const std::string& name, FileLoc loc)
    {
        SceneDescriptionObjectInstance objectInstance{};
        objectInstance.name = name;
        objectInstance.animatedTransform = state->CurrentTransformIsAnimated();
        objectInstance.location = CopySceneDescriptionFileLocation(loc);
        objectInstance.transform = state->CurrentTransform();
        state->chunk.objectInstances.push_back(std::move(objectInstance));
    }

    void SceneDescriptionBuilder::EndOfFiles()
    {
        if (!state->pushedGraphicsStates.empty())
            ErrorExit("Missing AttributeEnd before EndOfFiles in Spectra scene description parser.");
        if (state->rootBuilder)
        {
            if (state->description == nullptr)
                ErrorExit("SceneDescriptionBuilder has no target description at EndOfFiles.");
            AppendSceneDescriptionBuildChunk(*state->description, std::move(state->chunk));
        }
    }

    bool SceneDescriptionBuilder::IsImportAllowed() const
    {
        return state->worldBegun;
    }

    std::unique_ptr<SceneDescriptionBuilder> SceneDescriptionBuilder::CopyForImport()
    {
        if (state->description == nullptr)
            ErrorExit("Cannot copy Spectra scene description import target without a description.");
        std::unique_ptr<SceneDescriptionBuilder> builder = std::make_unique<SceneDescriptionBuilder>(state->description);
        builder->state = std::make_unique<SceneDescriptionBuilderState>(state->description, state->graphicsState, state->pushedGraphicsStates, state->namedCoordinateSystems, state->activeObjectDefinitionName, state->materialIndexBase + state->chunk.materials.size(), state->worldBegun);
        return builder;
    }

    void SceneDescriptionBuilder::MergeImported(std::unique_ptr<SceneDescriptionBuilder> imported)
    {
        if (imported == nullptr || imported->state == nullptr)
            ErrorExit("Spectra scene description import target is null.");
        state->MergeChunk(imported->state->chunk);
        state->importedBuilders.push_back(std::move(imported));
    }

    void SceneBuilder::Option(const std::string& name, const std::string& value,
                                   FileLoc loc)
    {
        std::string nName = NormalizeSceneOptionName(name);

        if (nName == "disablepixeljitter")
        {
            if (value == "true")
                Options->disablePixelJitter = true;
            else if (value == "false")
                Options->disablePixelJitter = false;
            else
                ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                                  value);
        }
        else if (nName == "disabletexturefiltering")
        {
            if (value == "true")
                Options->disableTextureFiltering = true;
            else if (value == "false")
                Options->disableTextureFiltering = false;
            else
                ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                                  value);
        }
        else if (nName == "disablewavelengthjitter")
        {
            if (value == "true")
                Options->disableWavelengthJitter = true;
            else if (value == "false")
                Options->disableWavelengthJitter = false;
            else
                ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                                  value);
        }
        else if (nName == "displacementedgescale")
        {
            if (!Atof(value, &Options->displacementEdgeScale))
                ErrorExitDeferred(&loc, "%s: expected floating-point option value", value);
        }
        else if (nName == "rendercoordsys")
        {
            if (value.size() < 3 || value.front() != '"' || value.back() != '"')
                ErrorExitDeferred(&loc, "%s: expected quoted string for option value", value);
            std::string renderCoordSys = value.substr(1, value.size() - 2);
            if (renderCoordSys == "camera")
                Options->renderingSpace = RenderingCoordinateSystem::Camera;
            else if (renderCoordSys == "cameraworld")
                Options->renderingSpace = RenderingCoordinateSystem::CameraWorld;
            else if (renderCoordSys == "world")
                Options->renderingSpace = RenderingCoordinateSystem::World;
            else
                ErrorExit("%s: unknown rendering coordinate system.", renderCoordSys);
        }
        else if (nName == "seed")
        {
            Options->seed = std::atoi(value.c_str());
        }
        else
            ErrorExitDeferred(&loc, "%s: unknown option", name);

        CopyOptionsToGPU();
    }

    void SceneBuilder::Transform(Float tr[16], FileLoc loc)
    {
        graphicsState.ForActiveTransforms([=](auto t)
        {
            return Transpose(spectra::Transform(spectra::SquareMatrix<4>(pstd::MakeSpan(tr, 16))));
        });
    }

    void SceneBuilder::ConcatTransform(Float tr[16], FileLoc loc)
    {
        graphicsState.ForActiveTransforms([=](auto t)
        {
            return t * Transpose(spectra::Transform(spectra::SquareMatrix<4>(pstd::MakeSpan(tr, 16))));
        });
    }

    void SceneBuilder::Rotate(Float angle, Float dx, Float dy, Float dz, FileLoc loc)
    {
        graphicsState.ForActiveTransforms(
            [=](auto t) { return t * spectra::Rotate(angle, Vector3f(dx, dy, dz)); });
    }

    void SceneBuilder::Scale(Float sx, Float sy, Float sz, FileLoc loc)
    {
        graphicsState.ForActiveTransforms(
            [=](auto t) { return t * spectra::Scale(sx, sy, sz); });
    }

    void SceneBuilder::LookAt(Float ex, Float ey, Float ez, Float lx, Float ly, Float lz,
                                   Float ux, Float uy, Float uz, FileLoc loc)
    {
        spectra::Transform lookAt =
            spectra::LookAt(Point3f(ex, ey, ez), Point3f(lx, ly, lz), Vector3f(ux, uy, uz));
        graphicsState.ForActiveTransforms([=](auto t) { return t * lookAt; });
    }

    void SceneBuilder::ActiveTransformAll(FileLoc loc)
    {
        graphicsState.activeTransformBits = AllTransformsBits;
    }

    void SceneBuilder::ActiveTransformEndTime(FileLoc loc)
    {
        graphicsState.activeTransformBits = EndTransformBits;
    }

    void SceneBuilder::ActiveTransformStartTime(FileLoc loc)
    {
        graphicsState.activeTransformBits = StartTransformBits;
    }

    void SceneBuilder::TransformTimes(Float start, Float end, FileLoc loc)
    {
        VERIFY_OPTIONS("TransformTimes");
        graphicsState.transformStartTime = start;
        graphicsState.transformEndTime = end;
    }

    void SceneBuilder::PixelFilter(const std::string& name, ParsedParameterVector params,
                                        FileLoc loc)
    {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        VERIFY_OPTIONS("PixelFilter");
        filter = SceneEntity(name, std::move(dict), loc);
    }

    void SceneBuilder::Film(const std::string& type, ParsedParameterVector params,
                                 FileLoc loc)
    {
        VERIFY_OPTIONS("spectra::Film");
        filmSeen = true;
        if (filmResolutionOverride.has_value())
            params = ApplyFilmResolutionOverride(std::move(params), *filmResolutionOverride, loc);
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        film = SceneEntity(type, std::move(dict), loc);
    }

    void SceneBuilder::Accelerator(const std::string& name, ParsedParameterVector params,
                                        FileLoc loc)
    {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
        VERIFY_OPTIONS("Accelerator");
        accelerator = SceneEntity(name, std::move(dict), loc);
    }

    void SceneBuilder::Integrator(const std::string& name, ParsedParameterVector params,
                                       FileLoc loc)
    {
        ParameterDictionary dict(std::move(params), graphicsState.colorSpace);

        VERIFY_OPTIONS("Integrator");
        integrator = SceneEntity(name, std::move(dict), loc);
    }

    void SceneBuilder::MediumInterface(const std::string& origInsideName,
                                            const std::string& origOutsideName, FileLoc loc)
    {
        std::string insideName = NormalizeUTF8(origInsideName);
        std::string outsideName = NormalizeUTF8(origOutsideName);

        graphicsState.currentInsideMedium = insideName;
        graphicsState.currentOutsideMedium = outsideName;
    }

    void SceneBuilder::Texture(const std::string& origName, const std::string& type,
                                    const std::string& texname, ParsedParameterVector params,
                                    FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        VERIFY_WORLD("Texture");

        ParameterDictionary dict(std::move(params), graphicsState.textureAttributes,
                                 graphicsState.colorSpace);

        if (type != "float" && type != "spectrum")
        {
            ErrorExitDeferred(
                &loc, "%s: texture type unknown. Must be \"float\" or \"spectrum\".", type);
            return;
        }

        std::set<std::string>& names =
            (type == "float") ? floatTextureNames : spectrumTextureNames;
        if (names.find(name) != names.end())
        {
            ErrorExitDeferred(&loc, "Redefining texture \"%s\".", name);
            return;
        }
        names.insert(name);

        if (type == "float")
            scene->AddFloatTexture(
                name, TextureSceneEntity(texname, std::move(dict), loc, RenderFromObject()));
        else
            scene->AddSpectrumTexture(
                name, TextureSceneEntity(texname, std::move(dict), loc, RenderFromObject()));
    }

    void SceneBuilder::Material(const std::string& name, ParsedParameterVector params,
                                     FileLoc loc)
    {
        VERIFY_WORLD("spectra::Material");

        ParameterDictionary dict(std::move(params), graphicsState.materialAttributes,
                                 graphicsState.colorSpace);

        graphicsState.currentMaterialIndex =
            scene->AddMaterial(SceneEntity(name, std::move(dict), loc));
        graphicsState.currentMaterialName.clear();
    }

    void SceneBuilder::MakeNamedMaterial(const std::string& origName,
                                              ParsedParameterVector params, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        VERIFY_WORLD("MakeNamedMaterial");

        ParameterDictionary dict(std::move(params), graphicsState.materialAttributes,
                                 graphicsState.colorSpace);

        if (namedMaterialNames.find(name) != namedMaterialNames.end())
        {
            ErrorExitDeferred(&loc, "%s: named material redefined.", name);
            return;
        }
        namedMaterialNames.insert(name);

        scene->AddNamedMaterial(name, SceneEntity("", std::move(dict), loc));
    }

    void SceneBuilder::NamedMaterial(const std::string& origName, FileLoc loc)
    {
        std::string name = NormalizeUTF8(origName);
        VERIFY_WORLD("NamedMaterial");
        graphicsState.currentMaterialName = name;
        graphicsState.currentMaterialIndex = -1;
    }

    void SceneBuilder::AreaLightSource(const std::string& name,
                                            ParsedParameterVector params, FileLoc loc)
    {
        VERIFY_WORLD("AreaLightSource");
        graphicsState.areaLightName = name;
        graphicsState.areaLightParams = ParameterDictionary(
            std::move(params), graphicsState.lightAttributes, graphicsState.colorSpace);
        graphicsState.areaLightLoc = loc;
    }

    // Scene Method Definitions
    void Scene::SetOptions(SceneEntity filter, SceneEntity film,
                                CameraSceneEntity camera, SceneEntity sampler,
                                SceneEntity integ, SceneEntity accel)
    {
        // Store information for specified integrator and accelerator
        filmColorSpace = film.parameters.ColorSpace();
        integrator = integ;
        accelerator = accel;

        // Immediately create filter and film
        Allocator alloc = threadAllocators.Get();
        Filter filt = Filter::Create(filter.name, filter.parameters, &filter.loc, alloc);

        // It's a little ugly to poke into the camera's parameters here, but we
        // have this circular dependency that spectra::Camera::Create() expects a
        // spectra::Film, yet now the film needs to know the exposure time from
        // the camera....
        Float exposureTime = camera.parameters.GetOneFloat("shutterclose", 1.f) -
            camera.parameters.GetOneFloat("shutteropen", 0.f);
        if (exposureTime <= 0)
            ErrorExit(&camera.loc,
                      "The specified camera shutter times imply that the shutter "
                      "does not open.  A black image will result.");

        this->film = Film::Create(film.name, film.parameters, exposureTime,
                                  camera.cameraTransform, filt, &film.loc, alloc);

        // Enqueue asynchronous job to create sampler
        samplerJob = RunAsync([sampler, this]()
        {
            Allocator alloc = threadAllocators.Get();
            Point2i res = this->film.FullResolution();
            return Sampler::Create(sampler.name, sampler.parameters, res, &sampler.loc,
                                   alloc);
        });

        // Enqueue asynchronous job to create camera
        cameraJob = RunAsync([camera, this]()
        {
            Allocator alloc = threadAllocators.Get();
            Medium cameraMedium = GetMedium(camera.medium, &camera.loc);

            Camera c = Camera::Create(camera.name, camera.parameters, cameraMedium,
                                      camera.cameraTransform, this->film, &camera.loc, alloc);
            return c;
        });
    }

    Camera Scene::GetCamera()
    {
        cameraJobMutex.lock();
        while (!camera)
        {
            pstd::optional<Camera> c = cameraJob->TryGetResult(&cameraJobMutex);
            if (c)
                camera = *c;
        }
        cameraJobMutex.unlock();
        return camera;
    }

    Sampler Scene::GetSampler()
    {
        samplerJobMutex.lock();
        while (!sampler)
        {
            pstd::optional<Sampler> s = samplerJob->TryGetResult(&samplerJobMutex);
            if (s)
                sampler = *s;
        }
        samplerJobMutex.unlock();
        return sampler;
    }

    void Scene::AddMedium(MediumSceneEntity medium)
    {
        // Define _create_ lambda function for _Medium_ creation
        auto create = [medium, this]()
        {
            std::string type = medium.parameters.GetOneString("type", "");
            // Check for missing medium ``type'' or animated medium transform
            if (type.empty())
                ErrorExit(&medium.loc, "No parameter \"string type\" found for medium.");
            if (medium.renderFromObject.IsAnimated())
                Warning(&medium.loc, "Animated transformation provided for medium. Only the "
                        "start transform will be used.");

            return Medium::Create(type, medium.parameters,
                                  medium.renderFromObject.startTransform, &medium.loc,
                                  threadAllocators.Get());
        };

        std::lock_guard<std::mutex> lock(mediaMutex);
        mediumJobs[medium.name] = RunAsync(create);
    }

    Medium Scene::GetMedium(const std::string& name, const FileLoc* loc)
    {
        if (name.empty())
            return nullptr;

        mediaMutex.lock();
        while (true)
        {
            if (auto iter = mediaMap.find(name); iter != mediaMap.end())
            {
                Medium m = iter->second;
                mediaMutex.unlock();
                return m;
            }
            else
            {
                auto fiter = mediumJobs.find(name);
                if (fiter == mediumJobs.end())
                    ErrorExit(loc, "%s: medium is not defined.", name);

                pstd::optional<Medium> m = fiter->second->TryGetResult(&mediaMutex);
                if (m)
                {
                    mediaMap[name] = *m;
                    mediumJobs.erase(fiter);
                    mediaMutex.unlock();
                    return *m;
                }
            }
        }
    }

    std::map<std::string, Medium> Scene::CreateMedia()
    {
        mediaMutex.lock();
        if (!mediumJobs.empty())
        {
            // Consume results for asynchronously-created _Medium_ objects
            for (auto& m : mediumJobs)
            {
                while (mediaMap.find(m.first) == mediaMap.end())
                {
                    pstd::optional<Medium> med = m.second->TryGetResult(&mediaMutex);
                    if (med)
                        mediaMap[m.first] = *med;
                }
            }
            mediumJobs.clear();
        }
        mediaMutex.unlock();
        return mediaMap;
    }

    Scene::Scene()
        : threadAllocators([]()
        {
            pstd::pmr::monotonic_buffer_resource* resource =
                new pstd::pmr::monotonic_buffer_resource(
                    1024 * 1024, &CUDATrackedMemoryResource::singleton);
            return Allocator(resource);
        })
    {
    }

    void Scene::AddNamedMaterial(std::string name, SceneEntity material)
    {
        std::lock_guard<std::mutex> lock(materialMutex);
        startLoadingNormalMaps(material.parameters);
        namedMaterials.push_back(std::make_pair(std::move(name), std::move(material)));
    }

    int Scene::AddMaterial(SceneEntity material)
    {
        std::lock_guard<std::mutex> lock(materialMutex);
        startLoadingNormalMaps(material.parameters);
        materials.push_back(std::move(material));
        return int(materials.size() - 1);
    }

    void Scene::startLoadingNormalMaps(const ParameterDictionary& parameters)
    {
        std::string filename = ResolveFilename(parameters.GetOneString("normalmap", ""));
        if (filename.empty())
            return;

        // Overload materialMutex, which we already hold, for the futures...
        if (normalMapJobs.find(filename) != normalMapJobs.end())
            // It's already in flight.
            return;

        auto create = [=, this](std::string filename)
        {
            Allocator alloc = threadAllocators.Get();
            ImageAndMetadata immeta =
                Image::Read(filename, Allocator(), ColorEncoding::Linear);
            Image& image = immeta.image;
            ImageChannelDesc rgbDesc = image.GetChannelDesc({"R", "G", "B"});
            if (!rgbDesc)
                ErrorExit("%s: normal map image must contain R, G, and B channels", filename);
            Image* normalMap = alloc.new_object<Image>(alloc);
            *normalMap = image.SelectChannels(rgbDesc);

            return normalMap;
        };
        normalMapJobs[filename] = RunAsync(create, filename);
    }

    void Scene::AddFloatTexture(std::string name, TextureSceneEntity texture)
    {
        if (texture.renderFromObject.IsAnimated())
            Warning(&texture.loc, "Animated world to texture transforms are not supported. "
                    "Using start transform.");

        std::lock_guard<std::mutex> lock(textureMutex);
        if (texture.name != "imagemap" && texture.name != "ptex")
        {
            serialFloatTextures.push_back(
                std::make_pair(std::move(name), std::move(texture)));
            return;
        }

        std::string filename =
            ResolveFilename(texture.parameters.GetOneString("filename", ""));
        if (filename.empty())
        {
            Error(&texture.loc, "\"string filename\" not provided for image texture.");
            ++nMissingTextures;
            return;
        }
        if (!FileExists(filename))
        {
            Error(&texture.loc, "%s: file not found.", filename);
            ++nMissingTextures;
            return;
        }

        if (loadingTextureFilenames.find(filename) != loadingTextureFilenames.end())
        {
            serialFloatTextures.push_back(
                std::make_pair(std::move(name), std::move(texture)));
            return;
        }
        loadingTextureFilenames.insert(filename);

        auto create = [=, this](TextureSceneEntity texture)
        {
            Allocator alloc = threadAllocators.Get();

            Transform renderFromTexture = texture.renderFromObject.startTransform;
            // Pass nullptr for the textures, since they shouldn't be accessed
            // anyway.
            TextureParameterDictionary texDict(&texture.parameters, nullptr);
            return FloatTexture::Create(texture.name, renderFromTexture, texDict,
                                        &texture.loc, alloc);
        };
        floatTextureJobs[name] = RunAsync(create, texture);
    }

    void Scene::AddSpectrumTexture(std::string name, TextureSceneEntity texture)
    {
        std::lock_guard<std::mutex> lock(textureMutex);

        if (texture.name != "imagemap" && texture.name != "ptex")
        {
            serialSpectrumTextures.push_back(
                std::make_pair(std::move(name), std::move(texture)));
            return;
        }

        std::string filename =
            ResolveFilename(texture.parameters.GetOneString("filename", ""));
        if (filename.empty())
        {
            Error(&texture.loc, "\"string filename\" not provided for image texture.");
            ++nMissingTextures;
            return;
        }
        if (!FileExists(filename))
        {
            Error(&texture.loc, "%s: file not found.", filename);
            ++nMissingTextures;
            return;
        }

        if (loadingTextureFilenames.find(filename) != loadingTextureFilenames.end())
        {
            serialSpectrumTextures.push_back(
                std::make_pair(std::move(name), std::move(texture)));
            return;
        }
        loadingTextureFilenames.insert(filename);

        asyncSpectrumTextures.push_back(std::make_pair(name, texture));

        auto create = [=, this](TextureSceneEntity texture)
        {
            Allocator alloc = threadAllocators.Get();

            Transform renderFromTexture = texture.renderFromObject.startTransform;
            // nullptr for the textures, as with float textures.
            TextureParameterDictionary texDict(&texture.parameters, nullptr);
            // Only create spectra::SpectrumType::Albedo for now; will get the other two
            // types in CreateTextures().
            return SpectrumTexture::Create(texture.name, renderFromTexture, texDict,
                                           SpectrumType::Albedo, &texture.loc, alloc);
        };
        spectrumTextureJobs[name] = RunAsync(create, texture);
    }

    void Scene::AddLight(LightSceneEntity light)
    {
        Medium lightMedium = GetMedium(light.medium, &light.loc);
        std::lock_guard<std::mutex> lock(lightMutex);

        if (light.renderFromObject.IsAnimated())
            Warning(&light.loc,
                    "Animated lights aren't supported. Using the start transform.");

        auto create = [this, light, lightMedium]()
        {
            return Light::Create(light.name, light.parameters,
                                 light.renderFromObject.startTransform,
                                 GetCamera().GetCameraTransform(), lightMedium, &light.loc,
                                 threadAllocators.Get());
        };
        lightJobs.push_back(RunAsync(create));
    }

    int Scene::AddAreaLight(SceneEntity light)
    {
        std::lock_guard<std::mutex> lock(areaLightMutex);
        areaLights.push_back(std::move(light));
        return areaLights.size() - 1;
    }

    void Scene::AddShapes(pstd::span<ShapeSceneEntity> s)
    {
        std::lock_guard<std::mutex> lock(shapeMutex);
        std::move(std::begin(s), std::end(s), std::back_inserter(shapes));
    }

    void Scene::AddAnimatedShape(AnimatedShapeSceneEntity shape)
    {
        std::lock_guard<std::mutex> lock(animatedShapeMutex);
        animatedShapes.push_back(std::move(shape));
    }

    void Scene::AddInstanceDefinition(InstanceDefinitionSceneEntity instance)
    {
        InstanceDefinitionSceneEntity* def =
            new InstanceDefinitionSceneEntity(std::move(instance));

        std::lock_guard<std::mutex> lock(instanceDefinitionMutex);
        instanceDefinitions[def->name] = def;
    }

    void Scene::AddInstanceUses(pstd::span<InstanceSceneEntity> in)
    {
        std::lock_guard<std::mutex> lock(instanceUseMutex);
        std::move(std::begin(in), std::end(in), std::back_inserter(instances));
    }

    void Scene::CreateMaterials(const NamedTextures& textures,
                                     std::map<std::string, Material>* namedMaterialsOut,
                                     std::vector<Material>* materialsOut)
    {
        std::lock_guard<std::mutex> lock(materialMutex);
        for (auto& job : normalMapJobs)
        {
            SPECTRA_CHECK(normalMaps.find(job.first) == normalMaps.end());
            normalMaps[job.first] = job.second->GetResult();
        }
        normalMapJobs.clear();

        // Named materials
        for (const auto& nm : namedMaterials)
        {
            const std::string& name = nm.first;
            const SceneEntity& mtl = nm.second;
            Allocator alloc = threadAllocators.Get();

            if (namedMaterialsOut->find(name) != namedMaterialsOut->end())
            {
                ErrorExit(&mtl.loc, "%s: trying to redefine named material.", name);
                continue;
            }

            std::string type = mtl.parameters.GetOneString("type", "");
            if (type.empty())
            {
                ErrorExit(&mtl.loc,
                          "%s: \"string type\" not provided in named material's parameters.",
                          name);
                continue;
            }

            std::string fn =
                ResolveFilename(nm.second.parameters.GetOneString("normalmap", ""));
            Image* normalMap = nullptr;
            if (!fn.empty())
            {
                SPECTRA_CHECK(normalMaps.find(fn) != normalMaps.end());
                normalMap = normalMaps[fn];
            }

            TextureParameterDictionary texDict(&mtl.parameters, &textures);
            Material m = Material::Create(type, texDict, normalMap, *namedMaterialsOut,
                                                &mtl.loc, alloc);
            (*namedMaterialsOut)[name] = m;
        }

        // Regular materials
        materialsOut->reserve(materials.size());
        for (const auto& mtl : materials)
        {
            Allocator alloc = threadAllocators.Get();
            std::string fn = ResolveFilename(mtl.parameters.GetOneString("normalmap", ""));
            Image* normalMap = nullptr;
            if (!fn.empty())
            {
                SPECTRA_CHECK(normalMaps.find(fn) != normalMaps.end());
                normalMap = normalMaps[fn];
            }

            TextureParameterDictionary texDict(&mtl.parameters, &textures);
            Material m = Material::Create(mtl.name, texDict, normalMap,
                                                *namedMaterialsOut, &mtl.loc, alloc);
            materialsOut->push_back(m);
        }
    }

    NamedTextures Scene::CreateTextures()
    {
        NamedTextures textures;

        if (nMissingTextures > 0)
            ErrorExit("%d missing textures", nMissingTextures);

        // Consume futures
        // The lock shouldn't be necessary since only the main thread should be
        // active when CreateTextures() is called, but valgrind doesn't know
        // that...
        textureMutex.lock();
        for (auto& tex : floatTextureJobs)
            textures.floatTextures[tex.first] = tex.second->GetResult();
        floatTextureJobs.clear();
        for (auto& tex : spectrumTextureJobs)
            textures.albedoSpectrumTextures[tex.first] = tex.second->GetResult();
        spectrumTextureJobs.clear();
        textureMutex.unlock();

        Allocator alloc = threadAllocators.Get();
        // Create the other SpectrumTypes for the spectrum textures.
        for (const auto& tex : asyncSpectrumTextures)
        {
            Transform renderFromTexture = tex.second.renderFromObject.startTransform;
            // These are all image textures, so nullptr is fine for the
            // textures, as earlier.
            TextureParameterDictionary texDict(&tex.second.parameters, nullptr);

            // These should be fast since they should hit the texture cache
            SpectrumTexture unboundedTex = SpectrumTexture::Create(
                tex.second.name, renderFromTexture, texDict, SpectrumType::Unbounded,
                &tex.second.loc, alloc);
            SpectrumTexture illumTex = SpectrumTexture::Create(
                tex.second.name, renderFromTexture, texDict, SpectrumType::Illuminant,
                &tex.second.loc, alloc);

            textures.unboundedSpectrumTextures[tex.first] = unboundedTex;
            textures.illuminantSpectrumTextures[tex.first] = illumTex;
        }

        // And do the rest serially
        for (auto& tex : serialFloatTextures)
        {
            Allocator alloc = threadAllocators.Get();

            Transform renderFromTexture = tex.second.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&tex.second.parameters, &textures);
            FloatTexture t = FloatTexture::Create(tex.second.name, renderFromTexture, texDict,
                                                  &tex.second.loc, alloc);
            textures.floatTextures[tex.first] = t;
        }

        for (auto& tex : serialSpectrumTextures)
        {
            Allocator alloc = threadAllocators.Get();

            if (tex.second.renderFromObject.IsAnimated())
                Warning(&tex.second.loc, "Animated world to texture transform not supported. "
                        "Using start transform.");

            Transform renderFromTexture = tex.second.renderFromObject.startTransform;
            TextureParameterDictionary texDict(&tex.second.parameters, &textures);
            SpectrumTexture albedoTex = SpectrumTexture::Create(
                tex.second.name, renderFromTexture, texDict, SpectrumType::Albedo,
                &tex.second.loc, alloc);
            SpectrumTexture unboundedTex = SpectrumTexture::Create(
                tex.second.name, renderFromTexture, texDict, SpectrumType::Unbounded,
                &tex.second.loc, alloc);
            SpectrumTexture illumTex = SpectrumTexture::Create(
                tex.second.name, renderFromTexture, texDict, SpectrumType::Illuminant,
                &tex.second.loc, alloc);

            textures.albedoSpectrumTextures[tex.first] = albedoTex;
            textures.unboundedSpectrumTextures[tex.first] = unboundedTex;
            textures.illuminantSpectrumTextures[tex.first] = illumTex;
        }

        return textures;
    }

    std::vector<Light> Scene::CreateLights(
        const NamedTextures& textures,
        std::map<int, pstd::vector<Light>*>* shapeIndexToAreaLights)
    {
        auto findMedium = [this](const std::string& s, const FileLoc* loc) -> Medium
        {
            if (s.empty())
                return nullptr;

            auto iter = mediaMap.find(s);
            if (iter == mediaMap.end())
                ErrorExit(loc, "%s: medium not defined", s);
            return iter->second;
        };

        Allocator alloc = threadAllocators.Get();

        auto getAlphaTexture = [&](const ParameterDictionary& parameters,
                                   const FileLoc* loc) -> FloatTexture
        {
            std::string alphaTexName = parameters.GetTexture("alpha");
            if (!alphaTexName.empty())
            {
                if (auto iter = textures.floatTextures.find(alphaTexName);
                    iter != textures.floatTextures.end())
                {
                    if (!BasicTextureEvaluator().CanEvaluate({iter->second}, {}))
                        // A warning will be issued elsewhere...
                        return nullptr;
                    return iter->second;
                }
                else
                    ErrorExit(loc, "%s: couldn't find float texture for \"alpha\" parameter.",
                              alphaTexName);
            }
            else if (Float alpha = parameters.GetOneFloat("alpha", 1.f); alpha < 1.f)
                return alloc.new_object<FloatConstantTexture>(alpha);
            else
                return nullptr;
        };

        std::vector<Light> lights;
        // Area Lights
        for (size_t i = 0; i < shapes.size(); ++i)
        {
            const auto& sh = shapes[i];

            if (sh.lightIndex == -1)
                continue;

            std::string materialName;
            if (!sh.materialName.empty())
            {
                auto iter =
                    std::find_if(namedMaterials.begin(), namedMaterials.end(),
                                 [&](auto iter) { return iter.first == sh.materialName; });
                if (iter == namedMaterials.end())
                    ErrorExit(&sh.loc, "%s: no named material defined.", sh.materialName);
                SPECTRA_CHECK(iter->second.parameters.GetStringArray("type").size() > 0);
                materialName = iter->second.parameters.GetOneString("type", "");
            }
            else
            {
                SPECTRA_CHECK_LT(sh.materialIndex, materials.size());
                materialName = materials[sh.materialIndex].name;
            }
            if (materialName == "interface" || materialName == "none" || materialName == "")
            {
                Warning(&sh.loc, "Ignoring area light specification for shape "
                        "with \"interface\" material.");
                continue;
            }

            pstd::vector<Shape> shapeObjects = Shape::Create(
                sh.name, sh.renderFromObject, sh.objectFromRender, sh.reverseOrientation,
                sh.parameters, textures.floatTextures, &sh.loc, alloc);

            FloatTexture alphaTex = getAlphaTexture(sh.parameters, &sh.loc);

            MediumInterface mi(findMedium(sh.insideMedium, &sh.loc),
                                     findMedium(sh.outsideMedium, &sh.loc));

            pstd::vector<Light>* shapeLights = new pstd::vector<Light>(alloc);
            const auto& areaLightEntity = areaLights[sh.lightIndex];
            for (Shape ps : shapeObjects)
            {
                Light area = Light::CreateArea(
                    areaLightEntity.name, areaLightEntity.parameters, *sh.renderFromObject,
                    mi, ps, alphaTex, &areaLightEntity.loc, alloc);
                if (area)
                {
                    lights.push_back(area);
                    shapeLights->push_back(area);
                }
            }

            (*shapeIndexToAreaLights)[i] = shapeLights;
        }


        std::lock_guard<std::mutex> lock(lightMutex);
        for (auto& job : lightJobs)
            lights.push_back(job->GetResult());

        return lights;
    }
} // namespace spectra::scene
