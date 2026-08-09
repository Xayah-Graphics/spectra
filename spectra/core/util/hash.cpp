module spectra.util.hash;

import std;

namespace spectra {
    namespace {
        struct Sha256 {
            Sha256() = default;

            void add(const std::span<const std::byte> bytes) {
                this->bit_count += static_cast<std::uint64_t>(bytes.size()) * 8u;
                for (const std::byte value : bytes) {
                    this->buffer[this->buffer_size++] = value;
                    if (this->buffer_size == this->buffer.size()) {
                        this->transform();
                        this->buffer_size = 0;
                    }
                }
            }

            [[nodiscard]] std::array<std::byte, 32> finish() {
                this->buffer[this->buffer_size++] = std::byte{0x80};
                if (this->buffer_size > 56) {
                    std::ranges::fill(this->buffer.begin() + this->buffer_size, this->buffer.end(), std::byte{});
                    this->transform();
                    this->buffer_size = 0;
                }
                std::ranges::fill(this->buffer.begin() + this->buffer_size, this->buffer.begin() + 56, std::byte{});
                for (std::uint32_t index = 0; index < 8; ++index) this->buffer[56 + index] = static_cast<std::byte>(this->bit_count >> ((7u - index) * 8u));
                this->transform();
                std::array<std::byte, 32> result{};
                for (std::uint32_t word = 0; word < this->state.size(); ++word)
                    for (std::uint32_t byte = 0; byte < 4; ++byte) result[word * 4u + byte] = static_cast<std::byte>(this->state[word] >> ((3u - byte) * 8u));
                return result;
            }

        private:
            void transform() noexcept {
                constexpr std::array<std::uint32_t, 64> constants{
                    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
                };
                std::array<std::uint32_t, 64> words{};
                for (std::uint32_t index = 0; index < 16; ++index)
                    for (std::uint32_t byte = 0; byte < 4; ++byte) words[index] |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(this->buffer[index * 4u + byte])) << ((3u - byte) * 8u);
                for (std::uint32_t index = 16; index < words.size(); ++index) {
                    const std::uint32_t s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
                    const std::uint32_t s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
                    words[index]           = words[index - 16] + s0 + words[index - 7] + s1;
                }
                std::array working = this->state;
                for (std::uint32_t index = 0; index < words.size(); ++index) {
                    const std::uint32_t sum1   = std::rotr(working[4], 6) ^ std::rotr(working[4], 11) ^ std::rotr(working[4], 25);
                    const std::uint32_t choice = (working[4] & working[5]) ^ (~working[4] & working[6]);
                    const std::uint32_t first  = working[7] + sum1 + choice + constants[index] + words[index];
                    const std::uint32_t sum0   = std::rotr(working[0], 2) ^ std::rotr(working[0], 13) ^ std::rotr(working[0], 22);
                    const std::uint32_t majority = (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
                    const std::uint32_t second = sum0 + majority;
                    for (std::uint32_t word = 7; word > 0; --word) working[word] = working[word - 1];
                    working[4] += first;
                    working[0] = first + second;
                }
                for (std::uint32_t index = 0; index < this->state.size(); ++index) this->state[index] += working[index];
            }

            std::array<std::uint32_t, 8> state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
            std::array<std::byte, 64> buffer{};
            std::uint64_t bit_count{};
            std::size_t buffer_size{};
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
