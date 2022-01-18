/*
 * H.264/HEVC hardware encoding using hjkidia hjkenc
 * Copyright (c) 2016 Timo Rothenpieler <timo@rothenpieler.org>
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

#include "hjkenc.h"
#include "hevc_sei.h"

#ifdef ABCD
#include "libavutil/hwcontext_cuda.h"
#endif

#include "libavutil/hwcontext.h"

#include "libavutil/hjk_check.h"

#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "atsc_a53.h"
#include "encode.h"
#include "internal.h"
#include "packet_internal.h"

#define CHECK_CU(x) FF_HJK_CHECK_DL(avctx, dl_fn->hjk_dl, x)

#define HJKENC_CAP 0x30
#define IS_CBR(rc) (rc == HJK_ENC_PARAMS_RC_CBR ||             \
                    rc == HJK_ENC_PARAMS_RC_CBR_LOWDELAY_HQ || \
                    rc == HJK_ENC_PARAMS_RC_CBR_HQ)

const enum AVPixelFormat ff_hjkenc_pix_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P016,      // Truncated to 10bits
    AV_PIX_FMT_YUV444P16, // Truncated to 10bits
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_CUDA,
#if CONFIG_D3D11VA
    AV_PIX_FMT_D3D11,
#endif
    AV_PIX_FMT_NONE
};

const AVCodecHWConfigInternal *const ff_hjkenc_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(CUDA,  CUDA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  CUDA),
#if CONFIG_D3D11VA
    HW_CONFIG_ENCODER_FRAMES(D3D11, D3D11VA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  D3D11VA),
#endif
    NULL,
};

#define IS_10BIT(pix_fmt)  (pix_fmt == AV_PIX_FMT_P010    || \
                            pix_fmt == AV_PIX_FMT_P016    || \
                            pix_fmt == AV_PIX_FMT_YUV444P16)

#define IS_YUV444(pix_fmt) (pix_fmt == AV_PIX_FMT_YUV444P || \
                            pix_fmt == AV_PIX_FMT_YUV444P16)

static const struct {
    HJKENCSTATUS hjkerr;
    int         averr;
    const char *desc;
} hjkenc_errors[] = {

    { HJK_ENC_SUCCESS,                      0,                "success"                  },
    { HJK_ENC_ERR_NO_ENCODE_DEVICE,         AVERROR(ENOENT),  "no encode device"         },
    { HJK_ENC_ERR_UNSUPPORTED_DEVICE,       AVERROR(ENOSYS),  "unsupported device"       },
    { HJK_ENC_ERR_INVALID_ENCODERDEVICE,    AVERROR(EINVAL),  "invalid encoder device"   },
    { HJK_ENC_ERR_INVALID_DEVICE,           AVERROR(EINVAL),  "invalid device"           },
    { HJK_ENC_ERR_DEVICE_NOT_EXIST,         AVERROR(EIO),     "device does not exist"    },
    { HJK_ENC_ERR_INVALID_PTR,              AVERROR(EFAULT),  "invalid ptr"              },
    { HJK_ENC_ERR_INVALID_EVENT,            AVERROR(EINVAL),  "invalid event"            },
    { HJK_ENC_ERR_INVALID_PARAM,            AVERROR(EINVAL),  "invalid param"            },
    { HJK_ENC_ERR_INVALID_CALL,             AVERROR(EINVAL),  "invalid call"             },
    { HJK_ENC_ERR_OUT_OF_MEMORY,            AVERROR(ENOMEM),  "out of memory"            },
    { HJK_ENC_ERR_ENCODER_NOT_INITIALIZED,  AVERROR(EINVAL),  "encoder not initialized"  },
    { HJK_ENC_ERR_UNSUPPORTED_PARAM,        AVERROR(ENOSYS),  "unsupported param"        },
    { HJK_ENC_ERR_LOCK_BUSY,                AVERROR(EAGAIN),  "lock busy"                },
    { HJK_ENC_ERR_NOT_ENOUGH_BUFFER,        AVERROR_BUFFER_TOO_SMALL, "not enough buffer"},
    { HJK_ENC_ERR_INVALID_VERSION,          AVERROR(EINVAL),  "invalid version"          },
    { HJK_ENC_ERR_MAP_FAILED,               AVERROR(EIO),     "map failed"               },
    { HJK_ENC_ERR_NEED_MORE_INPUT,          AVERROR(EAGAIN),  "need more input"          },
    { HJK_ENC_ERR_ENCODER_BUSY,             AVERROR(EAGAIN),  "encoder busy"             },
    { HJK_ENC_ERR_EVENT_NOT_REGISTERD,      AVERROR(EBADF),   "event not registered"     },
    { HJK_ENC_ERR_GENERIC,                  AVERROR_UNKNOWN,  "generic error"            },
    { HJK_ENC_ERR_INCOMPATIBLE_CLIENT_KEY,  AVERROR(EINVAL),  "incompatible client key"  },
    { HJK_ENC_ERR_UNIMPLEMENTED,            AVERROR(ENOSYS),  "unimplemented"            },
    { HJK_ENC_ERR_RESOURCE_REGISTER_FAILED, AVERROR(EIO),     "resource register failed" },
    { HJK_ENC_ERR_RESOURCE_NOT_REGISTERED,  AVERROR(EBADF),   "resource not registered"  },
    { HJK_ENC_ERR_RESOURCE_NOT_MAPPED,      AVERROR(EBADF),   "resource not mapped"      },
};

static int hjkenc_map_error(HJKENCSTATUS err, const char **desc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(hjkenc_errors); i++) {
        if (hjkenc_errors[i].hjkerr == err) {
            if (desc)
                *desc = hjkenc_errors[i].desc;
            return hjkenc_errors[i].averr;
        }
    }
    if (desc)
        *desc = "unknown error";
    return AVERROR_UNKNOWN;
}

static int hjkenc_print_error(AVCodecContext *avctx, HJKENCSTATUS err,
                             const char *error_string)
{
    const char *desc;
    const char *details = "(no details)";
    int ret = hjkenc_map_error(err, &desc);

#ifdef HJKENC_HAVE_GETLASTERRORSTRING
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &ctx->hjkenc_dload_funcs.hjkenc_funcs;

    if (p_hjkenc && ctx->hjkencoder)
        details = p_hjkenc->hjkEncGetLastErrorString(ctx->hjkencoder);
#endif

    av_log(avctx, AV_LOG_ERROR, "%s: %s (%d): %s\n", error_string, desc, err, details);

    return ret;
}

typedef struct GUIDTuple {
    const GUID guid;
    int flags;
} GUIDTuple;

#define PRESET_ALIAS(alias, name, ...) \
    [PRESET_ ## alias] = { HJK_ENC_PRESET_ ## name ## _GUID, __VA_ARGS__ }

#define PRESET(name, ...) PRESET_ALIAS(name, name, __VA_ARGS__)

static void hjkenc_map_preset(HjkencContext *ctx)
{
    GUIDTuple presets[] = {
#ifdef HJKENC_HAVE_NEW_PRESETS
        PRESET(P1),
        PRESET(P2),
        PRESET(P3),
        PRESET(P4),
        PRESET(P5),
        PRESET(P6),
        PRESET(P7),
        PRESET_ALIAS(SLOW,   P7, HJKENC_TWO_PASSES),
        PRESET_ALIAS(MEDIUM, P4, HJKENC_ONE_PASS),
        PRESET_ALIAS(FAST,   P1, HJKENC_ONE_PASS),
        // Compat aliases
        PRESET_ALIAS(DEFAULT,             P4, HJKENC_DEPRECATED_PRESET),
        PRESET_ALIAS(HP,                  P1, HJKENC_DEPRECATED_PRESET),
        PRESET_ALIAS(HQ,                  P7, HJKENC_DEPRECATED_PRESET),
        PRESET_ALIAS(BD,                  P5, HJKENC_DEPRECATED_PRESET),
        PRESET_ALIAS(LOW_LATENCY_DEFAULT, P4, HJKENC_DEPRECATED_PRESET | HJKENC_LOWLATENCY),
        PRESET_ALIAS(LOW_LATENCY_HP,      P1, HJKENC_DEPRECATED_PRESET | HJKENC_LOWLATENCY),
        PRESET_ALIAS(LOW_LATENCY_HQ,      P7, HJKENC_DEPRECATED_PRESET | HJKENC_LOWLATENCY),
        PRESET_ALIAS(LOSSLESS_DEFAULT,    P4, HJKENC_DEPRECATED_PRESET | HJKENC_LOSSLESS),
        PRESET_ALIAS(LOSSLESS_HP,         P1, HJKENC_DEPRECATED_PRESET | HJKENC_LOSSLESS),
#else
        PRESET(DEFAULT),
        PRESET(HP),
        PRESET(HQ),
        PRESET(BD),
        PRESET_ALIAS(SLOW,   HQ,    HJKENC_TWO_PASSES),
        PRESET_ALIAS(MEDIUM, HQ,    HJKENC_ONE_PASS),
        PRESET_ALIAS(FAST,   HP,    HJKENC_ONE_PASS),
        PRESET(LOW_LATENCY_DEFAULT, HJKENC_LOWLATENCY),
        PRESET(LOW_LATENCY_HP,      HJKENC_LOWLATENCY),
        PRESET(LOW_LATENCY_HQ,      HJKENC_LOWLATENCY),
        PRESET(LOSSLESS_DEFAULT,    HJKENC_LOSSLESS),
        PRESET(LOSSLESS_HP,         HJKENC_LOSSLESS),
#endif
    };

    GUIDTuple *t = &presets[ctx->preset];

    ctx->init_encode_params.presetGUID = t->guid;
    ctx->flags = t->flags;

#ifdef HJKENC_HAVE_NEW_PRESETS
    if (ctx->tuning_info == HJK_ENC_TUNING_INFO_LOSSLESS)
        ctx->flags |= HJKENC_LOSSLESS;
#endif
}

#undef PRESET
#undef PRESET_ALIAS

static void hjkenc_print_driver_requirement(AVCodecContext *avctx, int level)
{
#if HJKENCAPI_CHECK_VERSION(11, 1)
    const char *mihjker = "(unknown)";
#elif HJKENCAPI_CHECK_VERSION(11, 0)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "456.71";
# else
    const char *mihjker = "455.28";
# endif
#elif HJKENCAPI_CHECK_VERSION(10, 0)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "450.51";
# else
    const char *mihjker = "445.87";
# endif
#elif HJKENCAPI_CHECK_VERSION(9, 1)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "436.15";
# else
    const char *mihjker = "435.21";
# endif
#elif HJKENCAPI_CHECK_VERSION(9, 0)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "418.81";
# else
    const char *mihjker = "418.30";
# endif
#elif HJKENCAPI_CHECK_VERSION(8, 2)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "397.93";
# else
    const char *mihjker = "396.24";
#endif
#elif HJKENCAPI_CHECK_VERSION(8, 1)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "390.77";
# else
    const char *mihjker = "390.25";
# endif
#else
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *mihjker = "378.66";
# else
    const char *mihjker = "378.13";
# endif
#endif
    av_log(avctx, level, "The minimum required Hjkidia driver for hjkenc is %s or newer\n", mihjker);
}

static av_cold int hjkenc_load_libraries(AVCodecContext *avctx)
{
    HjkencContext *ctx            = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJKENCSTATUS err;
    uint32_t hjkenc_max_ver;
    int ret;

    ret = hjk_load_functions(&dl_fn->hjk_dl, avctx);
    if (ret < 0)
        return ret;

    ret = hjkenc_load_functions(&dl_fn->hjkenc_dl, avctx);
    if (ret < 0) {
        hjkenc_print_driver_requirement(avctx, AV_LOG_ERROR);
        return ret;
    }

    err = dl_fn->hjkenc_dl->HjkEncodeAPIGetMaxSupportedVersion(&hjkenc_max_ver);
    if (err != HJK_ENC_SUCCESS)
        return hjkenc_print_error(avctx, err, "Failed to query hjkenc max version");

    av_log(avctx, AV_LOG_VERBOSE, "Loaded Hjkenc version %d.%d\n", hjkenc_max_ver >> 4, hjkenc_max_ver & 0xf);

    if ((HJKENCAPI_MAJOR_VERSION << 4 | HJKENCAPI_MINOR_VERSION) > hjkenc_max_ver) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support the required hjkenc API version. "
               "Required: %d.%d Found: %d.%d\n",
               HJKENCAPI_MAJOR_VERSION, HJKENCAPI_MINOR_VERSION,
               hjkenc_max_ver >> 4, hjkenc_max_ver & 0xf);
        hjkenc_print_driver_requirement(avctx, AV_LOG_ERROR);
        return AVERROR(ENOSYS);
    }

    dl_fn->hjkenc_funcs.version = HJK_ENCODE_API_FUNCTION_LIST_VER;

    err = dl_fn->hjkenc_dl->HjkEncodeAPICreateInstance(&dl_fn->hjkenc_funcs);
    if (err != HJK_ENC_SUCCESS)
        return hjkenc_print_error(avctx, err, "Failed to create hjkenc instance");

    av_log(avctx, AV_LOG_VERBOSE, "Hjkenc initialized successfully\n");

    return 0;
}

static int hjkenc_push_context(AVCodecContext *avctx)
{
    HjkencContext *ctx            = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;

    if (ctx->d3d11_device)
        return 0;

    return CHECK_CU(dl_fn->hjk_dl->hjkCtxPushCurrent(ctx->hjk_context));
}

static int hjkenc_pop_context(AVCodecContext *avctx)
{
    HjkencContext *ctx            = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJcontext dummy;

    if (ctx->d3d11_device)
        return 0;

    return CHECK_CU(dl_fn->hjk_dl->hjkCtxPopCurrent(&dummy));
}

static av_cold int hjkenc_open_session(AVCodecContext *avctx)
{
    HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &ctx->hjkenc_dload_funcs.hjkenc_funcs;
    HJKENCSTATUS ret;

    params.version    = HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.apiVersion = HJKENCAPI_VERSION;
    if (ctx->d3d11_device) {
        params.device     = ctx->d3d11_device;
        params.deviceType = HJK_ENC_DEVICE_TYPE_DIRECTX;
    } else {
        params.device     = ctx->hjk_context;
        params.deviceType = HJK_ENC_DEVICE_TYPE_CUDA;
    }

    ret = p_hjkenc->hjkEncOpenEncodeSessionEx(&params, &ctx->hjkencoder);
    if (ret != HJK_ENC_SUCCESS) {
        ctx->hjkencoder = NULL;
        return hjkenc_print_error(avctx, ret, "OpenEncodeSessionEx failed");
    }

    return 0;
}

static int hjkenc_check_codec_support(AVCodecContext *avctx)
{
    HjkencContext *ctx                    = avctx->priv_data;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &ctx->hjkenc_dload_funcs.hjkenc_funcs;
    int i, ret, count = 0;
    GUID *guids = NULL;

    ret = p_hjkenc->hjkEncGetEncodeGUIDCount(ctx->hjkencoder, &count);

    if (ret != HJK_ENC_SUCCESS || !count)
        return AVERROR(ENOSYS);

    guids = av_malloc(count * sizeof(GUID));
    if (!guids)
        return AVERROR(ENOMEM);

    ret = p_hjkenc->hjkEncGetEncodeGUIDs(ctx->hjkencoder, guids, count, &count);
    if (ret != HJK_ENC_SUCCESS) {
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    ret = AVERROR(ENOSYS);
    for (i = 0; i < count; i++) {
        if (!memcmp(&guids[i], &ctx->init_encode_params.encodeGUID, sizeof(*guids))) {
            ret = 0;
            break;
        }
    }

fail:
    av_free(guids);

    return ret;
}

static int hjkenc_check_cap(AVCodecContext *avctx, HJK_ENC_CAPS cap)
{
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &ctx->hjkenc_dload_funcs.hjkenc_funcs;
    HJK_ENC_CAPS_PARAM params        = { 0 };
    int ret, val = 0;

    params.version     = HJK_ENC_CAPS_PARAM_VER;
    params.capsToQuery = cap;

    ret = p_hjkenc->hjkEncGetEncodeCaps(ctx->hjkencoder, ctx->init_encode_params.encodeGUID, &params, &val);

    if (ret == HJK_ENC_SUCCESS)
        return val;
    return 0;
}

static int hjkenc_check_capabilities(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    int ret;

    ret = hjkenc_check_codec_support(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_WARNING, "Codec not supported\n");
        return ret;
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_YUV444_ENCODE);
    if (IS_YUV444(ctx->data_pix_fmt) && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "YUV444P not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE);
    if (ctx->flags & HJKENC_LOSSLESS && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "Lossless encoding not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_WIDTH_MAX);
    if (ret < avctx->width) {
        av_log(avctx, AV_LOG_WARNING, "Width %d exceeds %d\n",
               avctx->width, ret);
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_HEIGHT_MAX);
    if (ret < avctx->height) {
        av_log(avctx, AV_LOG_WARNING, "Height %d exceeds %d\n",
               avctx->height, ret);
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_NUM_MAX_BFRAMES);
    if (ret < avctx->max_b_frames) {
        av_log(avctx, AV_LOG_WARNING, "Max B-frames %d exceed %d\n",
               avctx->max_b_frames, ret);

        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_FIELD_ENCODING);
    if (ret < 1 && avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        av_log(avctx, AV_LOG_WARNING,
               "Interlaced encoding is not supported. Supported level: %d\n",
               ret);
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_10BIT_ENCODE);
    if (IS_10BIT(ctx->data_pix_fmt) && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "10 bit encode not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_LOOKAHEAD);
    if (ctx->rc_lookahead > 0 && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "RC lookahead not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_TEMPORAL_AQ);
    if (ctx->temporal_aq > 0 && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "Temporal AQ not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_WEIGHTED_PREDICTION);
    if (ctx->weighted_pred > 0 && ret <= 0) {
        av_log (avctx, AV_LOG_WARNING, "Weighted Prediction not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_CABAC);
    if (ctx->coder == HJK_ENC_MJPEG_ENTROPY_CODING_MODE_CABAC && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "CABAC entropy coding not supported\n");
        return AVERROR(ENOSYS);
    }

#ifdef HJKENC_HAVE_BFRAME_REF_MODE
    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_BFRAME_REF_MODE);
    if (ctx->b_ref_mode == HJK_ENC_BFRAME_REF_MODE_EACH && ret != 1 && ret != 3) {
        av_log(avctx, AV_LOG_WARNING, "Each B frame as reference is not supported\n");
        return AVERROR(ENOSYS);
    } else if (ctx->b_ref_mode != HJK_ENC_BFRAME_REF_MODE_DISABLED && ret == 0) {
        av_log(avctx, AV_LOG_WARNING, "B frames as references are not supported\n");
        return AVERROR(ENOSYS);
    }
#else
    if (ctx->b_ref_mode != 0) {
        av_log(avctx, AV_LOG_WARNING, "B frames as references need SDK 8.1 at build time\n");
        return AVERROR(ENOSYS);
    }
#endif

#ifdef HJKENC_HAVE_MULTIPLE_REF_FRAMES
    ret = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_MULTIPLE_REF_FRAMES);
    if(avctx->refs != HJK_ENC_NUM_REF_FRAMES_AUTOSELECT && ret <= 0) {
        av_log(avctx, AV_LOG_WARNING, "Multiple reference frames are not supported by the device\n");
        return AVERROR(ENOSYS);
    }
#else
    if(avctx->refs != 0) {
        av_log(avctx, AV_LOG_WARNING, "Multiple reference frames need SDK 9.1 at build time\n");
        return AVERROR(ENOSYS);
    }
#endif

    ctx->support_dyn_bitrate = hjkenc_check_cap(avctx, HJK_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE);

    return 0;
}

static av_cold int hjkenc_check_device(AVCodecContext *avctx, int idx)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;
    char name[128] = { 0};
    int major, minor, ret;
    HJdevice hj_device;
    int loglevel = AV_LOG_VERBOSE;

    if (ctx->device == LIST_DEVICES)
        loglevel = AV_LOG_INFO;

    ret = CHECK_CU(dl_fn->hjk_dl->hjkDeviceGet(&hj_device, idx));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(dl_fn->hjk_dl->hjkDeviceGetName(name, sizeof(name), hj_device));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(dl_fn->hjk_dl->hjkDeviceComputeCapability(&major, &minor, hj_device));
    if (ret < 0)
        return ret;

    av_log(avctx, loglevel, "[ GPU #%d - < %s > has Compute SM %d.%d ]\n", idx, name, major, minor);
    if (((major << 4) | minor) < HJKENC_CAP) {
        av_log(avctx, loglevel, "does not support HJKENC\n");
        goto fail;
    }

    if (ctx->device != idx && ctx->device != ANY_DEVICE)
        return -1;

    ret = CHECK_CU(dl_fn->hjk_dl->hjkCtxCreate(&ctx->hjk_context_internal, 0, hj_device));
    if (ret < 0)
        goto fail;

    ctx->hjk_context = ctx->hjk_context_internal;
    ctx->hjk_stream = NULL;

    if ((ret = hjkenc_pop_context(avctx)) < 0)
        goto fail2;

    if ((ret = hjkenc_open_session(avctx)) < 0)
        goto fail2;

    if ((ret = hjkenc_check_capabilities(avctx)) < 0)
        goto fail3;

    av_log(avctx, loglevel, "supports HJKENC\n");

    dl_fn->hjkenc_device_count++;

    if (ctx->device == idx || ctx->device == ANY_DEVICE)
        return 0;

fail3:
    if ((ret = hjkenc_push_context(avctx)) < 0)
        return ret;

    p_hjkenc->hjkEncDestroyEncoder(ctx->hjkencoder);
    ctx->hjkencoder = NULL;

    if ((ret = hjkenc_pop_context(avctx)) < 0)
        return ret;

fail2:
    CHECK_CU(dl_fn->hjk_dl->hjkCtxDestroy(ctx->hjk_context_internal));
    ctx->hjk_context_internal = NULL;

fail:
    return AVERROR(ENOSYS);
}

static av_cold int hjkenc_setup_device(AVCodecContext *avctx)
{
    HjkencContext *ctx            = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_MJPEG:
        ctx->init_encode_params.encodeGUID = HJK_ENC_CODEC_MJPEG_GUID;
        break;
    case AV_CODEC_ID_HEVC:
        ctx->init_encode_params.encodeGUID = HJK_ENC_CODEC_HEVC_GUID;
        break;
    default:
        return AVERROR_BUG;
    }

    hjkenc_map_preset(ctx);

    if (ctx->flags & HJKENC_DEPRECATED_PRESET)
        av_log(avctx, AV_LOG_WARNING, "The selected preset is deprecated. Use p1 to p7 + -tune or fast/medium/slow.\n");

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA || avctx->pix_fmt == AV_PIX_FMT_D3D11 || avctx->hw_frames_ctx || avctx->hw_device_ctx) {
        AVHWFramesContext   *frames_ctx;
        AVHWDeviceContext   *hwdev_ctx;
#ifdef ABCD
        AVCUDADeviceContext *hjk_device_hwctx = NULL;
#endif
#if CONFIG_D3D11VA
        AVD3D11VADeviceContext *d3d11_device_hwctx = NULL;
#endif
        int ret;

        if (avctx->hw_frames_ctx) {
            frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
#ifdef ABCD
            if (frames_ctx->format == AV_PIX_FMT_CUDA)
                hjk_device_hwctx = frames_ctx->device_ctx->hwctx;
#if CONFIG_D3D11VA
            else if (frames_ctx->format == AV_PIX_FMT_D3D11)
                d3d11_device_hwctx = frames_ctx->device_ctx->hwctx;
#endif
            else
                return AVERROR(EINVAL);
#endif
        } else if (avctx->hw_device_ctx) {
            hwdev_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;
#ifdef ABCD
            if (hwdev_ctx->type == AV_HWDEVICE_TYPE_CUDA)
                hjk_device_hwctx = hwdev_ctx->hwctx;
#if CONFIG_D3D11VA
            else if (hwdev_ctx->type == AV_HWDEVICE_TYPE_D3D11VA)
                d3d11_device_hwctx = hwdev_ctx->hwctx;
#endif
            else
                return AVERROR(EINVAL);
#endif
        } else {
            return AVERROR(EINVAL);
        }

#ifdef ABCD
        if (hjk_device_hwctx) {
            ctx->hjk_context = hjk_device_hwctx->hjk_ctx;
            ctx->hjk_stream = hjk_device_hwctx->stream;
        }
#endif

#if CONFIG_D3D11VA
        else if (d3d11_device_hwctx) {
            ctx->d3d11_device = d3d11_device_hwctx->device;
            ID3D11Device_AddRef(ctx->d3d11_device);
        }
#endif

        ret = hjkenc_open_session(avctx);
        if (ret < 0)
            return ret;

        ret = hjkenc_check_capabilities(avctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_FATAL, "Provided device doesn't support required HJKENC features\n");
            return ret;
        }
    } else {
        int i, nb_devices = 0;

        if (CHECK_CU(dl_fn->hjk_dl->hjkInit(0)) < 0)
            return AVERROR_UNKNOWN;

        if (CHECK_CU(dl_fn->hjk_dl->hjkDeviceGetCount(&nb_devices)) < 0)
            return AVERROR_UNKNOWN;

        if (!nb_devices) {
            av_log(avctx, AV_LOG_FATAL, "No CUDA capable devices found\n");
                return AVERROR_EXTERNAL;
        }

        av_log(avctx, AV_LOG_VERBOSE, "%d CUDA capable devices found\n", nb_devices);

        dl_fn->hjkenc_device_count = 0;
        for (i = 0; i < nb_devices; ++i) {
            if ((hjkenc_check_device(avctx, i)) >= 0 && ctx->device != LIST_DEVICES)
                return 0;
        }

        if (ctx->device == LIST_DEVICES)
            return AVERROR_EXIT;

        if (!dl_fn->hjkenc_device_count) {
            av_log(avctx, AV_LOG_FATAL, "No capable devices found\n");
            return AVERROR_EXTERNAL;
        }

        av_log(avctx, AV_LOG_FATAL, "Requested GPU %d, but only %d GPUs are available!\n", ctx->device, nb_devices);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void set_constqp(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    rc->rateControlMode = HJK_ENC_PARAMS_RC_CONSTQP;

    if (ctx->init_qp_p >= 0) {
        rc->constQP.qpInterP = ctx->init_qp_p;
        if (ctx->init_qp_i >= 0 && ctx->init_qp_b >= 0) {
            rc->constQP.qpIntra = ctx->init_qp_i;
            rc->constQP.qpInterB = ctx->init_qp_b;
        } else if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->constQP.qpIntra = av_clip(
                rc->constQP.qpInterP * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, 51);
            rc->constQP.qpInterB = av_clip(
                rc->constQP.qpInterP * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, 51);
        } else {
            rc->constQP.qpIntra = rc->constQP.qpInterP;
            rc->constQP.qpInterB = rc->constQP.qpInterP;
        }
    } else if (ctx->cqp >= 0) {
        rc->constQP.qpInterP = rc->constQP.qpInterB = rc->constQP.qpIntra = ctx->cqp;
        if (avctx->b_quant_factor != 0.0)
            rc->constQP.qpInterB = av_clip(ctx->cqp * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, 51);
        if (avctx->i_quant_factor != 0.0)
            rc->constQP.qpIntra = av_clip(ctx->cqp * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, 51);
    }

    avctx->qmin = -1;
    avctx->qmax = -1;
}

static av_cold void set_vbr(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;
    int qp_inter_p;

    if (avctx->qmin >= 0 && avctx->qmax >= 0) {
        rc->enableMinQP = 1;
        rc->enableMaxQP = 1;

        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra  = avctx->qmin;

        rc->maxQP.qpInterB = avctx->qmax;
        rc->maxQP.qpInterP = avctx->qmax;
        rc->maxQP.qpIntra = avctx->qmax;

        qp_inter_p = (avctx->qmax + 3 * avctx->qmin) / 4; // biased towards Qmin
    } else if (avctx->qmin >= 0) {
        rc->enableMinQP = 1;

        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra = avctx->qmin;

        qp_inter_p = avctx->qmin;
    } else {
        qp_inter_p = 26; // default to 26
    }

    rc->enableInitialRCQP = 1;

    if (ctx->init_qp_p < 0) {
        rc->initialRCQP.qpInterP  = qp_inter_p;
    } else {
        rc->initialRCQP.qpInterP = ctx->init_qp_p;
    }

    if (ctx->init_qp_i < 0) {
        if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->initialRCQP.qpIntra = av_clip(
                rc->initialRCQP.qpInterP * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, 51);
        } else {
            rc->initialRCQP.qpIntra = rc->initialRCQP.qpInterP;
        }
    } else {
        rc->initialRCQP.qpIntra = ctx->init_qp_i;
    }

    if (ctx->init_qp_b < 0) {
        if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->initialRCQP.qpInterB = av_clip(
                rc->initialRCQP.qpInterP * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, 51);
        } else {
            rc->initialRCQP.qpInterB = rc->initialRCQP.qpInterP;
        }
    } else {
        rc->initialRCQP.qpInterB = ctx->init_qp_b;
    }
}

static av_cold void set_lossless(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    rc->rateControlMode = HJK_ENC_PARAMS_RC_CONSTQP;
    rc->constQP.qpInterB = 0;
    rc->constQP.qpInterP = 0;
    rc->constQP.qpIntra  = 0;

    avctx->qmin = -1;
    avctx->qmax = -1;
}

static void hjkenc_override_rate_control(AVCodecContext *avctx)
{
    HjkencContext *ctx    = avctx->priv_data;
    HJK_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    switch (ctx->rc) {
    case HJK_ENC_PARAMS_RC_CONSTQP:
        set_constqp(avctx);
        return;
    case HJK_ENC_PARAMS_RC_VBR_MINQP:
        if (avctx->qmin < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The variable bitrate rate-control requires "
                   "the 'qmin' option set.\n");
            set_vbr(avctx);
            return;
        }
        /* fall through */
    case HJK_ENC_PARAMS_RC_VBR_HQ:
    case HJK_ENC_PARAMS_RC_VBR:
        set_vbr(avctx);
        break;
    case HJK_ENC_PARAMS_RC_CBR:
    case HJK_ENC_PARAMS_RC_CBR_HQ:
    case HJK_ENC_PARAMS_RC_CBR_LOWDELAY_HQ:
        break;
    }

    rc->rateControlMode = ctx->rc;
}

static av_cold int hjkenc_recalc_surfaces(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    // default minimum of 4 surfaces
    // multiply by 2 for number of HJKENCs on gpu (hardcode to 2)
    // another multiply by 2 to avoid blocking next PBB group
    int nb_surfaces = FFMAX(4, ctx->encode_config.frameIntervalP * 2 * 2);

    // lookahead enabled
    if (ctx->rc_lookahead > 0) {
        // +1 is to account for lkd_bound calculation later
        // +4 is to allow sufficient pipelining with lookahead
        nb_surfaces = FFMAX(1, FFMAX(nb_surfaces, ctx->rc_lookahead + ctx->encode_config.frameIntervalP + 1 + 4));
        if (nb_surfaces > ctx->nb_surfaces && ctx->nb_surfaces > 0)
        {
            av_log(avctx, AV_LOG_WARNING,
                   "Defined rc_lookahead requires more surfaces, "
                   "increasing used surfaces %d -> %d\n", ctx->nb_surfaces, nb_surfaces);
        }
        ctx->nb_surfaces = FFMAX(nb_surfaces, ctx->nb_surfaces);
    } else {
        if (ctx->encode_config.frameIntervalP > 1 && ctx->nb_surfaces < nb_surfaces && ctx->nb_surfaces > 0)
        {
            av_log(avctx, AV_LOG_WARNING,
                   "Defined b-frame requires more surfaces, "
                   "increasing used surfaces %d -> %d\n", ctx->nb_surfaces, nb_surfaces);
            ctx->nb_surfaces = FFMAX(ctx->nb_surfaces, nb_surfaces);
        }
        else if (ctx->nb_surfaces <= 0)
            ctx->nb_surfaces = nb_surfaces;
        // otherwise use user specified value
    }

    ctx->nb_surfaces = FFMAX(1, FFMIN(MAX_REGISTERED_FRAMES, ctx->nb_surfaces));
    ctx->async_depth = FFMIN(ctx->async_depth, ctx->nb_surfaces - 1);

    return 0;
}

static av_cold void hjkenc_setup_rate_control(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;

    if (avctx->global_quality > 0)
        av_log(avctx, AV_LOG_WARNING, "Using global_quality with hjkenc is deprecated. Use qp instead.\n");

    if (ctx->cqp < 0 && avctx->global_quality > 0)
        ctx->cqp = avctx->global_quality;

    if (avctx->bit_rate > 0) {
        ctx->encode_config.rcParams.averageBitRate = avctx->bit_rate;
    } else if (ctx->encode_config.rcParams.averageBitRate > 0) {
        ctx->encode_config.rcParams.maxBitRate = ctx->encode_config.rcParams.averageBitRate;
    }

    if (avctx->rc_max_rate > 0)
        ctx->encode_config.rcParams.maxBitRate = avctx->rc_max_rate;

#ifdef HJKENC_HAVE_MULTIPASS
    ctx->encode_config.rcParams.multiPass = ctx->multipass;

    if (ctx->flags & HJKENC_ONE_PASS)
        ctx->encode_config.rcParams.multiPass = HJK_ENC_MULTI_PASS_DISABLED;
    if (ctx->flags & HJKENC_TWO_PASSES || ctx->twopass > 0)
        ctx->encode_config.rcParams.multiPass = HJK_ENC_TWO_PASS_FULL_RESOLUTION;

    if (ctx->rc < 0) {
        if (ctx->cbr) {
            ctx->rc = HJK_ENC_PARAMS_RC_CBR;
        } else if (ctx->cqp >= 0) {
            ctx->rc = HJK_ENC_PARAMS_RC_CONSTQP;
        } else if (ctx->quality >= 0.0f) {
            ctx->rc = HJK_ENC_PARAMS_RC_VBR;
        }
    }
#else
    if (ctx->rc < 0) {
        if (ctx->flags & HJKENC_ONE_PASS)
            ctx->twopass = 0;
        if (ctx->flags & HJKENC_TWO_PASSES)
            ctx->twopass = 1;

        if (ctx->twopass < 0)
            ctx->twopass = (ctx->flags & HJKENC_LOWLATENCY) != 0;

        if (ctx->cbr) {
            if (ctx->twopass) {
                ctx->rc = HJK_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
            } else {
                ctx->rc = HJK_ENC_PARAMS_RC_CBR;
            }
        } else if (ctx->cqp >= 0) {
            ctx->rc = HJK_ENC_PARAMS_RC_CONSTQP;
        } else if (ctx->twopass) {
            ctx->rc = HJK_ENC_PARAMS_RC_VBR_HQ;
        } else if (avctx->qmin >= 0 && avctx->qmax >= 0) {
            ctx->rc = HJK_ENC_PARAMS_RC_VBR_MINQP;
        }
    }
#endif

    if (ctx->rc >= 0 && ctx->rc & RC_MODE_DEPRECATED) {
        av_log(avctx, AV_LOG_WARNING, "Specified rc mode is deprecated.\n");
        av_log(avctx, AV_LOG_WARNING, "Use -rc constqp/cbr/vbr, -tune and -multipass instead.\n");

        ctx->rc &= ~RC_MODE_DEPRECATED;
    }

#ifdef HJKENC_HAVE_LDKFS
    if (ctx->ldkfs)
         ctx->encode_config.rcParams.lowDelayKeyFrameScale = ctx->ldkfs;
#endif

    if (ctx->flags & HJKENC_LOSSLESS) {
        set_lossless(avctx);
    } else if (ctx->rc >= 0) {
        hjkenc_override_rate_control(avctx);
    } else {
        ctx->encode_config.rcParams.rateControlMode = HJK_ENC_PARAMS_RC_VBR;
        set_vbr(avctx);
    }

    if (avctx->rc_buffer_size > 0) {
        ctx->encode_config.rcParams.vbvBufferSize = avctx->rc_buffer_size;
    } else if (ctx->encode_config.rcParams.averageBitRate > 0) {
        avctx->rc_buffer_size = ctx->encode_config.rcParams.vbvBufferSize = 2 * ctx->encode_config.rcParams.averageBitRate;
    }

    if (ctx->aq) {
        ctx->encode_config.rcParams.enableAQ   = 1;
        ctx->encode_config.rcParams.aqStrength = ctx->aq_strength;
        av_log(avctx, AV_LOG_VERBOSE, "AQ enabled.\n");
    }

    if (ctx->temporal_aq) {
        ctx->encode_config.rcParams.enableTemporalAQ = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Temporal AQ enabled.\n");
    }

    if (ctx->rc_lookahead > 0) {
        int lkd_bound = FFMIN(ctx->nb_surfaces, ctx->async_depth) -
                        ctx->encode_config.frameIntervalP - 4;

        if (lkd_bound < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "Lookahead not enabled. Increase buffer delay (-delay).\n");
        } else {
            ctx->encode_config.rcParams.enableLookahead = 1;
            ctx->encode_config.rcParams.lookaheadDepth  = av_clip(ctx->rc_lookahead, 0, lkd_bound);
            ctx->encode_config.rcParams.disableIadapt   = ctx->no_scenecut;
            ctx->encode_config.rcParams.disableBadapt   = !ctx->b_adapt;
            av_log(avctx, AV_LOG_VERBOSE,
                   "Lookahead enabled: depth %d, scenecut %s, B-adapt %s.\n",
                   ctx->encode_config.rcParams.lookaheadDepth,
                   ctx->encode_config.rcParams.disableIadapt ? "disabled" : "enabled",
                   ctx->encode_config.rcParams.disableBadapt ? "disabled" : "enabled");
        }
    }

    if (ctx->strict_gop) {
        ctx->encode_config.rcParams.strictGOPTarget = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Strict GOP target enabled.\n");
    }

    if (ctx->nonref_p)
        ctx->encode_config.rcParams.enableNonRefP = 1;

    if (ctx->zerolatency)
        ctx->encode_config.rcParams.zeroReorderDelay = 1;

    if (ctx->quality) {
        //cohjkert from float to fixed point 8.8
        int tmp_quality = (int)(ctx->quality * 256.0f);
        ctx->encode_config.rcParams.targetQuality = (uint8_t)(tmp_quality >> 8);
        ctx->encode_config.rcParams.targetQualityLSB = (uint8_t)(tmp_quality & 0xff);

        av_log(avctx, AV_LOG_VERBOSE, "CQ(%d) mode enabled.\n", tmp_quality);

        //CQ mode shall discard avg bitrate & honor max bitrate;
        ctx->encode_config.rcParams.averageBitRate = avctx->bit_rate = 0;
        ctx->encode_config.rcParams.maxBitRate = avctx->rc_max_rate;
    }
}

static av_cold int hjkenc_setup_mjpeg_config(AVCodecContext *avctx)
{
    HjkencContext *ctx                      = avctx->priv_data;
    HJK_ENC_CONFIG *cc                      = &ctx->encode_config;
    HJK_ENC_CONFIG_MJPEG *mjpeg               = &cc->encodeCodecConfig.mjpegConfig;
    HJK_ENC_CONFIG_MJPEG_VUI_PARAMETERS *vui = &mjpeg->mjpegVUIParameters;

    vui->colourMatrix = avctx->colorspace;
    vui->colourPrimaries = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;
    vui->videoFullRangeFlag = (avctx->color_range == AVCOL_RANGE_JPEG
        || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);

    vui->colourDescriptionPresentFlag =
        (avctx->colorspace != 2 || avctx->color_primaries != 2 || avctx->color_trc != 2);

    vui->videoSignalTypePresentFlag =
        (vui->colourDescriptionPresentFlag
        || vui->videoFormat != 5
        || vui->videoFullRangeFlag != 0);

    mjpeg->sliceMode = 3;
    mjpeg->sliceModeData = 1;

    mjpeg->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    mjpeg->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    mjpeg->outputAUD     = ctx->aud;

    if (ctx->dpb_size >= 0) {
        /* 0 means "let the hardware decide" */
        mjpeg->maxNumRefFrames = ctx->dpb_size;
    }
    if (avctx->gop_size >= 0) {
        mjpeg->idrPeriod = cc->gopLength;
    }

    if (IS_CBR(cc->rcParams.rateControlMode)) {
        mjpeg->outputBufferingPeriodSEI = 1;
    }

    mjpeg->outputPictureTimingSEI = 1;

    if (cc->rcParams.rateControlMode == HJK_ENC_PARAMS_RC_CBR_LOWDELAY_HQ ||
        cc->rcParams.rateControlMode == HJK_ENC_PARAMS_RC_CBR_HQ ||
        cc->rcParams.rateControlMode == HJK_ENC_PARAMS_RC_VBR_HQ) {
        mjpeg->adaptiveTransformMode = HJK_ENC_MJPEG_ADAPTIVE_TRANSFORM_ENABLE;
        mjpeg->fmoMode = HJK_ENC_MJPEG_FMO_DISABLE;
    }

    if (ctx->flags & HJKENC_LOSSLESS) {
        mjpeg->qpPrimeYZeroTransformBypassFlag = 1;
    } else {
        switch(ctx->profile) {
        case HJK_ENC_MJPEG_PROFILE_BASELINE:
            cc->profileGUID = HJK_ENC_MJPEG_PROFILE_BASELINE_GUID;
            avctx->profile = FF_PROFILE_MJPEG_BASELINE;
            break;
        case HJK_ENC_MJPEG_PROFILE_MAIN:
            cc->profileGUID = HJK_ENC_MJPEG_PROFILE_MAIN_GUID;
            avctx->profile = FF_PROFILE_MJPEG_MAIN;
            break;
        case HJK_ENC_MJPEG_PROFILE_HIGH:
            cc->profileGUID = HJK_ENC_MJPEG_PROFILE_HIGH_GUID;
            avctx->profile = FF_PROFILE_MJPEG_HIGH;
            break;
        case HJK_ENC_MJPEG_PROFILE_HIGH_444P:
            cc->profileGUID = HJK_ENC_MJPEG_PROFILE_HIGH_444_GUID;
            avctx->profile = FF_PROFILE_MJPEG_HIGH_444_PREDICTIVE;
            break;
        }
    }

    // force setting profile as high444p if input is AV_PIX_FMT_YUV444P
    if (ctx->data_pix_fmt == AV_PIX_FMT_YUV444P) {
        cc->profileGUID = HJK_ENC_MJPEG_PROFILE_HIGH_444_GUID;
        avctx->profile = FF_PROFILE_MJPEG_HIGH_444_PREDICTIVE;
    }

    mjpeg->chromaFormatIDC = avctx->profile == FF_PROFILE_MJPEG_HIGH_444_PREDICTIVE ? 3 : 1;

    mjpeg->level = ctx->level;

    if (ctx->coder >= 0)
        mjpeg->entropyCodingMode = ctx->coder;

#ifdef HJKENC_HAVE_BFRAME_REF_MODE
    mjpeg->useBFramesAsRef = ctx->b_ref_mode;
#endif

#ifdef HJKENC_HAVE_MULTIPLE_REF_FRAMES
    mjpeg->numRefL0 = avctx->refs;
    mjpeg->numRefL1 = avctx->refs;
#endif

    return 0;
}

static av_cold int hjkenc_setup_hevc_config(AVCodecContext *avctx)
{
    HjkencContext *ctx                      = avctx->priv_data;
    HJK_ENC_CONFIG *cc                      = &ctx->encode_config;
    HJK_ENC_CONFIG_HEVC *hevc               = &cc->encodeCodecConfig.hevcConfig;
    HJK_ENC_CONFIG_HEVC_VUI_PARAMETERS *vui = &hevc->hevcVUIParameters;

    vui->colourMatrix = avctx->colorspace;
    vui->colourPrimaries = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;
    vui->videoFullRangeFlag = (avctx->color_range == AVCOL_RANGE_JPEG
        || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);

    vui->colourDescriptionPresentFlag =
        (avctx->colorspace != 2 || avctx->color_primaries != 2 || avctx->color_trc != 2);

    vui->videoSignalTypePresentFlag =
        (vui->colourDescriptionPresentFlag
        || vui->videoFormat != 5
        || vui->videoFullRangeFlag != 0);

    hevc->sliceMode = 3;
    hevc->sliceModeData = 1;

    hevc->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    hevc->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    hevc->outputAUD     = ctx->aud;

    if (ctx->dpb_size >= 0) {
        /* 0 means "let the hardware decide" */
        hevc->maxNumRefFramesInDPB = ctx->dpb_size;
    }
    if (avctx->gop_size >= 0) {
        hevc->idrPeriod = cc->gopLength;
    }

    if (IS_CBR(cc->rcParams.rateControlMode)) {
        hevc->outputBufferingPeriodSEI = 1;
    }

    hevc->outputPictureTimingSEI = 1;

    switch (ctx->profile) {
    case HJK_ENC_HEVC_PROFILE_MAIN:
        cc->profileGUID = HJK_ENC_HEVC_PROFILE_MAIN_GUID;
        avctx->profile  = FF_PROFILE_HEVC_MAIN;
        break;
    case HJK_ENC_HEVC_PROFILE_MAIN_10:
        cc->profileGUID = HJK_ENC_HEVC_PROFILE_MAIN10_GUID;
        avctx->profile  = FF_PROFILE_HEVC_MAIN_10;
        break;
    case HJK_ENC_HEVC_PROFILE_REXT:
        cc->profileGUID = HJK_ENC_HEVC_PROFILE_FREXT_GUID;
        avctx->profile  = FF_PROFILE_HEVC_REXT;
        break;
    }

    // force setting profile as main10 if input is 10 bit
    if (IS_10BIT(ctx->data_pix_fmt)) {
        cc->profileGUID = HJK_ENC_HEVC_PROFILE_MAIN10_GUID;
        avctx->profile = FF_PROFILE_HEVC_MAIN_10;
    }

    // force setting profile as rext if input is yuv444
    if (IS_YUV444(ctx->data_pix_fmt)) {
        cc->profileGUID = HJK_ENC_HEVC_PROFILE_FREXT_GUID;
        avctx->profile = FF_PROFILE_HEVC_REXT;
    }

    hevc->chromaFormatIDC = IS_YUV444(ctx->data_pix_fmt) ? 3 : 1;

    hevc->pixelBitDepthMinus8 = IS_10BIT(ctx->data_pix_fmt) ? 2 : 0;

    hevc->level = ctx->level;

    hevc->tier = ctx->tier;

#ifdef HJKENC_HAVE_HEVC_BFRAME_REF_MODE
    hevc->useBFramesAsRef = ctx->b_ref_mode;
#endif

#ifdef HJKENC_HAVE_MULTIPLE_REF_FRAMES
    hevc->numRefL0 = avctx->refs;
    hevc->numRefL1 = avctx->refs;
#endif

    return 0;
}

static av_cold int hjkenc_setup_codec_config(AVCodecContext *avctx)
{
    switch (avctx->codec->id) {
    case AV_CODEC_ID_MJPEG:
        return hjkenc_setup_mjpeg_config(avctx);
    case AV_CODEC_ID_HEVC:
        return hjkenc_setup_hevc_config(avctx);
    /* Earlier switch/case will return if unknown codec is passed. */
    }

    return 0;
}

static void compute_dar(AVCodecContext *avctx, int *dw, int *dh) {
    int sw, sh;

    sw = avctx->width;
    sh = avctx->height;

    if (avctx->sample_aspect_ratio.num > 0 && avctx->sample_aspect_ratio.den > 0) {
        sw *= avctx->sample_aspect_ratio.num;
        sh *= avctx->sample_aspect_ratio.den;
    }

    av_reduce(dw, dh, sw, sh, 1024 * 1024);
}

static av_cold int hjkenc_setup_encoder(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;

    HJK_ENC_PRESET_CONFIG preset_config = { 0 };
    HJKENCSTATUS hjk_status = HJK_ENC_SUCCESS;
    AVCPBProperties *cpb_props;
    int res = 0;
    int dw, dh;

    ctx->encode_config.version = HJK_ENC_CONFIG_VER;
    ctx->init_encode_params.version = HJK_ENC_INITIALIZE_PARAMS_VER;

    ctx->init_encode_params.encodeHeight = avctx->height;
    ctx->init_encode_params.encodeWidth = avctx->width;

    ctx->init_encode_params.encodeConfig = &ctx->encode_config;

    preset_config.version = HJK_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = HJK_ENC_CONFIG_VER;

#ifdef HJKENC_HAVE_NEW_PRESETS
    ctx->init_encode_params.tuningInfo = ctx->tuning_info;

    if (ctx->flags & HJKENC_LOSSLESS)
        ctx->init_encode_params.tuningInfo = HJK_ENC_TUNING_INFO_LOSSLESS;
    else if (ctx->flags & HJKENC_LOWLATENCY)
        ctx->init_encode_params.tuningInfo = HJK_ENC_TUNING_INFO_LOW_LATENCY;

    hjk_status = p_hjkenc->hjkEncGetEncodePresetConfigEx(ctx->hjkencoder,
        ctx->init_encode_params.encodeGUID,
        ctx->init_encode_params.presetGUID,
        ctx->init_encode_params.tuningInfo,
        &preset_config);
#else
    hjk_status = p_hjkenc->hjkEncGetEncodePresetConfig(ctx->hjkencoder,
        ctx->init_encode_params.encodeGUID,
        ctx->init_encode_params.presetGUID,
        &preset_config);
#endif
    if (hjk_status != HJK_ENC_SUCCESS)
        return hjkenc_print_error(avctx, hjk_status, "Cannot get the preset configuration");

    memcpy(&ctx->encode_config, &preset_config.presetCfg, sizeof(ctx->encode_config));

    ctx->encode_config.version = HJK_ENC_CONFIG_VER;

    compute_dar(avctx, &dw, &dh);
    ctx->init_encode_params.darHeight = dh;
    ctx->init_encode_params.darWidth = dw;

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        ctx->init_encode_params.frameRateNum = avctx->framerate.num;
        ctx->init_encode_params.frameRateDen = avctx->framerate.den;
    } else {
        ctx->init_encode_params.frameRateNum = avctx->time_base.den;
        ctx->init_encode_params.frameRateDen = avctx->time_base.num * avctx->ticks_per_frame;
    }

    ctx->init_encode_params.enableEncodeAsync = 0;
    ctx->init_encode_params.enablePTD = 1;

#ifdef HJKENC_HAVE_NEW_PRESETS
    /* If lookahead isn't set from CLI, use value from preset.
     * P6 & P7 presets may enable lookahead for better quality.
     * */
    if (ctx->rc_lookahead == 0 && ctx->encode_config.rcParams.enableLookahead)
        ctx->rc_lookahead = ctx->encode_config.rcParams.lookaheadDepth;
#endif

    if (ctx->weighted_pred == 1)
        ctx->init_encode_params.enableWeightedPrediction = 1;

    if (ctx->bluray_compat) {
        ctx->aud = 1;
        ctx->dpb_size = FFMIN(FFMAX(avctx->refs, 0), 6);
        avctx->max_b_frames = FFMIN(avctx->max_b_frames, 3);
        switch (avctx->codec->id) {
        case AV_CODEC_ID_MJPEG:
            /* maximum level depends on used resolution */
            break;
        case AV_CODEC_ID_HEVC:
            ctx->level = HJK_ENC_LEVEL_HEVC_51;
            ctx->tier = HJK_ENC_TIER_HEVC_HIGH;
            break;
        }
    }

    if (avctx->gop_size > 0) {
        if (avctx->max_b_frames >= 0) {
            /* 0 is intra-only, 1 is I/P only, 2 is one B-Frame, 3 two B-frames, and so on. */
            ctx->encode_config.frameIntervalP = avctx->max_b_frames + 1;
        }

        ctx->encode_config.gopLength = avctx->gop_size;
    } else if (avctx->gop_size == 0) {
        ctx->encode_config.frameIntervalP = 0;
        ctx->encode_config.gopLength = 1;
    }

    hjkenc_recalc_surfaces(avctx);

    hjkenc_setup_rate_control(avctx);

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        ctx->encode_config.frameFieldMode = HJK_ENC_PARAMS_FRAME_FIELD_MODE_FIELD;
    } else {
        ctx->encode_config.frameFieldMode = HJK_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    }

    res = hjkenc_setup_codec_config(avctx);
    if (res)
        return res;

    res = hjkenc_push_context(avctx);
    if (res < 0)
        return res;

    hjk_status = p_hjkenc->hjkEncInitializeEncoder(ctx->hjkencoder, &ctx->init_encode_params);
    if (hjk_status != HJK_ENC_SUCCESS) {
        hjkenc_pop_context(avctx);
        return hjkenc_print_error(avctx, hjk_status, "InitializeEncoder failed");
    }

#ifdef HJKENC_HAVE_CUSTREAM_PTR
    if (ctx->hjk_context) {
        hjk_status = p_hjkenc->hjkEncSetIOCudaStreams(ctx->hjkencoder, &ctx->hjk_stream, &ctx->hjk_stream);
        if (hjk_status != HJK_ENC_SUCCESS) {
            hjkenc_pop_context(avctx);
            return hjkenc_print_error(avctx, hjk_status, "SetIOCudaStreams failed");
        }
    }
#endif

    res = hjkenc_pop_context(avctx);
    if (res < 0)
        return res;

    if (ctx->encode_config.frameIntervalP > 1)
        avctx->has_b_frames = 2;

    if (ctx->encode_config.rcParams.averageBitRate > 0)
        avctx->bit_rate = ctx->encode_config.rcParams.averageBitRate;

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = ctx->encode_config.rcParams.maxBitRate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = ctx->encode_config.rcParams.vbvBufferSize;

    return 0;
}

static HJK_ENC_BUFFER_FORMAT hjkenc_map_buffer_format(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        return HJK_ENC_BUFFER_FORMAT_YV12_PL;
    case AV_PIX_FMT_NV12:
        return HJK_ENC_BUFFER_FORMAT_HJK12_PL;
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P016:
        return HJK_ENC_BUFFER_FORMAT_YUV420_10BIT;
    case AV_PIX_FMT_YUV444P:
        return HJK_ENC_BUFFER_FORMAT_YUV444_PL;
    case AV_PIX_FMT_YUV444P16:
        return HJK_ENC_BUFFER_FORMAT_YUV444_10BIT;
    case AV_PIX_FMT_0RGB32:
        return HJK_ENC_BUFFER_FORMAT_ARGB;
    case AV_PIX_FMT_0BGR32:
        return HJK_ENC_BUFFER_FORMAT_ABGR;
    default:
        return HJK_ENC_BUFFER_FORMAT_UNDEFINED;
    }
}

static av_cold int hjkenc_alloc_surface(AVCodecContext *avctx, int idx)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;
    HjkencSurface* tmp_surface = &ctx->surfaces[idx];

    HJKENCSTATUS hjk_status;
    HJK_ENC_CREATE_BITSTREAM_BUFFER allocOut = { 0 };
    allocOut.version = HJK_ENC_CREATE_BITSTREAM_BUFFER_VER;

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA || avctx->pix_fmt == AV_PIX_FMT_D3D11) {
        ctx->surfaces[idx].in_ref = av_frame_alloc();
        if (!ctx->surfaces[idx].in_ref)
            return AVERROR(ENOMEM);
    } else {
        HJK_ENC_CREATE_INPUT_BUFFER allocSurf = { 0 };

        ctx->surfaces[idx].format = hjkenc_map_buffer_format(ctx->data_pix_fmt);
        if (ctx->surfaces[idx].format == HJK_ENC_BUFFER_FORMAT_UNDEFINED) {
            av_log(avctx, AV_LOG_FATAL, "Ihjkalid input pixel format: %s\n",
                   av_get_pix_fmt_name(ctx->data_pix_fmt));
            return AVERROR(EINVAL);
        }

        allocSurf.version = HJK_ENC_CREATE_INPUT_BUFFER_VER;
        allocSurf.width = avctx->width;
        allocSurf.height = avctx->height;
        allocSurf.bufferFmt = ctx->surfaces[idx].format;

        hjk_status = p_hjkenc->hjkEncCreateInputBuffer(ctx->hjkencoder, &allocSurf);
        if (hjk_status != HJK_ENC_SUCCESS) {
            return hjkenc_print_error(avctx, hjk_status, "CreateInputBuffer failed");
        }

        ctx->surfaces[idx].input_surface = allocSurf.inputBuffer;
        ctx->surfaces[idx].width = allocSurf.width;
        ctx->surfaces[idx].height = allocSurf.height;
    }

    hjk_status = p_hjkenc->hjkEncCreateBitstreamBuffer(ctx->hjkencoder, &allocOut);
    if (hjk_status != HJK_ENC_SUCCESS) {
        int err = hjkenc_print_error(avctx, hjk_status, "CreateBitstreamBuffer failed");
        if (avctx->pix_fmt != AV_PIX_FMT_CUDA && avctx->pix_fmt != AV_PIX_FMT_D3D11)
            p_hjkenc->hjkEncDestroyInputBuffer(ctx->hjkencoder, ctx->surfaces[idx].input_surface);
        av_frame_free(&ctx->surfaces[idx].in_ref);
        return err;
    }

    ctx->surfaces[idx].output_surface = allocOut.bitstreamBuffer;

    av_fifo_generic_write(ctx->unused_surface_queue, &tmp_surface, sizeof(tmp_surface), NULL);

    return 0;
}

static av_cold int hjkenc_setup_surfaces(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    int i, res = 0, res2;

    ctx->surfaces = av_mallocz_array(ctx->nb_surfaces, sizeof(*ctx->surfaces));
    if (!ctx->surfaces)
        return AVERROR(ENOMEM);

    ctx->timestamp_list = av_fifo_alloc(ctx->nb_surfaces * sizeof(int64_t));
    if (!ctx->timestamp_list)
        return AVERROR(ENOMEM);

    ctx->unused_surface_queue = av_fifo_alloc(ctx->nb_surfaces * sizeof(HjkencSurface*));
    if (!ctx->unused_surface_queue)
        return AVERROR(ENOMEM);

    ctx->output_surface_queue = av_fifo_alloc(ctx->nb_surfaces * sizeof(HjkencSurface*));
    if (!ctx->output_surface_queue)
        return AVERROR(ENOMEM);
    ctx->output_surface_ready_queue = av_fifo_alloc(ctx->nb_surfaces * sizeof(HjkencSurface*));
    if (!ctx->output_surface_ready_queue)
        return AVERROR(ENOMEM);

    res = hjkenc_push_context(avctx);
    if (res < 0)
        return res;

    for (i = 0; i < ctx->nb_surfaces; i++) {
        if ((res = hjkenc_alloc_surface(avctx, i)) < 0)
            goto fail;
    }

fail:
    res2 = hjkenc_pop_context(avctx);
    if (res2 < 0)
        return res2;

    return res;
}

static av_cold int hjkenc_setup_extradata(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;

    HJKENCSTATUS hjk_status;
    uint32_t outSize = 0;
    char tmpHeader[256];
    HJK_ENC_SEQUENCE_PARAM_PAYLOAD payload = { 0 };
    payload.version = HJK_ENC_SEQUENCE_PARAM_PAYLOAD_VER;

    payload.spsppsBuffer = tmpHeader;
    payload.inBufferSize = sizeof(tmpHeader);
    payload.outSPSPPSPayloadSize = &outSize;

    hjk_status = p_hjkenc->hjkEncGetSequenceParams(ctx->hjkencoder, &payload);
    if (hjk_status != HJK_ENC_SUCCESS) {
        return hjkenc_print_error(avctx, hjk_status, "GetSequenceParams failed");
    }

    avctx->extradata_size = outSize;
    avctx->extradata = av_mallocz(outSize + AV_INPUT_BUFFER_PADDING_SIZE);

    if (!avctx->extradata) {
        return AVERROR(ENOMEM);
    }

    memcpy(avctx->extradata, tmpHeader, outSize);

    return 0;
}

av_cold int ff_hjkenc_encode_close(AVCodecContext *avctx)
{
    HjkencContext *ctx               = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;
    int i, res;

    /* the encoder has to be flushed before it can be closed */
    if (ctx->hjkencoder) {
        HJK_ENC_PIC_PARAMS params        = { .version        = HJK_ENC_PIC_PARAMS_VER,
                                            .encodePicFlags = HJK_ENC_PIC_FLAG_EOS };

        res = hjkenc_push_context(avctx);
        if (res < 0)
            return res;

        p_hjkenc->hjkEncEncodePicture(ctx->hjkencoder, &params);
    }

    av_fifo_freep(&ctx->timestamp_list);
    av_fifo_freep(&ctx->output_surface_ready_queue);
    av_fifo_freep(&ctx->output_surface_queue);
    av_fifo_freep(&ctx->unused_surface_queue);

    if (ctx->surfaces && (avctx->pix_fmt == AV_PIX_FMT_CUDA || avctx->pix_fmt == AV_PIX_FMT_D3D11)) {
        for (i = 0; i < ctx->nb_registered_frames; i++) {
            if (ctx->registered_frames[i].mapped)
                p_hjkenc->hjkEncUnmapInputResource(ctx->hjkencoder, ctx->registered_frames[i].in_map.mappedResource);
            if (ctx->registered_frames[i].regptr)
                p_hjkenc->hjkEncUnregisterResource(ctx->hjkencoder, ctx->registered_frames[i].regptr);
        }
        ctx->nb_registered_frames = 0;
    }

    if (ctx->surfaces) {
        for (i = 0; i < ctx->nb_surfaces; ++i) {
            if (avctx->pix_fmt != AV_PIX_FMT_CUDA && avctx->pix_fmt != AV_PIX_FMT_D3D11)
                p_hjkenc->hjkEncDestroyInputBuffer(ctx->hjkencoder, ctx->surfaces[i].input_surface);
            av_frame_free(&ctx->surfaces[i].in_ref);
            p_hjkenc->hjkEncDestroyBitstreamBuffer(ctx->hjkencoder, ctx->surfaces[i].output_surface);
        }
    }
    av_freep(&ctx->surfaces);
    ctx->nb_surfaces = 0;

    av_frame_free(&ctx->frame);

    if (ctx->hjkencoder) {
        p_hjkenc->hjkEncDestroyEncoder(ctx->hjkencoder);

        res = hjkenc_pop_context(avctx);
        if (res < 0)
            return res;
    }
    ctx->hjkencoder = NULL;

    if (ctx->hjk_context_internal)
        CHECK_CU(dl_fn->hjk_dl->hjkCtxDestroy(ctx->hjk_context_internal));
    ctx->hjk_context = ctx->hjk_context_internal = NULL;

#if CONFIG_D3D11VA
    if (ctx->d3d11_device) {
        ID3D11Device_Release(ctx->d3d11_device);
        ctx->d3d11_device = NULL;
    }
#endif

    hjkenc_free_functions(&dl_fn->hjkenc_dl);
    hjk_free_functions(&dl_fn->hjk_dl);

    dl_fn->hjkenc_device_count = 0;

    av_log(avctx, AV_LOG_VERBOSE, "Hjkenc unloaded\n");

    return 0;
}

av_cold int ff_hjkenc_encode_init(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    int ret;

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA || avctx->pix_fmt == AV_PIX_FMT_D3D11) {
        AVHWFramesContext *frames_ctx;
        if (!avctx->hw_frames_ctx) {
            av_log(avctx, AV_LOG_ERROR,
                   "hw_frames_ctx must be set when using GPU frames as input\n");
            return AVERROR(EINVAL);
        }
        frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        if (frames_ctx->format != avctx->pix_fmt) {
            av_log(avctx, AV_LOG_ERROR,
                   "hw_frames_ctx must match the GPU frame type\n");
            return AVERROR(EINVAL);
        }
        ctx->data_pix_fmt = frames_ctx->sw_format;
    } else {
        ctx->data_pix_fmt = avctx->pix_fmt;
    }

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    if ((ret = hjkenc_load_libraries(avctx)) < 0)
        return ret;

    if ((ret = hjkenc_setup_device(avctx)) < 0)
        return ret;

    if ((ret = hjkenc_setup_encoder(avctx)) < 0)
        return ret;

    if ((ret = hjkenc_setup_surfaces(avctx)) < 0)
        return ret;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        if ((ret = hjkenc_setup_extradata(avctx)) < 0)
            return ret;
    }

    return 0;
}

static HjkencSurface *get_free_frame(HjkencContext *ctx)
{
    HjkencSurface *tmp_surf;

    if (!(av_fifo_size(ctx->unused_surface_queue) > 0))
        // queue empty
        return NULL;

    av_fifo_generic_read(ctx->unused_surface_queue, &tmp_surf, sizeof(tmp_surf), NULL);
    return tmp_surf;
}

static int hjkenc_copy_frame(AVCodecContext *avctx, HjkencSurface *hjk_surface,
            HJK_ENC_LOCK_INPUT_BUFFER *lock_buffer_params, const AVFrame *frame)
{
    int dst_linesize[4] = {
        lock_buffer_params->pitch,
        lock_buffer_params->pitch,
        lock_buffer_params->pitch,
        lock_buffer_params->pitch
    };
    uint8_t *dst_data[4];
    int ret;

    if (frame->format == AV_PIX_FMT_YUV420P)
        dst_linesize[1] = dst_linesize[2] >>= 1;

    ret = av_image_fill_pointers(dst_data, frame->format, hjk_surface->height,
                                 lock_buffer_params->bufferDataPtr, dst_linesize);
    if (ret < 0)
        return ret;

    if (frame->format == AV_PIX_FMT_YUV420P)
        FFSWAP(uint8_t*, dst_data[1], dst_data[2]);

    av_image_copy(dst_data, dst_linesize,
                  (const uint8_t**)frame->data, frame->linesize, frame->format,
                  avctx->width, avctx->height);

    return 0;
}

static int hjkenc_find_free_reg_resource(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;
    HJKENCSTATUS hjk_status;

    int i, first_round;

    if (ctx->nb_registered_frames == FF_ARRAY_ELEMS(ctx->registered_frames)) {
        for (first_round = 1; first_round >= 0; first_round--) {
            for (i = 0; i < ctx->nb_registered_frames; i++) {
                if (!ctx->registered_frames[i].mapped) {
                    if (ctx->registered_frames[i].regptr) {
                        if (first_round)
                            continue;
                        hjk_status = p_hjkenc->hjkEncUnregisterResource(ctx->hjkencoder, ctx->registered_frames[i].regptr);
                        if (hjk_status != HJK_ENC_SUCCESS)
                            return hjkenc_print_error(avctx, hjk_status, "Failed unregistering unused input resource");
                        ctx->registered_frames[i].ptr = NULL;
                        ctx->registered_frames[i].regptr = NULL;
                    }
                    return i;
                }
            }
        }
    } else {
        return ctx->nb_registered_frames++;
    }

    av_log(avctx, AV_LOG_ERROR, "Too many registered CUDA frames\n");
    return AVERROR(ENOMEM);
}

static int hjkenc_register_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;

    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    HJK_ENC_REGISTER_RESOURCE reg;
    int i, idx, ret;

    for (i = 0; i < ctx->nb_registered_frames; i++) {
        if (avctx->pix_fmt == AV_PIX_FMT_CUDA && ctx->registered_frames[i].ptr == frame->data[0])
            return i;
        else if (avctx->pix_fmt == AV_PIX_FMT_D3D11 && ctx->registered_frames[i].ptr == frame->data[0] && ctx->registered_frames[i].ptr_index == (intptr_t)frame->data[1])
            return i;
    }

    idx = hjkenc_find_free_reg_resource(avctx);
    if (idx < 0)
        return idx;

    reg.version            = HJK_ENC_REGISTER_RESOURCE_VER;
    reg.width              = frames_ctx->width;
    reg.height             = frames_ctx->height;
    reg.pitch              = frame->linesize[0];
    reg.resourceToRegister = frame->data[0];

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        reg.resourceType   = HJK_ENC_INPUT_RESOURCE_TYPE_HJKDEVICEPTR;
    }
    else if (avctx->pix_fmt == AV_PIX_FMT_D3D11) {
        reg.resourceType     = HJK_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        reg.subResourceIndex = (intptr_t)frame->data[1];
    }

    reg.bufferFormat       = hjkenc_map_buffer_format(frames_ctx->sw_format);
    if (reg.bufferFormat == HJK_ENC_BUFFER_FORMAT_UNDEFINED) {
        av_log(avctx, AV_LOG_FATAL, "Ihjkalid input pixel format: %s\n",
               av_get_pix_fmt_name(frames_ctx->sw_format));
        return AVERROR(EINVAL);
    }

    ret = p_hjkenc->hjkEncRegisterResource(ctx->hjkencoder, &reg);
    if (ret != HJK_ENC_SUCCESS) {
        hjkenc_print_error(avctx, ret, "Error registering an input resource");
        return AVERROR_UNKNOWN;
    }

    ctx->registered_frames[idx].ptr       = frame->data[0];
    ctx->registered_frames[idx].ptr_index = reg.subResourceIndex;
    ctx->registered_frames[idx].regptr    = reg.registeredResource;
    return idx;
}

static int hjkenc_upload_frame(AVCodecContext *avctx, const AVFrame *frame,
                                      HjkencSurface *hjkenc_frame)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;

    int res;
    HJKENCSTATUS hjk_status;

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA || avctx->pix_fmt == AV_PIX_FMT_D3D11) {
        int reg_idx = hjkenc_register_frame(avctx, frame);
        if (reg_idx < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not register an input HW frame\n");
            return reg_idx;
        }

        res = av_frame_ref(hjkenc_frame->in_ref, frame);
        if (res < 0)
            return res;

        if (!ctx->registered_frames[reg_idx].mapped) {
            ctx->registered_frames[reg_idx].in_map.version = HJK_ENC_MAP_INPUT_RESOURCE_VER;
            ctx->registered_frames[reg_idx].in_map.registeredResource = ctx->registered_frames[reg_idx].regptr;
            hjk_status = p_hjkenc->hjkEncMapInputResource(ctx->hjkencoder, &ctx->registered_frames[reg_idx].in_map);
            if (hjk_status != HJK_ENC_SUCCESS) {
                av_frame_unref(hjkenc_frame->in_ref);
                return hjkenc_print_error(avctx, hjk_status, "Error mapping an input resource");
            }
        }

        ctx->registered_frames[reg_idx].mapped += 1;

        hjkenc_frame->reg_idx                   = reg_idx;
        hjkenc_frame->input_surface             = ctx->registered_frames[reg_idx].in_map.mappedResource;
        hjkenc_frame->format                    = ctx->registered_frames[reg_idx].in_map.mappedBufferFmt;
        hjkenc_frame->pitch                     = frame->linesize[0];

        return 0;
    } else {
        HJK_ENC_LOCK_INPUT_BUFFER lockBufferParams = { 0 };

        lockBufferParams.version = HJK_ENC_LOCK_INPUT_BUFFER_VER;
        lockBufferParams.inputBuffer = hjkenc_frame->input_surface;

        hjk_status = p_hjkenc->hjkEncLockInputBuffer(ctx->hjkencoder, &lockBufferParams);
        if (hjk_status != HJK_ENC_SUCCESS) {
            return hjkenc_print_error(avctx, hjk_status, "Failed locking hjkenc input buffer");
        }

        hjkenc_frame->pitch = lockBufferParams.pitch;
        res = hjkenc_copy_frame(avctx, hjkenc_frame, &lockBufferParams, frame);

        hjk_status = p_hjkenc->hjkEncUnlockInputBuffer(ctx->hjkencoder, hjkenc_frame->input_surface);
        if (hjk_status != HJK_ENC_SUCCESS) {
            return hjkenc_print_error(avctx, hjk_status, "Failed unlocking input buffer!");
        }

        return res;
    }
}

static void hjkenc_codec_specific_pic_params(AVCodecContext *avctx,
                                            HJK_ENC_PIC_PARAMS *params,
                                            HJK_ENC_SEI_PAYLOAD *sei_data,
                                            int sei_count)
{
    HjkencContext *ctx = avctx->priv_data;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_MJPEG:
        params->codecPicParams.mjpegPicParams.sliceMode =
            ctx->encode_config.encodeCodecConfig.mjpegConfig.sliceMode;
        params->codecPicParams.mjpegPicParams.sliceModeData =
            ctx->encode_config.encodeCodecConfig.mjpegConfig.sliceModeData;
        if (sei_count > 0) {
            params->codecPicParams.mjpegPicParams.seiPayloadArray = sei_data;
            params->codecPicParams.mjpegPicParams.seiPayloadArrayCnt = sei_count;
        }

      break;
    case AV_CODEC_ID_HEVC:
        params->codecPicParams.hevcPicParams.sliceMode =
            ctx->encode_config.encodeCodecConfig.hevcConfig.sliceMode;
        params->codecPicParams.hevcPicParams.sliceModeData =
            ctx->encode_config.encodeCodecConfig.hevcConfig.sliceModeData;
        if (sei_count > 0) {
            params->codecPicParams.hevcPicParams.seiPayloadArray = sei_data;
            params->codecPicParams.hevcPicParams.seiPayloadArrayCnt = sei_count;
        }

        break;
    }
}

static inline void timestamp_queue_enqueue(AVFifoBuffer* queue, int64_t timestamp)
{
    av_fifo_generic_write(queue, &timestamp, sizeof(timestamp), NULL);
}

static inline int64_t timestamp_queue_dequeue(AVFifoBuffer* queue)
{
    int64_t timestamp = AV_NOPTS_VALUE;
    if (av_fifo_size(queue) > 0)
        av_fifo_generic_read(queue, &timestamp, sizeof(timestamp), NULL);

    return timestamp;
}

static int hjkenc_set_timestamp(AVCodecContext *avctx,
                               HJK_ENC_LOCK_BITSTREAM *params,
                               AVPacket *pkt)
{
    HjkencContext *ctx = avctx->priv_data;

    pkt->pts = params->outputTimeStamp;
    pkt->dts = timestamp_queue_dequeue(ctx->timestamp_list);

    pkt->dts -= FFMAX(ctx->encode_config.frameIntervalP - 1, 0) * FFMAX(avctx->ticks_per_frame, 1);

    return 0;
}

static int process_output_surface(AVCodecContext *avctx, AVPacket *pkt, HjkencSurface *tmpoutsurf)
{
    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;

    uint32_t slice_mode_data;
    uint32_t *slice_offsets = NULL;
    HJK_ENC_LOCK_BITSTREAM lock_params = { 0 };
    HJKENCSTATUS hjk_status;
    int res = 0;

    enum AVPictureType pict_type;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_MJPEG:
      slice_mode_data = ctx->encode_config.encodeCodecConfig.mjpegConfig.sliceModeData;
      break;
    case AV_CODEC_ID_H265:
      slice_mode_data = ctx->encode_config.encodeCodecConfig.hevcConfig.sliceModeData;
      break;
    default:
      av_log(avctx, AV_LOG_ERROR, "Unknown codec name\n");
      res = AVERROR(EINVAL);
      goto error;
    }
    slice_offsets = av_mallocz(slice_mode_data * sizeof(*slice_offsets));

    if (!slice_offsets) {
        res = AVERROR(ENOMEM);
        goto error;
    }

    lock_params.version = HJK_ENC_LOCK_BITSTREAM_VER;

    lock_params.doNotWait = 0;
    lock_params.outputBitstream = tmpoutsurf->output_surface;
    lock_params.sliceOffsets = slice_offsets;

    hjk_status = p_hjkenc->hjkEncLockBitstream(ctx->hjkencoder, &lock_params);
    if (hjk_status != HJK_ENC_SUCCESS) {
        res = hjkenc_print_error(avctx, hjk_status, "Failed locking bitstream buffer");
        goto error;
    }

    res = ff_get_encode_buffer(avctx, pkt, lock_params.bitstreamSizeInBytes, 0);

    if (res < 0) {
        p_hjkenc->hjkEncUnlockBitstream(ctx->hjkencoder, tmpoutsurf->output_surface);
        goto error;
    }

    memcpy(pkt->data, lock_params.bitstreamBufferPtr, lock_params.bitstreamSizeInBytes);

    hjk_status = p_hjkenc->hjkEncUnlockBitstream(ctx->hjkencoder, tmpoutsurf->output_surface);
    if (hjk_status != HJK_ENC_SUCCESS) {
        res = hjkenc_print_error(avctx, hjk_status, "Failed unlocking bitstream buffer, expect the gates of mordor to open");
        goto error;
    }


    if (avctx->pix_fmt == AV_PIX_FMT_CUDA || avctx->pix_fmt == AV_PIX_FMT_D3D11) {
        ctx->registered_frames[tmpoutsurf->reg_idx].mapped -= 1;
        if (ctx->registered_frames[tmpoutsurf->reg_idx].mapped == 0) {
            hjk_status = p_hjkenc->hjkEncUnmapInputResource(ctx->hjkencoder, ctx->registered_frames[tmpoutsurf->reg_idx].in_map.mappedResource);
            if (hjk_status != HJK_ENC_SUCCESS) {
                res = hjkenc_print_error(avctx, hjk_status, "Failed unmapping input resource");
                goto error;
            }
        } else if (ctx->registered_frames[tmpoutsurf->reg_idx].mapped < 0) {
            res = AVERROR_BUG;
            goto error;
        }

        av_frame_unref(tmpoutsurf->in_ref);

        tmpoutsurf->input_surface = NULL;
    }

    switch (lock_params.pictureType) {
    case HJK_ENC_PIC_TYPE_IDR:
        pkt->flags |= AV_PKT_FLAG_KEY;
    case HJK_ENC_PIC_TYPE_I:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case HJK_ENC_PIC_TYPE_P:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    case HJK_ENC_PIC_TYPE_B:
        pict_type = AV_PICTURE_TYPE_B;
        break;
    case HJK_ENC_PIC_TYPE_BI:
        pict_type = AV_PICTURE_TYPE_BI;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown picture type encountered, expect the output to be broken.\n");
        av_log(avctx, AV_LOG_ERROR, "Please report this error and include as much information on how to reproduce it as possible.\n");
        res = AVERROR_EXTERNAL;
        goto error;
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ff_side_data_set_encoder_stats(pkt,
        (lock_params.frameAvgQP - 1) * FF_QP2LAMBDA, NULL, 0, pict_type);

    res = hjkenc_set_timestamp(avctx, &lock_params, pkt);
    if (res < 0)
        goto error2;

    av_free(slice_offsets);

    return 0;

error:
    timestamp_queue_dequeue(ctx->timestamp_list);

error2:
    av_free(slice_offsets);

    return res;
}

static int output_ready(AVCodecContext *avctx, int flush)
{
    HjkencContext *ctx = avctx->priv_data;
    int nb_ready, nb_pending;

    nb_ready   = av_fifo_size(ctx->output_surface_ready_queue)   / sizeof(HjkencSurface*);
    nb_pending = av_fifo_size(ctx->output_surface_queue)         / sizeof(HjkencSurface*);
    if (flush)
        return nb_ready > 0;
    return (nb_ready > 0) && (nb_ready + nb_pending >= ctx->async_depth);
}

static void reconfig_encoder(AVCodecContext *avctx, const AVFrame *frame)
{
    HjkencContext *ctx = avctx->priv_data;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &ctx->hjkenc_dload_funcs.hjkenc_funcs;
    HJKENCSTATUS ret;

    HJK_ENC_RECONFIGURE_PARAMS params = { 0 };
    int needs_reconfig = 0;
    int needs_encode_config = 0;
    int reconfig_bitrate = 0, reconfig_dar = 0;
    int dw, dh;

    params.version = HJK_ENC_RECONFIGURE_PARAMS_VER;
    params.reInitEncodeParams = ctx->init_encode_params;

    compute_dar(avctx, &dw, &dh);
    if (dw != ctx->init_encode_params.darWidth || dh != ctx->init_encode_params.darHeight) {
        av_log(avctx, AV_LOG_VERBOSE,
               "aspect ratio change (DAR): %d:%d -> %d:%d\n",
               ctx->init_encode_params.darWidth,
               ctx->init_encode_params.darHeight, dw, dh);

        params.reInitEncodeParams.darHeight = dh;
        params.reInitEncodeParams.darWidth = dw;

        needs_reconfig = 1;
        reconfig_dar = 1;
    }

    if (ctx->rc != HJK_ENC_PARAMS_RC_CONSTQP && ctx->support_dyn_bitrate) {
        if (avctx->bit_rate > 0 && params.reInitEncodeParams.encodeConfig->rcParams.averageBitRate != avctx->bit_rate) {
            av_log(avctx, AV_LOG_VERBOSE,
                   "avg bitrate change: %d -> %d\n",
                   params.reInitEncodeParams.encodeConfig->rcParams.averageBitRate,
                   (uint32_t)avctx->bit_rate);

            params.reInitEncodeParams.encodeConfig->rcParams.averageBitRate = avctx->bit_rate;
            reconfig_bitrate = 1;
        }

        if (avctx->rc_max_rate > 0 && ctx->encode_config.rcParams.maxBitRate != avctx->rc_max_rate) {
            av_log(avctx, AV_LOG_VERBOSE,
                   "max bitrate change: %d -> %d\n",
                   params.reInitEncodeParams.encodeConfig->rcParams.maxBitRate,
                   (uint32_t)avctx->rc_max_rate);

            params.reInitEncodeParams.encodeConfig->rcParams.maxBitRate = avctx->rc_max_rate;
            reconfig_bitrate = 1;
        }

        if (avctx->rc_buffer_size > 0 && ctx->encode_config.rcParams.vbvBufferSize != avctx->rc_buffer_size) {
            av_log(avctx, AV_LOG_VERBOSE,
                   "vbv buffer size change: %d -> %d\n",
                   params.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize,
                   avctx->rc_buffer_size);

            params.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize = avctx->rc_buffer_size;
            reconfig_bitrate = 1;
        }

        if (reconfig_bitrate) {
            params.resetEncoder = 1;
            params.forceIDR = 1;

            needs_encode_config = 1;
            needs_reconfig = 1;
        }
    }

    if (!needs_encode_config)
        params.reInitEncodeParams.encodeConfig = NULL;

    if (needs_reconfig) {
        ret = p_hjkenc->hjkEncReconfigureEncoder(ctx->hjkencoder, &params);
        if (ret != HJK_ENC_SUCCESS) {
            hjkenc_print_error(avctx, ret, "failed to reconfigure hjkenc");
        } else {
            if (reconfig_dar) {
                ctx->init_encode_params.darHeight = dh;
                ctx->init_encode_params.darWidth = dw;
            }

            if (reconfig_bitrate) {
                ctx->encode_config.rcParams.averageBitRate = params.reInitEncodeParams.encodeConfig->rcParams.averageBitRate;
                ctx->encode_config.rcParams.maxBitRate = params.reInitEncodeParams.encodeConfig->rcParams.maxBitRate;
                ctx->encode_config.rcParams.vbvBufferSize = params.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize;
            }

        }
    }
}

static int hjkenc_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    HJKENCSTATUS hjk_status;
    HjkencSurface *tmp_out_surf, *in_surf;
    int res, res2;
    HJK_ENC_SEI_PAYLOAD sei_data[8];
    int sei_count = 0;
    int i;

    HjkencContext *ctx = avctx->priv_data;
    HjkencDynLoadFunctions *dl_fn = &ctx->hjkenc_dload_funcs;
    HJK_ENCODE_API_FUNCTION_LIST *p_hjkenc = &dl_fn->hjkenc_funcs;

    HJK_ENC_PIC_PARAMS pic_params = { 0 };
    pic_params.version = HJK_ENC_PIC_PARAMS_VER;

    if ((!ctx->hjk_context && !ctx->d3d11_device) || !ctx->hjkencoder)
        return AVERROR(EINVAL);

    if (frame && frame->buf[0]) {
        in_surf = get_free_frame(ctx);
        if (!in_surf)
            return AVERROR(EAGAIN);

        res = hjkenc_push_context(avctx);
        if (res < 0)
            return res;

        reconfig_encoder(avctx, frame);

        res = hjkenc_upload_frame(avctx, frame, in_surf);

        res2 = hjkenc_pop_context(avctx);
        if (res2 < 0)
            return res2;

        if (res)
            return res;

        pic_params.inputBuffer = in_surf->input_surface;
        pic_params.bufferFmt = in_surf->format;
        pic_params.inputWidth = in_surf->width;
        pic_params.inputHeight = in_surf->height;
        pic_params.inputPitch = in_surf->pitch;
        pic_params.outputBitstream = in_surf->output_surface;

        if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
            if (frame->top_field_first)
                pic_params.pictureStruct = HJK_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM;
            else
                pic_params.pictureStruct = HJK_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
        } else {
            pic_params.pictureStruct = HJK_ENC_PIC_STRUCT_FRAME;
        }

        if (ctx->forced_idr >= 0 && frame->pict_type == AV_PICTURE_TYPE_I) {
            pic_params.encodePicFlags =
                ctx->forced_idr ? HJK_ENC_PIC_FLAG_FORCEIDR : HJK_ENC_PIC_FLAG_FORCEINTRA;
        } else {
            pic_params.encodePicFlags = 0;
        }

        pic_params.inputTimeStamp = frame->pts;

        if (ctx->a53_cc && av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC)) {
            void *a53_data = NULL;
            size_t a53_size = 0;

            if (ff_alloc_a53_sei(frame, 0, (void**)&a53_data, &a53_size) < 0) {
                av_log(ctx, AV_LOG_ERROR, "Not enough memory for closed captions, skipping\n");
            }

            if (a53_data) {
                sei_data[sei_count].payloadSize = (uint32_t)a53_size;
                sei_data[sei_count].payloadType = 4;
                sei_data[sei_count].payload = (uint8_t*)a53_data;
                sei_count ++;
            }
        }

        if (ctx->s12m_tc && av_frame_get_side_data(frame, AV_FRAME_DATA_S12M_TIMECODE)) {
            void *tc_data = NULL;
            size_t tc_size = 0;

            if (ff_alloc_timecode_sei(frame, avctx->framerate, 0, (void**)&tc_data, &tc_size) < 0) {
                av_log(ctx, AV_LOG_ERROR, "Not enough memory for timecode sei, skipping\n");
            }

            if (tc_data) {
                sei_data[sei_count].payloadSize = (uint32_t)tc_size;
                sei_data[sei_count].payloadType = SEI_TYPE_TIME_CODE;
                sei_data[sei_count].payload = (uint8_t*)tc_data;
                sei_count ++;
            }
        }

        hjkenc_codec_specific_pic_params(avctx, &pic_params, sei_data, sei_count);
    } else {
        pic_params.encodePicFlags = HJK_ENC_PIC_FLAG_EOS;
    }

    res = hjkenc_push_context(avctx);
    if (res < 0)
        return res;

    hjk_status = p_hjkenc->hjkEncEncodePicture(ctx->hjkencoder, &pic_params);

    for ( i = 0; i < sei_count; i++)
        av_freep(&sei_data[i].payload);

    res = hjkenc_pop_context(avctx);
    if (res < 0)
        return res;

    if (hjk_status != HJK_ENC_SUCCESS &&
        hjk_status != HJK_ENC_ERR_NEED_MORE_INPUT)
        return hjkenc_print_error(avctx, hjk_status, "EncodePicture failed!");

    if (frame && frame->buf[0]) {
        av_fifo_generic_write(ctx->output_surface_queue, &in_surf, sizeof(in_surf), NULL);
        timestamp_queue_enqueue(ctx->timestamp_list, frame->pts);
    }

    /* all the pending buffers are now ready for output */
    if (hjk_status == HJK_ENC_SUCCESS) {
        while (av_fifo_size(ctx->output_surface_queue) > 0) {
            av_fifo_generic_read(ctx->output_surface_queue, &tmp_out_surf, sizeof(tmp_out_surf), NULL);
            av_fifo_generic_write(ctx->output_surface_ready_queue, &tmp_out_surf, sizeof(tmp_out_surf), NULL);
        }
    }

    return 0;
}

int ff_hjkenc_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    HjkencSurface *tmp_out_surf;
    int res, res2;

    HjkencContext *ctx = avctx->priv_data;

    AVFrame *frame = ctx->frame;

    if ((!ctx->hjk_context && !ctx->d3d11_device) || !ctx->hjkencoder)
        return AVERROR(EINVAL);

    if (!frame->buf[0]) {
        res = ff_encode_get_frame(avctx, frame);
        if (res < 0 && res != AVERROR_EOF)
            return res;
    }

    res = hjkenc_send_frame(avctx, frame);
    if (res < 0) {
        if (res != AVERROR(EAGAIN))
            return res;
    } else
        av_frame_unref(frame);

    if (output_ready(avctx, avctx->internal->draining)) {
        av_fifo_generic_read(ctx->output_surface_ready_queue, &tmp_out_surf, sizeof(tmp_out_surf), NULL);

        res = hjkenc_push_context(avctx);
        if (res < 0)
            return res;

        res = process_output_surface(avctx, pkt, tmp_out_surf);

        res2 = hjkenc_pop_context(avctx);
        if (res2 < 0)
            return res2;

        if (res)
            return res;

        av_fifo_generic_write(ctx->unused_surface_queue, &tmp_out_surf, sizeof(tmp_out_surf), NULL);
    } else if (avctx->internal->draining) {
        return AVERROR_EOF;
    } else {
        return AVERROR(EAGAIN);
    }

    return 0;
}

av_cold void ff_hjkenc_encode_flush(AVCodecContext *avctx)
{
    HjkencContext *ctx = avctx->priv_data;

    hjkenc_send_frame(avctx, NULL);
    av_fifo_reset(ctx->timestamp_list);
}
