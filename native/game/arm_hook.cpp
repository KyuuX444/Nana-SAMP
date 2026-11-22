#include "arm_hook.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace nana::game {

#if defined(__arm__)

namespace {

std::mutex gHookMutex;
uint8_t* gTrampPage = nullptr;
size_t gTrampOffset = 0;
size_t gTrampPageSize = 0;

void* AllocTrampolineSlot(size_t bytes) {
    size_t aligned = (bytes + 15u) & ~static_cast<size_t>(15u);
    if (gTrampPage == nullptr || gTrampOffset + aligned > gTrampPageSize) {
        size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (pageSize == 0) pageSize = 4096;
        size_t allocSize = pageSize * 4;
        void* mem = mmap(nullptr, allocSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return nullptr;
        gTrampPage = static_cast<uint8_t*>(mem);
        gTrampPageSize = allocSize;
        gTrampOffset = 0;
    }
    void* slot = gTrampPage + gTrampOffset;
    gTrampOffset += aligned;
    return slot;
}

bool MakePageRWX(uintptr_t addr, size_t len) {
    size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    if (pageSize == 0) pageSize = 4096;
    uintptr_t pageStart = addr & ~(pageSize - 1);
    size_t protectLen = pageSize;
    if (addr + len > pageStart + pageSize) {
        protectLen = pageSize * 2;
    }
    return mprotect(reinterpret_cast<void*>(pageStart), protectLen,
                    PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

inline bool IsThumb32(uint16_t hw) {
    uint16_t top5 = hw >> 11;
    return top5 == 0x1D || top5 == 0x1E || top5 == 0x1F;
}

bool InstallThumbHook(uintptr_t targetAddr, uintptr_t replacementAddr, void** outOriginal) {
    // Strip Thumb bit to obtain actual code address
    uintptr_t codeAddr = targetAddr & ~static_cast<uintptr_t>(1);
    bool needsPadNop = (codeAddr & 2u) != 0;
    size_t patchBytes = needsPadNop ? 10 : 8;

    // Scan complete Thumb-16 / Thumb-32 instructions covering at least patchBytes
    auto* src = reinterpret_cast<const uint16_t*>(codeAddr);
    size_t copiedBytes = 0;
    std::vector<uint16_t> tramp;
    tramp.reserve(24);

    while (copiedBytes < patchBytes) {
        size_t idx = copiedBytes / 2;
        uint16_t hw1 = src[idx];
        uintptr_t insnPc = codeAddr + copiedBytes;

        if (IsThumb32(hw1)) {
            uint16_t hw2 = src[idx + 1];
            tramp.push_back(hw1);
            tramp.push_back(hw2);
            copiedBytes += 4;
        } else if ((hw1 & 0xF800u) == 0x4800u) {
            // 16-bit Thumb LDR Rt, [PC, #imm8*4] -> relocate to absolute literal load:
            //   LDR.W Rt, [PC, #4]  (or aligned)
            //   B +6
            //   .word literalAddr
            //   LDR Rt, [Rt]
            uint32_t rt = (hw1 >> 8) & 0x7u;
            uint32_t imm8 = hw1 & 0xFFu;
            uintptr_t poolAddr = ((insnPc + 4u) & ~static_cast<uintptr_t>(3u)) + (imm8 * 4u);
            uint32_t loadedWord = *reinterpret_cast<const uint32_t*>(poolAddr);

            // Ensure 4-byte alignment before LDR.W Rt, [PC, #2] + B +4 + .word loadedWord
            if ((tramp.size() & 1u) != 0) {
                tramp.push_back(0xBF00u); // NOP
            }
            // LDR.W Rt, [PC, #4]: 0xF8DF (rt<<12 | 4)
            tramp.push_back(0xF8DFu);
            tramp.push_back(static_cast<uint16_t>((rt << 12) | 4u));
            // 16-bit B +2 (skips 2-byte NOP + 4-byte literal? Wait: PC at this insn is +4, so B +2 skips 4 bytes!)
            tramp.push_back(0xE002u); // B +4 bytes past the 2-byte pad + 4-byte word -> wait:
            tramp.push_back(0xBF00u); // 2-byte align pad so literal is at +4 from LDR.W PC!
            // Now literal is at LDR.W_addr + 8 = (LDR.W_addr + 4) + 4!
            tramp.push_back(static_cast<uint16_t>(loadedWord & 0xFFFFu));
            tramp.push_back(static_cast<uint16_t>((loadedWord >> 16) & 0xFFFFu));
            copiedBytes += 2;
        } else if ((hw1 & 0xFF78u) == 0x4478u) {
            // 16-bit Thumb ADD Rd, PC (0x4478..0x447F for r0..r7, or high reg 0x44F8..0x44FF):
            // Relocate PC value (insnPc + 4) cleanly without clobbering any other register:
            //   PUSH {R12}              ; save scratch
            //   LDR.W R12, [PC, #4]     ; load original (insnPc + 4)
            //   B +4                    ; skip pad + literal
            //   NOP
            //   .word (insnPc + 4)
            //   ADD Rd, R12
            //   POP {R12}
            uint32_t rd = (hw1 & 0x7u) | ((hw1 & 0x80u) >> 4);
            uint32_t origPc = static_cast<uint32_t>(insnPc + 4u);

            // PUSH.W {R12} = 0xF84D 0xCD04
            tramp.push_back(0xF84Du);
            tramp.push_back(0xCD04u);
            if ((tramp.size() & 1u) != 0) {
                tramp.push_back(0xBF00u);
            }
            // LDR.W R12, [PC, #4] = 0xF8DF 0xC004
            tramp.push_back(0xF8DFu);
            tramp.push_back(0xC004u);
            tramp.push_back(0xE002u); // B +4 bytes past NOP + literal
            tramp.push_back(0xBF00u);
            tramp.push_back(static_cast<uint16_t>(origPc & 0xFFFFu));
            tramp.push_back(static_cast<uint16_t>((origPc >> 16) & 0xFFFFu));
            // ADD Rd, R12: 0x4460 | ((rd & 8) << 4) | (rd & 7)
            uint16_t addRdR12 = static_cast<uint16_t>(0x4460u | ((rd & 0x8u) << 4) | (rd & 0x7u));
            tramp.push_back(addRdR12);
            // POP.W {R12} = 0xF85D 0xCB04
            tramp.push_back(0xF85Du);
            tramp.push_back(0xCB04u);
            copiedBytes += 2;
        } else {
            tramp.push_back(hw1);
            copiedBytes += 2;
        }
    }

    // Align trampoline to 4 bytes before LDR.W PC, [PC, #0] ; .word resumeAddr
    if ((tramp.size() & 1u) != 0) {
        tramp.push_back(0xBF00u); // NOP
    }
    uintptr_t resumeAddr = (codeAddr + copiedBytes) | 1u;
    tramp.push_back(0xF8DFu);
    tramp.push_back(0xF000u);
    tramp.push_back(static_cast<uint16_t>(resumeAddr & 0xFFFFu));
    tramp.push_back(static_cast<uint16_t>((resumeAddr >> 16) & 0xFFFFu));

    size_t trampSizeBytes = tramp.size() * sizeof(uint16_t);
    void* trampMem = AllocTrampolineSlot(trampSizeBytes);
    if (!trampMem) return false;

    std::memcpy(trampMem, tramp.data(), trampSizeBytes);
    __builtin___clear_cache(static_cast<char*>(trampMem),
                            static_cast<char*>(trampMem) + trampSizeBytes);

    if (outOriginal != nullptr) {
        *outOriginal = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(trampMem) | 1u);
    }

    if (!MakePageRWX(codeAddr, copiedBytes)) return false;

    std::vector<uint16_t> patch;
    if (needsPadNop) {
        patch.push_back(0xBF00u); // NOP to align LDR.W PC, [PC, #0] to 4-byte boundary
    }
    uintptr_t repThumb = replacementAddr; // Keep caller's ARM/Thumb bit intact!
    patch.push_back(0xF8DFu);
    patch.push_back(0xF000u);
    patch.push_back(static_cast<uint16_t>(repThumb & 0xFFFFu));
    patch.push_back(static_cast<uint16_t>((repThumb >> 16) & 0xFFFFu));
    while (patch.size() * 2 < copiedBytes) {
        patch.push_back(0xBF00u);
    }

    std::memcpy(reinterpret_cast<void*>(codeAddr), patch.data(), patch.size() * 2);
    __builtin___clear_cache(reinterpret_cast<char*>(codeAddr),
                            reinterpret_cast<char*>(codeAddr) + patch.size() * 2);
    return true;
}

bool InstallArm32Hook(uintptr_t targetAddr, uintptr_t replacementAddr, void** outOriginal) {
    // ARM mode: 8-byte patch: LDR PC, [PC, #-4] (0xE51FF004) ; .word replacementAddr
    auto* src = reinterpret_cast<const uint32_t*>(targetAddr);
    uint32_t tramp[4];
    tramp[0] = src[0];
    tramp[1] = src[1];
    tramp[2] = 0xE51FF004u;
    tramp[3] = static_cast<uint32_t>(targetAddr + 8);

    void* trampMem = AllocTrampolineSlot(sizeof(tramp));
    if (!trampMem) return false;
    std::memcpy(trampMem, tramp, sizeof(tramp));
    __builtin___clear_cache(static_cast<char*>(trampMem),
                            static_cast<char*>(trampMem) + sizeof(tramp));
    if (outOriginal) *outOriginal = trampMem;

    if (!MakePageRWX(targetAddr, 8)) return false;
    uint32_t patch[2] = { 0xE51FF004u, static_cast<uint32_t>(replacementAddr) };
    std::memcpy(reinterpret_cast<void*>(targetAddr), patch, sizeof(patch));
    __builtin___clear_cache(reinterpret_cast<char*>(targetAddr),
                            reinterpret_cast<char*>(targetAddr) + sizeof(patch));
    return true;
}

} // namespace

bool ArmHook::Install(void* target, void* replacement, void** outOriginal) {
    if (!target || !replacement) return false;
    std::lock_guard<std::mutex> lock(gHookMutex);
    uintptr_t tAddr = reinterpret_cast<uintptr_t>(target);
    uintptr_t rAddr = reinterpret_cast<uintptr_t>(replacement);
    if ((tAddr & 1u) != 0) {
        return InstallThumbHook(tAddr, rAddr, outOriginal);
    }
    return InstallArm32Hook(tAddr, rAddr, outOriginal);
}

#else

bool ArmHook::Install(void*, void*, void**) {
    return false;
}

#endif

} // namespace nana::game
