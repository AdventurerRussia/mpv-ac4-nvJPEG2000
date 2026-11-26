/*
 * JPEG 2000 decoder using NVIDIA nvJPEG2000 GPU library (dynamic loading)
 *
 * Copyright (c) 2024-2026 mpv AC-4 Edition contributors
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

#include "config.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

#ifdef _WIN32

#include <windows.h>
#include <wchar.h>
#include "nvjpeg2k_loader.h"

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif

#define MAX_COMPONENTS 4

enum NVColor {
    NVCOL_UNKNOWN = 0,
    NVCOL_RGB,
    NVCOL_YUV,
    NVCOL_XYZ,
};

typedef struct NVJPEG2KStreamSignature {
    uint32_t width;
    uint32_t height;
    uint32_t ncomp;
    uint16_t rsiz;
    uint8_t  precision;
    uint8_t  sgn;
    uint8_t  sub_x;
    uint8_t  sub_y;
    uint8_t  bad_off;
    uint8_t  has_nlt3;
    uint8_t  valid;
} NVJPEG2KStreamSignature;

typedef struct NVJPEG2KContext {
    AVClass              *av_class;
    int                   gpu_device;

    HMODULE               nvjpeg2k_dll;
    HMODULE               nvcuda_dll;
    NVJPEG2KFunctions     fn;

    CUdevice              cuda_dev;
    CUcontext             cuda_ctx;

    nvjpeg2kHandle_t      handle;
    nvjpeg2kDecodeState_t decode_state;
    nvjpeg2kStream_t      jpeg_stream;

    CUdeviceptr           dev_buf[MAX_COMPONENTS];
    size_t                dev_buf_size[MAX_COMPONENTS];
    size_t                dev_pitch[MAX_COMPONENTS];
    size_t                dev_buf_h[MAX_COMPONENTS];
    void                 *host_buf[MAX_COMPONENTS];
    size_t                host_buf_size[MAX_COMPONENTS];

    NVJPEG2KStreamSignature unsupported_sig;
    int                   has_unsupported_sig;
    AVCodecContext       *sw;
    int                   force_sw;

    /* NVAPI DRS Power Management Mode state */
    int                   nvapi_pstate_mode;    /* 0=Auto, 1=MaxPerf, -1=disabled */
    int                   nvapi_applied;        /* 1 if we changed DRS setting */
    uint32_t              nvapi_prev_mode;      /* previous DRS setting to restore */
} NVJPEG2KContext;

static int load_sym(AVCodecContext *avctx, HMODULE dll,
                    const char *dll_name, const char *fn_name, void **out)
{
    *out = (void *)GetProcAddress(dll, fn_name);
    if (!*out) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: symbol %s not found in %s\n", fn_name, dll_name);
        return AVERROR(ENOSYS);
    }
    return 0;
}

#define LOAD_SYM(dll, dll_name, field, sym)                                    \
    do {                                                                       \
        void *_p = NULL;                                                      \
        if (load_sym(avctx, dll, dll_name, sym, &_p) < 0)                      \
            return AVERROR(ENOSYS);                                           \
        ctx->fn.field = (__typeof__(ctx->fn.field))_p;                        \
    } while (0)

static int sig_matches(const NVJPEG2KStreamSignature *a, const NVJPEG2KStreamSignature *b)
{
    if (!a->valid || !b->valid)
        return 0;
    return a->width == b->width &&
           a->height == b->height &&
           a->ncomp == b->ncomp &&
           a->rsiz == b->rsiz &&
           a->precision == b->precision &&
           a->sgn == b->sgn &&
           a->sub_x == b->sub_x &&
           a->sub_y == b->sub_y &&
           a->bad_off == b->bad_off &&
           a->has_nlt3 == b->has_nlt3;
}

static inline int status_is_unsupported(nvjpeg2kStatus_t st)
{
    return st == NVJPEG2K_STATUS_JPEG_NOT_SUPPORTED ||
           st == NVJPEG2K_STATUS_ARCH_MISMATCH ||
           st == NVJPEG2K_STATUS_IMPLEMENTATION_NOT_SUPPORTED;
}

static inline int status_is_corrupt(nvjpeg2kStatus_t st)
{
    return st == NVJPEG2K_STATUS_BAD_JPEG;
}

static int cuda_enter(AVCodecContext *avctx, NVJPEG2KContext *ctx, CUcontext *saved)
{
    CUresult cr;
    if (!ctx->cuda_ctx || !ctx->fn.CtxGetCurrent || !ctx->fn.CtxSetCurrent)
        return AVERROR(ENOSYS);

    cr = ctx->fn.CtxGetCurrent(saved);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuCtxGetCurrent failed (%d)\n", (int)cr);
        *saved = NULL;
        return AVERROR_EXTERNAL;
    }
    if (*saved == ctx->cuda_ctx)
        return 0;

    cr = ctx->fn.CtxSetCurrent(ctx->cuda_ctx);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuCtxSetCurrent failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }
    return 1;
}

static void cuda_leave(NVJPEG2KContext *ctx, CUcontext saved, int switched)
{
    if (switched > 0 && ctx->fn.CtxSetCurrent)
        ctx->fn.CtxSetCurrent(saved);
}

static HMODULE load_nvjpeg_dll(void)
{
    wchar_t path[MAX_PATH];
    HMODULE self = NULL;
    DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    HMODULE mod[2] = { NULL, NULL };
    int nmod = 0;

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)load_nvjpeg_dll, &self))
        mod[nmod++] = self;
    mod[nmod++] = NULL;

    for (int i = 0; i < nmod; i++) {
        DWORD n = GetModuleFileNameW(mod[i], path, MAX_PATH);
        wchar_t *slash;
        if (!n || n >= MAX_PATH)
            continue;
        slash = wcsrchr(path, L'\\');
        if (!slash)
            continue;
        if ((size_t)(slash - path) + 1 + wcslen(L"nvjpeg2k_0.dll") >= MAX_PATH)
            continue;
        wcscpy(slash + 1, L"nvjpeg2k_0.dll");
        self = LoadLibraryExW(path, NULL, flags);
        if (self)
            return self;
    }
    return NULL;
}

#define PSTATE_ID 0x1057EB71
#define NVAPI_OK 0

typedef void* (*NvAPI_QueryInterface_t)(uint32_t offset);
typedef int (*NvAPI_Initialize_t)(void);
typedef int (*NvAPI_DRS_CreateSession_t)(void **hSession);
typedef int (*NvAPI_DRS_DestroySession_t)(void *hSession);
typedef int (*NvAPI_DRS_LoadSettings_t)(void *hSession);
typedef int (*NvAPI_DRS_SaveSettings_t)(void *hSession);
typedef int (*NvAPI_DRS_FindProfileByName_t)(void *hSession, const wchar_t *profileName, void **hProfile);
typedef int (*NvAPI_DRS_CreateProfile_t)(void *hSession, void *pProfile, void **hProfile);
typedef int (*NvAPI_DRS_FindApplicationByName_t)(void *hSession, const wchar_t *appName, void **hProfile, void *pApp);
typedef int (*NvAPI_DRS_CreateApplication_t)(void *hSession, void *hProfile, void *pApp);
typedef int (*NvAPI_DRS_GetSetting_t)(void *hSession, void *hProfile, uint32_t settingId, void *pSetting);
typedef int (*NvAPI_DRS_SetSetting_t)(void *hSession, void *hProfile, void *pSetting);

#pragma pack(push, 8)
typedef struct {
    uint32_t valueLength;
    uint8_t  valueData[4096];
} NVDRS_BINARY_SETTING;

typedef struct {
    uint32_t version;
    wchar_t  settingName[2048];
    uint32_t settingId;
    uint32_t settingType;
    uint32_t settingLocation;
    uint32_t isCurrentPredefined;
    uint32_t isPredefinedValid;
    union {
        uint32_t             u32;
        NVDRS_BINARY_SETTING bin;
        wchar_t              wsz[2048];
        float                f32;
    } predefined;
    union {
        uint32_t             u32;
        NVDRS_BINARY_SETTING bin;
        wchar_t              wsz[2048];
        float                f32;
    } current;
} NVDRS_SETTING;

typedef struct {
    uint32_t version;
    wchar_t  profileName[2048];
    uint32_t gpuSupport;
    uint32_t isPredefined;
    uint32_t numOfApps;
    uint32_t numOfSettings;
} NVDRS_PROFILE;

typedef struct {
    uint32_t version;
    uint32_t isPredefined;
    wchar_t  appName[2048];
    wchar_t  userFriendlyName[2048];
    wchar_t  launcher[2048];
    wchar_t  fileInFolder[2048];
    uint32_t flags;
    wchar_t  commandLine[2048];
} NVDRS_APPLICATION;
#pragma pack(pop)

/* Clean status prefixes for mpv OSD & terminal console */
#define NVAPI_PREFIX_OK   "[OK] "
#define NVAPI_PREFIX_WARN "[WARN] "
#define NVAPI_PREFIX_ERR  "[ERR] "

static void nvapi_apply_pstate(AVCodecContext *avctx, NVJPEG2KContext *ctx, int mode)
{
    HMODULE hNvApi;
    NvAPI_QueryInterface_t NvAPI_QueryInterface;
    NvAPI_Initialize_t NvAPI_Initialize;
    NvAPI_DRS_CreateSession_t NvAPI_DRS_CreateSession;
    NvAPI_DRS_DestroySession_t NvAPI_DRS_DestroySession;
    NvAPI_DRS_LoadSettings_t NvAPI_DRS_LoadSettings;
    NvAPI_DRS_SaveSettings_t NvAPI_DRS_SaveSettings;
    NvAPI_DRS_FindProfileByName_t NvAPI_DRS_FindProfileByName;
    NvAPI_DRS_CreateProfile_t NvAPI_DRS_CreateProfile;
    NvAPI_DRS_FindApplicationByName_t NvAPI_DRS_FindApplicationByName;
    NvAPI_DRS_CreateApplication_t NvAPI_DRS_CreateApplication;
    NvAPI_DRS_GetSetting_t NvAPI_DRS_GetSetting;
    NvAPI_DRS_SetSetting_t NvAPI_DRS_SetSetting;
    void *hSession = NULL, *hProfile = NULL;
    wchar_t exePath[MAX_PATH];
    NVDRS_APPLICATION app;
    NVDRS_SETTING setting;
    int st;

    if (mode < 0) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_WARN "NVIDIA Power Mode: Skipped (disabled via nvapi_pstate=-1)\n");
        return;
    }

    hNvApi = LoadLibraryExW(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hNvApi)
        hNvApi = LoadLibraryA("nvapi64.dll");
    if (!hNvApi) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_WARN "NVIDIA Power Mode: Unavailable (nvapi64.dll not found)\n");
        return;
    }

    NvAPI_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(hNvApi, "nvapi_QueryInterface");
    if (!NvAPI_QueryInterface) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (nvapi_QueryInterface missing)\n");
        FreeLibrary(hNvApi);
        return;
    }

    NvAPI_Initialize = (NvAPI_Initialize_t)NvAPI_QueryInterface(0x0150E828);
    NvAPI_DRS_CreateSession = (NvAPI_DRS_CreateSession_t)NvAPI_QueryInterface(0x0694D52E);
    NvAPI_DRS_DestroySession = (NvAPI_DRS_DestroySession_t)NvAPI_QueryInterface(0xDAD9CFF8);
    NvAPI_DRS_LoadSettings = (NvAPI_DRS_LoadSettings_t)NvAPI_QueryInterface(0x375DBD6B);
    NvAPI_DRS_SaveSettings = (NvAPI_DRS_SaveSettings_t)NvAPI_QueryInterface(0xFCBC7E14);
    NvAPI_DRS_FindProfileByName = (NvAPI_DRS_FindProfileByName_t)NvAPI_QueryInterface(0x7E4A9A0B);
    NvAPI_DRS_CreateProfile = (NvAPI_DRS_CreateProfile_t)NvAPI_QueryInterface(0xCC176068);
    NvAPI_DRS_FindApplicationByName = (NvAPI_DRS_FindApplicationByName_t)NvAPI_QueryInterface(0xEEE566B2);
    NvAPI_DRS_CreateApplication = (NvAPI_DRS_CreateApplication_t)NvAPI_QueryInterface(0x4347A9DE);
    NvAPI_DRS_GetSetting = (NvAPI_DRS_GetSetting_t)NvAPI_QueryInterface(0x73BF8338);
    NvAPI_DRS_SetSetting = (NvAPI_DRS_SetSetting_t)NvAPI_QueryInterface(0x577DD202);

    if (!NvAPI_Initialize || !NvAPI_DRS_CreateSession || !NvAPI_DRS_DestroySession ||
        !NvAPI_DRS_LoadSettings || !NvAPI_DRS_SaveSettings || !NvAPI_DRS_GetSetting || !NvAPI_DRS_SetSetting) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (DRS entry points unresolved)\n");
        FreeLibrary(hNvApi);
        return;
    }

    st = NvAPI_Initialize();
    if (st != NVAPI_OK) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (NvAPI_Initialize=%d)\n", st);
        FreeLibrary(hNvApi);
        return;
    }

    st = NvAPI_DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (CreateSession=%d)\n", st);
        FreeLibrary(hNvApi);
        return;
    }

    st = NvAPI_DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (LoadSettings=%d)\n", st);
        NvAPI_DRS_DestroySession(hSession);
        FreeLibrary(hNvApi);
        return;
    }

    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (GetModuleFileName failed)\n");
        NvAPI_DRS_DestroySession(hSession);
        FreeLibrary(hNvApi);
        return;
    }

    for (int i = 0; exePath[i]; i++) {
        if (exePath[i] == L'\\') exePath[i] = L'/';
        exePath[i] = towlower(exePath[i]);
    }

    memset(&app, 0, sizeof(app));
    app.version = sizeof(app) | (4 << 16);
    if (NvAPI_DRS_FindApplicationByName)
        st = NvAPI_DRS_FindApplicationByName(hSession, exePath, &hProfile, &app);
    else
        st = -1;

    if (st != NVAPI_OK) {
        if (NvAPI_DRS_FindProfileByName)
            st = NvAPI_DRS_FindProfileByName(hSession, exePath, &hProfile);
        if (st != NVAPI_OK && NvAPI_DRS_CreateProfile) {
            NVDRS_PROFILE prof;
            memset(&prof, 0, sizeof(prof));
            prof.version = sizeof(prof) | (1 << 16);
            wcsncpy(prof.profileName, exePath, 2047);
            prof.gpuSupport = 0x7;
            st = NvAPI_DRS_CreateProfile(hSession, &prof, &hProfile);
        }
        if (st == NVAPI_OK && hProfile && NvAPI_DRS_CreateApplication) {
            NVDRS_APPLICATION newApp;
            memset(&newApp, 0, sizeof(newApp));
            newApp.version = sizeof(newApp) | (4 << 16);
            wcsncpy(newApp.appName, exePath, 2047);
            wcsncpy(newApp.userFriendlyName, exePath, 2047);
            NvAPI_DRS_CreateApplication(hSession, hProfile, &newApp);
        }
    }

    if (!hProfile) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_WARN "NVIDIA Power Mode: Could not locate/create app profile\n");
        NvAPI_DRS_DestroySession(hSession);
        FreeLibrary(hNvApi);
        return;
    }

    memset(&setting, 0, sizeof(setting));
    setting.version = sizeof(setting) | (1 << 16);
    st = NvAPI_DRS_GetSetting(hSession, hProfile, PSTATE_ID, &setting);
    if (st == NVAPI_OK) {
        ctx->nvapi_prev_mode = setting.current.u32;
    } else {
        ctx->nvapi_prev_mode = 0; /* Default Adaptive */
    }

    if (ctx->nvapi_prev_mode != (uint32_t)mode) {
        memset(&setting, 0, sizeof(setting));
        setting.version = sizeof(setting) | (1 << 16);
        setting.settingId = PSTATE_ID;
        setting.current.u32 = (uint32_t)mode;
        st = NvAPI_DRS_SetSetting(hSession, hProfile, &setting);
        if (st != NVAPI_OK) {
            av_log(avctx, AV_LOG_WARNING,
                   "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (SetSetting=%d)\n", st);
        } else {
            st = NvAPI_DRS_SaveSettings(hSession);
            if (st != NVAPI_OK) {
                av_log(avctx, AV_LOG_WARNING,
                       "nvjpeg2k: " NVAPI_PREFIX_ERR "NVIDIA Power Mode: Failed (SaveSettings=%d)\n", st);
            } else {
                ctx->nvapi_applied = 1;
                av_log(avctx, AV_LOG_WARNING,
                       "nvjpeg2k: " NVAPI_PREFIX_OK "NVIDIA Power Mode: Prefer Maximum Performance enabled (Mode %d, prev: %u)\n",
                       mode, (unsigned)ctx->nvapi_prev_mode);
            }
        }
    } else {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: " NVAPI_PREFIX_OK "NVIDIA Power Mode: Prefer Maximum Performance already active (Mode %d)\n",
               mode);
    }

    NvAPI_DRS_DestroySession(hSession);
    FreeLibrary(hNvApi);
}

static void nvapi_restore_pstate(AVCodecContext *avctx, NVJPEG2KContext *ctx)
{
    HMODULE hNvApi;
    NvAPI_QueryInterface_t NvAPI_QueryInterface;
    NvAPI_Initialize_t NvAPI_Initialize;
    NvAPI_DRS_CreateSession_t NvAPI_DRS_CreateSession;
    NvAPI_DRS_DestroySession_t NvAPI_DRS_DestroySession;
    NvAPI_DRS_LoadSettings_t NvAPI_DRS_LoadSettings;
    NvAPI_DRS_SaveSettings_t NvAPI_DRS_SaveSettings;
    NvAPI_DRS_FindApplicationByName_t NvAPI_DRS_FindApplicationByName;
    NvAPI_DRS_FindProfileByName_t NvAPI_DRS_FindProfileByName;
    NvAPI_DRS_SetSetting_t NvAPI_DRS_SetSetting;
    void *hSession = NULL, *hProfile = NULL;
    wchar_t exePath[MAX_PATH];
    NVDRS_APPLICATION app;
    NVDRS_SETTING setting;
    int st;

    if (!ctx->nvapi_applied)
        return;

    hNvApi = LoadLibraryExW(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hNvApi)
        hNvApi = LoadLibraryA("nvapi64.dll");
    if (!hNvApi)
        return;

    NvAPI_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(hNvApi, "nvapi_QueryInterface");
    if (!NvAPI_QueryInterface) {
        FreeLibrary(hNvApi);
        return;
    }

    NvAPI_Initialize = (NvAPI_Initialize_t)NvAPI_QueryInterface(0x0150E828);
    NvAPI_DRS_CreateSession = (NvAPI_DRS_CreateSession_t)NvAPI_QueryInterface(0x0694D52E);
    NvAPI_DRS_DestroySession = (NvAPI_DRS_DestroySession_t)NvAPI_QueryInterface(0xDAD9CFF8);
    NvAPI_DRS_LoadSettings = (NvAPI_DRS_LoadSettings_t)NvAPI_QueryInterface(0x375DBD6B);
    NvAPI_DRS_SaveSettings = (NvAPI_DRS_SaveSettings_t)NvAPI_QueryInterface(0xFCBC7E14);
    NvAPI_DRS_FindApplicationByName = (NvAPI_DRS_FindApplicationByName_t)NvAPI_QueryInterface(0xEEE566B2);
    NvAPI_DRS_FindProfileByName = (NvAPI_DRS_FindProfileByName_t)NvAPI_QueryInterface(0x7E4A9A0B);
    NvAPI_DRS_SetSetting = (NvAPI_DRS_SetSetting_t)NvAPI_QueryInterface(0x577DD202);

    if (!NvAPI_Initialize || !NvAPI_DRS_CreateSession || !NvAPI_DRS_DestroySession ||
        !NvAPI_DRS_LoadSettings || !NvAPI_DRS_SaveSettings || !NvAPI_DRS_SetSetting) {
        FreeLibrary(hNvApi);
        return;
    }

    if (NvAPI_Initialize() != NVAPI_OK || NvAPI_DRS_CreateSession(&hSession) != NVAPI_OK) {
        FreeLibrary(hNvApi);
        return;
    }

    if (NvAPI_DRS_LoadSettings(hSession) != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        FreeLibrary(hNvApi);
        return;
    }

    if (GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        for (int i = 0; exePath[i]; i++) {
            if (exePath[i] == L'\\') exePath[i] = L'/';
            exePath[i] = towlower(exePath[i]);
        }
        memset(&app, 0, sizeof(app));
        app.version = sizeof(app) | (4 << 16);
        if (NvAPI_DRS_FindApplicationByName)
            st = NvAPI_DRS_FindApplicationByName(hSession, exePath, &hProfile, &app);
        else
            st = -1;
        if (st != NVAPI_OK && NvAPI_DRS_FindProfileByName)
            st = NvAPI_DRS_FindProfileByName(hSession, exePath, &hProfile);

        if (st == NVAPI_OK && hProfile) {
            memset(&setting, 0, sizeof(setting));
            setting.version = sizeof(setting) | (1 << 16);
            setting.settingId = PSTATE_ID;
            setting.current.u32 = ctx->nvapi_prev_mode;
            NvAPI_DRS_SetSetting(hSession, hProfile, &setting);
            NvAPI_DRS_SaveSettings(hSession);
            av_log(avctx, AV_LOG_WARNING,
                   "nvjpeg2k: " NVAPI_PREFIX_OK "NVIDIA Power Mode: Restored default power mode (%u)\n",
                   (unsigned)ctx->nvapi_prev_mode);
        }
    }

    ctx->nvapi_applied = 0;
    NvAPI_DRS_DestroySession(hSession);
    FreeLibrary(hNvApi);
}

static void free_device_mem(NVJPEG2KContext *ctx)
{
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (ctx->dev_buf[i] && ctx->fn.MemFree) {
            ctx->fn.MemFree(ctx->dev_buf[i]);
            ctx->dev_buf[i] = 0;
            ctx->dev_buf_size[i] = 0;
            ctx->dev_pitch[i] = 0;
            ctx->dev_buf_h[i] = 0;
        }
        if (ctx->host_buf[i] && ctx->fn.MemFreeHost) {
            ctx->fn.MemFreeHost(ctx->host_buf[i]);
            ctx->host_buf[i] = NULL;
            ctx->host_buf_size[i] = 0;
        }
    }
}

static uint32_t rb32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t rb64(const uint8_t *p)
{
    return ((uint64_t)rb32(p) << 32) | (uint64_t)rb32(p + 4);
}

static const uint8_t *find_codestream(const uint8_t *data, int size, int *out_size)
{
    size_t usize;
    size_t off = 0;

    if (!data || size <= 0) {
        *out_size = 0;
        return data;
    }
    *out_size = size;

    if (size >= 2 && data[0] == 0xFF && data[1] == 0x4F)
        return data;
    if (size < 12 || rb32(data) != 12 || memcmp(data + 4, "jP  ", 4) != 0)
        return data;

    usize = (size_t)size;
    while (off + 8 <= usize) {
        size_t rem = usize - off;
        uint32_t lbox = rb32(data + off);
        size_t header_len;
        size_t box_len;

        if (lbox == 1) {
            uint64_t xlbox;
            if (rem < 16)
                break;
            xlbox = rb64(data + off + 8);
            if (xlbox < 16 || xlbox > rem)
                break;
            header_len = 16;
            box_len = (size_t)xlbox;
        } else if (lbox == 0) {
            header_len = 8;
            box_len = rem;
        } else if (lbox >= 8) {
            if ((size_t)lbox > rem)
                break;
            header_len = 8;
            box_len = (size_t)lbox;
        } else {
            /* lbox in 2..7 is reserved/invalid */
            break;
        }

        if (!memcmp(data + off + 4, "jp2c", 4)) {
            size_t payload_off = off + header_len;
            size_t plen = box_len - header_len;
            if (payload_off < usize && plen > 0) {
                *out_size = (int)FFMIN(plen, (size_t)INT_MAX);
                return data + payload_off;
            }
            break;
        }

        if (lbox == 0)
            break; /* Last box in file */

        off += box_len;
    }
    return data;
}

static void parse_hints(const uint8_t *data, int size, int *rsiz, int *mct, int *bad_off, int *has_nlt3,
                        NVJPEG2KStreamSignature *sig)
{
    const uint8_t *cs;
    int cs_size, i;
    *rsiz = -1;
    *mct = -1;
    *bad_off = 0;
    *has_nlt3 = 0;
    if (sig)
        memset(sig, 0, sizeof(*sig));
    cs = find_codestream(data, size, &cs_size);
    if (cs_size < 2 || cs[0] != 0xFF || cs[1] != 0x4F)
        return;
    i = 2;
    while (i + 1 < cs_size) {
        unsigned marker;
        int len;
        if (cs[i] != 0xFF)
            return;
        while (i < cs_size && cs[i] == 0xFF)
            i++;
        if (i >= cs_size)
            return;
        marker = cs[i++];
        if (marker == 0x90 || marker == 0x93 || marker == 0xD9)
            return;
        if (i + 2 > cs_size)
            return;
        len = (cs[i] << 8) | cs[i + 1];
        if (len < 2 || i + len > cs_size)
            return;
        if (marker == 0x51 && len >= 38) {
            uint32_t x0 = rb32(cs + i + 12);
            uint32_t y0 = rb32(cs + i + 16);
            uint32_t xt0 = rb32(cs + i + 28);
            uint32_t yt0 = rb32(cs + i + 32);
            *rsiz = (cs[i + 2] << 8) | cs[i + 3];
            if (x0 || y0 || xt0 || yt0)
                *bad_off = 1;
            if (sig) {
                sig->valid = 1;
                sig->rsiz = (uint16_t)*rsiz;
                sig->width = rb32(cs + i + 4);
                sig->height = rb32(cs + i + 8);
                sig->ncomp = (cs[i + 36] << 8) | cs[i + 37];
                if (sig->ncomp > 0 && len >= 38 + 3) {
                    uint8_t ssiz = cs[i + 38];
                    sig->precision = (ssiz & 0x7F) + 1;
                    sig->sgn = (ssiz >> 7) & 1;
                    sig->sub_x = cs[i + 39];
                    sig->sub_y = cs[i + 40];
                }
                sig->bad_off = (uint8_t)*bad_off;
            }
        } else if (marker == 0x52 && len >= 7) {
            *mct = cs[i + 6];
        } else if (marker == 0x54 && len >= 4) {
            /* NLT marker segment (ISO/IEC 15444-2 Annex A.4.10) */
            /* If Cnlt is 1 byte, Tnlt is at cs[i+3]; if 2 bytes, at cs[i+4] */
            if (cs[i + 2] == 3 || (len >= 5 && cs[i + 3] == 3) || (len >= 6 && cs[i + 4] == 3))
                *has_nlt3 = 1;
            if (sig)
                sig->has_nlt3 = (uint8_t)*has_nlt3;
        }
        i += len;
    }
}

static int container_is_yuv(AVCodecContext *avctx)
{
    switch (avctx->colorspace) {
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_YCGCO:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
    case AVCOL_SPC_ICTCP:
        return 1;
    default:
        return 0;
    }
}

static int cinema_rsiz(int rsiz)
{
    int p = rsiz & 0x3fff;
    return p == AV_PROFILE_JPEG2000_DCINEMA_2K ||
           p == AV_PROFILE_JPEG2000_DCINEMA_4K;
}

static enum NVColor classify_color(AVCodecContext *avctx, nvjpeg2kColorSpace_t jp2_cs,
                                   uint32_t ncomp, int prec, int rsiz, int mct)
{
    if (ncomp == 3 && prec == 12 && cinema_rsiz(rsiz))
        return NVCOL_XYZ;
    if (jp2_cs == NVJPEG2K_COLORSPACE_SRGB)
        return NVCOL_RGB;
    if (jp2_cs == NVJPEG2K_COLORSPACE_SYCC)
        return NVCOL_YUV;
    if (mct > 0)
        return NVCOL_RGB;
    if (container_is_yuv(avctx))
        return NVCOL_YUV;
    return NVCOL_RGB;
}

static void set_color_metadata(AVCodecContext *avctx, AVFrame *frame,
                               const AVPixFmtDescriptor *desc,
                               enum NVColor col, nvjpeg2kColorSpace_t jp2_cs,
                               uint32_t ncomp)
{
    /* 1. Digital Cinema XYZ: strictly SMPTE 428-1, ignore any conflicting avctx values */
    if (col == NVCOL_XYZ || frame->format == AV_PIX_FMT_XYZ12LE) {
        frame->color_primaries = AVCOL_PRI_SMPTE428;
        frame->color_trc       = AVCOL_TRC_SMPTE428;
        frame->colorspace      = AVCOL_SPC_RGB;
        frame->color_range     = AVCOL_RANGE_JPEG;
        frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
        return;
    }

    /* 2. Monochrome / Gray */
    if (ncomp == 1) {
        frame->colorspace      = AVCOL_SPC_UNSPECIFIED;
        frame->color_range     = (avctx->color_range != AVCOL_RANGE_UNSPECIFIED) ?
                                 avctx->color_range : AVCOL_RANGE_JPEG;
        frame->color_primaries = avctx->color_primaries;
        frame->color_trc       = (jp2_cs == NVJPEG2K_COLORSPACE_GRAY && avctx->color_trc == AVCOL_TRC_UNSPECIFIED) ?
                                 AVCOL_TRC_IEC61966_2_1 : avctx->color_trc;
        frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
        return;
    }

    /* 3. RGB family */
    if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
        if (jp2_cs == NVJPEG2K_COLORSPACE_SRGB) {
            frame->colorspace      = AVCOL_SPC_RGB;
            frame->color_range     = (avctx->color_range != AVCOL_RANGE_UNSPECIFIED) ?
                                     avctx->color_range : AVCOL_RANGE_JPEG;
            frame->color_primaries = (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED) ?
                                     avctx->color_primaries : AVCOL_PRI_BT709;
            frame->color_trc       = (avctx->color_trc != AVCOL_TRC_UNSPECIFIED) ?
                                     avctx->color_trc : AVCOL_TRC_IEC61966_2_1;
        } else {
            /* Unknown / raw J2K / not supported:
             * Do not invent display colorimetry. For colorspace, never apply a YUV matrix to RGB format. */
            frame->colorspace      = (avctx->colorspace == AVCOL_SPC_RGB) ?
                                     AVCOL_SPC_RGB : AVCOL_SPC_UNSPECIFIED;
            frame->color_range     = avctx->color_range;
            frame->color_primaries = avctx->color_primaries;
            frame->color_trc       = avctx->color_trc;
        }
        return;
    }

    /* 4. YUV / other formats */
    if (jp2_cs == NVJPEG2K_COLORSPACE_SYCC) {
        /* Per ITU-T T.800 Annex I.7.3.2: sYCC uses BT.601 matrix (SMPTE 170M), full range, BT.709 primaries, sRGB transfer */
        frame->colorspace      = (avctx->colorspace != AVCOL_SPC_UNSPECIFIED) ?
                                 avctx->colorspace : AVCOL_SPC_SMPTE170M;
        frame->color_range     = (avctx->color_range != AVCOL_RANGE_UNSPECIFIED) ?
                                 avctx->color_range : AVCOL_RANGE_JPEG;
        frame->color_primaries = (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED) ?
                                 avctx->color_primaries : AVCOL_PRI_BT709;
        frame->color_trc       = (avctx->color_trc != AVCOL_TRC_UNSPECIFIED) ?
                                 avctx->color_trc : AVCOL_TRC_IEC61966_2_1;
        /* T.800 does not specify chroma sample location; never force LEFT */
        frame->chroma_location = avctx->chroma_sample_location;
    } else {
        /* Unknown / raw J2K / not supported: inherit from avctx without guessing or forcing defaults */
        frame->colorspace      = avctx->colorspace;
        frame->color_range     = avctx->color_range;
        frame->color_primaries = avctx->color_primaries;
        frame->color_trc       = avctx->color_trc;
        frame->chroma_location = avctx->chroma_sample_location;
    }
}

static enum AVPixelFormat pick_depth(int prec,
                                    enum AVPixelFormat f8, enum AVPixelFormat f10,
                                    enum AVPixelFormat f12, enum AVPixelFormat f16)
{
    if (prec <= 8)
        return f8;
    if (prec <= 10)
        return f10;
    if (prec <= 12)
        return f12 != AV_PIX_FMT_NONE ? f12 : f16;
    return f16;
}

static enum AVPixelFormat select_pixel_format(uint32_t ncomp,
                                             const nvjpeg2kImageComponentInfo_t comp[],
                                             enum NVColor col, int *plane_map)
{
    uint32_t w0, h0, w1, h1, w2, h2, w3, h3;
    int prec;

    if (ncomp < 1 || ncomp > 4)
        return AV_PIX_FMT_NONE;
    prec = comp[0].precision;
    w0 = comp[0].component_width;
    h0 = comp[0].component_height;

    if (ncomp == 1) {
        plane_map[0] = 0;
        return pick_depth(prec, AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10LE,
                          AV_PIX_FMT_GRAY12LE, AV_PIX_FMT_GRAY16LE);
    }

    w1 = comp[1].component_width;
    h1 = comp[1].component_height;

    if (ncomp == 2) {
        if (w1 != w0 || h1 != h0)
            return AV_PIX_FMT_NONE;
        plane_map[0] = 0;
        plane_map[1] = 1;
        return prec <= 8 ? AV_PIX_FMT_YA8 : AV_PIX_FMT_YA16LE;
    }

    w2 = comp[2].component_width;
    h2 = comp[2].component_height;

    if (ncomp == 3 && prec == 12 && col == NVCOL_XYZ &&
        w1 == w0 && h1 == h0 && w2 == w0 && h2 == h0) {
        plane_map[0] = 0;
        plane_map[1] = 1;
        plane_map[2] = 2;
        return AV_PIX_FMT_XYZ12LE;
    }

    if (ncomp == 4) {
        w3 = comp[3].component_width;
        h3 = comp[3].component_height;
        if (w1 == w0 && h1 == h0 && w2 == w0 && h2 == h0 && w3 == w0 && h3 == h0) {
            if (col == NVCOL_YUV) {
                plane_map[0] = 0;
                plane_map[1] = 1;
                plane_map[2] = 2;
                plane_map[3] = 3;
                return pick_depth(prec, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA444P10LE,
                                  AV_PIX_FMT_YUVA444P12LE, AV_PIX_FMT_YUVA444P16LE);
            }
            plane_map[0] = 2;
            plane_map[1] = 0;
            plane_map[2] = 1;
            plane_map[3] = 3;
            return pick_depth(prec, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10LE,
                              AV_PIX_FMT_GBRAP12LE, AV_PIX_FMT_GBRAP16LE);
        }
        if (w3 != w0 || h3 != h0 || w1 != w2 || h1 != h2)
            return AV_PIX_FMT_NONE;
        plane_map[0] = 0;
        plane_map[1] = 1;
        plane_map[2] = 2;
        plane_map[3] = 3;
        if (w1 == (w0 + 1) / 2 && h1 == (h0 + 1) / 2)
            return pick_depth(prec, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA420P10LE,
                              AV_PIX_FMT_NONE, AV_PIX_FMT_YUVA420P16LE);
        if (w1 == (w0 + 1) / 2 && h1 == h0)
            return pick_depth(prec, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA422P10LE,
                              AV_PIX_FMT_YUVA422P12LE, AV_PIX_FMT_YUVA422P16LE);
        return AV_PIX_FMT_NONE;
    }

    if (w1 != w2 || h1 != h2)
        return AV_PIX_FMT_NONE;

    if (w1 < w0 || h1 < h0 || w2 < w0 || h2 < h0) {
        plane_map[0] = 0;
        plane_map[1] = 1;
        plane_map[2] = 2;
        if (w1 == (w0 + 1) / 2 && h1 == (h0 + 1) / 2)
            return pick_depth(prec, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE,
                              AV_PIX_FMT_YUV420P12LE, AV_PIX_FMT_YUV420P16LE);
        if (w1 == (w0 + 1) / 2 && h1 == h0)
            return pick_depth(prec, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV422P10LE,
                              AV_PIX_FMT_YUV422P12LE, AV_PIX_FMT_YUV422P16LE);
        if (w1 == w0 && h1 == (h0 + 1) / 2)
            return pick_depth(prec, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV440P10LE,
                              AV_PIX_FMT_YUV440P12LE, AV_PIX_FMT_NONE);
        return AV_PIX_FMT_NONE;
    }

    if (w1 != w0 || h1 != h0)
        return AV_PIX_FMT_NONE;

    if (col == NVCOL_YUV) {
        plane_map[0] = 0;
        plane_map[1] = 1;
        plane_map[2] = 2;
        return pick_depth(prec, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10LE,
                          AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_YUV444P16LE);
    }
    plane_map[0] = 2;
    plane_map[1] = 0;
    plane_map[2] = 1;
    return pick_depth(prec, AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP10LE,
                      AV_PIX_FMT_GBRP12LE, AV_PIX_FMT_GBRP16LE);
}

static int check_components(AVCodecContext *avctx, const nvjpeg2kImageInfo_t *info,
                            const nvjpeg2kImageComponentInfo_t comp[], uint32_t ncomp)
{
    int prec;
    if (ncomp < 1 || ncomp > 4) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: unsupported component count %u\n", ncomp);
        return AVERROR_PATCHWELCOME;
    }
    if (!info->image_width || !info->image_height)
        return AVERROR_INVALIDDATA;
    prec = comp[0].precision;
    if (prec < 1 || prec > 16 || comp[0].sgn) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: unsupported precision %d sgn %u\n", prec, comp[0].sgn);
        return AVERROR_PATCHWELCOME;
    }
    if (comp[0].component_width != info->image_width ||
        comp[0].component_height != info->image_height)
        return AVERROR_PATCHWELCOME;
    for (uint32_t c = 0; c < ncomp; c++) {
        if (!comp[c].component_width || !comp[c].component_height)
            return AVERROR_INVALIDDATA;
        if (comp[c].sgn || comp[c].precision != prec) {
            av_log(avctx, AV_LOG_WARNING,
                   "nvjpeg2k: component %u precision/sign differs\n", c);
            return AVERROR_PATCHWELCOME;
        }
    }
    return 0;
}

static int ensure_buffers(AVCodecContext *avctx, NVJPEG2KContext *ctx,
                          const nvjpeg2kImageComponentInfo_t comp[],
                          uint32_t ncomp, int bpp)
{
    CUdeviceptr new_dev[MAX_COMPONENTS] = { 0 };
    size_t new_pitch[MAX_COMPONENTS] = { 0 };
    size_t new_dev_size[MAX_COMPONENTS] = { 0 };
    size_t new_dev_h[MAX_COMPONENTS] = { 0 };
    void *new_host[MAX_COMPONENTS] = { NULL };
    size_t new_host_size[MAX_COMPONENTS] = { 0 };
    int needs_new_dev[MAX_COMPONENTS] = { 0 };
    int needs_new_host[MAX_COMPONENTS] = { 0 };
    int ret = 0;

    for (uint32_t c = 0; c < ncomp; c++) {
        uint32_t w = comp[c].component_width;
        uint32_t h = comp[c].component_height;
        size_t width_bytes;
        size_t pitch = 0;
        size_t total = 0;
        size_t needed_host;

        if (!w || !h || (size_t)bpp > SIZE_MAX / w) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
        width_bytes = (size_t)w * (size_t)bpp;
        if (width_bytes > SIZE_MAX / 4) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        /* Check if existing dev_buf can be reused */
        if (ctx->dev_buf[c] && width_bytes <= ctx->dev_pitch[c] &&
            (size_t)h <= ctx->dev_buf_h[c]) {
            needs_new_dev[c] = 0;
            pitch = ctx->dev_pitch[c];
        } else {
            CUdeviceptr dev = 0;
            CUresult cr;

            needs_new_dev[c] = 1;
            if (ctx->fn.MemAllocPitch) {
                cr = ctx->fn.MemAllocPitch(&dev, &pitch, width_bytes, h, 4);
                if (cr != CUDA_SUCCESS || !dev || pitch < width_bytes) {
                    if (dev && ctx->fn.MemFree)
                        ctx->fn.MemFree(dev);
                    dev = 0;
                }
            }
            if (!dev) {
                pitch = (width_bytes + 255u) & ~(size_t)255;
                if (pitch < width_bytes || pitch > SIZE_MAX / h) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                total = pitch * (size_t)h;
                if (!ctx->fn.MemAlloc) {
                    ret = AVERROR(ENOSYS);
                    goto fail;
                }
                cr = ctx->fn.MemAlloc(&dev, total);
                if (cr != CUDA_SUCCESS || !dev) {
                    av_log(avctx, AV_LOG_ERROR,
                           "nvjpeg2k: cuMemAlloc failed (%d)\n", (int)cr);
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            } else {
                if (pitch > SIZE_MAX / h) {
                    if (ctx->fn.MemFree)
                        ctx->fn.MemFree(dev);
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                total = pitch * (size_t)h;
            }

            new_dev[c] = dev;
            new_pitch[c] = pitch;
            new_dev_size[c] = total;
            new_dev_h[c] = h;
        }

        /* Check host buffer requirements */
        if (pitch > SIZE_MAX / h) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        needed_host = pitch * (size_t)h;

        if (ctx->host_buf[c] && ctx->host_buf_size[c] >= needed_host) {
            needs_new_host[c] = 0;
        } else {
            void *host = NULL;
            CUresult cr;

            needs_new_host[c] = 1;
            if (!ctx->fn.MemAllocHost) {
                ret = AVERROR(ENOSYS);
                goto fail;
            }
            cr = ctx->fn.MemAllocHost(&host, needed_host);
            if (cr != CUDA_SUCCESS || !host) {
                av_log(avctx, AV_LOG_ERROR,
                       "nvjpeg2k: cuMemAllocHost failed (%d)\n", (int)cr);
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            new_host[c] = host;
            new_host_size[c] = needed_host;
        }
    }

    /* All allocations succeeded! Commit new buffers and free superseded ones. */
    for (uint32_t c = 0; c < ncomp; c++) {
        if (needs_new_dev[c]) {
            if (ctx->dev_buf[c] && ctx->fn.MemFree)
                ctx->fn.MemFree(ctx->dev_buf[c]);
            ctx->dev_buf[c] = new_dev[c];
            ctx->dev_pitch[c] = new_pitch[c];
            ctx->dev_buf_size[c] = new_dev_size[c];
            ctx->dev_buf_h[c] = new_dev_h[c];
        }
        if (needs_new_host[c]) {
            if (ctx->host_buf[c] && ctx->fn.MemFreeHost)
                ctx->fn.MemFreeHost(ctx->host_buf[c]);
            ctx->host_buf[c] = new_host[c];
            ctx->host_buf_size[c] = new_host_size[c];
        }
    }

    /* Free unused components if ncomp decreased */
    for (uint32_t c = ncomp; c < MAX_COMPONENTS; c++) {
        if (ctx->dev_buf[c]) {
            if (ctx->fn.MemFree)
                ctx->fn.MemFree(ctx->dev_buf[c]);
            ctx->dev_buf[c] = 0;
            ctx->dev_buf_size[c] = 0;
            ctx->dev_pitch[c] = 0;
            ctx->dev_buf_h[c] = 0;
        }
        if (ctx->host_buf[c]) {
            if (ctx->fn.MemFreeHost)
                ctx->fn.MemFreeHost(ctx->host_buf[c]);
            ctx->host_buf[c] = NULL;
            ctx->host_buf_size[c] = 0;
        }
    }

    return 0;

fail:
    /* Clean up any freshly allocated replacement buffers; keep old ones intact */
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (new_dev[i] && ctx->fn.MemFree)
            ctx->fn.MemFree(new_dev[i]);
        if (new_host[i] && ctx->fn.MemFreeHost)
            ctx->fn.MemFreeHost(new_host[i]);
    }
    return ret;
}

static int sw_fallback(AVCodecContext *avctx, NVJPEG2KContext *ctx,
                       AVFrame *frame, AVPacket *avpkt)
{
    const AVCodec *codec;
    int ret;

    if (!ctx->sw) {
        codec = avcodec_find_decoder_by_name("jpeg2000");
        if (!codec)
            return AVERROR_DECODER_NOT_FOUND;
        ctx->sw = avcodec_alloc_context3(codec);
        if (!ctx->sw)
            return AVERROR(ENOMEM);
        ctx->sw->flags = avctx->flags;
        ctx->sw->flags2 = avctx->flags2;
        ctx->sw->width = avctx->width;
        ctx->sw->height = avctx->height;
        ctx->sw->pix_fmt = AV_PIX_FMT_NONE;
        ctx->sw->color_primaries = avctx->color_primaries;
        ctx->sw->color_trc = avctx->color_trc;
        ctx->sw->colorspace = avctx->colorspace;
        ctx->sw->color_range = avctx->color_range;
        ctx->sw->chroma_sample_location = avctx->chroma_sample_location;
        ctx->sw->pkt_timebase = avctx->pkt_timebase;
        ctx->sw->max_pixels = avctx->max_pixels;
        ctx->sw->thread_count = 1;
        ret = avcodec_open2(ctx->sw, codec, NULL);
        if (ret < 0) {
            avcodec_free_context(&ctx->sw);
            return ret;
        }
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: stream is not supported on the GPU, using software jpeg2000\n");
    }

    av_frame_unref(frame);
    ret = avcodec_send_packet(ctx->sw, avpkt);
    if (ret < 0)
        return ret;
    ret = avcodec_receive_frame(ctx->sw, frame);
    if (ret < 0)
        return ret;

    avctx->pix_fmt = frame->format;
    avctx->width = frame->width;
    avctx->height = frame->height;
    avctx->profile = ctx->sw->profile;
    avctx->bits_per_raw_sample = ctx->sw->bits_per_raw_sample;

    ret = ff_attach_decode_data(avctx, frame);
    if (ret < 0)
        return ret;

    return avpkt->size;
}

static void store_sample(uint8_t *dst, int hi, unsigned v, int shift)
{
    if (shift > 0)
        v <<= shift;
    else if (shift < 0)
        v >>= -shift;
    if (hi) {
        uint16_t *d = (uint16_t *)dst;
        *d = (uint16_t)v;
    } else {
        *dst = (uint8_t)v;
    }
}

static int pack_plane(const AVPixFmtDescriptor *desc, AVFrame *frame,
                      int plane, const void *src, size_t src_pitch, int bpp,
                      uint32_t W, uint32_t H, int prec)
{
    int pw, ph, depth, shift, hi, dst_b, cw = 0, ch = 0;
    uint8_t *dst;
    int dstride;
    const uint8_t *sbase;

    if (plane < 0 || plane >= desc->nb_components || !frame->data[plane])
        return AVERROR_INVALIDDATA;
    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB) && plane > 0 && plane < 3) {
        cw = desc->log2_chroma_w;
        ch = desc->log2_chroma_h;
    }
    pw = AV_CEIL_RSHIFT(frame->width, cw);
    ph = AV_CEIL_RSHIFT(frame->height, ch);
    if ((int)W != pw || (int)H != ph)
        return AVERROR_PATCHWELCOME;
    depth = desc->comp[plane].depth;
    shift = depth - prec;
    if (shift >= 32 || shift <= -32)
        return AVERROR_PATCHWELCOME;
    hi = depth > 8;
    dst_b = hi ? 2 : 1;
    dst = frame->data[plane];
    dstride = frame->linesize[plane];
    sbase = src;
    if (shift == 0) {
        av_image_copy_plane(dst, dstride, sbase, (int)src_pitch,
                            (int)W * dst_b, (int)H);
        return 0;
    }
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *d = dst + (size_t)y * dstride;
        const uint8_t *s = sbase + (size_t)y * src_pitch;
        for (uint32_t x = 0; x < W; x++) {
            unsigned v = bpp == 1 ? s[x] : ((const uint16_t *)s)[x];
            v &= (prec >= 16) ? 0xFFFFu : ((1u << prec) - 1u);
            store_sample(d + (size_t)x * dst_b, hi, v, shift);
        }
    }
    return 0;
}

static int pack_frame(AVCodecContext *avctx, NVJPEG2KContext *ctx, AVFrame *frame,
                      enum AVPixelFormat pix_fmt, const int *plane_map, uint32_t ncomp,
                      const nvjpeg2kImageComponentInfo_t comp[], int bpp,
                      enum NVColor col, nvjpeg2kColorSpace_t jp2_cs)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int ret;

    if (!desc)
        return AVERROR_INVALIDDATA;
    if (pix_fmt == AV_PIX_FMT_XYZ12LE) {
        uint32_t W = comp[0].component_width;
        uint32_t H = comp[0].component_height;
        int dstride = frame->linesize[0];
        if ((int)W != frame->width || (int)H != frame->height || !frame->data[0])
            return AVERROR_PATCHWELCOME;
        for (uint32_t y = 0; y < H; y++) {
            uint16_t *d = (uint16_t *)(frame->data[0] + (size_t)y * dstride);
            const uint16_t *rx = (const uint16_t *)((uint8_t *)ctx->host_buf[0] + (size_t)y * ctx->dev_pitch[0]);
            const uint16_t *ry = (const uint16_t *)((uint8_t *)ctx->host_buf[1] + (size_t)y * ctx->dev_pitch[1]);
            const uint16_t *rz = (const uint16_t *)((uint8_t *)ctx->host_buf[2] + (size_t)y * ctx->dev_pitch[2]);
            for (uint32_t x = 0; x < W; x++) {
                d[3 * x + 0] = (uint16_t)((rx[x] & 0x0FFF) << 4);
                d[3 * x + 1] = (uint16_t)((ry[x] & 0x0FFF) << 4);
                d[3 * x + 2] = (uint16_t)((rz[x] & 0x0FFF) << 4);
            }
        }
        set_color_metadata(avctx, frame, desc, col, jp2_cs, ncomp);
        return 0;
    }

    for (uint32_t c = 0; c < ncomp; c++) {
        ret = pack_plane(desc, frame, plane_map[c], ctx->host_buf[c],
                         ctx->dev_pitch[c], bpp,
                         comp[c].component_width, comp[c].component_height,
                         comp[c].precision);
        if (ret < 0)
            return ret;
    }
    set_color_metadata(avctx, frame, desc, col, jp2_cs, ncomp);
    return 0;
}

static av_cold int nvjpeg2k_decode_init(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;
    nvjpeg2kStatus_t st;
    CUresult cr;
    CUcontext saved = NULL;
    int major = -1, minor = -1, patch = -1;
    int switched = -1;

    ctx->nvjpeg2k_dll = load_nvjpeg_dll();
    if (!ctx->nvjpeg2k_dll) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: nvjpeg2k_0.dll not found\n");
        return AVERROR_DECODER_NOT_FOUND;
    }
    ctx->nvcuda_dll = LoadLibraryExW(L"nvcuda.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ctx->nvcuda_dll) {
        av_log(avctx, AV_LOG_INFO, "nvjpeg2k: nvcuda.dll not found\n");
        return AVERROR_DECODER_NOT_FOUND;
    }

    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", CreateSimple, "nvjpeg2kCreateSimple");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", Destroy, "nvjpeg2kDestroy");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", DecodeStateCreate, "nvjpeg2kDecodeStateCreate");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", DecodeStateDestroy, "nvjpeg2kDecodeStateDestroy");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamCreate, "nvjpeg2kStreamCreate");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamDestroy, "nvjpeg2kStreamDestroy");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamParse, "nvjpeg2kStreamParse");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamGetImageInfo, "nvjpeg2kStreamGetImageInfo");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamGetImageComponentInfo, "nvjpeg2kStreamGetImageComponentInfo");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", Decode, "nvjpeg2kDecode");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", GetProperty, "nvjpeg2kGetProperty");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamGetColorSpace, "nvjpeg2kStreamGetColorSpace");
    load_sym(avctx, ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll",
             "nvjpeg2kStreamGetComponentNLTInfo",
             (void **)&ctx->fn.StreamGetComponentNLTInfo);

    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", Init, "cuInit");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", DeviceGet, "cuDeviceGet");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", PrimaryCtxRetain, "cuDevicePrimaryCtxRetain");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", PrimaryCtxRelease, "cuDevicePrimaryCtxRelease_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", CtxGetCurrent, "cuCtxGetCurrent");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", CtxSetCurrent, "cuCtxSetCurrent");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemAlloc, "cuMemAlloc_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemAllocPitch, "cuMemAllocPitch_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemFree, "cuMemFree_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemAllocHost, "cuMemAllocHost_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemFreeHost, "cuMemFreeHost");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemcpyDtoHAsync, "cuMemcpyDtoHAsync_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", CtxSynchronize, "cuCtxSynchronize");

    if (ctx->fn.GetProperty(NVJPEG2K_LIBRARY_MAJOR_VERSION, &major) != NVJPEG2K_STATUS_SUCCESS ||
        ctx->fn.GetProperty(NVJPEG2K_LIBRARY_MINOR_VERSION, &minor) != NVJPEG2K_STATUS_SUCCESS ||
        ctx->fn.GetProperty(NVJPEG2K_LIBRARY_PATCH_LEVEL, &patch) != NVJPEG2K_STATUS_SUCCESS ||
        major < 0 || minor < 0 || major > 32 || minor > 99 ||
        (major == 0 && minor < 8)) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: need nvJPEG2000 >= 0.8, got %d.%d.%d\n", major, minor, patch);
        return AVERROR_DECODER_NOT_FOUND;
    }

    cr = ctx->fn.Init(0);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuInit failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }
    cr = ctx->fn.DeviceGet(&ctx->cuda_dev, ctx->gpu_device);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: CUDA device %d is not available (%d)\n", ctx->gpu_device, (int)cr);
        return AVERROR_DECODER_NOT_FOUND;
    }
    cr = ctx->fn.PrimaryCtxRetain(&ctx->cuda_ctx, ctx->cuda_dev);
    if (cr != CUDA_SUCCESS || !ctx->cuda_ctx) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuDevicePrimaryCtxRetain failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }
    switched = cuda_enter(avctx, ctx, &saved);
    if (switched < 0)
        return AVERROR_EXTERNAL;

    st = ctx->fn.CreateSimple(&ctx->handle);
    if (st == NVJPEG2K_STATUS_ARCH_MISMATCH) {
        av_log(avctx, AV_LOG_INFO, "nvjpeg2k: GPU architecture is not supported\n");
        cuda_leave(ctx, saved, switched);
        return AVERROR_DECODER_NOT_FOUND;
    }
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: CreateSimple failed (%d)\n", (int)st);
        cuda_leave(ctx, saved, switched);
        return AVERROR_EXTERNAL;
    }
    st = ctx->fn.DecodeStateCreate(ctx->handle, &ctx->decode_state);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: DecodeStateCreate failed (%d)\n", (int)st);
        cuda_leave(ctx, saved, switched);
        return AVERROR_EXTERNAL;
    }
    st = ctx->fn.StreamCreate(&ctx->jpeg_stream);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: StreamCreate failed (%d)\n", (int)st);
        cuda_leave(ctx, saved, switched);
        return AVERROR_EXTERNAL;
    }
    cuda_leave(ctx, saved, switched);
    av_log(avctx, AV_LOG_INFO,
           "nvjpeg2k: GPU JPEG 2000 decoder ready (nvJPEG2000 %d.%d.%d, CUDA device %d)\n",
           major, minor, patch, ctx->gpu_device);

    /* Elevate GPU Power Management Mode via NVAPI DRS */
    nvapi_apply_pstate(avctx, ctx, ctx->nvapi_pstate_mode);
    return 0;
}

static int nvjpeg2k_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                 int *got_frame, AVPacket *avpkt)
{
    NVJPEG2KContext *ctx = avctx->priv_data;
    nvjpeg2kStatus_t st;
    nvjpeg2kImageInfo_t img_info;
    nvjpeg2kImageComponentInfo_t comp_info[MAX_COMPONENTS];
    nvjpeg2kImage_t decoded_img;
    void *pixel_data[MAX_COMPONENTS];
    size_t pitch_arr[MAX_COMPONENTS];
    int plane_map[MAX_COMPONENTS] = { 0, 1, 2, 3 };
    int ret = 0, switched = -1, fallback = 0, sticky = 0, have_meta = 0;
    CUcontext saved = NULL;
    int rsiz = -1, mct = -1, bad_off = 0, has_nlt3 = 0, bpp, prec;
    uint32_t ncomp;
    enum AVPixelFormat pix_fmt;
    enum NVColor col;
    CUresult cr;
    NVJPEG2KStreamSignature sig;

    *got_frame = 0;
    if (!avpkt->size)
        return 0;

    parse_hints(avpkt->data, avpkt->size, &rsiz, &mct, &bad_off, &has_nlt3, &sig);

    if (ctx->has_unsupported_sig && sig.valid) {
        if (sig_matches(&ctx->unsupported_sig, &sig)) {
            ret = sw_fallback(avctx, ctx, frame, avpkt);
            if (ret >= 0)
                *got_frame = 1;
            return ret;
        }
        /* Signature changed: reset sticky state and try GPU decode on the new stream */
        ctx->has_unsupported_sig = 0;
        ctx->force_sw = 0;
    } else if (ctx->force_sw && !sig.valid) {
        ret = sw_fallback(avctx, ctx, frame, avpkt);
        if (ret >= 0)
            *got_frame = 1;
        return ret;
    }

    if ((switched = cuda_enter(avctx, ctx, &saved)) < 0)
        return switched;

    if (bad_off) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: non-zero image/tile offset is not supported on the GPU\n");
        fallback = 1;
        sticky = 1;
        goto done;
    }
    if (has_nlt3) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: NLT type 3 marker present in codestream, using software fallback\n");
        fallback = 1;
        sticky = 1;
        goto done;
    }

    st = ctx->fn.StreamParse(ctx->handle, avpkt->data, (size_t)avpkt->size,
                             1, 0, ctx->jpeg_stream);
    if (st == NVJPEG2K_STATUS_SUCCESS) {
        have_meta = 1;
    } else {
        st = ctx->fn.StreamParse(ctx->handle, avpkt->data, (size_t)avpkt->size,
                                 0, 0, ctx->jpeg_stream);
    }
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_WARNING, "nvjpeg2k: StreamParse failed (%d)\n", (int)st);
        if (status_is_unsupported(st)) {
            fallback = 1;
            sticky = 1;
        } else if (status_is_corrupt(st)) {
            fallback = 1;
            sticky = 0;
        } else {
            ret = AVERROR_EXTERNAL;
        }
        goto done;
    }

    st = ctx->fn.StreamGetImageInfo(ctx->jpeg_stream, &img_info);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        if (status_is_unsupported(st)) {
            fallback = 1;
            sticky = 1;
        } else if (status_is_corrupt(st)) {
            fallback = 1;
            sticky = 0;
        } else {
            av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: StreamGetImageInfo failed (%d)\n", (int)st);
            ret = AVERROR_INVALIDDATA;
        }
        goto done;
    }
    sig.width = img_info.image_width;
    sig.height = img_info.image_height;
    sig.ncomp = img_info.num_components;
    sig.valid = 1;

    ncomp = img_info.num_components;
    if (ncomp > MAX_COMPONENTS) {
        fallback = 1;
        sticky = 1;
        goto done;
    }
    for (uint32_t c = 0; c < ncomp; c++) {
        st = ctx->fn.StreamGetImageComponentInfo(ctx->jpeg_stream, &comp_info[c], c);
        if (st != NVJPEG2K_STATUS_SUCCESS) {
            ret = AVERROR_INVALIDDATA;
            goto done;
        }
    }
    ret = check_components(avctx, &img_info, comp_info, ncomp);
    if (ret == AVERROR_PATCHWELCOME) {
        fallback = 1;
        sticky = 1;
        ret = 0;
        goto done;
    }
    if (ret < 0)
        goto done;

    if (ctx->fn.StreamGetComponentNLTInfo) {
        for (uint32_t c = 0; c < ncomp; c++) {
            nvjpeg2kNLTInfo_t nlt_info;
            memset(&nlt_info, 0, sizeof(nlt_info));
            st = ctx->fn.StreamGetComponentNLTInfo(ctx->jpeg_stream, c, &nlt_info);
            if (st == NVJPEG2K_STATUS_SUCCESS && nlt_info.type == NVJPEG2K_NLT_TYPE3) {
                av_log(avctx, AV_LOG_INFO,
                       "nvjpeg2k: component %u uses NLT type 3 (floating-point), "
                       "using software fallback\n", c);
                fallback = 1;
                sticky = 1;
                sig.has_nlt3 = 1;
                goto done;
            }
        }
    }

    nvjpeg2kColorSpace_t jp2_cs = NVJPEG2K_COLORSPACE_UNKNOWN;
    if (have_meta && ctx->fn.StreamGetColorSpace) {
        st = ctx->fn.StreamGetColorSpace(ctx->jpeg_stream, &jp2_cs);
        if (st != NVJPEG2K_STATUS_SUCCESS)
            jp2_cs = NVJPEG2K_COLORSPACE_UNKNOWN;
    }

    prec = comp_info[0].precision;
    sig.precision = (uint8_t)prec;
    sig.sgn = comp_info[0].sgn;

    col = classify_color(avctx, jp2_cs, ncomp, prec, rsiz, mct);
    pix_fmt = select_pixel_format(ncomp, comp_info, col, plane_map);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_INFO, "nvjpeg2k: pixel format is not supported on the GPU\n");
        fallback = 1;
        sticky = 1;
        goto done;
    }
    if (cinema_rsiz(rsiz))
        avctx->profile = rsiz & 0x3fff;

    ret = av_image_check_size2((int)img_info.image_width, (int)img_info.image_height,
                               avctx->max_pixels, pix_fmt, 0, avctx);
    if (ret < 0)
        goto done;

    bpp = prec <= 8 ? 1 : 2;
    ret = ensure_buffers(avctx, ctx, comp_info, ncomp, bpp);
    if (ret < 0)
        goto done;

    for (uint32_t c = 0; c < ncomp; c++) {
        pixel_data[c] = (void *)(uintptr_t)ctx->dev_buf[c];
        pitch_arr[c] = ctx->dev_pitch[c];
    }
    decoded_img.pixel_data = pixel_data;
    decoded_img.pitch_in_bytes = pitch_arr;
    decoded_img.pixel_type = bpp == 1 ? NVJPEG2K_UINT8 : NVJPEG2K_UINT16;
    decoded_img.num_components = ncomp;

    st = ctx->fn.Decode(ctx->handle, ctx->decode_state, ctx->jpeg_stream,
                        &decoded_img, NULL);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_WARNING, "nvjpeg2k: Decode failed (%d)\n", (int)st);
        if (status_is_unsupported(st)) {
            fallback = 1;
            sticky = 1;
        } else if (status_is_corrupt(st)) {
            fallback = 1;
            sticky = 0;
        } else {
            ret = AVERROR_EXTERNAL;
        }
        goto done;
    }

    for (uint32_t c = 0; c < ncomp; c++) {
        size_t total = ctx->dev_pitch[c] * comp_info[c].component_height;
        cr = ctx->fn.MemcpyDtoHAsync(ctx->host_buf[c], ctx->dev_buf[c], total, NULL);
        if (cr != CUDA_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuMemcpyDtoHAsync failed (%d)\n", (int)cr);
            ret = AVERROR_EXTERNAL;
            goto done;
        }
    }
    cr = ctx->fn.CtxSynchronize();
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuCtxSynchronize failed (%d)\n", (int)cr);
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    ret = ff_set_dimensions(avctx, (int)img_info.image_width, (int)img_info.image_height);
    if (ret < 0)
        goto done;
    avctx->pix_fmt = pix_fmt;
    avctx->bits_per_raw_sample = prec;
    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        goto done;
    ret = pack_frame(avctx, ctx, frame, pix_fmt, plane_map, ncomp, comp_info, bpp, col, jp2_cs);
    if (ret == AVERROR_PATCHWELCOME) {
        av_frame_unref(frame);
        fallback = 1;
        sticky = 1;
        ret = 0;
        goto done;
    }
    if (ret < 0) {
        av_frame_unref(frame);
        goto done;
    }

    frame->flags |= AV_FRAME_FLAG_KEY;
    frame->pict_type = AV_PICTURE_TYPE_I;
    *got_frame = 1;
    ret = avpkt->size;

done:
    cuda_leave(ctx, saved, switched);
    if (fallback) {
        if (sticky && sig.valid) {
            ctx->unsupported_sig = sig;
            ctx->has_unsupported_sig = 1;
            ctx->force_sw = 1;
        }
        ret = sw_fallback(avctx, ctx, frame, avpkt);
        if (ret >= 0)
            *got_frame = 1;
        else
            *got_frame = 0;
    }
    return ret;
}

static void nvjpeg2k_decode_flush(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;
    if (ctx->sw)
        avcodec_flush_buffers(ctx->sw);
}

static av_cold int nvjpeg2k_decode_close(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;
    CUcontext saved = NULL;
    int switched = -1;
    int in_context = 0;

    avcodec_free_context(&ctx->sw);

    if (ctx->cuda_ctx) {
        switched = cuda_enter(avctx, ctx, &saved);
        if (switched >= 0)
            in_context = 1;
        else
            av_log(avctx, AV_LOG_WARNING,
                   "nvjpeg2k: unable to enter CUDA context for resource destruction\n");
    }

    if (in_context) {
        if (ctx->jpeg_stream && ctx->fn.StreamDestroy) {
            nvjpeg2kStatus_t st = ctx->fn.StreamDestroy(ctx->jpeg_stream);
            if (st != NVJPEG2K_STATUS_SUCCESS)
                av_log(avctx, AV_LOG_WARNING, "nvjpeg2k: StreamDestroy failed (%d)\n", (int)st);
            ctx->jpeg_stream = NULL;
        }
        if (ctx->decode_state && ctx->fn.DecodeStateDestroy) {
            nvjpeg2kStatus_t st = ctx->fn.DecodeStateDestroy(ctx->decode_state);
            if (st != NVJPEG2K_STATUS_SUCCESS)
                av_log(avctx, AV_LOG_WARNING, "nvjpeg2k: DecodeStateDestroy failed (%d)\n", (int)st);
            ctx->decode_state = NULL;
        }
        if (ctx->handle && ctx->fn.Destroy) {
            nvjpeg2kStatus_t st = ctx->fn.Destroy(ctx->handle);
            if (st != NVJPEG2K_STATUS_SUCCESS)
                av_log(avctx, AV_LOG_WARNING, "nvjpeg2k: nvjpeg2kDestroy failed (%d)\n", (int)st);
            ctx->handle = NULL;
        }
        free_device_mem(ctx);
    } else {
        /* If we could not enter context (or have no context), free host buffers only */
        for (int i = 0; i < MAX_COMPONENTS; i++) {
            if (ctx->host_buf[i] && ctx->fn.MemFreeHost) {
                ctx->fn.MemFreeHost(ctx->host_buf[i]);
                ctx->host_buf[i] = NULL;
                ctx->host_buf_size[i] = 0;
            }
        }
    }

    /* Restore prior context only if we switched away from a different context */
    cuda_leave(ctx, saved, switched);

    if (ctx->cuda_ctx && ctx->fn.PrimaryCtxRelease) {
        ctx->fn.PrimaryCtxRelease(ctx->cuda_dev);
        ctx->cuda_ctx = NULL;
    }
    if (ctx->nvcuda_dll) {
        FreeLibrary(ctx->nvcuda_dll);
        ctx->nvcuda_dll = NULL;
    }
    if (ctx->nvjpeg2k_dll) {
        FreeLibrary(ctx->nvjpeg2k_dll);
        ctx->nvjpeg2k_dll = NULL;
    }

    /* Restore GPU Power Management Mode via NVAPI DRS if modified */
    nvapi_restore_pstate(avctx, ctx);

    return 0;
}

#else

typedef struct NVJPEG2KContext {
    AVClass *av_class;
    int      gpu_device;
    int      nvapi_pstate_mode;
} NVJPEG2KContext;

static av_cold int nvjpeg2k_decode_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_INFO, "nvjpeg2k: implemented on Windows only\n");
    return AVERROR_DECODER_NOT_FOUND;
}

static int nvjpeg2k_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                 int *got_frame, AVPacket *avpkt)
{
    (void)avctx;
    (void)frame;
    (void)avpkt;
    *got_frame = 0;
    return AVERROR_DECODER_NOT_FOUND;
}

static av_cold int nvjpeg2k_decode_close(AVCodecContext *avctx)
{
    (void)avctx;
    return 0;
}

static void nvjpeg2k_decode_flush(AVCodecContext *avctx)
{
    (void)avctx;
}

#endif

#define OFFSET(x) offsetof(NVJPEG2KContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "gpu", "NVIDIA GPU index", OFFSET(gpu_device), AV_OPT_TYPE_INT,
      { .i64 = 0 }, 0, 15, FLAGS },
    { "nvapi_pstate", "NVAPI power management mode (0=Adaptive, 1=MaxPerf, -1=Disabled)",
      OFFSET(nvapi_pstate_mode), AV_OPT_TYPE_INT, { .i64 = 1 }, -1, 5, FLAGS },
    { NULL },
};

static const AVClass nvjpeg2k_class = {
    .class_name = "jpeg2000_nvjpeg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVPixelFormat nvjpeg2k_pix_fmts[] = {
    AV_PIX_FMT_XYZ12LE,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP10LE, AV_PIX_FMT_GBRP12LE, AV_PIX_FMT_GBRP16LE,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10LE, AV_PIX_FMT_GBRAP12LE, AV_PIX_FMT_GBRAP16LE,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV420P12LE, AV_PIX_FMT_YUV420P16LE,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA420P10LE, AV_PIX_FMT_YUVA420P16LE,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV422P12LE, AV_PIX_FMT_YUV422P16LE,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA422P10LE, AV_PIX_FMT_YUVA422P12LE, AV_PIX_FMT_YUVA422P16LE,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV440P10LE, AV_PIX_FMT_YUV440P12LE,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_YUV444P16LE,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA444P10LE, AV_PIX_FMT_YUVA444P12LE, AV_PIX_FMT_YUVA444P16LE,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10LE, AV_PIX_FMT_GRAY12LE, AV_PIX_FMT_GRAY16LE,
    AV_PIX_FMT_YA8, AV_PIX_FMT_YA16LE,
    AV_PIX_FMT_NONE
};

const FFCodec ff_jpeg2000_nvjpeg_decoder = {
    .p.name         = "jpeg2000_nvjpeg",
    CODEC_LONG_NAME("JPEG 2000 (NVIDIA GPU, nvJPEG2000)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .p.priv_class   = &nvjpeg2k_class,
    .p.wrapper_name = "nvjpeg2k",
    CODEC_PIXFMTS_ARRAY(nvjpeg2k_pix_fmts),
    .priv_data_size = sizeof(NVJPEG2KContext),
    .init           = nvjpeg2k_decode_init,
    .close          = nvjpeg2k_decode_close,
    .flush          = nvjpeg2k_decode_flush,
    FF_CODEC_DECODE_CB(nvjpeg2k_decode_frame),
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
