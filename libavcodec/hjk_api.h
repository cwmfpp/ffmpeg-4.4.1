#ifndef AVCODEC_HJK_API_H
#define AVCODEC_HJK_API_H

#include <stdint.h>
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/hjk_check.h"

#define HJK_ENC_PIC_PARAMS_VER 1
#define HJK_ENC_RECONFIGURE_PARAMS_VER 1
#define HJK_ENC_LOCK_INPUT_BUFFER_VER 1
#define HJK_ENC_LOCK_BITSTREAM_VER 1
#define HJK_ENC_MAP_INPUT_RESOURCE_VER 1
#define HJK_ENC_REGISTER_RESOURCE_VER 1
#define HJK_ENC_SEQUENCE_PARAM_PAYLOAD_VER 1
#define HJK_ENC_CREATE_INPUT_BUFFER_VER 1
#define HJK_ENC_CREATE_BITSTREAM_BUFFER_VER 1
#define HJK_ENC_PRESET_CONFIG_VER 1
#define HJK_ENC_INITIALIZE_PARAMS_VER 1
#define HJK_ENC_CONFIG_VER 1
#define HJK_ENC_CAPS_PARAM_VER 1
#define HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER 1
#define HJKENCAPI_VERSION 1
#define HJK_ENCODE_API_FUNCTION_LIST_VER 1
#define HJKENCAPI_MAJOR_VERSION 1
#define HJKENCAPI_MINOR_VERSION 1

typedef enum {
    HJK_ENC_DEVICE_TYPE_CUDA,
    HJK_ENC_DEVICE_TYPE_DIRECTX,
}HJK_ENC_DEVICE_TYPE;

typedef enum {
    HJK_ENC_INPUT_RESOURCE_TYPE_HJKDEVICEPTR,
    HJK_ENC_INPUT_RESOURCE_TYPE_DIRECTX,
} HJK_ENC_INPUT_RESOURCE_TYPE;

typedef void * HJKstream;

typedef enum {
    HJK_ENC_PARAMS_RC_CONSTQP,
    HJK_ENC_PARAMS_RC_VBR_MINQP,
    HJK_ENC_PARAMS_RC_VBR_HQ,
    HJK_ENC_PARAMS_RC_VBR,
    HJK_ENC_PARAMS_RC_CBR,
    HJK_ENC_PARAMS_RC_CBR_HQ,
    HJK_ENC_PARAMS_RC_CBR_LOWDELAY_HQ,
} HJK_ENC_PARAMS_RC;

typedef struct HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS {
    int version;
    int apiVersion;
    void *device;
    int deviceType;
}HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS;

typedef enum {
    hjk_enc_caps_1,
}HJK_ENC_CAPS;

typedef enum {
    HJK_ENC_PIC_TYPE_IDR,
    HJK_ENC_PIC_TYPE_I,
    HJK_ENC_PIC_TYPE_P,
    HJK_ENC_PIC_TYPE_B,
    HJK_ENC_PIC_TYPE_BI,
};

typedef struct HJK_ENC_CAPS_PARAM{
    int version;
    HJK_ENC_CAPS capsToQuery;
}HJK_ENC_CAPS_PARAM;

typedef struct _CONST_QP {
    int qpInterP;
    int qpIntra;
    int qpInterB;
} CONST_QP;

typedef struct _INITIAL_RCQP {
    int qpInterP;
    int qpIntra;
    int qpInterB;
} INITIAL_RCQP;

typedef struct _MIN_QP {
    int qpInterP;
    int qpIntra;
    int qpInterB;
} MIN_QP;

typedef struct _MAX_QP {
    int qpInterP;
    int qpIntra;
    int qpInterB;
} MAX_QP;

typedef struct _HJK_ENC_RC_PARAMS {
    int averageBitRate;
    int maxBitRate;
    int multiPass;
    int lowDelayKeyFrameScale;
    int rateControlMode; //HJK_ENC_PARAMS_RC
    int vbvBufferSize;
    int enableAQ;
    int aqStrength;
    int enableTemporalAQ;
    int enableLookahead;
    int lookaheadDepth;
    char *disableIadapt; //"disabled" : "enabled",
    char *disableBadapt; //"disabled" : "enabled");
    int strictGOPTarget;
    int enableNonRefP;
    int zeroReorderDelay;
    int targetQuality;
    int targetQualityLSB;
    CONST_QP constQP;
    int enableInitialRCQP;
    INITIAL_RCQP initialRCQP;
    int enableMinQP;
    int enableMaxQP;
    MIN_QP minQP;
    MAX_QP maxQP
}HJK_ENC_RC_PARAMS;

typedef enum {
    FF_PROFILE_MJPEG_BASELINE,
    FF_PROFILE_MJPEG_MAIN,
    FF_PROFILE_MJPEG_HIGH,
    FF_PROFILE_MJPEG_HIGH_444_PREDICTIVE,
} FF_PROFILE_MJPEG;

typedef enum {
    HJK_ENC_MJPEG_PROFILE_BASELINE_GUID,
    HJK_ENC_MJPEG_PROFILE_MAIN_GUID,
    HJK_ENC_MJPEG_PROFILE_HIGH_GUID,
    HJK_ENC_MJPEG_PROFILE_HIGH_444_GUID,
} HJK_ENC_MJPEG_PROFILE;

typedef struct HJK_ENC_CONFIG_MJPEG_VUI_PARAMETERS {
    int colourMatrix;
    int colourPrimaries;
    int transferCharacteristics;
    int videoFullRangeFlag;
    int colourDescriptionPresentFlag;
    int videoSignalTypePresentFlag;
    int videoFormat;
}HJK_ENC_CONFIG_MJPEG_VUI_PARAMETERS;

typedef struct _HJK_ENC_SEI_PAYLOAD {
                uint32_t payloadSize;
                int payloadType;//SEI_TYPE_TIME_CODE;
                uint8_t *payload;
}HJK_ENC_SEI_PAYLOAD;

typedef struct _HJK_ENC_CONFIG_MJPEG {
    int sliceMode;
    int sliceModeData;
    HJK_ENC_SEI_PAYLOAD *seiPayloadArray;
    int seiPayloadArrayCnt;
    int disableSPSPPS;
    int repeatSPSPPS;
    int outputAUD;
    int maxNumRefFrames;
    int idrPeriod;
    int outputBufferingPeriodSEI;
    int outputPictureTimingSEI;
    int adaptiveTransformMode; // HJK_ENC_MJPEG_ADAPTIVE_TRANSFORM ;
    int fmoMode;               // HJK_ENC_MJPEG_FMO;
    int qpPrimeYZeroTransformBypassFlag;
    int chromaFormatIDC;
    int level;
    int entropyCodingMode;
    int useBFramesAsRef;
    int numRefL0;
    int numRefL1;
    HJK_ENC_CONFIG_MJPEG_VUI_PARAMETERS mjpegVUIParameters;
}HJK_ENC_CONFIG_MJPEG;

typedef enum{
    HJK_ENC_LEVEL_HEVC_51,
}HJK_ENC_LEVEL_HEVC;

typedef enum {
    HJK_ENC_TIER_HEVC_HIGH,
}HJK_ENC_TIER_HEVC;

enum {
    HJK_ENC_HEVC_PROFILE_MAIN_GUID,
    HJK_ENC_HEVC_PROFILE_MAIN10_GUID,
    HJK_ENC_HEVC_PROFILE_FREXT_GUID,
}HJK_ENC_HEVC_PROFILE_GUID;

typedef struct _HJK_ENC_CONFIG_HEVC_VUI_PARAMETERS {
    int colourMatrix;
    int colourPrimaries;
    int transferCharacteristics;
    int videoFullRangeFlag;
    int colourDescriptionPresentFlag;
    int videoSignalTypePresentFlag;
    int videoFormat;
}HJK_ENC_CONFIG_HEVC_VUI_PARAMETERS;

typedef struct _HJK_ENC_CONFIG_HEVC {
    int sliceMode;
    int sliceModeData;
    HJK_ENC_SEI_PAYLOAD *seiPayloadArray;
    int seiPayloadArrayCnt;
    int disableSPSPPS;
    int repeatSPSPPS;
    int outputAUD;
    int maxNumRefFramesInDPB;
    int idrPeriod;
    int outputBufferingPeriodSEI;
    int outputPictureTimingSEI;
    int chromaFormatIDC;
    int pixelBitDepthMinus8;
    int level;
    int tier;
    int useBFramesAsRef;
    int numRefL0;
    int numRefL1;
    HJK_ENC_CONFIG_HEVC_VUI_PARAMETERS hevcVUIParameters;
}HJK_ENC_CONFIG_HEVC;

typedef struct HJK_ENCODE_CODEC_CONFIG {
    HJK_ENC_CONFIG_MJPEG mjpegConfig;
    HJK_ENC_CONFIG_HEVC hevcConfig;
}HJK_ENCODE_CODEC_CONFIG;

typedef enum{
    HJK_ENC_PARAMS_FRAME_FIELD_MODE_FIELD,
    HJK_ENC_PARAMS_FRAME_FIELD_MODE_FRAME,
}HJK_ENC_PARAMS_FRAME_FIELD_MODE;

typedef enum {
    HJK_ENC_MJPEG_ADAPTIVE_TRANSFORM_ENABLE,
}HJK_ENC_MJPEG_ADAPTIVE_TRANSFORM;

typedef enum {
    HJK_ENC_MJPEG_FMO_DISABLE,
}HJK_ENC_MJPEG_FMO;

typedef struct HJK_ENC_CONFIG {
    int version;
    int frameIntervalP;
    int gopLength;
    HJK_ENC_PARAMS_FRAME_FIELD_MODE frameFieldMode;
    int profileGUID;
    HJK_ENC_RC_PARAMS rcParams;
    HJK_ENCODE_CODEC_CONFIG encodeCodecConfig;
}HJK_ENC_CONFIG;

typedef struct HJK_ENC_PRESET_CONFIG {
    int version;
    HJK_ENC_CONFIG presetCfg;
}HJK_ENC_PRESET_CONFIG;

typedef enum {
    HJK_ENC_CODEC_MJPEG_GUID,
    HJK_ENC_CODEC_HEVC_GUID,
    HJK_ENC_PRESET_DEFAULT_GUID,
    HJK_ENC_PRESET_HP_GUID,
    HJK_ENC_PRESET_HQ_GUID,
    HJK_ENC_PRESET_BD_GUID,
    HJK_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID,
    HJK_ENC_PRESET_LOW_LATENCY_HP_GUID,
    HJK_ENC_PRESET_LOW_LATENCY_HQ_GUID,
    HJK_ENC_PRESET_LOSSLESS_DEFAULT_GUID,
    HJK_ENC_PRESET_LOSSLESS_HP_GUID,
} HJK_ENC_CODEC_GUID;

typedef HJK_ENC_CODEC_GUID GUID;

typedef enum {
    HJK_ENC_CAPS_SUPPORT_YUV444_ENCODE,
    HJK_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE,
    HJK_ENC_CAPS_WIDTH_MAX,
    HJK_ENC_CAPS_HEIGHT_MAX,
    HJK_ENC_CAPS_NUM_MAX_BFRAMES,
    HJK_ENC_CAPS_SUPPORT_FIELD_ENCODING,
    HJK_ENC_CAPS_SUPPORT_10BIT_ENCODE,
    HJK_ENC_CAPS_SUPPORT_LOOKAHEAD,
    HJK_ENC_CAPS_SUPPORT_TEMPORAL_AQ,
    HJK_ENC_CAPS_SUPPORT_WEIGHTED_PREDICTION,
    HJK_ENC_CAPS_SUPPORT_CABAC,
    HJK_ENC_CAPS_SUPPORT_BFRAME_REF_MODE,
    HJK_ENC_CAPS_SUPPORT_MULTIPLE_REF_FRAMES,
    HJK_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE,
} HJK_ENC_CAPS_SUPPORT;

typedef enum {
    HJK_ENC_MJPEG_ENTROPY_CODING_MODE_CABAC
}HJK_ENC_MJPEG_ENTROPY_CODING_MODE;

typedef struct _HJK_ENC_INITIALIZE_PARAMS {
    int presetGUID;
    int encodeGUID; // HJK_ENC_CODEC_GUID 
    int version;    // HJK_ENC_INITIALIZE_PARAMS_VER;
    int encodeHeight;
    int encodeWidth;
    HJK_ENC_CONFIG *encodeConfig;
    int tuningInfo; // HJK_ENC_TUNING_INFO_LOSSLESS;
    int darHeight;
    int darWidth;
    int frameRateNum;
    int frameRateDen;
    int enableEncodeAsync;
    int enablePTD;
    int enableWeightedPrediction;
} HJK_ENC_INITIALIZE_PARAMS;

typedef enum {
    HJK_ENC_BUFFER_FORMAT_YV12_PL,
    HJK_ENC_BUFFER_FORMAT_HJK12_PL,
    HJK_ENC_BUFFER_FORMAT_YUV420_10BIT,
    HJK_ENC_BUFFER_FORMAT_YUV444_PL,
    HJK_ENC_BUFFER_FORMAT_YUV444_10BIT,
    HJK_ENC_BUFFER_FORMAT_ARGB,
    HJK_ENC_BUFFER_FORMAT_ABGR,
    HJK_ENC_BUFFER_FORMAT_UNDEFINED,
} HJK_ENC_BUFFER_FORMAT;

typedef void *HJK_ENC_INPUT_PTR;
typedef void *HJK_ENC_OUTPUT_PTR;
typedef void *HJK_ENC_REGISTERED_PTR;

typedef struct _HJK_ENC_CREATE_INPUT_BUFFER {
    int version; // HJK_ENC_CREATE_INPUT_BUFFER_VER;
    int width;
    int height;
    HJK_ENC_BUFFER_FORMAT bufferFmt;
    HJK_ENC_INPUT_PTR inputBuffer;
} HJK_ENC_CREATE_INPUT_BUFFER;

typedef struct _HJK_ENC_CREATE_BITSTREAM_BUFFER {
    int version; // HJK_ENC_CREATE_BITSTREAM_BUFFER_VER;
    void *bitstreamBuffer;
} HJK_ENC_CREATE_BITSTREAM_BUFFER;

typedef struct _HJK_ENC_SEQUENCE_PARAM_PAYLOAD {
    int version; // HJK_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    void *spsppsBuffer;
    int inBufferSize;
    int *outSPSPPSPayloadSize;
} HJK_ENC_SEQUENCE_PARAM_PAYLOAD;

typedef enum {
    HJK_ENC_PIC_FLAG_EOS,
    HJK_ENC_PIC_FLAG_FORCEIDR,
    HJK_ENC_PIC_FLAG_FORCEINTRA,
} HJK_ENC_PIC_FLAG;

typedef enum {
    HJK_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM,
    HJK_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP,
    HJK_ENC_PIC_STRUCT_FRAME,
} HJK_ENC_PIC_STRUCT;

typedef struct _CODEC_PIC_PARAMS{
    HJK_ENC_CONFIG_MJPEG mjpegPicParams;
    HJK_ENC_CONFIG_HEVC hevcPicParams;
}CODEC_PIC_PARAMS;

typedef struct _HJK_ENC_PIC_PARAMS {
    int version; // HJK_ENC_PIC_PARAMS_VER;
    HJK_ENC_INPUT_PTR inputBuffer;
    int bufferFmt;
    int inputWidth;
    int inputHeight;
    int inputPitch;
    HJK_ENC_OUTPUT_PTR outputBitstream;
    int pictureStruct;  // HJK_ENC_PIC_STRUCT;
    int inputTimeStamp; // frame->pts;
    int encodePicFlags; // HJK_ENC_PIC_FLAG;
    CODEC_PIC_PARAMS codecPicParams;
} HJK_ENC_PIC_PARAMS;

typedef struct _HJK_ENC_MAP_INPUT_RESOURCE {
    int version; // HJK_ENC_MAP_INPUT_RESOURCE_VER;
    HJK_ENC_REGISTERED_PTR registeredResource;
    HJK_ENC_INPUT_PTR mappedResource;
    HJK_ENC_BUFFER_FORMAT mappedBufferFmt;
} HJK_ENC_MAP_INPUT_RESOURCE;

typedef struct _HJK_ENC_REGISTER_RESOURCE {
    int version; // HJK_ENC_REGISTER_RESOURCE_VER;
    int width;
    int height;
    int pitch;
    int resourceToRegister;
    int resourceType; // HJK_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR
                      // HJK_ENC_INPUT_RESOURCE_TYPE_DIRECTX
    intptr_t subResourceIndex;
    HJK_ENC_BUFFER_FORMAT bufferFormat;
    HJK_ENC_REGISTERED_PTR registeredResource;
} HJK_ENC_REGISTER_RESOURCE;

typedef struct _HJK_ENC_LOCK_BITSTREAM {
    int version; // HJK_ENC_LOCK_BITSTREAM_VER;
    int doNotWait;
    HJK_ENC_OUTPUT_PTR outputBitstream;
    int sliceOffsets;
    int64_t bitstreamSizeInBytes;
    void *bitstreamBufferPtr;
    int pictureType;
    int frameAvgQP;
    int outputTimeStamp;
} HJK_ENC_LOCK_BITSTREAM;

typedef struct _HJK_ENC_LOCK_INPUT_BUFFER {
    int version;
    HJK_ENC_INPUT_PTR inputBuffer;
    int pitch;
    uint8_t *bufferDataPtr;
}HJK_ENC_LOCK_INPUT_BUFFER;

typedef struct _HJK_ENC_RECONFIGURE_PARAMS {
    int version; // HJK_ENC_RECONFIGURE_PARAMS_VER;
    int resetEncoder;
    int forceIDR;
    HJK_ENC_INITIALIZE_PARAMS reInitEncodeParams;
} HJK_ENC_RECONFIGURE_PARAMS;

typedef struct _HJK_ENCODE_API_FUNCTION_LIST{
    int version;
    int (*hjkEncGetLastErrorString)(void *handle);
    int (*hjkEncOpenEncodeSessionEx)(
        HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *open_params, void **handle);
    int (*hjkEncGetEncodeGUIDCount)(void *handle, int *count);
    int (*hjkEncGetEncodeGUIDs)(void *handle, void *guid, int count,
                                int *ptr_count);
    int (*hjkEncGetEncodeCaps)(void *handle, int encodeGUID,
                               HJK_ENC_CAPS_PARAM *caps_params, int *val);
    int (*hjkEncDestroyEncoder)(void *handle);
    int (*hjkEncGetEncodePresetConfigEx)(void *handle, int encodeGUID,
                                         int presetGUID, int tuningInfo,
                                         HJK_ENC_PRESET_CONFIG *preset_config);

    int (*hjkEncGetEncodePresetConfig)(void *handle,
                                       int encodeGUID,
                                       int presetGUID,
                                       HJK_ENC_PRESET_CONFIG *preset_config);

    int (*hjkEncInitializeEncoder)(void *handle, HJK_ENC_INITIALIZE_PARAMS *init_encode_params);
    int (*hjkEncSetIOCudaStreams)(void *handle, HJKstream *cu_stream,
                                  HJKstream *cu_stream1);
    int (*hjkEncCreateInputBuffer)(void *handle, HJK_ENC_CREATE_INPUT_BUFFER *allocSurf);
    int (*hjkEncCreateBitstreamBuffer)(void *handle, HJK_ENC_CREATE_BITSTREAM_BUFFER *allocOut);
    int (*hjkEncDestroyInputBuffer)(void *handle,
                                    HJK_ENC_INPUT_PTR input_surface);
    int (*hjkEncGetSequenceParams)(void *handle, HJK_ENC_SEQUENCE_PARAM_PAYLOAD *payload);
    int (*hjkEncEncodePicture)(void *handle, HJK_ENC_PIC_PARAMS *params);
    int (*hjkEncUnmapInputResource)(
        void *handle, HJK_ENC_INPUT_PTR mappedResource);
    int (*hjkEncUnregisterResource)(void *handle,
                                    HJK_ENC_REGISTERED_PTR regptr);
    int (*hjkEncDestroyBitstreamBuffer)(void *handle,
                                        HJK_ENC_OUTPUT_PTR output_surface);
    int (*hjkEncRegisterResource)(void *handle, HJK_ENC_REGISTER_RESOURCE *reg);
    int (*hjkEncMapInputResource)(void *handle,
                                  HJK_ENC_MAP_INPUT_RESOURCE *in_map);
    int (*hjkEncLockInputBuffer)(void *handle, HJK_ENC_LOCK_INPUT_BUFFER *lockBufferParams);
    int (*hjkEncUnlockInputBuffer)(void *handle, HJK_ENC_INPUT_PTR input_surface);
    int (*hjkEncLockBitstream)(void *handle, HJK_ENC_LOCK_BITSTREAM *lock_params);
    int (*hjkEncUnlockBitstream)(void *handle, HJK_ENC_OUTPUT_PTR output_surface);
    int (*hjkEncReconfigureEncoder)(void *handle, HJK_ENC_RECONFIGURE_PARAMS *params);

}HJK_ENCODE_API_FUNCTION_LIST;

typedef void *HJKcontext;
typedef struct _HJcontext {

}HJcontext;

typedef void * HJdevice;

typedef struct _HjkFunctions {
    int (*hjkCtxPushCurrent)(HJKcontext hjk_context);
    int (*hjkCtxPopCurrent)(HJcontext *dummy);
    int (*hjkDeviceGet)(HJdevice * cu_device, int idx);
    int (*hjkDeviceGetName)(char *name, int name_size, HJdevice cu_device);
    int (*hjkDeviceComputeCapability)(int *major, int *minor,
                                      HJdevice cu_device);
    int (*hjkCtxCreate)(HJKcontext * hjk_context_internal, int size,
                        HJdevice cu_device);
    int (*hjkCtxDestroy)(HJKcontext * hjk_context_internal);
    int (*hjkInit)(int size);
    int (*hjkDeviceGetCount)(int *nb_devices);
    hjk_check_GetErrorName_cb *hjkGetErrorName;
} HjkFunctions;

typedef struct _HjkencFunctions {
    int (*HjkEncodeAPIGetMaxSupportedVersion)(uint32_t *hjkenc_max_ver);
    int (*HjkEncodeAPICreateInstance)(HJK_ENCODE_API_FUNCTION_LIST *hjkenc_funcs);
} HjkencFunctions;

typedef enum {
    HJK_ENC_SUCCESS,
    HJK_ENC_ERR_NO_ENCODE_DEVICE,
    HJK_ENC_ERR_UNSUPPORTED_DEVICE,
    HJK_ENC_ERR_INVALID_ENCODERDEVICE,
    HJK_ENC_ERR_INVALID_DEVICE,
    HJK_ENC_ERR_DEVICE_NOT_EXIST,
    HJK_ENC_ERR_INVALID_PTR,
    HJK_ENC_ERR_INVALID_EVENT,
    HJK_ENC_ERR_INVALID_PARAM,
    HJK_ENC_ERR_INVALID_CALL,
    HJK_ENC_ERR_OUT_OF_MEMORY,
    HJK_ENC_ERR_ENCODER_NOT_INITIALIZED,
    HJK_ENC_ERR_UNSUPPORTED_PARAM,
    HJK_ENC_ERR_LOCK_BUSY,
    HJK_ENC_ERR_NOT_ENOUGH_BUFFER,
    HJK_ENC_ERR_INVALID_VERSION,
    HJK_ENC_ERR_MAP_FAILED,
    HJK_ENC_ERR_NEED_MORE_INPUT,
    HJK_ENC_ERR_ENCODER_BUSY,
    HJK_ENC_ERR_EVENT_NOT_REGISTERD,
    HJK_ENC_ERR_GENERIC,
    HJK_ENC_ERR_INCOMPATIBLE_CLIENT_KEY,
    HJK_ENC_ERR_UNIMPLEMENTED,
    HJK_ENC_ERR_RESOURCE_REGISTER_FAILED,
    HJK_ENC_ERR_RESOURCE_NOT_REGISTERED,
    HJK_ENC_ERR_RESOURCE_NOT_MAPPED,
} HJKENCSTATUS;

int hjk_load_functions(HjkFunctions **hjk_dl, void *avctx);
int hjkenc_load_functions(HjkencFunctions **hjkenc_dl, void *avctx);
void hjkenc_free_functions(HjkencFunctions *hjkenc_dl);
void hjk_free_functions(HjkFunctions *hjk_dl);

#endif /* AVCODEC_HJK_API_H */
