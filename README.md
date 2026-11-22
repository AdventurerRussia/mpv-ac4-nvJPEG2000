# mpv AC-4 & nvJPEG2000 (Windows x64)

[![Release](https://img.shields.io/github/v/release/AdventurerRussia/mpv-ac4?include_prereleases&color=blue&style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)
[![mpv version](https://img.shields.io/badge/mpv-v0.42.0--dev%20(bb4823e)-orange?style=flat-square)](https://github.com/mpv-player/mpv)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-9.0.2%20("Lei")-red?style=flat-square)](https://github.com/FFmpeg/FFmpeg/tree/release/9.0)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-informational?style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)
[![Audio](https://img.shields.io/badge/audio-Dolby%20AC--4%20(ETSI%20TS%20103%20190)-success?style=flat-square)](https://www.etsi.org/deliver/etsi_ts/103100_103199/10319001/01.03.01_60/ts_10319001v010301p.pdf)
[![Video](https://img.shields.io/badge/video-JPEG%202000%20(NVIDIA%20nvJPEG2000)-76B900?style=flat-square&logo=nvidia&logoColor=white)](https://developer.nvidia.com/nvjpeg2000)
[![License](https://img.shields.io/badge/license-GPLv3-green?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.html)

A custom Windows x64 build of **[mpv](https://github.com/mpv-player/mpv)** media player (version **v0.42.0-dev**, git commit [`bb4823e`](https://github.com/mpv-player/mpv/commit/bb4823e5f0eb92a830242e60dfdcc50d1f2e0410)) featuring native decoding of **Dolby AC-4** audio streams ([ETSI TS 103 190-1](https://www.etsi.org/deliver/etsi_ts/103100_103199/10319001/01.03.01_60/ts_10319001v010301p.pdf)) and GPU hardware-accelerated **JPEG 2000** video decoding via NVIDIA **nvJPEG2000** (DCP cinema `AV_PIX_FMT_XYZ12LE` and general RGB/Greyscale).

---
> **Experimental project**
>
> This is an experimental, AI-assisted project created primarily for learning and exploration. Both the implementation and the test cases were developed with the help of AI, based on NVIDIA's official documentation. The tests have not been independently validated or extensively reviewed, so correct behavior cannot be guaranteed in all scenarios.
>
> The project is provided as-is, without guarantees of reliability, completeness, or compatibility. It is not intended to be a production-ready alternative to the original mpv or FFmpeg.

---

## ⚡ About The Project

In upstream [FFmpeg](https://github.com/FFmpeg/FFmpeg) and standard mpv distributions (including popular builds by [shinchiro](https://github.com/shinchiro/mpv-winbuild-cmake) and [zhongfly](https://github.com/zhongfly/mpv-winbuild)), an AC-4 audio decoder is not present — upstream code only includes an elementary stream demuxer. When attempting to play a video with an AC-4 audio stream, players fail to decode any sound.

In this project, the native AC-4 decoder developed by **[Paul B Mahol (richardpl)](https://github.com/richardpl)** as part of the **[librempeg](https://github.com/librempeg/librempeg)** fork has been ported and adapted for the latest stable **[FFmpeg 9.0.2 ("Lei")](https://github.com/FFmpeg/FFmpeg/tree/release/9.0)** (a patch for **[FFmpeg 7.1](https://github.com/FFmpeg/FFmpeg/tree/release/7.1)** is also provided), preserving the `libswresample` resampling library required by mpv. The mpv player is built against this custom FFmpeg 9.0.2 (`libavcodec 63`) and decodes AC-4 audio directly out of the box without requiring external filters.

### 📌 Core Component Versions:
* **mpv**: `v0.42.0-dev` (commit [`bb4823e`](https://github.com/mpv-player/mpv/commit/bb4823e5f0eb92a830242e60dfdcc50d1f2e0410))
* **FFmpeg**: `n9.0.2 ("Lei")` (`libavcodec 63.1.102`, `libavformat 63.1.102`, `libavutil 61.1.102`, `libswresample 7.1.102`)
* **libplacebo**: `v7.387.0`
* **libass**: `0.17.4`

---

## 📥 Download Prebuilt Release (Portable)

Ready-to-use portable packages for Windows x64 are available in the **Releases** tab:
* **[Download MPV-AC4-Windows-x64.zip](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)**

### Folder Structure ([shinchiro](https://github.com/shinchiro/mpv-winbuild-cmake) / [rossy](https://github.com/rossy/mpv-install) standard):
```text
MPV PLAYER AC-4/
├── fonts/               # User fonts for OSD and subtitles
├── installer/           # Windows file association scripts (rossy/mpv-install)
│   ├── mpv-install.bat  # Register player in Windows and associate extensions (Right click -> Run as administrator)
│   └── mpv-uninstall.bat# Unregister player and remove registry keys
├── mpv/                 # Configuration directory:
│   ├── fonts.conf       # Fontconfig configuration
│   ├── input.conf       # Keybindings
│   └── mpv.conf         # Video, audio, and GPU hardware acceleration options
├── script-opts/         # Script configuration files
├── scripts/             # Lua and JavaScript plugins
├── mpv-document.ico     # File icon for associated media
├── mpv-icon.ico         # Player executable icon
├── mpv.com              # Console wrapper for log output
└── mpv.exe              # Main GUI executable
```
> All 39 runtime dependency DLLs (FFmpeg, libplacebo, libass, etc.) are located directly next to `mpv.exe` and marked as hidden (`attrib +h`) to maintain a clean, uncluttered directory view matching shinchiro's release style.

---

## 🎯 Supported Features

| Format / Feature | Status | Details |
|---|:---:|---|
| **AC-4 Stereo (2.0)** | ✅ Verified | 48000 Hz, clean audio with perfect A/V sync (`believer.mp4`, `Lady Gaga Bloody Mary`) |
| **AC-4 Multichannel (5.1)** | ✅ Verified | 6 channels, tested on real ATSC 3.0 broadcast captures (`KDAF.ts`) |
| **JPEG 2000 GPU (nvJPEG2000)** | ✅ Verified | Optional GPU decoder (`jpeg2000_nvjpeg`, mpv `--vd=jpeg2000_nvjpeg`). The default JPEG 2000 decoder stays the software one. Supports grey, RGB/GBR, YUV 4:4:4, 4:2:2, 4:2:0 and 4:4:0, YUVA 4:4:4/4:2:2/4:2:0, and DCI XYZ 12-bit (`AV_PIX_FMT_XYZ12LE`) when the codestream profile is Digital Cinema. If the DLL, GPU, or this subset is missing, init returns `AVERROR_DECODER_NOT_FOUND` (so mpv can try the next decoder) or the frame is decoded with software `jpeg2000`. |
| **MP4 / MOV Containers** | ✅ Supported | Standard MP4/ISOBMFF containers containing AC-4 tracks |
| **MPEG-TS Containers** | ✅ Supported | Digital television broadcast captures (ATSC 3.0, DVB-T2) |
| **Multi-Language Audio Tracks** | ✅ Supported | Switching between presentations/substreams with `#` (`--aid=1`, `--aid=2`) |
| **46034 Hz Rate (NTSC pull-down)** | ✅ Supported | Spec-compliant clock for 59.94 fps ATSC 3.0 broadcast video (`ratio 25025:24000`) |
| **AC-4 Atmos (Part 2 / A-JOC)** | ❌ Not Supported | Dynamic 3D audio objects (stubbed in decoder with `AVERROR_PATCHWELCOME`). An open-source C implementation of AC-4 Atmos does not yet exist in FFmpeg (experimental research is ongoing in other languages, e.g., the Rust project [MacinDecode-Spatial-Player](https://github.com/SakuzyPeng/MacinDecode-Spatial-Player)). |

---

## 🛠 Technical Details of the Port ([FFmpeg 9.0.2](https://github.com/FFmpeg/FFmpeg/tree/release/9.0) / [7.1](https://github.com/FFmpeg/FFmpeg/tree/release/7.1))

1. **`libavcodec/kbdwin.h`**:
   The maximum Kaiser-Bessel window size (`FF_KBD_WINDOW_MAX`) has been increased from `1024` to `2048` points. AC-4 applies a 2048-point Kaiser-Bessel window, and without this change decoding aborted on `av_assert0(n <= FF_KBD_WINDOW_MAX)`.
2. **`libavcodec/ac4dec.c`**:
   Implemented a dedicated static `ac4_vector_fmul_reverse_c` function to eliminate dependencies on private unexported DSP symbols. For FFmpeg 9.0, registered through the internal codec structure `const FFCodec ff_ac4_decoder`.
3. **`libavcodec/ac4_parser.c`**:
   Adapted to the modern internal parser API of FFmpeg 9.0 (`parser_internal.h`, `const FFCodecParser ff_ac4_parser`, and `PARSER_CODEC_LIST(AV_CODEC_ID_AC4)`). For FFmpeg 7.1, an `AVCodecParser` public structure is used.
4. **Build System Registration**:
   `ff_ac4_decoder` and `ff_ac4_parser` are registered in `allcodecs.c`, `parsers.c`, and `libavcodec/Makefile`.
5. **`libavcodec/nvjpeg2kdec.c` & `nvjpeg2k_loader.h`**:
   Optional GPU JPEG 2000 decoder (`-c:v jpeg2000_nvjpeg`, mpv `--vd=jpeg2000_nvjpeg`). It is registered after the software decoder, so FFmpeg and mpv keep using software JPEG 2000 unless the GPU decoder is requested. `nvjpeg2k_0.dll` is loaded by full path from the application directory or from the directory of the FFmpeg module (`LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`); `nvcuda.dll` is loaded from System32. No CUDA toolkit is required at build time. The library version is checked with `nvjpeg2kGetProperty` (0.8 or newer). Device memory uses `cuMemAllocPitch`, host staging uses pinned `cuMemAllocHost`, and the decoded planes are copied with `cuMemcpyDtoHAsync` on the default stream followed by one `cuCtxSynchronize`. The previous CUDA context is saved with `cuCtxGetCurrent` and restored with `cuCtxSetCurrent` (`cuCtxPushCurrent` returns `CUDA_ERROR_INVALID_CONTEXT` on a retained primary context with this driver). 8-bit streams are decoded as `NVJPEG2K_UINT8`. Output samples are scaled to the destination depth; 10- and 12-bit planar formats stay LSB-aligned, while `XYZ12LE` is stored left-shifted by 4, which is what FFmpeg expects. Colour is taken from the codestream: Digital Cinema `Rsiz` selects XYZ, `nvjpeg2kStreamGetColorSpace` selects sRGB or sYCC, an MCT marks RGB, and container colour metadata is used only after that. Unknown subsampling, a signed component, a bit depth outside 1..16, or a non-zero image/tile offset is not decoded on the GPU; that frame is passed to the software `jpeg2000` decoder. The decoder is single-threaded. Option `-gpu` (default 0) selects the CUDA device. `nvjpeg2k_0.dll` is NVIDIA software and is not covered by the LGPL/GPL of FFmpeg and mpv; redistribute it only under NVIDIA's terms.

---

## 🔨 Building From Source (Windows UCRT64 / [MSYS2](https://www.msys2.org/))

### 1. Prepare Environment
Install [MSYS2](https://www.msys2.org/) and the UCRT64 toolchain:
```bash
pacman -Syu --noconfirm
pacman -S --needed --noconfirm git base-devel mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-pkgconf \
    mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-libass \
    mingw-w64-ucrt-x86_64-libplacebo
```

### 2. Clone FFmpeg and Apply Patches (9.0.2)
```bash
git clone --depth 1 -b n9.0.2 https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg

# 1. Dolby AC-4 audio decoder & parser
git apply ../patches/ffmpeg-9.0.2-ac4.patch

# 2. NVIDIA nvJPEG2000 GPU video decoder (optional, can be used together or standalone)
git apply ../patches/ffmpeg-9.0.2-nvjpeg2k.patch

./configure \
    --prefix=/install \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-debug \
    --enable-gpl \
    --enable-version3

make -j$(nproc)
make install
cd ..
```

### 3. Build mpv
```bash
git clone --depth 1 https://github.com/mpv-player/mpv.git mpv
export PKG_CONFIG_PATH="/install/lib/pkgconfig:$PKG_CONFIG_PATH"

meson setup mpv-build mpv \
    --prefix=/install_prefix \
    --pkg-config-path=/install/lib/pkgconfig \
    -Dtests=false \
    -Dmanpage-build=disabled

ninja -C mpv-build
ninja -C mpv-build install
```

---

## 📜 Authors & Licenses

- **[mpv](https://github.com/mpv-player/mpv)**: GPLv2+ / LGPLv2.1+
- **[FFmpeg](https://github.com/FFmpeg/FFmpeg)**: GPLv3 (includes AC-4 decoder code)
- Original AC-4 decoder implementation: **[Paul B Mahol (richardpl)](https://github.com/richardpl)** ([librempeg project](https://github.com/librempeg/librempeg))
- Windows installer scripts: **[rossy/mpv-install](https://github.com/rossy/mpv-install)**
- Reference build system: **[shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake)**
