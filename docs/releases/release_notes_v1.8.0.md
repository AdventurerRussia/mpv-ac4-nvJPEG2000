# Release Notes — MPV with AC-4, NVIDIA nvJPEG2000 & VVdeC (v1.8.0)

## Overview
Release **v1.8.0** introduces native NVIDIA GPU Power Management Mode control directly integrated into the `jpeg2000_nvjpeg` decoder via NVAPI DRS (Driver Reloaded Settings), replaces external Python and Lua helpers with zero-overhead C implementation, adds clear terminal status logging, and updates component documentation and badges.

---

## What's New & Improvements

### 1. Native NVAPI Power Management Control in `nvjpeg2kdec`
- **Dynamic NVAPI DRS Runtime Loading**:
  - Dynamically loads `nvapi64.dll` at runtime without external SDK dependencies.
  - Automatically identifies the host application executable path (whether `mpv.exe`, `mpv.com`, or `ffmpeg.exe`) and manages its application profile.
- **Automatic High-Performance P-State Triggering**:
  - Automatically elevates the GPU Power Management Mode to **"Prefer Maximum Performance"** (`Mode 1`) upon decoder initialization (`decode_init`). This prevents frame-time jitter and eliminates micro-stutter by keeping CUDA cores and memory at full clocks during playback.
- **Guaranteed Symmetrical Restoration**:
  - Upon decoder shutdown (`decode_close`), the decoder cleanly restores the user's previous GPU power state (typically `Mode 0` Adaptive), ensuring the GPU enters low-power idle states immediately after playback ends.
- **AVOption Interface**:
  - Added `-nvapi_pstate <int>` codec option:
    - `1`: Prefer Maximum Performance (default).
    - `0`: Adaptive / Normal.
    - `-1`: Disabled (bypasses NVAPI control).

### 2. Enhanced Terminal & Console Status Logging
- Added clear, readable status tags in terminal and console logs:
  - `[OK]`: Logged when power mode is successfully applied or restored.
  - `[WARN]`: Logged when NVAPI is unavailable, disabled, or profile warning occurs.
  - `[ERR]`: Logged on driver API failure with explicit status codes.

### 3. Component Documentation & Release Badges
- Updated `README.md` with official release badge for **NVIDIA nvJPEG2000 v0.12.0** (`nvjpeg2k_0.dll` v0.12.0.58).
- Updated project build status badge to **v1.8.0**.

---

## Checksums & Package Details
- **Distribution Package**: `MPV-AC4-nvJPEG2000-Windows-1.8.0-x64.zip`
- **Inner Directory**: `MPV-AC4-nvJPEG2000-Windows-x64/`
- **Integrated Components**:
  - `mpv`: `v0.42.0-dev` (`bb4823e`)
  - `FFmpeg`: `n9.0.2` (`Lei`) with Dolby AC-4 & nvJPEG2000
  - `nvJPEG2000`: `v0.12.0` (`0.12.0.58`)
  - `VVdeC`: `v3.4.0-dev`
  - `libplacebo`: `v7.387.0`
  - `yt-dlp`: `2026.11.23`
