## mpv AC-4 & nvJPEG2000 Edition v1.5.0

### What's New and Fixed in v1.5.0 (Color Metadata & Quality Enhancement):

* **T06 — JP2 / JPEG 2000 Color Metadata Propagation to AVFrame**:
  * **Accurate Colorimetry Transfer**: Full end-to-end propagation of stream and container color metadata into `AVFrame` (`colorspace`, `color_range`, `color_primaries`, `color_trc`, `chroma_location`).
  * **Standard sRGB Handling (`NVJPEG2K_COLORSPACE_SRGB`)**: Correctly maps to `AVCOL_SPC_RGB`, full range (`AVCOL_RANGE_JPEG`), BT.709 primaries, and IEC 61966-2-1 sRGB TRC. Guarantees that container-level YUV matrices are never incorrectly applied to RGB pixel formats.
  * **Standard sYCC Handling (`NVJPEG2K_COLORSPACE_SYCC`)**: Fully compliant with ITU-T T.800 Annex I.7.3.2 — maps to BT.601 matrix (`AVCOL_SPC_SMPTE170M`), full range (`AVCOL_RANGE_JPEG`), BT.709 primaries, and sRGB TRC. Chroma siting defaults to `UNSPECIFIED` (as unspecified in T.800) instead of artificially forcing `LEFT`.
  * **Digital Cinema XYZ (`AV_PIX_FMT_XYZ12LE`)**: Strictly enforces SMPTE ST 428-1 (`AVCOL_PRI_SMPTE428`, `AVCOL_TRC_SMPTE428`, `AVCOL_SPC_RGB`, `AVCOL_RANGE_JPEG`, `AVCHROMA_LOC_UNSPECIFIED`), completely isolated from conflicting container YUV properties.
  * **Elimination of Artificial Fallbacks**: Removed unconditional defaults that forced `AVCOL_RANGE_MPEG` (limited range) and `AVCHROMA_LOC_LEFT` when stream and container metadata were unspecified. Raw `.j2k` streams cleanly report `UNSPECIFIED` unless explicitly defined by the container.
  * **Inter-Frame Metadata Isolation**: Decoder properties are set directly on output `AVFrame` instances without mutating `AVCodecContext`, preventing cross-frame metadata leakage.

* **T13 — cuMemAllocPitch 2D Pitch Alignment Audit**:
  * Confirmed strict adherence to NVIDIA CUDA 2D pitch alignment guidelines across all components, resolutions, and sub-sampled chroma planes.

* **Release Packaging Compliance**:
  * Release archive strictly follows the dynamic naming convention: `MPV-AC4-nvJPEG2000-Windows-1.5.0-x64.zip`.
  * Top-level directory inside the archive is statically named `MPV-AC4-nvJPEG2000-Windows-x64/`.
  * Clean build without user configuration files (`.conf`), user scripts (`.lua`), or temporary directories.
  * Includes empty placeholder directories: `portable_config/`, `scripts/`, `script-opts/`, `shaders/`.
