#pragma once

#include <cstdint>
#include <string_view>

namespace aerial::memory {

// Base address of Minecraft.Windows.exe in the current process.
uintptr_t base();

// Size of the module's mapped image.
size_t imageSize();

// Absolute address for an RVA taken from the IDA database.
inline uintptr_t rva(uintptr_t offset) { return base() + offset; }

template <typename T>
inline T* at(uintptr_t offset) { return reinterpret_cast<T*>(base() + offset); }

template <typename T>
inline T fn(uintptr_t offset) { return reinterpret_cast<T>(base() + offset); }

// Pattern scan over the module's executable sections.
//   "48 8B ?? 48 89 ?? ?? ?? ?? ?? E8" — '?' or '??' is a wildcard byte.
// Returns 0 when not found. Prefer RVAs from the IDB; use this only when a
// signature is more stable than the offset (e.g. across game revisions).
uintptr_t findPattern(std::string_view pattern);

// Same as findPattern but returns an RVA (absolute - base), for logging/dumping.
uintptr_t findPatternRva(std::string_view pattern);

// Resolves a RIP-relative operand at `address + offset` (4-byte displacement
// followed by `instructionSize - offset - 4` trailing bytes).
uintptr_t resolveRip(uintptr_t address, int offset, int instructionSize);

// Follows a relative CALL/JMP at `address` to its destination.
uintptr_t followCall(uintptr_t address);

// Reads a pointer chain, returning nullptr as soon as a link is unmapped.
void* chain(void* start, std::initializer_list<ptrdiff_t> offsets);

// True when `address` points at committed, readable memory.
bool isReadable(const void* address, size_t size = 1);

// Temporarily makes a region writable and copies `size` bytes.
bool patch(uintptr_t address, const void* bytes, size_t size);

// Overwrites `size` bytes with 0x90 (NOP).
bool nop(uintptr_t address, size_t size);

} // namespace aerial::memory
