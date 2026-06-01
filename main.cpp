#include "spectra/pathtracer.h"
#include "spectra/spectra.h"
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main(const int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: spectra <scene-name>");
        xayah::Spectra spectra{"Spectra"};
        spectra.register_plugin(std::make_unique<xayah::SpectraPathtracer>(std::string{argv[1]}));
        spectra.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
