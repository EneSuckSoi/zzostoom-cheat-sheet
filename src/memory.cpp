#include "memory.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace zz::mem {

Module MainModule() {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return {base, nt->OptionalHeader.SizeOfImage};
}

namespace {

struct PatternByte {
    uint8_t value;
    bool wildcard;
};

std::vector<PatternByte> ParsePattern(const char* pattern) {
    std::vector<PatternByte> out;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (*p == '?') {
            out.push_back({0, true});
            while (*p == '?') ++p;
            continue;
        }
        char hex[3] = {p[0], p[1], 0};
        out.push_back({static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)), false});
        p += 2;
    }
    return out;
}

}  // namespace

uintptr_t FindPattern(const Module& module, const char* pattern) {
    auto bytes = ParsePattern(pattern);
    if (bytes.empty() || bytes[0].wildcard) {
        // Patterns starting with a wildcard: skip leading wildcards and adjust afterwards.
        size_t lead = 0;
        while (lead < bytes.size() && bytes[lead].wildcard) ++lead;
        if (lead == bytes.size()) return 0;
        std::vector<PatternByte> rest(bytes.begin() + lead, bytes.end());
        std::string rebuilt;
        for (auto& b : rest) {
            char buf[4];
            if (b.wildcard)
                rebuilt += "?? ";
            else {
                std::snprintf(buf, sizeof(buf), "%02X ", b.value);
                rebuilt += buf;
            }
        }
        uintptr_t hit = FindPattern(module, rebuilt.c_str());
        return hit ? hit - lead : 0;
    }

    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module.base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    const size_t n = bytes.size();
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = reinterpret_cast<const uint8_t*>(module.base + section->VirtualAddress);
        size_t len = section->Misc.VirtualSize;
        if (len < n) continue;
        const uint8_t first = bytes[0].value;
        const uint8_t* end = start + len - n + 1;
        for (const uint8_t* p = start; p < end;) {
            p = static_cast<const uint8_t*>(std::memchr(p, first, end - p));
            if (!p) break;
            size_t k = 1;
            for (; k < n; ++k) {
                if (!bytes[k].wildcard && p[k] != bytes[k].value) break;
            }
            if (k == n) return reinterpret_cast<uintptr_t>(p);
            ++p;
        }
    }
    return 0;
}

uintptr_t ResolveRip(uintptr_t insn, int disp_offset, int insn_len) {
    if (!insn) return 0;
    int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(insn + disp_offset), sizeof(disp));
    return insn + insn_len + disp;
}

static bool PlausibleAddress(uintptr_t address, size_t size) {
    return address >= 0x10000 && address + size < 0x7FFFFFFFFFFFull && address + size > address;
}

// Reads and writes are a plain "rep movsb" that may fault: a vectored exception handler turns an
// access violation inside that one instruction into a "false" return. No system call is made on
// the normal path. (ReadProcessMemory/WriteProcessMemory cost a system call each and
// WriteProcessMemory flushes the instruction cache on every core; VirtualQuery scans page tables
// of huge regions - both caused visible stutters when used every frame.)
extern "C" int zz_safe_copy(void* dst, const void* src, size_t size);
extern "C" char zz_safe_copy_begin[], zz_safe_copy_end[], zz_safe_copy_fail[];
__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    ".globl zz_safe_copy\n"
    "zz_safe_copy:\n"
    "    push rsi\n"
    "    push rdi\n"
    "    mov rdi, rcx\n"
    "    mov rsi, rdx\n"
    "    mov rcx, r8\n"
    ".globl zz_safe_copy_begin\n"
    "zz_safe_copy_begin:\n"
    "    rep movsb\n"
    ".globl zz_safe_copy_end\n"
    "zz_safe_copy_end:\n"
    "    mov eax, 1\n"
    "    pop rdi\n"
    "    pop rsi\n"
    "    ret\n"
    ".globl zz_safe_copy_fail\n"
    "zz_safe_copy_fail:\n"
    "    xor eax, eax\n"
    "    pop rdi\n"
    "    pop rsi\n"
    "    ret\n"
    ".att_syntax prefix\n");

namespace {

LONG CALLBACK SafeCopyHandler(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR &&
        code != STATUS_GUARD_PAGE_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto rip = info->ContextRecord->Rip;
    if (rip >= reinterpret_cast<uintptr_t>(zz_safe_copy_begin) &&
        rip < reinterpret_cast<uintptr_t>(zz_safe_copy_end)) {
        info->ContextRecord->Rip = reinterpret_cast<uintptr_t>(zz_safe_copy_fail);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool g_handler_installed = false;

}  // namespace

void Init() {
    if (g_handler_installed) return;
    g_handler_installed = AddVectoredExceptionHandler(1, SafeCopyHandler) != nullptr;
}

bool ReadRaw(uintptr_t address, void* out, size_t size) {
    if (!PlausibleAddress(address, size) || !g_handler_installed) return false;
    return zz_safe_copy(out, reinterpret_cast<const void*>(address), size) != 0;
}

bool WriteRaw(uintptr_t address, const void* data, size_t size) {
    if (!PlausibleAddress(address, size) || !g_handler_installed) return false;
    return zz_safe_copy(reinterpret_cast<void*>(address), data, size) != 0;
}

uintptr_t ReadPtr(uintptr_t address) {
    return Read<uintptr_t>(address).value_or(0);
}

bool SetBits(uintptr_t address, uint8_t mask, bool on) {
    auto v = Read<uint8_t>(address);
    if (!v) return false;
    uint8_t next = on ? (*v | mask) : (*v & ~mask);
    if (next == *v) return true;
    return Write<uint8_t>(address, next);
}

std::optional<bool> GetBits(uintptr_t address, uint8_t mask) {
    auto v = Read<uint8_t>(address);
    if (!v) return std::nullopt;
    return (*v & mask) == mask;
}

}  // namespace zz::mem
