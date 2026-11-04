// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "common/common_types.h"
#include "common/logging.h"
#include "video_core/nx2_profile.h"

// Where the emulated CPU cores spend their time (with the GPU thread profiler, every 10 s):
// running guest code (JIT), idle (no guest thread to run: the game waits, e.g. on the GPU), and
// the rest (kernel, SVCs, services answered on the core's thread). Plus the guest threads that run
// the most guest code: a frame bound by the game's own CPU work shows one near 1000 ms.
namespace Kernel::Nx2GuestProfile {

struct ThreadSlot {
    std::atomic<u64> id{~u64{0}};
    std::atomic<u64> ns{};
};

struct CoreData {
    std::atomic<u64> guest_ns{};
    std::atomic<u64> idle_ns{};
    std::atomic<u64> svcs{};
    std::array<ThreadSlot, 16> threads{};
};

inline std::array<CoreData, 8> g_cores{};
inline std::atomic<u64> g_window_start{};

inline bool Enabled() {
    return VideoCommon::Nx2Profile::Enabled();
}

inline u64 Now() {
    return VideoCommon::Nx2Profile::Now();
}

inline void AddGuest(size_t core, u64 thread_id, u64 ns) {
    if (core >= g_cores.size()) {
        return;
    }
    auto& data = g_cores[core];
    data.guest_ns.fetch_add(ns, std::memory_order_relaxed);
    for (auto& slot : data.threads) {
        u64 id = slot.id.load(std::memory_order_relaxed);
        if (id == ~u64{0} &&
            slot.id.compare_exchange_strong(id, thread_id, std::memory_order_relaxed)) {
            id = thread_id;
        }
        if (id == thread_id) {
            slot.ns.fetch_add(ns, std::memory_order_relaxed);
            return;
        }
    }
}

inline void AddIdle(size_t core, u64 ns) {
    if (core < g_cores.size()) {
        g_cores[core].idle_ns.fetch_add(ns, std::memory_order_relaxed);
    }
}

inline void AddSvc(size_t core) {
    if (core < g_cores.size()) {
        g_cores[core].svcs.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void MaybeReport() {
    const u64 now = Now();
    u64 start = g_window_start.load(std::memory_order_relaxed);
    if (start == 0) {
        g_window_start.compare_exchange_strong(start, now, std::memory_order_relaxed);
        return;
    }
    if (now - start < 10'000'000'000ULL ||
        !g_window_start.compare_exchange_strong(start, now, std::memory_order_relaxed)) {
        return;
    }
    const double seconds = static_cast<double>(now - start) / 1e9;
    const auto per_second = [seconds](u64 ns) { return static_cast<double>(ns) / 1e6 / seconds; };
    std::string line = "Guest CPU per second:";
    struct Busy {
        u64 id;
        u64 ns;
        size_t core;
    };
    std::vector<Busy> busiest;
    for (size_t core = 0; core < g_cores.size(); ++core) {
        auto& data = g_cores[core];
        const u64 guest = data.guest_ns.exchange(0, std::memory_order_relaxed);
        const u64 idle = data.idle_ns.exchange(0, std::memory_order_relaxed);
        const u64 svcs = data.svcs.exchange(0, std::memory_order_relaxed);
        if (guest == 0 && idle == 0 && svcs == 0) {
            continue;
        }
        const double guest_ms = per_second(guest);
        const double idle_ms = per_second(idle);
        line += fmt::format(" | core {}: guest code {:.0f} ms, idle {:.0f} ms, kernel/services {:.0f} "
                            "ms, {:.0f} SVCs",
                            core, guest_ms, idle_ms, std::max(0.0, 1000.0 - guest_ms - idle_ms),
                            static_cast<double>(svcs) / seconds);
        for (auto& slot : data.threads) {
            const u64 ns = slot.ns.exchange(0, std::memory_order_relaxed);
            const u64 id = slot.id.exchange(~u64{0}, std::memory_order_relaxed);
            if (ns == 0 || id == ~u64{0}) {
                continue;
            }
            const auto it = std::find_if(busiest.begin(), busiest.end(),
                                         [id](const Busy& busy) { return busy.id == id; });
            if (it != busiest.end()) {
                it->ns += ns;
            } else {
                busiest.push_back({id, ns, core});
            }
        }
    }
    std::sort(busiest.begin(), busiest.end(),
              [](const Busy& a, const Busy& b) { return a.ns > b.ns; });
    line += " | busiest guest threads:";
    for (size_t i = 0; i < std::min<size_t>(busiest.size(), 5); ++i) {
        line += fmt::format(" {} (core {}) {:.0f} ms;", busiest[i].id, busiest[i].core,
                            per_second(busiest[i].ns));
    }
    LOG_INFO(Core, "{}", line);
}

} // namespace Kernel::Nx2GuestProfile
