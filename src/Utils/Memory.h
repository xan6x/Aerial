#pragma once

#include <cstdint>
#include <string_view>

namespace aerial::memory {

uintptr_t base();

size_t imageSize();

inline uintptr_t rva(uintptr_t offset) { return base() + offset; }

template <typename T>
inline T* at(uintptr_t offset) { return reinterpret_cast<T*>(base() + offset); }

template <typename T>
inline T fn(uintptr_t offset) { return reinterpret_cast<T>(base() + offset); }

uintptr_t findPattern(std::string_view pattern);

uintptr_t findPatternRva(std::string_view pattern);

uintptr_t resolveRip(uintptr_t address, int offset, int instructionSize);

uintptr_t followCall(uintptr_t address);

void* chain(void* start, std::initializer_list<ptrdiff_t> offsets);

bool isReadable(const void* address, size_t size = 1);

bool patch(uintptr_t address, const void* bytes, size_t size);

bool nop(uintptr_t address, size_t size);

}
