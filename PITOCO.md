# Pitoco

Pitoco is a Switch 2 emulator derived from Eglantine (and through it Eden and yuzu). It keeps
everything Eglantine does: the general options and APIs, the system applets and Switch
integration, keys, firmware and updates. What changes is the inside: Switch 2 games are run so
that the PC's own graphics card, CPU and RAM do the work, almost as if the game ran on the PC
natively, with the emulator doing as little as possible besides loading and checking the game's
files.

## Modes

- **Pitoco mode (experimental, on by default):** the paths tuned for the reference PC: the GPU
  work split over two threads, frames prepared ahead, the rig profile, high priority, keeping the
  game in RAM and the other options on the Switch 2 Rig page. Switch it off under
  Configure > Switch 2 Rig.
- **Legacy:** with Pitoco mode off, Switch 2 games run the way Eden and Eglantine run them, and the
  Switch 2 Rig options have no effect.

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

Lime green Web 2.0 theme ("Pitoco", the default; the other themes stay in Configure > UI) and the
Pitoco icon (`dist/pitoco.ico`, `dist/Pitoco.png`).
