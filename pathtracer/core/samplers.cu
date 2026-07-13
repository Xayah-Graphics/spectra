#include <pathtracer/core/cameras.cuh>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/core/filters.cuh>
#include <pathtracer/core/paramdict.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/core/samplers.cuh>
#include <string>
#include <vector>

namespace spectra {
    // HaltonSampler Method Definitions
    HaltonSampler::HaltonSampler(int samplesPerPixel, Point2i fullRes, RandomizeStrategy randomize, int seed, Allocator alloc) : samplesPerPixel(samplesPerPixel), randomize(randomize) {
        if (randomize == RandomizeStrategy::PermuteDigits) digitPermutations = ComputeRadicalInversePermutations(seed, alloc);
        // Find radical inverse base scales and exponents that cover sampling area
        for (int i = 0; i < 2; ++i) {
            int base  = (i == 0) ? 2 : 3;
            int scale = 1, exp = 0;
            while (scale < std::min(fullRes[i], MaxHaltonResolution)) {
                scale *= base;
                ++exp;
            }
            baseScales[i]    = scale;
            baseExponents[i] = exp;
        }

        // Compute multiplicative inverses for _baseScales_
        multInverse[0] = multiplicativeInverse(baseScales[1], baseScales[0]);
        multInverse[1] = multiplicativeInverse(baseScales[0], baseScales[1]);
    }

    HaltonSampler* HaltonSampler::Create(const ParameterDictionary& parameters, Point2i fullResolution, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        int nsamp = parameters.GetOneInt("pixelsamples", 16);
        if (config.pixel_samples) nsamp = *config.pixel_samples;
        int seed = parameters.GetOneInt("seed", config.seed);

        RandomizeStrategy randomizer;
        std::string s = parameters.GetOneString("randomization", "permutedigits");
        if (s == "none")
            randomizer = RandomizeStrategy::None;
        else if (s == "permutedigits")
            randomizer = RandomizeStrategy::PermuteDigits;
        else if (s == "fastowen")
            throw std::runtime_error(diagnostics::Format("\"fastowen\" randomization not supported by Halton sampler."));
        else if (s == "owen")
            randomizer = RandomizeStrategy::Owen;
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: unknown randomization strategy given to HaltonSampler", s));

        return alloc.new_object<HaltonSampler>(nsamp, fullResolution, randomizer, seed, alloc);
    }

    PaddedSobolSampler* PaddedSobolSampler::Create(const ParameterDictionary& parameters, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        int nsamp = parameters.GetOneInt("pixelsamples", 16);
        if (config.pixel_samples) nsamp = *config.pixel_samples;
        int seed = parameters.GetOneInt("seed", config.seed);

        RandomizeStrategy randomizer;
        std::string s = parameters.GetOneString("randomization", "fastowen");
        if (s == "none")
            randomizer = RandomizeStrategy::None;
        else if (s == "permutedigits")
            randomizer = RandomizeStrategy::PermuteDigits;
        else if (s == "fastowen")
            randomizer = RandomizeStrategy::FastOwen;
        else if (s == "owen")
            randomizer = RandomizeStrategy::Owen;
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: unknown randomization strategy given to PaddedSobolSampler", s));

        return alloc.new_object<PaddedSobolSampler>(nsamp, randomizer, seed);
    }

    // ZSobolSampler Method Definitions
    ZSobolSampler* ZSobolSampler::Create(const ParameterDictionary& parameters, Point2i fullResolution, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        int nsamp = parameters.GetOneInt("pixelsamples", 16);
        if (config.pixel_samples) nsamp = *config.pixel_samples;
        int seed = parameters.GetOneInt("seed", config.seed);

        RandomizeStrategy randomizer;
        std::string s = parameters.GetOneString("randomization", "fastowen");
        if (s == "none")
            randomizer = RandomizeStrategy::None;
        else if (s == "permutedigits")
            randomizer = RandomizeStrategy::PermuteDigits;
        else if (s == "fastowen")
            randomizer = RandomizeStrategy::FastOwen;
        else if (s == "owen")
            randomizer = RandomizeStrategy::Owen;
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: unknown randomization strategy given to ZSobolSampler", s));

        return alloc.new_object<ZSobolSampler>(nsamp, fullResolution, randomizer, seed);
    }

    // PMJ02BNSampler Method Definitions
    PMJ02BNSampler::PMJ02BNSampler(int samplesPerPixel, int seed, Allocator alloc) : samplesPerPixel(samplesPerPixel), seed(seed) {
        if (!IsPowerOf4(samplesPerPixel))
            diagnostics::PrintWarning("PMJ02BNSampler results are best with power-of-4 samples per "
                                      "pixel (1, 4, 16, 64, ...)");
        // Get sorted pmj02bn samples for pixel samples
        if (samplesPerPixel > nPMJ02bnSamples) throw std::runtime_error(diagnostics::Format("PMJ02BNSampler only supports up to %d samples per pixel", nPMJ02bnSamples));
        // Compute _pixelTileSize_ for pmj02bn pixel samples and allocate _pixelSamples_
        pixelTileSize     = 1 << (Log4Int(nPMJ02bnSamples) - Log4Int(RoundUpPow4(samplesPerPixel)));
        int nPixelSamples = pixelTileSize * pixelTileSize * samplesPerPixel;
        pixelSamples      = alloc.new_object<pstd::vector<Point2f>>(nPixelSamples, alloc);

        // Loop over pmj02bn samples and associate them with their pixels
        std::vector<int> nStored(pixelTileSize * pixelTileSize, 0);
        for (int i = 0; i < nPMJ02bnSamples; ++i) {
            Point2f p = GetPMJ02BNSample(0, i);
            p *= pixelTileSize;
            int pixelOffset = int(p.x) + int(p.y) * pixelTileSize;
            if (nStored[pixelOffset] == samplesPerPixel) {
                CHECK(!IsPowerOf4(samplesPerPixel));
                continue;
            }
            int sampleOffset = pixelOffset * samplesPerPixel + nStored[pixelOffset];
            CHECK((*pixelSamples)[sampleOffset] == Point2f(0, 0));
            (*pixelSamples)[sampleOffset] = Point2f(p - Floor(p));
            ++nStored[pixelOffset];
        }

        for (int i = 0; i < nStored.size(); ++i) CHECK_EQ(nStored[i], samplesPerPixel);
    }

    PMJ02BNSampler* PMJ02BNSampler::Create(const ParameterDictionary& parameters, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        int nsamp = parameters.GetOneInt("pixelsamples", 16);
        if (config.pixel_samples) nsamp = *config.pixel_samples;
        int seed = parameters.GetOneInt("seed", config.seed);
        return alloc.new_object<PMJ02BNSampler>(nsamp, seed, alloc);
    }

    IndependentSampler* IndependentSampler::Create(const ParameterDictionary& parameters, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        int ns = parameters.GetOneInt("pixelsamples", 4);
        if (config.pixel_samples) ns = *config.pixel_samples;
        int seed = parameters.GetOneInt("seed", config.seed);
        return alloc.new_object<IndependentSampler>(ns, seed);
    }

    // SobolSampler Method Definitions

    SobolSampler* SobolSampler::Create(const ParameterDictionary& parameters, Point2i fullResolution, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        int nsamp = parameters.GetOneInt("pixelsamples", 16);
        if (config.pixel_samples) nsamp = *config.pixel_samples;

        RandomizeStrategy randomizer;
        std::string s = parameters.GetOneString("randomization", "fastowen");
        if (s == "none")
            randomizer = RandomizeStrategy::None;
        else if (s == "permutedigits")
            randomizer = RandomizeStrategy::PermuteDigits;
        else if (s == "fastowen")
            randomizer = RandomizeStrategy::FastOwen;
        else if (s == "owen")
            randomizer = RandomizeStrategy::Owen;
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: unknown randomization strategy given to SobolSampler", s));

        int seed = parameters.GetOneInt("seed", config.seed);

        return alloc.new_object<SobolSampler>(nsamp, fullResolution, randomizer, seed);
    }

    // StratifiedSampler Method Definitions

    StratifiedSampler* StratifiedSampler::Create(const ParameterDictionary& parameters, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        bool jitter  = parameters.GetOneBool("jitter", true);
        int xSamples = parameters.GetOneInt("xsamples", 4);
        int ySamples = parameters.GetOneInt("ysamples", 4);
        if (config.pixel_samples) {
            int nSamples = *config.pixel_samples;
            int div      = std::sqrt(nSamples);
            while (nSamples % div) {
                CHECK_GT(div, 0);
                --div;
            }
            xSamples = nSamples / div;
            ySamples = nSamples / xSamples;
            CHECK_EQ(nSamples, xSamples * ySamples);
        }
        int seed = parameters.GetOneInt("seed", config.seed);

        return alloc.new_object<StratifiedSampler>(xSamples, ySamples, jitter, seed);
    }

    // Sampler Method Definitions
    Sampler Sampler::Create(const std::string& name, const ParameterDictionary& parameters, Point2i fullRes, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc) {
        Sampler sampler = nullptr;
        if (name == "zsobol") sampler = ZSobolSampler::Create(parameters, fullRes, config, loc, alloc);
        // Create remainder of _Sampler_ types
        else if (name == "paddedsobol")
            sampler = PaddedSobolSampler::Create(parameters, config, loc, alloc);
        else if (name == "halton")
            sampler = HaltonSampler::Create(parameters, fullRes, config, loc, alloc);
        else if (name == "sobol")
            sampler = SobolSampler::Create(parameters, fullRes, config, loc, alloc);
        else if (name == "pmj02bn")
            sampler = PMJ02BNSampler::Create(parameters, config, loc, alloc);
        else if (name == "independent")
            sampler = IndependentSampler::Create(parameters, config, loc, alloc);
        else if (name == "stratified")
            sampler = StratifiedSampler::Create(parameters, config, loc, alloc);
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: sampler type unknown.", name));
        if (!sampler) throw std::runtime_error(diagnostics::Format(loc, "%s: unable to create sampler.", name));
        parameters.ReportUnused();

        return sampler;
    }
} // namespace spectra
