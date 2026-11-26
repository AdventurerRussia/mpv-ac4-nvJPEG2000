# Rules and Guidelines

## Release Notes & Changelogs
- All release notes, changelogs, GitHub release descriptions, and lists of changes MUST ALWAYS be written in English.
- While direct conversation with the user may be in Russian (or the user's language), all release documentation, `release_notes_*.md` files, and GitHub releases must strictly use English.

## Release Archive Packaging Rules
- The release zip archive attached to GitHub releases MUST follow the dynamic version naming convention:
  `MPV-AC4-nvJPEG2000-Windows-<version>-x64.zip` (e.g., `MPV-AC4-nvJPEG2000-Windows-1.4.0-x64.zip`, where `<version>` dynamically reflects the released version tag).
- Inside the ZIP archive, the top-level directory MUST NOT contain the version number; it must ALWAYS be statically named:
  `MPV-AC4-nvJPEG2000-Windows-x64/`
- All player distribution files must be placed inside this `MPV-AC4-nvJPEG2000-Windows-x64/` folder.
- The player distribution must be clean: strictly no user scripts (`.lua`) or temporary files.
- Placeholders for user storage MUST be included as empty directories: `fonts/`, `mpv/`, `scripts/`, `script-opts/`, `shaders/`.
- Inside `mpv/`, empty placeholder files MUST be created: `fonts.conf`, `mpv.conf`, `input.conf`.
- The folder `portable_config/` MUST NOT be created or included; `mpv/` is used instead.
- Temporary build or decoder directories like `DECODIR` must never be included.

## Project Structure & Directory Layout
- `src/` — Source code for decoders and loaders (`nvjpeg2kdec.c`, `nvjpeg2k_loader.h`, `ac4dec.c`, `ac4_parser.c`, `libvvdec.c`).
- `tests/` — Automated unit tests, mock suites, and decoder audit tests (`test_t01_comprehensive.c`, `test_t02_jp2box.c`, `test_t08_pitch_dtoh.c`, `test_t13_pitch.c`, etc.).
- `samples/` — Test media files:
  - `samples/jpeg2000/` — J2K/JP2 box test files (`sample_colr_*.jp2`, `sample_nlt*.jp2`, `seq_*.j2k`) and video streams (`test_*.mkv`).
  - `samples/ac4/` — Dolby AC-4 / Atmos audio streams and test videos (`.mp4`, `.ts`, `.wav`).
- `agent/` — Automation scripts, build helpers, and packaging scripts (`make_release_zip.ps1`, `msys.ps1`, `package_portable.py`).
- `MPV PLAYER AC-4/` — Ready-to-run portable distribution of mpv with nvJPEG2000, AC-4, and VVdeC.
- `mpv-ac4-repo/` — Local clone of the GitHub repository (`origin main`).
- `patches/` — Standalone patch files for FFmpeg and mpv.
- `releases_archive/` — Historical release archives and zip packages.
- `docs/releases/` — Historical release notes and reports.
- `install/` — Built FFmpeg prefix binaries and libraries.
- `mpv-build/` — mpv build artifacts and ninja targets.
