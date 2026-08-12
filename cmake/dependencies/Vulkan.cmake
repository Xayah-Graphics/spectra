find_package(Vulkan 1.4 REQUIRED)
list(GET Vulkan_INCLUDE_DIRS 0 SPECTRA_VULKAN_INCLUDE_DIR)
cmake_path(GET SPECTRA_VULKAN_INCLUDE_DIR PARENT_PATH SPECTRA_VULKAN_SDK_DIR)

add_library(spectra_vulkan)
add_library(spectra::vulkan ALIAS spectra_vulkan)
target_sources(
        spectra_vulkan
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${SPECTRA_VULKAN_INCLUDE_DIR}"
        FILES "${SPECTRA_VULKAN_INCLUDE_DIR}/vulkan/vulkan.cppm"
)
target_link_libraries(spectra_vulkan PUBLIC Vulkan::Vulkan)
if (WIN32)
    target_compile_definitions(spectra_vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
endif ()
