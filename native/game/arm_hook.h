#pragma once

#include <cstdint>
#include <cstddef>

namespace nana::game {

/**
 * Clean from-scratch ARM32 (Thumb-2 & ARM) / AArch64 inline trampoline hooker.
 *
 * On armeabi-v7a (GTA SA 2.00):
 * - Automatically detects Thumb-2 (addr & 1) vs ARM mode.
 * - Patches target entry with LDR.W PC, [PC, #0] + 32-bit target address (8 bytes,
 *   or 10 bytes with leading 16-bit NOP when 2-byte aligned) without clobbering any GPR.
 * - Copies exact instruction boundaries into an executable RWX trampoline page and
 *   relocates 16-bit PC-relative LDR instructions before branching back to target + N.
 */
class ArmHook {
public:
    static bool Install(void* target, void* replacement, void** outOriginal);
};

} // namespace nana::game
