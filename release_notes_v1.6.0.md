## mpv AC-4 & nvJPEG2000 Edition v1.6.0

### What's New and Fixed in v1.6.0 (Robustness, Error Recovery & Transitions):

* **T07 — Signature-Based Sticky Software Fallback & Frame-Only Corrupt Recovery**:
  * **Frame-Only Corrupt Recovery (`NVJPEG2K_STATUS_BAD_JPEG = 3`)**: Corrupt packets or transmission errors now fall back to the software decoder for the affected frame only (`sticky = 0`). Subsequent valid frames immediately resume GPU hardware decoding, fixing a long-standing issue where a single corrupted packet permanently crippled GPU acceleration for the remainder of playback.
  * **Stream Signature Isolation (`NVJPEG2KStreamSignature`)**: Features genuinely unsupported by GPU hardware (such as unsupported sub-sampling, signed samples, NLT curves, or tile offsets) engage sticky fallback tied exclusively to that stream signature (`width`, `height`, `ncomp`, `rsiz`, `precision`, `sub_x`, `sub_y`, etc.).
  * **Adaptive Signature Resetting**: If a stream switches resolution, bit-depth, profile, or format mid-stream, the sticky state automatically resets and retries GPU acceleration on the new signature.
  * **Seeking & Flush Support**: Implemented the `.flush = nvjpeg2k_decode_flush` callback in `FFCodec`. Seeking via `avcodec_flush_buffers` properly clears internal software decoder state while preserving stream signature cache.
  * **Proper Error Propagation**: Internal and programming errors (`ALLOCATOR_FAILURE`, `INTERNAL_ERROR`) return explicit error codes (`AVERROR(ENOMEM)`, `AVERROR_EXTERNAL`) rather than silently masquerading as stream fallback.

* **T12 — Pixel Format Direct Assignment Contract & Dynamic Mid-Stream Transitions**:
  * **FFmpeg 9.0.2 Architecture Alignment**: Fully audited and verified direct `avctx->pix_fmt` assignment against FFmpeg 9.0.2 `libavcodec/decode.c` requirements. Setting `pix_fmt` and dimensions before `ff_get_buffer()` strictly adheres to FFmpeg's image decoder design (matching native `jpeg2000dec.c`).
  * **100% PixFmts Capability Containment**: Verified that every format produced by `select_pixel_format()` strictly belongs to the declared `nvjpeg2k_pix_fmts` capability array.
  * **Dynamic Format Transitions**: Verified on NVIDIA GPU hardware that mid-stream transitions across bit depths (8/10/12/16-bit), color spaces (RGB `GBRP` ↔ YUV `YUV444P` ↔ DCI `XYZ12LE`), chroma subsamplings (4:2:0 ↔ 4:2:2 ↔ 4:4:4), and resolution changes decode cleanly and automatically trigger filtergraph / VO reinitialization without crashes or frame drops.
  * **Documentation**: Added comprehensive documentation on pixel format handling and mid-stream transitions to `README.md`.

* **Release Packaging Compliance**:
  * Release archive follows the dynamic naming convention: `MPV-AC4-nvJPEG2000-Windows-1.6.0-x64.zip`.
  * Top-level directory inside the archive is statically named `MPV-AC4-nvJPEG2000-Windows-x64/`.
  * Clean build without user configuration files (`.conf`), user scripts (`.lua`), or temporary build artifacts.
  * Includes empty placeholder directories: `portable_config/`, `scripts/`, `script-opts/`, `shaders/`.
