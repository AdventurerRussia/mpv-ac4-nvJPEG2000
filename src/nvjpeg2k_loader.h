/*
 * nvJPEG2000 dynamic loader header
 * Hand-crafted from DLL exports + NVIDIA nvJPEG2000 SDK documentation.
 * No compile-time dependency on nvJPEG2000 SDK or CUDA toolkit required.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_NVJPEG2K_LOADER_H
#define AVCODEC_NVJPEG2K_LOADER_H

#include <stdint.h>
#include <stddef.h>

typedef struct nvjpeg2kHandle         *nvjpeg2kHandle_t;
typedef struct nvjpeg2kDecodeState    *nvjpeg2kDecodeState_t;
typedef struct nvjpeg2kStream         *nvjpeg2kStream_t;
typedef struct nvjpeg2kDecodeParams   *nvjpeg2kDecodeParams_t;

typedef unsigned long long CUdeviceptr;
typedef int                CUresult;
#define CUDA_SUCCESS 0

typedef enum {
    NVJPEG2K_STATUS_SUCCESS           = 0,
    NVJPEG2K_STATUS_NOT_INITIALIZED   = 1,
    NVJPEG2K_STATUS_INVALID_PARAMETER = 2,
    NVJPEG2K_STATUS_BAD_JPEG          = 3,
    NVJPEG2K_STATUS_JPEG_NOT_SUPPORTED = 4,
    NVJPEG2K_STATUS_ALLOCATOR_FAILURE = 5,
    NVJPEG2K_STATUS_EXECUTION_FAILED  = 6,
    NVJPEG2K_STATUS_ARCH_MISMATCH     = 7,
    NVJPEG2K_STATUS_INTERNAL_ERROR    = 8,
    NVJPEG2K_STATUS_IMPLEMENTATION_NOT_SUPPORTED = 9,
} nvjpeg2kStatus_t;

typedef enum {
    NVJPEG2K_UINT8  = 0,
    NVJPEG2K_UINT16 = 1,
    NVJPEG2K_INT16  = 2,
    NVJPEG2K_FP16   = 3,
    NVJPEG2K_FP32   = 4,
} nvjpeg2kImageType_t;

typedef enum {
    NVJPEG2K_NLT_NONE  = 0,
    NVJPEG2K_NLT_TYPE3 = 3,
} nvjpeg2kNLTType_t;

typedef struct {
    nvjpeg2kNLTType_t type;
    uint8_t           bit_depth;
} nvjpeg2kNLTInfo_t;

typedef enum {
    NVJPEG2K_COLORSPACE_NOT_SUPPORTED = -1,
    NVJPEG2K_COLORSPACE_UNKNOWN       =  0,
    NVJPEG2K_COLORSPACE_SRGB          =  1,
    NVJPEG2K_COLORSPACE_GRAY          =  2,
    NVJPEG2K_COLORSPACE_SYCC          =  3,
} nvjpeg2kColorSpace_t;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert((int)NVJPEG2K_COLORSPACE_NOT_SUPPORTED == -1, "NVJPEG2K_COLORSPACE_NOT_SUPPORTED");
_Static_assert((int)NVJPEG2K_COLORSPACE_UNKNOWN == 0, "NVJPEG2K_COLORSPACE_UNKNOWN");
_Static_assert((int)NVJPEG2K_COLORSPACE_SRGB == 1, "NVJPEG2K_COLORSPACE_SRGB");
_Static_assert((int)NVJPEG2K_COLORSPACE_GRAY == 2, "NVJPEG2K_COLORSPACE_GRAY");
_Static_assert((int)NVJPEG2K_COLORSPACE_SYCC == 3, "NVJPEG2K_COLORSPACE_SYCC");
#endif

typedef enum {
    NVJPEG2K_LIBRARY_MAJOR_VERSION = 0,
    NVJPEG2K_LIBRARY_MINOR_VERSION = 1,
    NVJPEG2K_LIBRARY_PATCH_LEVEL   = 2,
} nvjpeg2kLibraryPropertyType;

typedef struct {
    uint32_t component_width;
    uint32_t component_height;
    uint8_t  precision;
    uint8_t  sgn;
} nvjpeg2kImageComponentInfo_t;

typedef struct {
    uint32_t image_width;
    uint32_t image_height;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t num_tiles_x;
    uint32_t num_tiles_y;
    uint32_t num_components;
} nvjpeg2kImageInfo_t;

typedef struct {
    void               **pixel_data;
    size_t              *pitch_in_bytes;
    nvjpeg2kImageType_t  pixel_type;
    uint32_t             num_components;
} nvjpeg2kImage_t;

typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kCreateSimple)(nvjpeg2kHandle_t *handle);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDestroy)(nvjpeg2kHandle_t handle);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecodeStateCreate)(
    nvjpeg2kHandle_t handle, nvjpeg2kDecodeState_t *decode_state);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecodeStateDestroy)(
    nvjpeg2kDecodeState_t decode_state);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamCreate)(nvjpeg2kStream_t *stream);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamDestroy)(nvjpeg2kStream_t stream);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamParse)(
    nvjpeg2kHandle_t handle,
    const unsigned char *data, size_t length,
    int save_metadata, int save_stream,
    nvjpeg2kStream_t stream);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamGetImageInfo)(
    nvjpeg2kStream_t stream, nvjpeg2kImageInfo_t *image_info);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamGetImageComponentInfo)(
    nvjpeg2kStream_t stream,
    nvjpeg2kImageComponentInfo_t *comp_info,
    uint32_t component_id);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecode)(
    nvjpeg2kHandle_t handle,
    nvjpeg2kDecodeState_t decode_state,
    nvjpeg2kStream_t jpeg_stream,
    nvjpeg2kImage_t *destination,
    void *cuda_stream);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kGetProperty)(
    nvjpeg2kLibraryPropertyType type, int *value);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamGetColorSpace)(
    nvjpeg2kStream_t stream, nvjpeg2kColorSpace_t *colorspace);
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamGetComponentNLTInfo)(
    nvjpeg2kStream_t stream_handle, uint32_t component_id, nvjpeg2kNLTInfo_t *nlt_info);

typedef int   CUdevice;
typedef void *CUcontext;
typedef void *CUstream;

typedef CUresult (*PFN_cuInit)(unsigned int Flags);
typedef CUresult (*PFN_cuDeviceGet)(CUdevice *device, int ordinal);
typedef CUresult (*PFN_cuDevicePrimaryCtxRetain)(CUcontext *pctx, CUdevice dev);
typedef CUresult (*PFN_cuDevicePrimaryCtxRelease)(CUdevice dev);
typedef CUresult (*PFN_cuCtxGetCurrent)(CUcontext *pctx);
typedef CUresult (*PFN_cuCtxSetCurrent)(CUcontext ctx);
typedef CUresult (*PFN_cuMemAlloc_v2)(CUdeviceptr *dptr, size_t bytesize);
typedef CUresult (*PFN_cuMemAllocPitch_v2)(CUdeviceptr *dptr, size_t *pPitch,
                                          size_t WidthInBytes, size_t Height,
                                          unsigned int ElementSizeBytes);
typedef CUresult (*PFN_cuMemFree_v2)(CUdeviceptr dptr);
typedef CUresult (*PFN_cuMemAllocHost_v2)(void **pp, size_t bytesize);
typedef CUresult (*PFN_cuMemFreeHost)(void *p);
typedef CUresult (*PFN_cuMemcpyDtoHAsync_v2)(void *dstHost, CUdeviceptr srcDevice,
                                            size_t ByteCount, CUstream hStream);
typedef CUresult (*PFN_cuCtxSynchronize)(void);

typedef struct NVJPEG2KFunctions {
    PFN_nvjpeg2kCreateSimple                   CreateSimple;
    PFN_nvjpeg2kDestroy                        Destroy;
    PFN_nvjpeg2kDecodeStateCreate              DecodeStateCreate;
    PFN_nvjpeg2kDecodeStateDestroy             DecodeStateDestroy;
    PFN_nvjpeg2kStreamCreate                   StreamCreate;
    PFN_nvjpeg2kStreamDestroy                  StreamDestroy;
    PFN_nvjpeg2kStreamParse                    StreamParse;
    PFN_nvjpeg2kStreamGetImageInfo             StreamGetImageInfo;
    PFN_nvjpeg2kStreamGetImageComponentInfo    StreamGetImageComponentInfo;
    PFN_nvjpeg2kDecode                         Decode;
    PFN_nvjpeg2kGetProperty                    GetProperty;
    PFN_nvjpeg2kStreamGetColorSpace            StreamGetColorSpace;
    PFN_nvjpeg2kStreamGetComponentNLTInfo      StreamGetComponentNLTInfo;
    PFN_cuInit                                 Init;
    PFN_cuDeviceGet                            DeviceGet;
    PFN_cuDevicePrimaryCtxRetain               PrimaryCtxRetain;
    PFN_cuDevicePrimaryCtxRelease              PrimaryCtxRelease;
    PFN_cuCtxGetCurrent                        CtxGetCurrent;
    PFN_cuCtxSetCurrent                        CtxSetCurrent;
    PFN_cuMemAlloc_v2                          MemAlloc;
    PFN_cuMemAllocPitch_v2                     MemAllocPitch;
    PFN_cuMemFree_v2                           MemFree;
    PFN_cuMemAllocHost_v2                      MemAllocHost;
    PFN_cuMemFreeHost                          MemFreeHost;
    PFN_cuMemcpyDtoHAsync_v2                   MemcpyDtoHAsync;
    PFN_cuCtxSynchronize                       CtxSynchronize;
} NVJPEG2KFunctions;

#endif /* AVCODEC_NVJPEG2K_LOADER_H */
