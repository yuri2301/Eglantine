// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>

#include "common/common_types.h"

// Ocarina of Time's real-time speed (from upstream Eglantine's nx2_oot_time.h, c6d2acd3).
// The game's frame loop (main+0x6e6be0) sets each frame's step to 60 / target fps and never reads
// the clock, so below 60 fps everything plays in slow motion. The loader patches the loop to load
// the step from a word in the zeros after .text, and each present writes the measured frame time
// there (1.0 = one 60th of a second): at 30 fps the game steps twice as far per frame and plays at
// real speed. EDEN_NX2_OOT_REAL_TIME=0 leaves the game's fixed step.
namespace Service::Nx2OotTime {

inline constexpr u64 ProgramId = 0x0400486024DEC000ULL;
inline constexpr std::array<u8, 20> BuildId{0xE0, 0xF3, 0xCC, 0xFA, 0xF5, 0x00, 0x65,
                                            0xAE, 0x4E, 0xA0, 0x1E, 0xB5, 0xAE, 0xB1,
                                            0x3B, 0x1A, 0x45, 0xDA, 0x33, 0x48};
inline constexpr u64 DeltaOffset = 0x3C461E0;     ///< The step (float), starts at 1.0
inline constexpr u64 LoadDeltaOffset = 0x3C461E4; ///< ldr s8, DeltaOffset
inline constexpr u64 ReturnOffset = 0x3C461E8;    ///< b HookOffset + 4
inline constexpr u64 HookOffset = 0x6E6CC4;       ///< fdiv s8, s1, s0 -> b LoadDeltaOffset

struct PatchWord {
    u64 offset; ///< From the module's start
    u32 original;
    u32 replacement;
};

inline constexpr std::array<PatchWord, 4> Patch{{
    {DeltaOffset, 0x00000000, 0x3F800000},     // 1.0f
    {LoadDeltaOffset, 0x00000000, 0x1CFFFFE8}, // ldr s8, 0x3c461e0
    {ReturnOffset, 0x00000000, 0x172A82B8},    // b 0x6e6cc8
    {HookOffset, 0x1E201828, 0x14D57D48},      // fdiv s8, s1, s0 -> b 0x3c461e4
}};

/// Guest address of the step word in the running game (0: not patched). One application runs
/// at a time; the loader sets it and each present writes the step there.
inline std::atomic<u64> delta_address{0};

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_OOT_REAL_TIME");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

/// The step for the frame after a present, from the smoothed time between presents.
class FrameDelta {
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] float Update(Clock::time_point now) {
        if (last != Clock::time_point{}) {
            // A pause or a shader stall isn't time the game should catch up on.
            const float seconds = std::chrono::duration<float>(now - last).count();
            average_seconds += ((std::min)(seconds, MaxFrameSeconds) - average_seconds) * Smoothing;
        }
        last = now;
        const float delta = std::clamp(average_seconds * 60.0f, 1.0f, MaxDelta);
        // Near 60 fps, keep the game's own fixed step instead of following timing jitter.
        return delta < SnapBelow ? 1.0f : delta;
    }

private:
    static constexpr float MaxFrameSeconds = 0.1f;
    static constexpr float Smoothing = 0.125f;
    static constexpr float MaxDelta = 4.0f;
    static constexpr float SnapBelow = 1.1f;

    Clock::time_point last{};
    float average_seconds = 1.0f / 60.0f;
};

} // namespace Service::Nx2OotTime
