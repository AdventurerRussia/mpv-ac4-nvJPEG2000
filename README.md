# mpv — AC-4, NVIDIA nvJPEG2000 & Fraunhofer VVdeC (Windows x64)

[![Build](https://img.shields.io/badge/build-1.7.0-blue?style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4-nvJPEG2000-VVdeC/releases)
[![mpv](https://img.shields.io/badge/mpv-0.42.0--dev-orange?style=flat-square)](https://github.com/mpv-player/mpv)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-9.0.2-red?style=flat-square)](https://github.com/FFmpeg/FFmpeg/tree/release/9.0)
[![VVdeC](https://img.shields.io/badge/VVdeC-3.4.0--dev-blue?style=flat-square)](https://github.com/fraunhoferhhi/vvdec)
[![libplacebo](https://img.shields.io/badge/libplacebo-7.387.0-purple?style=flat-square)](https://github.com/haasn/libplacebo)
[![libass](https://img.shields.io/badge/libass-0.17.4-teal?style=flat-square)](https://github.com/libass/libass)
[![Dolby AC-4](https://img.shields.io/badge/audio-Dolby%20AC--4-success?style=flat-square)](https://www.etsi.org/deliver/etsi_ts/103100_103199/10319001/01.03.01_60/ts_10319001v010301p.pdf)
[![NVIDIA nvJPEG2000](https://img.shields.io/badge/video-NVIDIA%20nvJPEG2000-76B900?style=flat-square&logo=nvidia&logoColor=white)](https://developer.nvidia.com/nvjpeg2000)
[![H.266 / VVC](https://img.shields.io/badge/video-H.266%20%2F%20VVC-2962FF?style=flat-square)](https://github.com/fraunhoferhhi/vvdec)
[![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-informational?style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4-nvJPEG2000-VVdeC/releases)
[![GPLv3](https://img.shields.io/badge/license-GPLv3-green?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.html)

A custom **Windows x64 build of [mpv](https://github.com/mpv-player/mpv)** that extends the FFmpeg-based playback pipeline with three additional decoding capabilities:

- **Dolby AC-4 audio** — native decoding integrated into a custom FFmpeg build, including stereo and multichannel presentations.
- **JPEG 2000 video on NVIDIA GPUs** — an optional CUDA-based decoder powered by **NVIDIA nvJPEG2000**, including support for Digital Cinema XYZ 12-bit (`AV_PIX_FMT_XYZ12LE`) and supported RGB, YUV and grayscale formats. The standard FFmpeg software decoder remains available as a fallback.
- **H.266 / VVC video** — software decoding through **Fraunhofer HHI VVdeC** (`libvvdec`). A small mpv codec-priority patch makes VVdeC the preferred software decoder when hardware VVC decoding is unavailable; FFmpeg's native `vvc` decoder remains selectable.

The build combines **mpv v0.42.0-dev** (commit [`bb4823e`](https://github.com/mpv-player/mpv/commit/bb4823e5f0eb92a830242e60dfdcc50d1f2e0410)) with a patched **FFmpeg 9.0.2** and the component versions listed above. It is distributed as a portable package, with the required runtime libraries included.

---
> **Experimental project**
>
> This is an experimental, AI-assisted project created primarily for learning and exploration. Both the implementation and the test cases were developed with the help of AI, based on NVIDIA's official documentation. The tests have not been independently validated or extensively reviewed, so correct behavior cannot be guaranteed in all scenarios.
>
> The project is provided as-is, without guarantees of reliability, completeness, or compatibility. It is not intended to be a production-ready alternative to the original mpv or FFmpeg.

---

## ⚡ About The Project

This project focuses on extending mpv's playback support for formats and decoding paths that are not available in a typical Windows mpv/FFmpeg build.

### Dolby AC-4 in FFmpeg

The upstream FFmpeg codebase provides AC-4 stream demuxing, but does not include the AC-4 audio decoder used by this build. The decoder, originally developed by **[Paul B Mahol (richardpl)](https://github.com/richardpl)** in the **[librempeg](https://github.com/librempeg/librempeg)** project, has been ported and adapted to **FFmpeg 9.0.2**. The integration preserves the `libswresample` functionality required by mpv and allows AC-4 tracks to be decoded directly, without external audio filters.

The repository also includes a patch for **FFmpeg 7.1**. The current Windows package is built with FFmpeg 9.0.2.

### NVIDIA nvJPEG2000

The optional `jpeg2000_nvjpeg` decoder uses NVIDIA's **nvJPEG2000** library and CUDA driver API to decode supported JPEG 2000 streams on the GPU. It can be selected in mpv with `--vd=jpeg2000_nvjpeg`. The regular FFmpeg `jpeg2000` software decoder remains available and is used when the GPU decoder is not selected or cannot handle a particular stream.

### Fraunhofer VVdeC for H.266 / VVC

**[VVdeC](https://github.com/fraunhoferhhi/vvdec)** is integrated into FFmpeg as `libvvdec`. The accompanying mpv patch changes software decoder priority so that `libvvdec` is preferred over FFmpeg's native `vvc` decoder when no suitable hardware decoder is available. The native decoder remains available and can be forced with `--vd=vvc`.

### Component Versions

| Component | Version | Role |
|---|---|---|
| [mpv](https://github.com/mpv-player/mpv) | `v0.42.0-dev` (`bb4823e`) | Media player |
| [FFmpeg](https://github.com/FFmpeg/FFmpeg/tree/release/9.0) | `n9.0.2` | Patched decoding and media framework |
| [VVdeC](https://github.com/fraunhoferhhi/vvdec) | `3.4.0-dev` | H.266 / VVC software decoder |
| [libplacebo](https://github.com/haasn/libplacebo) | `v7.387.0` | GPU rendering and video processing |
| [libass](https://github.com/libass/libass) | `0.17.4` | Subtitle rendering |
| NVIDIA nvJPEG2000 | `0.8+` API requirement | JPEG 2000 GPU decoder |

FFmpeg library versions in this build: `libavcodec 63.1.102`, `libavformat 63.1.102`, `libavutil 61.1.102`, and `libswresample 7.1.102`.

---

## 📥 Download Prebuilt Release (Portable)

Ready-to-use portable packages for Windows x64 are available in the **Releases** tab:
* **[Download MPV-AC4-nvJPEG2000-Windows-x64.zip](https://github.com/AdventurerRussia/mpv-ac4-nvJPEG2000-VVdeC/releases/latest)**

### Package Layout

```text
MPV-AC4-nvJPEG2000-Windows-x64/
├── installer/           # Windows file-association scripts
│   ├── mpv-install.bat
│   └── mpv-uninstall.bat
├── portable_config/     # mpv portable configuration directory
├── script-opts/         # Script configuration files
├── scripts/             # Lua and JavaScript scripts
├── shaders/             # GPU shader files
├── mpv.com              # Console wrapper
├── mpv.exe              # Main player executable
├── yt-dlp.exe           # YouTube and other site integration
├── nvjpeg2k_0.dll       # NVIDIA nvJPEG2000 runtime
├── libvvdec.dll         # Fraunhofer VVdeC runtime
└── *.dll                # FFmpeg and other runtime dependencies
```

The package includes **41 DLL files**, including the custom FFmpeg libraries, `libvvdec.dll`, `nvjpeg2k_0.dll`, libplacebo, libass and their runtime dependencies. The DLLs are stored alongside `mpv.exe` so the portable build can load its dependencies from the application directory.

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

   **Pixel Format Handling & Dynamic Transitions**:
   `jpeg2000_nvjpeg` assigns `avctx->pix_fmt` and updates dimensions before calling `ff_get_buffer()`, conforming strictly to the FFmpeg 9.0.2 decoder contract (mirroring native `jpeg2000dec` behavior). All selectable pixel formats are guaranteed to be a subset of the codec's declared `pix_fmts` array in `FFCodec`. The decoder seamlessly supports dynamic mid-stream transitions between frames:
   - **Bit-depth changes**: 8-bit, 10-bit, 12-bit, and 16-bit depths.
   - **Color family changes**: RGB (`GBRP`), YUV (`YUV444P`, `YUV422P`, `YUV420P`, etc.), Grayscale (`GRAY`), and DCI Digital Cinema (`XYZ12LE`).
   - **Chroma subsampling changes**: 4:2:0, 4:2:2, 4:4:0, and 4:4:4.
   - **Resolution changes**: arbitrary width and height updates validated through `av_image_check_size2` and `ff_set_dimensions`.
   Downstream players (mpv `vo`) and filter chains (`vf`) dynamically reconfigure upon receiving the updated frame properties without requiring manual decoder restart.
6. **`libavcodec/libvvdec.c`** (from `patches/ffmpeg-9.0.2-libvvdec.patch`):
   Integration of [Fraunhofer HHI VVdeC](https://github.com/fraunhoferhhi/vvdec) as a software H.266/VVC decoder (`libvvdec`). This is the official Fraunhofer patch (`v9-libvvdec.patch`) adapted for FFmpeg 9.0.2 — it registers `const FFCodec ff_libvvdec_decoder` in `allcodecs.c`, adds `--enable-libvvdec` to `configure`, and links via `pkg-config libvvdec`. Build VVdeC from source first (`-DBUILD_SHARED_LIBS=ON -DVVDEC_ENABLE_LINK_TIME_OPT=OFF`) and install to sysroot before running FFmpeg configure.

   **mpv decoder priority patch** (`patches/mpv-0.42.0-libvvdec-priority.patch`): Modifies `common/codecs.c` to prefer `libvvdec` over the FFmpeg native `vvc` decoder when no hardware VVC decoder is available. When GPU hwdec does not support VVC (which is the case on virtually all current consumer GPUs), mpv will automatically select `libvvdec` as the first software fallback. The native `vvc` decoder remains available as a secondary fallback, or can be forced with `--vd=vvc`.

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

# 2. NVIDIA nvJPEG2000 GPU video decoder (optional)
git apply ../patches/ffmpeg-9.0.2-nvjpeg2k.patch

# 3. Fraunhofer VVdeC H.266/VVC software decoder (optional)
#    Requires VVdeC library built and installed first — see step 2a below
git apply ../patches/ffmpeg-9.0.2-libvvdec.patch

./configure \
    --prefix=/install \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-debug \
    --enable-gpl \
    --enable-version3 \
    --enable-libvvdec   # omit if not using VVdeC

make -j$(nproc)
make install
cd ..
```

### 2a. Build VVdeC (required for `ffmpeg-9.0.2-libvvdec.patch`)
```bash
# Install cmake
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-cmake

git clone https://github.com/fraunhoferhhi/vvdec.git
cmake -B vvdec/build -S vvdec \
    -G Ninja \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/ucrt64 \
    -DVVDEC_ENABLE_LINK_TIME_OPT=OFF
ninja -C vvdec/build install
```

### 3. Apply mpv patch (optional — prioritizes libvvdec for VVC)
```bash
git clone --depth 1 https://github.com/mpv-player/mpv.git mpv
cd mpv

# Makes mpv prefer libvvdec over native vvc when no hwdec is available
git apply ../patches/mpv-0.42.0-libvvdec-priority.patch

cd ..
```

### 4. Build mpv
```bash
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
- VVdeC H.266/VVC decoder: **[Fraunhofer HHI](https://github.com/fraunhoferhhi/vvdec)** (BSD 3-Clause); integration patch by Thomas Siedel / Spin Digital
- Windows installer scripts: **[rossy/mpv-install](https://github.com/rossy/mpv-install)**
- Reference build system: **[shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake)**
