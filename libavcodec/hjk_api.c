#include "hjk_api.h" 

#include <stdio.h>
#include <string.h>

static int hjk_CtxPushCurrent(HJKcontext hjk_context)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_CtxPopCurrent(HJcontext *dummy)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_DeviceGet(HJdevice * cu_device, int idx)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_DeviceGetName(char *name, int name_size, HJdevice cu_device)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_DeviceComputeCapability(int *major, int *minor,
								  HJdevice cu_device)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_CtxCreate(HJKcontext * hjk_context_internal, int size,
					HJdevice cu_device)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_CtxDestroy(HJKcontext * hjk_context_internal)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_Init(int size)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_DeviceGetCount(int *nb_devices)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_check_GetErrorName(HJKresult error, const char** pstr)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static HjkFunctions hjk_functions = {
    .hjkCtxPushCurrent = hjk_CtxPushCurrent,
    .hjkCtxPopCurrent = hjk_CtxPopCurrent,
    .hjkDeviceGet = hjk_DeviceGet,
    .hjkDeviceGetName = hjk_DeviceGetName,
    .hjkDeviceComputeCapability = hjk_DeviceComputeCapability,
    .hjkCtxCreate = hjk_CtxCreate,
    .hjkCtxDestroy = hjk_CtxDestroy,
    .hjkInit = hjk_Init,
    .hjkDeviceGetCount = hjk_DeviceGetCount,
    .hjkGetErrorName = hjk_check_GetErrorName,
};


int hjk_load_functions(HjkFunctions **hjk_dl, void *avctx)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    *hjk_dl = &hjk_functions;

    return 0;
}

static int hjk_EncodeAPIGetMaxSupportedVersion(uint32_t *hjkenc_max_ver)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

////
static int hjk_EncGetLastErrorString(void *handle)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncOpenEncodeSessionEx(
	HJK_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *open_params, void **handle)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncGetEncodeGUIDCount(void *handle, int *count)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncGetEncodeGUIDs(void *handle, void *guid, int count,
							int *ptr_count)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncGetEncodeCaps(void *handle, int encodeGUID,
						   HJK_ENC_CAPS_PARAM *caps_params, int *val)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncDestroyEncoder(void *handle)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncGetEncodePresetConfigEx(void *handle, int encodeGUID,
									 int presetGUID, int tuningInfo,
									 HJK_ENC_PRESET_CONFIG *preset_config)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncGetEncodePresetConfig(void *handle,
								   int encodeGUID,
								   int presetGUID,
								   HJK_ENC_PRESET_CONFIG *preset_config)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncInitializeEncoder(void *handle, HJK_ENC_INITIALIZE_PARAMS *init_encode_params)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncSetIOCudaStreams(void *handle, HJKstream *cu_stream,
							  HJKstream *cu_stream1)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncCreateInputBuffer(void *handle, HJK_ENC_CREATE_INPUT_BUFFER *allocSurf)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncCreateBitstreamBuffer(void *handle, HJK_ENC_CREATE_BITSTREAM_BUFFER *allocOut)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncDestroyInputBuffer(void *handle,
								HJK_ENC_INPUT_PTR input_surface)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncGetSequenceParams(void *handle, HJK_ENC_SEQUENCE_PARAM_PAYLOAD *payload)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncEncodePicture(void *handle, HJK_ENC_PIC_PARAMS *params)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncUnmapInputResource(
	void *handle, HJK_ENC_INPUT_PTR mappedResource)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncUnregisterResource(void *handle,
								HJK_ENC_REGISTERED_PTR regptr)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncDestroyBitstreamBuffer(void *handle,
									HJK_ENC_OUTPUT_PTR output_surface)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncRegisterResource(void *handle, HJK_ENC_REGISTER_RESOURCE *reg)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncMapInputResource(void *handle,
							  HJK_ENC_MAP_INPUT_RESOURCE *in_map)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncLockInputBuffer(void *handle, HJK_ENC_LOCK_INPUT_BUFFER *lockBufferParams)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncUnlockInputBuffer(void *handle, HJK_ENC_INPUT_PTR input_surface)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncLockBitstream(void *handle, HJK_ENC_LOCK_BITSTREAM *lock_params)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncUnlockBitstream(void *handle, HJK_ENC_OUTPUT_PTR output_surface)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

static int hjk_EncReconfigureEncoder(void *handle, HJK_ENC_RECONFIGURE_PARAMS *params)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return 0;
}

HJK_ENCODE_API_FUNCTION_LIST hjk_encode_api_function_list_member = {
    .hjkEncGetLastErrorString = hjk_EncGetLastErrorString,
    .hjkEncOpenEncodeSessionEx = hjk_EncOpenEncodeSessionEx,
    .hjkEncGetEncodeGUIDCount = hjk_EncGetEncodeGUIDCount,
    .hjkEncGetEncodeGUIDs = hjk_EncGetEncodeGUIDs,
    .hjkEncGetEncodeCaps = hjk_EncGetEncodeCaps,
    .hjkEncDestroyEncoder = hjk_EncDestroyEncoder,
    .hjkEncGetEncodePresetConfigEx = hjk_EncGetEncodePresetConfigEx,
    .hjkEncGetEncodePresetConfig = hjk_EncGetEncodePresetConfig,
    .hjkEncInitializeEncoder = hjk_EncInitializeEncoder,
    .hjkEncSetIOCudaStreams = hjk_EncSetIOCudaStreams,
    .hjkEncCreateInputBuffer = hjk_EncCreateInputBuffer,
    .hjkEncCreateBitstreamBuffer = hjk_EncCreateBitstreamBuffer,
    .hjkEncDestroyInputBuffer = hjk_EncDestroyInputBuffer,
    .hjkEncGetSequenceParams = hjk_EncGetSequenceParams,
    .hjkEncEncodePicture = hjk_EncEncodePicture,
    .hjkEncUnmapInputResource = hjk_EncUnmapInputResource,
    .hjkEncUnregisterResource = hjk_EncUnregisterResource,
    .hjkEncDestroyBitstreamBuffer = hjk_EncDestroyBitstreamBuffer,
    .hjkEncRegisterResource = hjk_EncRegisterResource,
    .hjkEncMapInputResource = hjk_EncMapInputResource,
    .hjkEncLockInputBuffer = hjk_EncLockInputBuffer,
    .hjkEncUnlockInputBuffer = hjk_EncUnlockInputBuffer,
    .hjkEncLockBitstream = hjk_EncLockBitstream,
    .hjkEncUnlockBitstream = hjk_EncUnlockBitstream,
    .hjkEncReconfigureEncoder = hjk_EncReconfigureEncoder,
};

////
static int hjk_EncodeAPICreateInstance(HJK_ENCODE_API_FUNCTION_LIST *hjkenc_funcs)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    memcpy(hjkenc_funcs, &hjk_encode_api_function_list_member,
           sizeof(hjk_encode_api_function_list_member));

    return 0;
}

static HjkencFunctions hjk_enc_functions = {
    .HjkEncodeAPIGetMaxSupportedVersion = hjk_EncodeAPIGetMaxSupportedVersion,
    .HjkEncodeAPICreateInstance = hjk_EncodeAPICreateInstance,
};

int hjkenc_load_functions(HjkencFunctions **hjkenc_dl, void *avctx)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    *hjkenc_dl = &hjk_enc_functions;

    return 0;
}


void hjkenc_free_functions(HjkencFunctions *hjkenc_dl)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return;
}

void hjk_free_functions(HjkFunctions *hjk_dl)
{
    printf("%d %s \n", __LINE__, __FUNCTION__);

    return;
}