include(CMakeParseArguments)

function(spectra_add_provider target)
    cmake_parse_arguments(PARSE_ARGV 1 provider "" "MODULE;TYPE" "")
    if (provider_UNPARSED_ARGUMENTS OR NOT provider_MODULE OR NOT provider_TYPE)
        message(FATAL_ERROR "spectra_add_provider requires exactly MODULE and TYPE")
    endif ()

    set(SPECTRA_PROVIDER_MODULE "${provider_MODULE}")
    set(SPECTRA_PROVIDER_TYPE "${provider_TYPE}")
    set(SpectraSDK_INTERNAL_ABI "${SpectraSDK_INTERNAL_DIRECTORY}/abi.h")
    set(generated_directory "${CMAKE_CURRENT_BINARY_DIR}/spectra-sdk/${target}")
    file(MAKE_DIRECTORY "${generated_directory}")
    configure_file("${SpectraSDK_INTERNAL_DIRECTORY}/provider_entry.cpp.in" "${generated_directory}/entry.cpp" @ONLY)

    add_library(${target} SHARED)
    target_sources(
            ${target}
            PRIVATE
            "${generated_directory}/entry.cpp"
            PRIVATE
            FILE_SET spectra_sdk_bridge TYPE CXX_MODULES
            BASE_DIRS "${SpectraSDK_INTERNAL_DIRECTORY}"
            FILES "${SpectraSDK_INTERNAL_DIRECTORY}/bridge.ixx"
    )
    target_link_libraries(${target} PRIVATE SpectraSDK::sdk CUDA::cudart)
    set_target_properties(
            ${target}
            PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${target}.spectra-provider"
    )
endfunction()
