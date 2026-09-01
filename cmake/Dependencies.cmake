include_guard(GLOBAL)

include(FetchContent)

set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)

FetchContent_Declare(
        glfw
        URL "https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4"
        URL_HASH SHA256=A133DDC3D3C66143EBA9035621DB8E0BCF34DBA1EE9514A9E23E96AFD39FD57A
        SYSTEM
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        imgui
        URL "https://codeload.github.com/ocornut/imgui/zip/b334d19b667958ed970000073644d911fae17e57"
        URL_HASH SHA256=504BC8171B80B8C92F035EBC899F6B3086C9CFA56EFADEE4962753DEB38626A2
)

FetchContent_Declare(
        imguizmo
        URL "https://codeload.github.com/CedricGuillemet/ImGuizmo/zip/dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d"
        URL_HASH SHA256=0CBB48F6A9041FE18E0D66380AF76BF8EF1D836C6913F9F4E8A0D512F42A3116
        SOURCE_SUBDIR _spectra_source_only
)

FetchContent_Declare(
        lodepng
        URL "https://codeload.github.com/lvandeve/lodepng/zip/ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a"
        URL_HASH SHA256=E3E5A62322B7AFD5037D0AAA27188457226CCDAACC1353D0A1EFCCCD8F87F504
)

FetchContent_Declare(
        onetbb
        URL "https://codeload.github.com/oneapi-src/oneTBB/zip/refs/tags/v2021.9.0"
        URL_HASH SHA256=FCEBB93CB9F7E882F62CD351B1C093DBEFDCAE04B616227DC716B0A5EFA9E8AB
        SYSTEM
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        openusd
        URL "https://codeload.github.com/PixarAnimationStudios/OpenUSD/zip/refs/tags/v26.08"
        URL_HASH SHA256=FBF509E8292055A0BE7801F56218ED5D15A3C9022E24636FCC9CA8587345C760
        SYSTEM
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        openvdb
        URL "https://codeload.github.com/AcademySoftwareFoundation/openvdb/zip/refs/tags/v13.0.0"
        URL_HASH SHA256=37B31E0E67CD071B4AAFBF924B4AC7DCE9B15C439C0E25B4B35AA47F3939A5FC
        SYSTEM
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        zlib
        URL "https://codeload.github.com/madler/zlib/zip/refs/tags/v1.3.1"
        URL_HASH SHA256=50B24B47BF19E1F35D2A21FF36D2A366638CDF958219A66F30CE0861201760E6
        SYSTEM
        EXCLUDE_FROM_ALL
        OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
        blosc
        URL "https://codeload.github.com/Blosc/c-blosc/zip/refs/tags/v1.21.6"
        URL_HASH SHA256=1919C97D55023C04AA8771EA8235B63E9DA3C22E3D2A68340B33710D19C2A2EB
        SYSTEM
        EXCLUDE_FROM_ALL
        OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
        tinyexr
        URL "https://codeload.github.com/syoyo/tinyexr/zip/refs/tags/v3.2.0"
        URL_HASH SHA256=12A458E2E92A7072E26927BA4E53276CE35605EFB2E0418E7C5210CBC81DE2DF
        SOURCE_SUBDIR _spectra_source_only
)

set(SPECTRA_DEPENDENCIES_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/dependencies")

macro(spectra_require_dependency dependency)
    include("${SPECTRA_DEPENDENCIES_DIRECTORY}/${dependency}.cmake")
endmacro()
