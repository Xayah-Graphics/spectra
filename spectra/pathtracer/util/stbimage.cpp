#include <spectra/pathtracer/util/check.h>

#define STBI_NO_PNG
// too old school
#define STBI_NO_PIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT CHECK
#include <stb/stb_image.h>
