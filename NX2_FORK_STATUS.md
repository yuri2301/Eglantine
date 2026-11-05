# Pitoco (formerly yuri2301/Eglantine nx2-experimental): status and handoff

The project is now Pitoco (see [PITOCO.md](PITOCO.md)), on the `pitoco` branch.

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
| 15 | Memory line: textures/buffers report the caches' own bytes; Rig option "Keep the game in RAM" (`nx2_keep_in_ram`, off): soft minimum working set of 60% of RAM, at most all but 4 GB (9.7 GB on this PC) | 3 MKW sessions (dynamic FPS on, sync core speed on): frames 28–55 ms, far fewer 100 ms hitches (0–2 per 600 frames, was 3–7). P-switch: draws fell from ~60k/s to 3–6k/s and fps hit target at once. Kart slower than the map at times. DKB: purple screen (wrong upscale pair). Host RAM still 95–99% |
| 16 | DKB: the upscale fallback takes only frame-sized pairs (same aspect, ≥320x180, ≤4x) and the largest output, scans cbuf0+0x80..0x400 and logs the candidates once per program; the back thread's method calls timed (1 in 32) so "other" shrinks to what is really unzoned; memory line reset at each game; graphics pipelines evicted after 18,000 unused frames (was 9,000) and the log counts evicted ones built again | — (not tested yet) |
| 17 | Kernel: `svcWaitForAddress` wait type 3 (WaitIfEqual64, 64-bit compare on an 8-byte-aligned address); the 64-bit SVC passes the value as s64. Shader cache loader: an entry of 0 or more than 5 stages, or code larger than the rest of the file, ends the load and the file is cut to the last whole entry (a power cut left MKW's cache with a zeroed tail; the 0-stage entry read past an empty list and corrupted the heap at boot). The 30 FPS mode (`mkw_30fps`) and the "recommended MKW settings" prompt are removed | Ocarina boots, plays its AV1 title video at 60 fps (MSVC build; the MinGW/PGO FFmpeg has no AV1 decoder) and crashes 3 s into gameplay (43 shaders refused). DKB shows graphics for the first time (upscale pair 960x540 -> 1080p), flashing purple, T-posing DK, crash. MKW (legacy mode, MSVC): a 23-minute Grand Prix played, 40-50 ms frames, hitches with 200-300 ms GPU waits, RAM 87-97% |
| Pitoco 1 (build 17 on `pitoco`) | Name, icon, lime green theme, `pitoco_mode` | see build 17 |
| Pitoco 2 (build 18) | One Pitoco mode decided at load (`Settings::PitocoActive`, logged): GPU split (methods + macros), high priority, keep in RAM following free RAM (`UpdateKeepInRam` every 600 frames), texture VRAM limit = live budget less what the rest of the process holds less 1 GiB; legacy = Eden/Eglantine (no split). Rig profile and frames ahead off, the per-option rig settings hidden. Shaders: ISETP.EX with a combining predicate, TLD4 on ARRAY_2D, RED F32 ADD (Ocarina: 41 of 43 refusals). Upscale fallback reuses the last pair when a pass has none (DKB purple frames). Crash handler: `pitoco_crash.txt`/`.dmp` in the log folder, `pitoco.map` shipped. Debug probe of title 4's addresses removed | Ocarina (both runs logged Pitoco mode on): title screen and intro, no 3D after the intro cutscene; one run reached gameplay (draws 120/s → 1,944/s at 290 s, five new game compute programs refused) and crashed in `QueryCacheRuntime::Barriers`: the front recorded fence work after a drain while the back's idle hook submitted the same command buffer. Dump empty (struct packing) |
| Pitoco 0.1.9 (in code, not built) | Developer log `pitoco_dev.txt`, page faults/s in the memory line, switches in effect listed at load, shipped `nx2_switches.txt` emptied, build 18 VRAM raise reverted, version `0.X.Y beta`. Split: from the end of a drain until it sends more work the front holds the back's state (`HoldBack`, it runs the idle hook itself; the back skips its hook while held; `WaitSubmitted` flushes on the front when it holds). Upscale last-pair reuse only within 1 MiB of the program that found the pair (Ocarina's own network at 0x1149018100 had the frame overwritten). Crash dump: 4-byte packed exception record | — |

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
  - Worse than the default, where the game locks to a steady 30 or 60 (an early reading; the user preferred dynamic FPS in play).
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

## Ocarina on 0.1.9 (MSVC, Parallel GPU execution)
- AV1 intro plays (libdav1d). Gameplay starts at ~255 s: draws jump from 120/s to 13,000-28,000/s, so the game draws its 3D scene, but nothing reaches the screen.
- **Likely cause: untracked global memory.** "Storage buffer failed to track, using global memory fallbacks": 1,402 shaders while loading the cache, 271 more as gameplay starts (MKW: 0). The fallback (`DefineGlobalMemoryFunctions`) only looks in the storage buffers the program binds; an address outside them reads zero. Ocarina's 3D shaders read data through pointers the tracker can't follow, so their inputs come back as zeros. The log now names the program and the expression the address is built from (once per program), to tell a tracker gap (small fix) from pointers loaded from memory (needs guest GPU addresses translated in shaders: a page table plus buffer device addresses).
- Smaller: 23 draws skipped for rejected programs (sign extend 4, scratch registers 2, opcode 0x243 and others); 8 matrix-multiply programs (the upscaler) replaced by the copy fallback.
- PGO run (title screen, stopped at ~50 s): crash on close, a use-after-free in `Scheduler::SubmitExecution` from `~RasterizerVulkan` during `ShutdownMainProcess`. The split is already stopped there, so it's not the race. Non-critical.

## MKW on 0.1.9 (PGO, first clean A/B: no switches in effect)
- Compatibility (51-436 s, more menu time): 25.7 fps mean, races 13-27 fps, 14 frames over 100 ms per 600 frames.
- Parallel GPU execution (437-1960 s): 25.3 fps mean, races a steady 20-28 fps, 6 frames over 100 ms per 600 frames. The user: playable, compatibility unplayable; made the default and renamed (was "Pitoco mode", setting `parallel_gpu_execution`).
- **The spikes follow pipeline builds.** 1,639 graphics pipelines built on the GPU thread during play (~3.9 ms each, up to 93 ms); windows with 0-10 builds had 0-2 frames over 100 ms, those with 130-200 had 26-37. Sources: 1,473 pipelines in the disk cache not built at boot (only those in the "used" list of recent sessions are), and eviction (unused for 18,000 frames: 945 destroyed at 1,201 s, ~400 of them built again in later races). Each built pipeline holds ~0.45 MB of driver memory, and RAM was at 89-97% (the game process 8.2-11 GiB, the rest of the PC 4.7-7.1 GiB), page faults 2,400-77,000/s.

## Crashes (critical: ends normal play of MKW or Ocarina, an emulator bug, fixed first; non-critical: rare path, exit, dropped game or missing feature, logged and deferred)
| Crash | Class | Where | State |
| --- | --- | --- | --- |
| Split race: the front records into the Vulkan command buffer while the back's idle hook submits it (`QueryCacheRuntime::Barriers` Ocarina, `CopyBuffer` via `FlushGpuWrittenCommands` DKB, `SubmitExecution` via `FlushDeferred` MKW Grand Prix at 1,071 s) | Critical | `gpu_split.cpp` idle hook vs front after `Drain` | Fixed in 0.1.9 (`HoldBack`); all three reports were build 18. Confirm with MKW on the 0.1.9 MSVC build |
| Ocarina after the intro cutscene | Critical (no image, not a crash on 0.1.9) | untracked global memory reads zero (see "Ocarina on 0.1.9") | 0.1.9 MSVC: no crash in 314 s; the 3D is drawn but its inputs read zero |
| Crash on close | Non-critical | exit: `~RasterizerVulkan` -> `Scheduler::Finish` -> `SubmitExecution` (use-after-free, split already stopped) | Deferred |
| Unimplemented services logged as Critical (`nx2:vi2` 201, `IGraphicsScaleController` 1050, `IGeneralService` 101, `nx2:gsv` 903, `prepo:u` 10500) | Non-critical | services | Stubbed, they don't end the game |
| No AV1 in the PGO build (Ocarina's intro video) | Non-critical | MinGW FFmpeg | Use the MSVC build for Ocarina |
| Donkey Kong Bananza | — | — | Development halted (user, 2026-11-05) |

## Code review, 2026-11-05 (in code, not built)
- Removed the Bananza indirect-reset fix (`TryNx2IndirectReset`, `IsIndirectResetShader`, `indirect_reset.h` and its test, the per-program hash): it ran a check on every non-indexed draw of every game and only ever fired in Bananza.
- Removed debug traces from the draw and clear paths (`TraceWindowTarget`, CB24 trace with Bananza shader addresses, `EDEN_NX2_TRACE_VS`, after-upscale trace, `EDEN_NX2_SKIP_VS`, `EDEN_NX2_RT_TRACE`), from the upscale fallback (image sampling with GPU downloads, `EDEN_NX2_UPSCALE_SOURCE`) and from every macro call (`EDEN_NX2_MME_LOG`, `EDEN_NX2_MME_RECORD`, `EDEN_NATIVE_FRAME_CAPTURE`).
- Added: per-method time breakdown of the back thread in `pitoco_dev.txt` (profiler on).
- Checked: every front-side use of the back's state after a drain is now covered by `HoldBack`; `ReadGpuWrittenCommandList` takes its own lock; flush and invalidate requests run on the back in order.
- Measured, not a cost: the remaining `getenv` switches are read once into statics (a few ns per check). They stay until the strip in plan step 2.

## Overview after build 18 (Pitoco 0.1.8)
- **No A/B so far was real.** The release's `nx2_switches.txt` set `EDEN_NX2_SPLIT=2`, so "legacy" ran the GPU split too. Emptied for 0.1.9; the switches in effect are listed at load ("Developer switches in effect").
- **MKW has been ~21-24 fps on average since build 15**, 15-20 fps in races, ~40 in menus, in every mode. In a 12 fps race second: the GPU thread's method processing 655 ms/s on the back (1.22M method calls/s; 4.84M 3D register writes of which 2.59M rewrite the same value; 32k draws/s at ~20 us each), the front 524 ms/s pushing plus 235 ms/s waiting for the back (264 ms/s on writers of GPU-built lists). Guest CPU cores ~55% busy: the game waits for the GPU emulation.
- **RAM is the scarce resource:** 16 GiB, the rest of the PC holds 7-10 GiB, the game 9.2 GiB. Build 18's VRAM raise (textures to 8.3 GiB, driver commit with it) filled RAM to 97% and the keep-in-RAM floor fell from 9.7 to 1 GiB: Pitoco mode choppier than legacy. Reverted.
- **Both crashes (Ocarina, DKB) were one race** between the split's two threads (front records after a drain while the back's idle hook submits). Fixed for 0.1.9.
- **122 `EDEN_NX2_*` switches** and 11 rig settings: experiments that each keep a branch alive.

## Plan: one mode, Parallel GPU execution (user, 2026-11-05)
1. **0.1.9 (in code):** race fix, VRAM revert, empty switches file, developer log `pitoco_dev.txt` (user log keeps warnings, errors, general lines) with page faults/s, crash dump fix, upscale reuse within one network, version naming `0.X.Y beta`.
2. **Strip:** remove the 122 switches and the rig settings that are experiments; Pitoco mode = what ran best (legacy + split), every remaining choice fixed in code; legacy = Eden unchanged.
3. **Thin the GPU path (the measured cost):** same-value writes are already dropped at the front (`FrontWrite`), so first measure which executable methods the back's 655 ms/s of method time goes to (per-method breakdown in `pitoco_dev.txt`, after 0.1.9); GPU-built draw lists drawn by the GPU itself (`vkCmdDrawIndexedIndirect`, no CPU read); per draw only the state groups that changed.
4. **GPU-resident images (an option, off by default):** render targets the CPU never reads stay on the host GPU only, without guest-memory copies or page tracking. ASTC still decodes (RTX has no ASTC hardware) and textures still need Vulkan images.
5. **Memory:** guest memory committed on first touch instead of 14 GiB up front.
6. **Games:** Ocarina's gameplay shaders (F2I, warp sum, gathers, sign extend, funnel shift, scratch registers, its own network); DKB's T-pose (10 shaders fall back to global memory: storage buffers not tracked).

## Next steps, in order
0. **Pitoco 2 / build 18 (done, not tested):** test MKW, Ocarina and DKB in Pitoco mode and in legacy, on the same settings; send `pitoco_crash.txt` after any crash.
   - **Next build: GPU-built draw lists as indirect draws.** MKW's culling dispatches fill ~3,700 command lists a second with 5-word draw parameters; the front waits for the back to run their writers ("writers of GPU-built lists", ~235 ms/s in legacy MKW) and reading them ahead (build 11) left 35% a frame old (popping shadows). Issue those draws as vkCmdDrawIndexedIndirect from the list's own GPU address, so neither thread waits and the words are always current.
   - Later: game memory committed as it is used (SEC_RESERVE + commit on first touch) instead of the 14 GiB SEC_COMMIT block; the remaining Ocarina refusals (SHF.R.U64, F2I mode, warp sum, an unknown opcode 0x4a4, scratch-register pressure) and its "SPIR-V Instruction is not implemented" compute pipeline; AV1 for the MinGW/PGO build (dav1d or NVDEC).
1. **Build 16 (done, not tested):** DKB with the new upscale fallback (send the log: the "Upscale candidate" lines show which images it saw); in MKW, read the back thread's "method" time and what is left in "other", and the "built again" count in the eviction lines.
   - **Build 17 (done, not tested):** boot Ocarina of Time (0400486024DEC000, update 2.0.0) and send the log: the next stop, if any, is past the 64-bit wait.
2. **GPU reads the draw lists directly:** when a draw macro's parameters come from a GPU-written GP entry in the indirect-draw layout above, issue `vkCmdDrawIndexedIndirect` from the buffer cache's buffer on the back. No CPU readback, no drain, no stale lists. Make it generic (pattern based), not MKW-only.
3. **Native GPU macros:** recognise NVN's common Turing MME macros by code hash and run native C++ instead of the macro JIT on the front (~1.1M macro calls/s). Generic for every game on the same NVN version.
4. **Spread the back's draw preparation** across spare cores (read handles, image views, buffer updates, stage-buffer binds ≈ 230 ms/s), or use device-generated commands (VK_EXT_device_generated_commands) for groups of draws.
5. **Memory:** commit the 12+2 GiB guest block on touch instead of `SEC_COMMIT` up front (`src/common/host_memory.cpp`), to cut Windows' commit charge.
6. **Study:** find the NVN library in MKW's main executable (RE workspace had `main.img` and scripts) and count the NVN functions called per frame, to size a graphics wrapper.
7. DKB: if the fallback still shows nothing, try `EDEN_NX2_TENSOR=1` (emulates the game's own tensor upscale; its first compile takes minutes).

## Log of the build 14 dynamic-FPS session (2,330 s, docked)
- 93 frame reports: 28–57 ms, never a locked 60; 231 frames over 100 ms (3 single frames over 1 s, near 151 s, 635 s and 1952 s).
- Back thread ~70% busy on average; 130 of 222 reports under 200 ms/s idle.
- **Pipeline builds:** 4,358 runtime pipelines, 18.4 s on the GPU thread in total; up to 321 in 10 s (1.7 s). The disk shader cache is on, so they should drop on later runs of the same tracks.
- **RAM:** host RAM averaged 95% used (max 97%), working set 6–6.6 GB of a 12–13 GB private commit, commit charge 46/49 GB. Windows was paging the game: the case `nx2_keep_in_ram` is for.
- User settings in that session: `mkw_dynamic_fps` on (recommended off), DLSS on, high priority on, frame pacing switched between 60/90/120.

## Log of the build 15 sessions (MKW x3, then DKB)
- **P-switch (1342–1372 s):** draws fell from ~55–63k/s to 2.5–6k/s and the back thread went 65–90% idle: fps follows the game's draw count at ~13 µs per draw. Removing draws the game wants is not an option; making each draw cheaper (steps 2–4) is.
- **Kart slower than the map:** MKW steps its physics once per frame. Frames slower than the mode the game picked slow the kart while timer-driven parts keep pace; `mkw_dynamic_fps` makes it worse by pacing at interval 1 while the game keeps switching. Those settings were tried in build 16 and played worse than the user's own setup: settings are not the fix; the per-draw cost is.
- **Heavy racing, back thread (1403 s):** "other" 267 ms/s unzoned (1.66M method calls/s were only counted, not timed), bind stage buffers 96, update buffers 75, flush work 56, fill image views 45, read handles 40, pipeline lookup 38, method sink 38; 8,704 buffer downloads/s (9 MB/s).
- **Pipelines:** 600 evicted at once after 9,000 frames at 1403 s, then 6–181 every ~25 s.
- **DKB:** gameplay-level work (~25k draws/s) behind a purple screen. The game's tensor (HMMA) upscale program 0x2116149400 was refused and the fallback blitted a 4x4 image into a 360x202 one, so the real output was never written.
- The memory line kept the previous game's figures until the new one drew its first frame (fixed in build 16).

## Ocarina of Time (0400486024DEC000, NUP package, update v131072 = 2.0.0)
- Engine "Owl" (TotK family: agl/gsys, Phive, Bake, Banc), Havok 2025.2 (physics, animation, cloth, AI), CRIWARE audio, NintendoWare G3d2 22.2.8, Vfx3, sead, NPLN 1.55.1 (gRPC, OpenSSL, libcurl). 738 `nvn*` entry names (MKW/DKB: 700).
- Upscaling: its own DLSS (`nvnCommandBufferEvaluateDlss`, `UpscaleDynamic`, dynamic resolution) and NDSR with SMAA 2.3.2; DLSS may hit the same tensor-program refusal as DKB.
- The main NSO is ZBIC-compressed (zstd variant, kinnay/zbic); sdk and subsdk0 are LZ4.
- Build 15 log: boots, presents two frames at 29.4–29.6 s, then aborts (2001-0120, svcBreak). Backtrace: `std::__libcpp_atomic_wait` → `nn::os::WaitForAddress` (sdk+0x16cd80: svc 0x34 with arbitration type 3, the full 64-bit value in x2, infinite timeout; anything but success or InvalidState aborts) → `nn::diag::detail::Abort`. The emulator answered InvalidEnumValue. Fixed in build 17. `nn::os::SignalToAddress` uses plain Signal (type 0).
- `tools/svc_generator.py` still describes WaitForAddress with an int32 value; svc.cpp/svc.h were edited by hand (64-bit wrapper only), so regenerating would undo it.

## DKB main executable (base/exefs/main, build id a1dd7480bca7b6e8...)
- 25.5 MiB code, 5.9 MiB rodata, 2.8 MiB data (MKW: 40.2 / 8.5 / 2.9). Internal name SilverStar, CSPGO build, platform "Ounce".
- Same NVN API as MKW: the same 700 `nvn*` entry names, with ray tracing, acceleration structures, DLSS networks, shading rate and sparse textures.
- Different SDK point release: NintendoWare 19.3.2 (MKW 19.3.3), no byte-identical 4 KiB code pages with MKW. Native macros (step 3) must therefore match the macros uploaded at run time by hash, not offsets in the executable.
- Middleware: Havok 2022.1 (Physics, Animation, Cloth), sead, NintendoWare Vfx2/G3d2/Ui2d, SMAA_For_NDSR. MKW instead has Pia, NPLN and NintendoWare_Ndsr.
- Steps 2–4 (GPU indirect draws, native macros, spread draw preparation) work at the NVN/GPU level, so they should help DKB too once it boots. The purple screen still needs a DKB log.

## Working rules (from the user)
- No recommended settings. Each build is tested on the user's own setup unchanged, so a log compares builds, not settings.
- Fix the game's frame rate in the emulator's code (the per-draw cost), not by capping or forcing frame modes. The 30 FPS mode slowed the game's clock and was removed in build 17.
