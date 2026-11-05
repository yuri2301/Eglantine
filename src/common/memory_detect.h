// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace Common {

struct MemoryInfo {
    u64 TotalPhysicalMemory{};
    u64 TotalSwapMemory{};
};

/**
 * Gets the memory info of the host system
 * @return Reference to a MemoryInfo struct with the physical and swap memory sizes in bytes
 */
[[nodiscard]] const MemoryInfo& GetMemInfo();

/// What this process and the host use right now, in bytes (zeros where the host can't tell).
struct MemoryUsage {
    u64 process_working_set{}; ///< Physical memory the process occupies
    u64 process_private{};     ///< Memory committed to the process (its share of the commit charge)
    u64 available_physical{};  ///< Host physical memory still free
    u64 commit_used{};         ///< Host commit charge (RAM plus page file in use)
    u64 commit_limit{};        ///< Host commit limit
    u32 physical_load_percent{};
    u64 page_faults{};         ///< Page faults of the process so far (soft and hard)
};

[[nodiscard]] MemoryUsage GetMemoryUsage();

} // namespace Common
