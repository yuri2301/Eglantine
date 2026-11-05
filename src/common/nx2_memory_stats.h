// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "common/common_types.h"

// Where the emulator's memory goes, for the profiler's memory line: each part sets its figure
// (bytes) now and then, and the GPU thread's report prints them every 10 s.
namespace Common::Nx2Memory {

enum class Stat : u32 {
    GameUsed,          ///< Guest memory the game's kernel has handed out (application pool)
    GameSize,          ///< The application pool's size
    JitCode,           ///< Host memory committed for translated guest code (all cores)
    TextureCache,      ///< Images and their views the texture cache counts (device memory)
    BufferCache,       ///< Buffers the buffer cache counts (device memory)
    StagingUpload,     ///< Vulkan staging buffers for uploads (system RAM)
    StagingDownload,   ///< Vulkan staging buffers for downloads (system RAM)
    StagingDevice,     ///< Vulkan staging buffers in device memory
    StreamBuffer,      ///< The upload stream buffer (system RAM)
    VulkanDeviceHeaps, ///< This process's use of the GPU's own heaps (VK_EXT_memory_budget)
    VulkanHostHeaps,   ///< This process's use of system RAM heaps through Vulkan
    Count,
};

inline std::array<std::atomic<u64>, static_cast<size_t>(Stat::Count)> g_stats{};

inline void Set(Stat stat, u64 bytes) {
    g_stats[static_cast<size_t>(stat)].store(bytes, std::memory_order_relaxed);
}

/// At each game's start: the last game's figures would stand until the new one sets its own.
inline void Reset() {
    for (auto& stat : g_stats) {
        stat.store(0, std::memory_order_relaxed);
    }
}

[[nodiscard]] inline u64 Get(Stat stat) {
    return g_stats[static_cast<size_t>(stat)].load(std::memory_order_relaxed);
}

} // namespace Common::Nx2Memory
