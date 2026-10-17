#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace zz::mem {

struct Module {
    uintptr_t base = 0;
    size_t size = 0;
};

// Installs the exception handler that makes Read/Write fault-tolerant. Call once at start.
void Init();

Module MainModule();

// IDA style pattern, e.g. "48 8B 05 ?? ?? ?? ?? 48 85 C0". Only executable sections are scanned.
uintptr_t FindPattern(const Module& module, const char* pattern);

// Target of a RIP-relative operand: insn + insn_len + *(int32_t*)(insn + disp_offset).
uintptr_t ResolveRip(uintptr_t insn, int disp_offset, int insn_len);

// Reads/writes that never crash: invalid addresses just return false.
bool ReadRaw(uintptr_t address, void* out, size_t size);
bool WriteRaw(uintptr_t address, const void* data, size_t size);

template <typename T>
std::optional<T> Read(uintptr_t address) {
    T value{};
    if (address && ReadRaw(address, &value, sizeof(T))) return value;
    return std::nullopt;
}

template <typename T>
bool Write(uintptr_t address, const T& value) {
    return address && WriteRaw(address, &value, sizeof(T));
}

// Returns the pointer stored at `address`, or 0.
uintptr_t ReadPtr(uintptr_t address);

// Sets/clears `mask` in the byte at `address`. Returns false if unreadable.
bool SetBits(uintptr_t address, uint8_t mask, bool on);
std::optional<bool> GetBits(uintptr_t address, uint8_t mask);

}  // namespace zz::mem
