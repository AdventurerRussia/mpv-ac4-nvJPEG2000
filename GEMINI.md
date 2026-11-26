# mpv AC-4 & nvJPEG2000 Project Guide

## 1. Project Overview & Architecture
This project is an advanced Windows x64 multimedia playback stack based on **mpv** and **FFmpeg 9.0.2 ("Lei")** with custom high-performance decoders:
1. **`jpeg2000_nvjpeg`**: GPU-accelerated JPEG 2000 video/image decoder utilizing NVIDIA's `nvJPEG2000` (dynamic runtime loading via `nvcuda.dll` and `nvjpeg2k_0.dll`). Supports 8/10/12/16-bit, YUV/RGB/XYZ, sub-sampling, and seamless software fallback.
2. **`ac4`**: Dolby AC-4 audio decoder with Dolby Atmos / immersive audio bitstream parsing.
3. **`libvvdec`**: Fraunhofer HHI VVdeC software decoder for next-gen H.266 / VVC video, patched into mpv with automatic fallback priority over native VVC.

---

## 2. Environment & Virtual Drive Setup
- **Subst Drive `R:\`**: All build scripts, include paths, and tests assume the project root is mounted as drive `R:\`.
  - Mount command: `subst R: "C:\Users\overe\Desktop\mpv ac-4"` (or run `mount-subst.bat`).
  - If a shell restarts and `R:\` is missing, re-run `subst R: "C:\Users\overe\Desktop\mpv ac-4"`.
- **Toolchain (MSYS2 UCRT64)**:
  - Toolchain root: `R:\msys64` (GCC 16, Ninja, CMake, Python).
  - To execute build commands in the MSYS2 UCRT64 environment from PowerShell/cmd, use:
    `powershell -ExecutionPolicy Bypass -File R:\agent\msys.ps1 "<command>"`
    *(Example: `powershell -ExecutionPolicy Bypass -File R:\agent\msys.ps1 "gcc --version"`)*.

---

## 3. Core Source Locations & Workflow
- **Active decoder source**: `R:\src\ffmpeg-upstream\libavcodec\nvjpeg2kdec.c` and `R:\src\ffmpeg-upstream\libavcodec\nvjpeg2k_loader.h`.
- **Repository mirror**: `R:\mpv-ac4-repo\` (push changes here to publish to GitHub).
- **Fast Build Cycle**:
  1. Edit `R:\src\ffmpeg-upstream\libavcodec\nvjpeg2kdec.c`.
  2. Run `powershell -ExecutionPolicy Bypass -File R:\agent\fast_rebuild_nvjpeg.ps1` (~15s incremental compile & deploy).
  3. Run tests with `powershell -ExecutionPolicy Bypass -File R:\agent\run_all_tests.ps1`.
  4. Verify sample media with `powershell -ExecutionPolicy Bypass -File R:\agent\test_samples_nvjpeg.ps1`.
  5. Sync patch and commit: `powershell -ExecutionPolicy Bypass -File R:\agent\update_nvjpeg_patch.ps1`.

---

## 4. Development & Audit Rules (T01–T13)
When working on decoder tasks and audit items:
1. **Red-Green Testing**:
   - For confirmed defects, unit tests must fail on unpatched code and pass after the fix.
   - Tests belong in `R:\tests\test_t*.c` and must be executable without physical GPU access (using mock tables/loaders where applicable).
2. **Resource Safety & Atomicity**:
   - Never free an existing valid buffer/resource before a replacement has been successfully allocated.
   - On allocation failure (OOM or deferred CUDA async error), old buffers and metadata must remain intact.
3. **No Phantom Functions**:
   - Do not invent CUDA Driver or nvJPEG2000 functions/flags. Check exact signatures in `nvjpeg2k_loader.h` and official CUDA Toolkit documentation.
4. **Hardware Claims**:
   - Never invent or guess FPS, latency, PCIe bandwidth, or memory numbers without physical hardware benchmarks.

---

## 5. Release Notes & Changelogs
- All release notes, changelogs, GitHub release descriptions, and lists of changes MUST ALWAYS be written in English.
- While direct conversation with the user may be in Russian (or the user's language), all release documentation, `release_notes_*.md` files, and GitHub releases must strictly use English.

---

## 6. Release Archive Packaging Rules
- The release zip archive attached to GitHub releases MUST follow the dynamic version naming convention:
  `MPV-AC4-nvJPEG2000-Windows-<version>-x64.zip` (e.g., `MPV-AC4-nvJPEG2000-Windows-1.7.0-x64.zip`).
- Inside the ZIP archive, the top-level directory MUST NOT contain the version number; it must ALWAYS be statically named:
  `MPV-AC4-nvJPEG2000-Windows-x64/`
- All player distribution files must be placed inside this `MPV-AC4-nvJPEG2000-Windows-x64/` folder.
- The player distribution must be clean: strictly no user scripts (`.lua`) or temporary files.
- Placeholders for user storage MUST be included as empty directories: `fonts/`, `mpv/`, `scripts/`, `script-opts/`, `shaders/`.
- Inside `mpv/`, empty placeholder files MUST be created: `fonts.conf`, `mpv.conf`, `input.conf`.
- The folder `portable_config/` MUST NOT be created or included; `mpv/` is used instead.
- `yt-dlp.exe` must always be checked and updated to the latest available upstream version (`yt-dlp -U`) before packaging.
- Temporary build or decoder directories like `DECODIR` must never be included.
- Package via: `powershell -ExecutionPolicy Bypass -File R:\agent\make_release_zip.ps1 -Version <version>`.

---

## 7. Project Structure & Directory Layout
- `src/` — Source code for decoders and loaders (`nvjpeg2kdec.c`, `nvjpeg2k_loader.h`, `ac4dec.c`, `ac4_parser.c`, `libvvdec.c`).
- `tests/` — Automated unit tests, mock suites, and decoder audit tests (`test_t01_comprehensive.c`, `test_t02_jp2box.c`, `test_t08_pitch_dtoh.c`, `test_t13_pitch.c`, etc.).
- `samples/` — Test media files:
  - `samples/jpeg2000/` — J2K/JP2 box test files (`sample_colr_*.jp2`, `sample_nlt*.jp2`, `seq_*.j2k`) and video streams (`test_*.mkv`).
  - `samples/ac4/` — Dolby AC-4 / Atmos audio streams and test videos (`.mp4`, `.ts`, `.wav`).
- `agent/` — Automation scripts, developer tools, and packaging scripts:
  - `agent/run_all_tests.ps1` — Runs and verifies the complete test suite across `tests/` (T01–T13) with automated compilation.
  - `agent/fast_rebuild_nvjpeg.ps1` — Incremental rebuild of `nvjpeg2kdec.o` and `avcodec-63.dll` with instant deploy to player (~15s).
  - `agent/test_samples_nvjpeg.ps1` — Automated batch verification of all media samples in `samples/jpeg2000/`.
  - `agent/update_nvjpeg_patch.ps1` — Synchronizes `src/` and updates the standalone patch file.
  - `agent/make_release_zip.ps1` — Builds clean release archive according to release rules (auto-updates `yt-dlp`).
  - `agent/msys.ps1` — Runner for commands inside the MSYS2 UCRT64 toolchain environment.
- `MPV PLAYER AC-4/` — Ready-to-run portable distribution of mpv with nvJPEG2000, AC-4, and VVdeC.
- `mpv-ac4-repo/` — Local clone of the GitHub repository (`origin main`).
- `patches/` — Standalone patch files for FFmpeg and mpv.
- `releases_archive/` — Historical release archives and zip packages.
- `docs/releases/` — Historical release notes and reports.
- `install/` — Built FFmpeg prefix binaries and libraries.
- `mpv-build/` — mpv build artifacts and ninja targets.
