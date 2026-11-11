// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"

// Frame overlap copies, for a game running a frame ahead of the GPU thread. After frame k's
// present returns, the game writes frame k+1's data while the GPU thread may still read frame
// k's from the same memory (Ocarina of Time: about 280 pages a frame, its foliage data, which
// flashed white). The present arms the frame guard (Memory::ArmFrameGuard without a wait): the
// game's first write to a page frame k may still read saves a copy of the page here first. The
// GPU thread's reads of guest memory see the copy until it reaches frame k's fence, which drops
// the copies and marks their memory written, so frame k+1 reads the new data.
// EDEN_NX2_FRAME_OVERLAP=0 turns the copies off (the game then reads the next frame's data).
// EDEN_NX2_FRAME_HAZARD=1 counts the reads that took a copy (logged with the present stats).
namespace Core::Nx2FrameHazard {

inline const bool copies_enabled = [] {
    const char* value = std::getenv("EDEN_NX2_FRAME_OVERLAP");
    return value == nullptr || value[0] != '0';
}();
inline const bool counting = [] {
    const char* value = std::getenv("EDEN_NX2_FRAME_HAZARD");
    return value != nullptr && value[0] == '1';
}();

constexpr u64 PageBits = 12;
constexpr u64 PageSize = u64{1} << PageBits;
using PageCopy = std::array<u8, PageSize>;

enum Reader : u32 { DeviceRead, DeviceSpan, GpuRead, BufferPointer, Readers };

struct Hit {
    u64 count{};
    u64 bytes{};
};

struct Entry {
    u64 serial{};
    std::unique_ptr<PageCopy> copy;
    std::vector<u64> device_pages; ///< Device addresses mapping this page (for the invalidation)
};

struct State {
    std::mutex mutex;
    /// Host page (pointer >> 12) -> its copy as frame `serial` (in flight when written) reads it.
    std::unordered_map<uintptr_t, Entry> pages;
    std::vector<std::unique_ptr<PageCopy>> free_copies;
    std::atomic<size_t> page_count{};
    std::atomic<u64> frames_done{};
    // Stats since the last Take.
    u64 recorded{};
    Hit hits[Readers]{};
    std::unordered_map<u64, u64> hit_pages;
};

inline State& Get() {
    static State state;
    return state;
}

/// Whether the GPU thread may need a copy now (cheap check before the lock).
[[nodiscard]] inline bool Active() {
    return Get().page_count.load(std::memory_order_acquire) != 0;
}

/// The game is about to write `host` (a page frame `serial` may still read): saves the page
/// unless it has a copy already. `device_pages` lists the device addresses of that page.
template <typename DevicePages>
inline void Record(const u8* host, u64 serial, DevicePages&& device_pages) {
    if (host == nullptr) {
        return;
    }
    auto& s = Get();
    const uintptr_t page = reinterpret_cast<uintptr_t>(host) >> PageBits;
    std::scoped_lock lk{s.mutex};
    auto [it, inserted] = s.pages.try_emplace(page);
    if (!inserted) {
        return;
    }
    Entry& entry = it->second;
    entry.serial = serial;
    if (copies_enabled) {
        if (s.free_copies.empty()) {
            entry.copy = std::make_unique<PageCopy>();
        } else {
            entry.copy = std::move(s.free_copies.back());
            s.free_copies.pop_back();
        }
        std::memcpy(entry.copy->data(), reinterpret_cast<const u8*>(page << PageBits), PageSize);
    }
    device_pages(entry.device_pages);
    ++s.recorded;
    s.page_count.store(s.pages.size(), std::memory_order_release);
}

/// Frame `serial` reached its fence (on the thread that signals it): drops the copies frame
/// `serial` and earlier read, and calls invalidate(device address, size) for their memory.
template <typename Invalidate>
inline void FrameDone(u64 serial, Invalidate&& invalidate) {
    auto& s = Get();
    std::vector<u64> written;
    {
        std::scoped_lock lk{s.mutex};
        u64 done = s.frames_done.load(std::memory_order_relaxed);
        if (serial > done) {
            s.frames_done.store(serial, std::memory_order_release);
        }
        for (auto it = s.pages.begin(); it != s.pages.end();) {
            if (it->second.serial <= serial) {
                written.insert(written.end(), it->second.device_pages.begin(),
                               it->second.device_pages.end());
                if (it->second.copy && s.free_copies.size() < 4096) {
                    s.free_copies.push_back(std::move(it->second.copy));
                }
                it = s.pages.erase(it);
            } else {
                ++it;
            }
        }
        s.page_count.store(s.pages.size(), std::memory_order_release);
    }
    for (const u64 device_page : written) {
        invalidate(device_page, PageSize);
    }
}

/// A GPU-side read of guest memory at device address `device_addr` copied into `dest` (or only
/// checked when dest is null): puts the saved copies of pages written for the next frame over
/// it. `to_host(page)` gives the host pointer of a device page. Returns whether a copy applied.
template <typename ToHost>
inline bool Overlay(u64 device_addr, void* dest, size_t size, Reader reader, ToHost&& to_host) {
    if (size == 0 || !Active()) {
        return false;
    }
    auto& s = Get();
    const u64 end = device_addr + size;
    bool hit = false;
    std::scoped_lock lk{s.mutex};
    const u64 done = s.frames_done.load(std::memory_order_acquire);
    for (u64 page = device_addr & ~(PageSize - 1); page < end; page += PageSize) {
        const u8* const host = to_host(page);
        if (host == nullptr) {
            continue;
        }
        const auto it = s.pages.find(reinterpret_cast<uintptr_t>(host) >> PageBits);
        if (it == s.pages.end() || it->second.serial <= done) {
            continue;
        }
        hit = true;
        if (counting) {
            ++s.hit_pages[page >> PageBits];
        }
        if (dest != nullptr && it->second.copy) {
            const u64 from = (std::max)(page, device_addr);
            const u64 to = (std::min)(page + PageSize, end);
            std::memcpy(static_cast<u8*>(dest) + (from - device_addr),
                        it->second.copy->data() + (from - page), to - from);
        }
    }
    if (hit && counting) {
        ++s.hits[reader].count;
        s.hits[reader].bytes += size;
    }
    return hit;
}

struct Stats {
    u64 recorded{};
    Hit hits[Readers]{};
    std::vector<std::pair<u64, u64>> top_pages; ///< (device page address, reads), most first
    size_t distinct_pages{};
};

inline Stats Take() {
    auto& s = Get();
    std::scoped_lock lk{s.mutex};
    Stats stats;
    stats.recorded = s.recorded;
    std::copy(std::begin(s.hits), std::end(s.hits), std::begin(stats.hits));
    stats.distinct_pages = s.hit_pages.size();
    for (const auto& [page, count] : s.hit_pages) {
        stats.top_pages.emplace_back(page << PageBits, count);
    }
    std::sort(stats.top_pages.begin(), stats.top_pages.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (stats.top_pages.size() > 12) {
        stats.top_pages.resize(12);
    }
    s.recorded = 0;
    std::fill(std::begin(s.hits), std::end(s.hits), Hit{});
    s.hit_pages.clear();
    return stats;
}

} // namespace Core::Nx2FrameHazard
