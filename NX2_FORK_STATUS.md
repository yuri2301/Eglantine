# yuri2301/Eglantine — nx2-experimental: status and handoff

A fork of [Djihads80/Eglantine](https://github.com/Djihads80/Eglantine) (Switch 2 emulator, Eden/yuzu based), tuned for one PC:
**Intel i5-10400F (Comet Lake, 6C/12T, AVX2/FMA/BMI2, no AVX-512) · RTX 3060 12 GB · 16 GB RAM · Windows 11.**
Goal: stable 60 fps in **Mario Kart World** (MKW, title 0400C3F00006E000). Second test game: Donkey Kong Bananza (DKB, purple screen/crash, not investigated yet).

Last updated: 2026-11-04, after build 14.

## How builds and tests work
- Push to branch `nx2-experimental` on `yuri2301/Eglantine`. GitHub Actions (`.github/workflows/release-binaries.yml`) builds **Windows only** and publishes release `build-N`.
  - If a push doesn't trigger it: `gh api -X POST repos/yuri2301/Eglantine/actions/workflows/release-binaries.yml/dispatches -f ref=nx2-experimental -F 'inputs[publish_release]=true'`.
  - A build takes about 50 minutes. Running builds are not cancelled; new pushes queue.
- Assets: `eglantine-windows-x86_64.zip` (MSVC, ~115 MB) and `eglantine-windows-x86_64-pgo.zip` (MinGW clang + `eglantine.profdata`, ~66 MB).
  - The user uses the **MSVC zip**. Builds 2–11's -pgo used Eden's Switch 1 profile, which upstream measured at about half speed in MKW.
  - Builds 12+ use the repo's own profile; it hasn't been compared on this PC yet.
- The user tests and sends `%APPDATA%\eden\log\eden_log.txt`, with the profiler on ("Log performance and memory every 10 seconds" on the Switch 2 Rig page).
- Upstream: remote `djihads`. Builds 12+ merged `djihads/nx2` and `djihads/nx2-experimental`; merge again from time to time.
  - Never push to Djihads80 or zortos293. Never open PRs.
- Commit trailers used: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and the session link.

## Reading the log (profiler on)
- `nx2:vi2 last 600 presents: frame X us (max, N over 100 ms), blocked on the GPU fence ...` — the frame time; 16.7 ms is 60 fps.
- `GPU thread per second:` is the front thread (parses commands, runs macros).
  - `split drain` is its time waiting for the back.
  - `waits for the back:` breaks that down: GPU-written commands, semaphore, macro DMA read, writers of GPU-built lists.
- `GPU back thread per second:` is the back thread (engine methods, rasterizer, draws).
  - `split wait` is its idle time. It is the bottleneck when that is low.
- `Guest CPU per second:` gives per emulated core the time in game code, idle, and kernel/services, plus the busiest guest threads.
- `Memory breakdown:` — game memory in use / pool, translated code, VRAM heaps, system RAM through Vulkan (staging, stream buffer), process RAM, rest of PC.
  - "textures" and "buffers" are the caches' own image/buffer byte counts since build 15 (before: both showed the device total).
- `nx2:vi2 present #N swap interval 1|2` — MKW changes its own target between 60 and 30.

## Build history (what changed, what the logs showed)
| Build | Change | Result (MKW) |
|---|---|---|
| base | Djihads build-129 | Didn't boot: the game list pointed at a partial dump. Complete dump: `Downloads\Mario Kart World Experimental Build\MKW [ROM]` |
| 2 | Windows-only CI | Boots; best ~33 fps; RAM 92% |
| 4–5 | Rig profile; texture cache may use 3/4 of VRAM (`nx2_texture_vram_share`); log spam fixed | VRAM finally used (6–7 GB) |
| 6 | **Switch 2 Rig** settings page (global + per game); the profile only changes settings left at default; CPU Clocks no longer scales guest time for Switch 2 titles | Wrong game speed (100+ fps) fixed |
| 7 | `-DYUZU_BUILD_PRESET=cometlake` (`-march=skylake`, MSVC `/arch:AVX2`) | Heavy scenes still 25–30 fps |
| 8 | Back merges single constant-buffer writes (`coalesce_mask`); drain reasons timed; profiler cheaper | Back less busy, fps unchanged |
| 9 | Macro state writes (Direct, non-executable) go to the back's method sink | Back method calls 3.1M → 1.4M/s |
| 10 | Read-ahead of GPU-written lists (turned itself off); acquire prediction; SEM_EXECUTE checks on the back; guest CPU profiler | Game CPU cores ~40% busy, ~55% idle: **the game's CPU work is not the limit** |
| 11 | Read-ahead kept on (setting); all-channel release table + memory check for >= acquires; zones for sink/deferred calls; dumps of GPU-written lists | **27–31 fps heavy, 55–58 light.** The front waits for GPU-written lists dropped 170–270 → 2 ms/s, but tree shadows popped (~35% of read-ahead lists were a frame old) |
| 12 | Merged upstream nx2 + nx2-experimental: ReadGpuWrittenCommands/WaitForWriters, per-draw cuts, MKW 30 FPS + dynamic FPS options, eglantine.profdata | — |
| 13 | Read-ahead **off** by default | — |
| 14 | Memory breakdown line (dynarmic counts committed code memory) | **Long stretches locked 60 fps, stretches locked 30 (game's choice, back ~81% busy), heavy free roam 20–40 ms.** Game memory 9.3 of 9.8 GB; emulator's own RAM extras < 1 GB; VRAM 5.8 GB; process RAM 4.3 GB (Windows pushed the rest out) |
| 15 | Memory line: textures/buffers report the caches' own bytes; Rig option "Keep the game in RAM" (`nx2_keep_in_ram`, off): soft minimum working set of 60% of RAM, at most all but 4 GB (9.7 GB on this PC) | — (not tested yet) |

Official upstream build (nx2 09e539a, MSVC, handheld) on the same PC: ~19 fps average, ~11 hitches per 600 frames. Ours (build 10): ~31 fps average, ~2 hitches per 600 frames.

## Key findings
- **The bottleneck is the GPU split's back thread** (`src/video_core/gpu_split.cpp`, Vulkan rasterizer). It handles ~55k draws/s at ~13 µs per draw and is ~81–90% busy in heavy scenes. 60 fps there needs it ~40% faster.
- MKW's "GPU-written command lists" are 5-word continuations of a draw macro's parameters, filled by GPU culling: indexCount, instanceCount, firstIndex, vertexOffset, firstInstance — the layout of VkDrawIndexedIndirectCommand. instanceCount 0 means culled.
  - Example: `00000045 00000007 00000000 0000005c 00000002`.
- The game's own CPU threads are not the limit (~40% busy).
- MKW changes swap interval 1↔2 hundreds of times a minute in heavy scenes, which feels uneven.
- Upstream's `mkw_dynamic_fps`, tested on build 14 (docked, 38 min):
  - Frames 30–57 ms (about 18–33 fps), never locked 60, and 3,379 swap-interval changes.
  - The back thread stayed ~80% busy throughout, so the uncapped game simply saturates it.
  - Worse than the default, where the game locks to a steady 30 or 60. Recommended: off. `mkw_30fps` is the option for steady pacing.
  - Late in that session the game used 9.7 of 9.8 GB, translated code 760 MB, VRAM 8.6 GB.
- The user describes an "artificial, pushed" feeling. Suspects: the 60/30 switching, and the extra frame of latency from `nx2_frames_ahead` / async presentation. A test with "Prepare the next frame..." off was suggested.
- RAM: the game itself uses ~9.3 GB. On 16 GB, Windows compresses or pages part of it. The "rest of PC" figure (~11 GB) includes Windows' compressed pages.

## Switch 2 Rig settings (Category::Nx2Rig; `src/common/settings.h`, labels in `src/qt_common/config/shared_translation.cpp`)
- `nx2_rig_profile` (on): suggest values only for settings left at default (`src/core/core.cpp`).
- `nx2_honor_cpu_clock` (off), `nx2_frames_ahead` (on; off = upstream's per-present choice), `nx2_gpu_split` (MethodsAndMacros).
- `nx2_texture_vram_share` (75), `nx2_pin_gpu_threads` (off), `nx2_high_priority` (off), `nx2_keep_in_ram` (off, build 15).
- `nx2_read_ahead_gpu_commands` (off), `nx2_gpu_profiler` (on).
- Environment overrides exist for A/B tests:
  - `EDEN_NX2_SPLIT_SINK_MACRO=0`
  - `EDEN_NX2_SPLIT_COALESCE=0`
  - `EDEN_NX2_PREDICT_ACQUIRES=0`
  - `EDEN_NX2_READ_AHEAD_GPU_COMMANDS=0/1`
  - `EDEN_NX2_FRAMES_AHEAD=0/1`
  - `EDEN_NX2_GPU_PROFILE=0/1`
  - `EDEN_NX2_KEEP_SETTINGS`

## Next steps, in order
1. **Build 15 (done, not tested):** memory line split fixed; "Keep the game in RAM" added. Test it with a heavy MKW session: compare "this process in RAM", "host RAM % used" and the 100 ms hitches with it on and off.
2. **GPU reads the draw lists directly:** when a draw macro's parameters come from a GPU-written GP entry in the indirect-draw layout above, issue `vkCmdDrawIndexedIndirect` from the buffer cache's buffer on the back. No CPU readback, no drain, no stale lists. Make it generic (pattern based), not MKW-only.
3. **Native GPU macros:** recognise NVN's common Turing MME macros by code hash and run native C++ instead of the macro JIT on the front (~1.1M macro calls/s). Generic for every game on the same NVN version.
4. **Spread the back's draw preparation** across spare cores (read handles, image views, buffer updates, stage-buffer binds ≈ 230 ms/s), or use device-generated commands (VK_EXT_device_generated_commands) for groups of draws.
5. **Memory:** commit the 12+2 GiB guest block on touch instead of `SEC_COMMIT` up front (`src/common/host_memory.cpp`), to cut Windows' commit charge.
6. **Study:** find the NVN library in MKW's main executable (RE workspace had `main.img` and scripts) and count the NVN functions called per frame, to size a graphics wrapper.
7. DKB: investigate the purple screen and crash when the user sends a DKB log (executable notes below).

## Log of the build 14 dynamic-FPS session (2,330 s, docked)
- 93 frame reports: 28–57 ms, never a locked 60; 231 frames over 100 ms (3 single frames over 1 s, near 151 s, 635 s and 1952 s).
- Back thread ~70% busy on average; 130 of 222 reports under 200 ms/s idle.
- **Pipeline builds:** 4,358 runtime pipelines, 18.4 s on the GPU thread in total; up to 321 in 10 s (1.7 s). The disk shader cache is on, so they should drop on later runs of the same tracks.
- **RAM:** host RAM averaged 95% used (max 97%), working set 6–6.6 GB of a 12–13 GB private commit, commit charge 46/49 GB. Windows was paging the game: the case `nx2_keep_in_ram` is for.
- User settings in that session: `mkw_dynamic_fps` on (recommended off), DLSS on, high priority on, frame pacing switched between 60/90/120.

## DKB main executable (base/exefs/main, build id a1dd7480bca7b6e8...)
- 25.5 MiB code, 5.9 MiB rodata, 2.8 MiB data (MKW: 40.2 / 8.5 / 2.9). Internal name SilverStar, CSPGO build, platform "Ounce".
- Same NVN API as MKW: the same 700 `nvn*` entry names, with ray tracing, acceleration structures, DLSS networks, shading rate and sparse textures.
- Different SDK point release: NintendoWare 19.3.2 (MKW 19.3.3), no byte-identical 4 KiB code pages with MKW. Native macros (step 3) must therefore match the macros uploaded at run time by hash, not offsets in the executable.
- Middleware: Havok 2022.1 (Physics, Animation, Cloth), sead, NintendoWare Vfx2/G3d2/Ui2d, SMAA_For_NDSR. MKW instead has Pia, NPLN and NintendoWare_Ndsr.
- Steps 2–4 (GPU indirect draws, native macros, spread draw preparation) work at the NVN/GPU level, so they should help DKB too once it boots. The purple screen still needs a DKB log.
