module;

#include <Windows.h>

#include <bcrypt.h>

module spectra.util.hash;

import std;

namespace spectra {
    namespace {
        struct Sha256 {
            Sha256() {
                if (BCryptOpenAlgorithmProvider(&this->algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("Failed to open the Windows SHA-256 provider");
                if (BCryptCreateHash(this->algorithm, &this->hash, nullptr, 0, nullptr, 0, 0) < 0) {
                    BCryptCloseAlgorithmProvider(this->algorithm, 0);
                    this->algorithm = nullptr;
                    throw std::runtime_error("Failed to create a SHA-256 hash");
                }
            }

            ~Sha256() {
                if (this->hash) BCryptDestroyHash(this->hash);
                if (this->algorithm) BCryptCloseAlgorithmProvider(this->algorithm, 0);
            }

            Sha256(const Sha256&)            = delete;
            Sha256(Sha256&&)                 = delete;
            Sha256& operator=(const Sha256&) = delete;
            Sha256& operator=(Sha256&&)      = delete;

            void add(const std::span<const std::byte> bytes) {
                std::size_t offset{};
                while (offset != bytes.size()) {
                    const ULONG size = static_cast<ULONG>(std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<ULONG>::max()));
                    if (BCryptHashData(this->hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data() + offset)), size, 0) < 0) throw std::runtime_error("Failed to update a SHA-256 hash");
                    offset += size;
                }
            }

            [[nodiscard]] std::array<std::byte, 32> finish() {
                std::array<std::byte, 32> result{};
                if (BCryptFinishHash(this->hash, reinterpret_cast<PUCHAR>(result.data()), static_cast<ULONG>(result.size()), 0) < 0) throw std::runtime_error("Failed to finish a SHA-256 hash");
                BCryptDestroyHash(this->hash);
                this->hash = nullptr;
                return result;
            }

        private:
            BCRYPT_ALG_HANDLE algorithm{};
            BCRYPT_HASH_HANDLE hash{};
        };

        [[nodiscard]] std::string hexadecimal(const std::array<std::byte, 32>& digest) {
            constexpr std::string_view digits = "0123456789abcdef";
            std::string result(64, '0');
            for (std::size_t index = 0; index != digest.size(); ++index) {
                const std::uint8_t value = std::to_integer<std::uint8_t>(digest[index]);
                result[index * 2]        = digits[value >> 4];
                result[index * 2 + 1]    = digits[value & 0x0f];
            }
            return result;
        }
    } // namespace

    std::string sha256_hex(const std::span<const std::span<const std::byte>> blocks) {
        Sha256 hash{};
        for (const std::span<const std::byte> block : blocks) hash.add(block);
        return hexadecimal(hash.finish());
    }

    std::string sha256_file(const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary};
        if (!stream) throw std::runtime_error(std::format("Failed to open file for SHA-256: {}", path.string()));
        Sha256 hash{};
        std::array<std::byte, 64 * 1024> block{};
        while (stream) {
            stream.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
            const std::streamsize size = stream.gcount();
            if (size > 0) hash.add(std::span{block.data(), static_cast<std::size_t>(size)});
        }
        if (!stream.eof()) throw std::runtime_error(std::format("Failed to read file for SHA-256: {}", path.string()));
        return hexadecimal(hash.finish());
    }
} // namespace spectra
