# Pitoco

Pitoco is a Switch 2 emulator derived from Eglantine (and through it Eden and yuzu). It keeps
everything Eglantine does: the general options and APIs, the system applets and Switch
integration, keys, firmware and updates. What changes is the inside: Switch 2 games are run so
that the PC's own graphics card, CPU and RAM do the work, almost as if the game ran on the PC
natively, with the emulator doing as little as possible besides loading and checking the game's
files.

## Modes

- **Parallel GPU execution (the default, and where all development goes):** Configure > Switch 2
  Rig, decided once when a game starts and written to the log ("Parallel GPU execution: on").
  The game's GPU commands are processed on two threads in parallel: the first reads the command
  lists and runs the GPU macros, the second applies the state and records the draws for Vulkan.
  The emulator runs at high priority, and the game's memory is kept in RAM while the PC has RAM
  free and given back when it runs short. It never changes your settings. (Called "Pitoco mode"
  up to 0.1.9; its setting was renamed, so it starts on for everyone.)
- **Compatibility (off):** Switch 2 games run the way Eden runs them, on one GPU thread. Kept for
  comparison; in Mario Kart World on 0.1.9 it stalled more than twice as often (14 frames over
  100 ms per 600 frames against 6).
- Logs: `eden_log.txt` is the user log (general lines, warnings, errors); `pitoco_dev.txt` next
  to it has everything, including the profiler, memory and GPU details, for development.
- After a crash, `pitoco_crash.txt` (and `pitoco_crash.dmp`) in the log folder give the place;
  `pitoco.map` next to `pitoco.exe` names the function.

## Reference PC

The proof of concept is one PC: RTX 3060 12 GB, Intel i5-10400F (6 cores, 12 threads), 16 GB RAM.
Tuning for RTX graphics cards and Intel CPUs in general comes later.

## Working rules

- No recommended settings. Each build is tested on the user's own setup unchanged, so a log
  compares builds, not settings.
- The frame rate is fixed in the emulator's code, not by capping or forcing frame modes.
- Measure first: a change to the per-frame cost comes with the profiler numbers that justify it.

## Status

The measured bottleneck is the GPU back thread, at about 13 µs per draw. The plan for cutting
it, the build history and the log analyses are in [NX2_FORK_STATUS.md](NX2_FORK_STATUS.md).

## Look

Lime green Web 2.0 theme ("Pitoco", the default), with "Pitoco Sky" (sky blue and white) and
"Pitoco Silver" (white and silver grey, light blue accents) in the same style; Eden's themes stay
in Configure > UI. The Pitoco icon is `dist/pitoco.ico` and `dist/Pitoco.png`.
