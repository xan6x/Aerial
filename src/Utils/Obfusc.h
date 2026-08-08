#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace aerial::obfusc {

constexpr std::uint32_t seed() {
    std::uint32_t s = 2166136261u;
    for (char c : __TIME__)
        s = (s ^ static_cast<std::uint8_t>(c)) * 16777619u;
    return s;
}

constexpr std::uint8_t keyAt(std::size_t i) {
    const std::uint32_t s = seed();
    return static_cast<std::uint8_t>((s >> ((i % 4) * 8)) ^ (i * 31u + 0x5Du));
}

template <typename Char, std::size_t N>
struct Encrypted {
    std::array<Char, N> data{};

    constexpr explicit Encrypted(const Char (&in)[N]) {
        for (std::size_t i = 0; i < N; ++i)
            data[i] = static_cast<Char>(in[i] ^ static_cast<Char>(keyAt(i)));
    }

    std::basic_string<Char> decode() const {
        std::basic_string<Char> out(N - 1, Char{});
        for (std::size_t i = 0; i + 1 < N; ++i)
            out[i] = static_cast<Char>(data[i] ^ static_cast<Char>(keyAt(i)));
        return out;
    }
};

}

#define AERIAL_STR(literal)                                                       \
    ([] {                                                                         \
        constexpr ::aerial::obfusc::Encrypted<char, sizeof(literal)> e{literal};  \
        return e.decode();                                                        \
    }())

#define AERIAL_WSTR(literal)                                                                      \
    ([] {                                                                                         \
        constexpr ::aerial::obfusc::Encrypted<wchar_t, sizeof(literal) / sizeof(wchar_t)> e{      \
            literal};                                                                             \
        return e.decode();                                                                        \
    }())
