#ifdef USE_AFTERMATH
#include "vk_nv_aftermath.h"

#include "vk_common.h"

#include "xash3d_types.h"

#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"

#include <stdio.h>

static const char *aftermathErrorName(GFSDK_Aftermath_Result result) {
	switch (result) {
#define CASE(c) case c: return #c;
		CASE(GFSDK_Aftermath_Result_NotAvailable)
		CASE(GFSDK_Aftermath_Result_Fail)
		CASE(GFSDK_Aftermath_Result_FAIL_VersionMismatch)
		CASE(GFSDK_Aftermath_Result_FAIL_NotInitialized)
		CASE(GFSDK_Aftermath_Result_FAIL_InvalidAdapter)
		CASE(GFSDK_Aftermath_Result_FAIL_InvalidParameter)
		CASE(GFSDK_Aftermath_Result_FAIL_Unknown)
		CASE(GFSDK_Aftermath_Result_FAIL_ApiError)
		CASE(GFSDK_Aftermath_Result_FAIL_NvApiIncompatible)
		CASE(GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList)
		CASE(GFSDK_Aftermath_Result_FAIL_AlreadyInitialized)
		CASE(GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible)
		CASE(GFSDK_Aftermath_Result_FAIL_DriverInitFailed)
		CASE(GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported)
		CASE(GFSDK_Aftermath_Result_FAIL_OutOfMemory)
		CASE(GFSDK_Aftermath_Result_FAIL_GetDataOnBundle)
		CASE(GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext)
		CASE(GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled)
		CASE(GFSDK_Aftermath_Result_FAIL_NoResourcesRegistered)
		CASE(GFSDK_Aftermath_Result_FAIL_ThisResourceNeverRegistered)
		CASE(GFSDK_Aftermath_Result_FAIL_NotSupportedInUWP)
		CASE(GFSDK_Aftermath_Result_FAIL_D3dDllNotSupported)
		CASE(GFSDK_Aftermath_Result_FAIL_D3dDllInterceptionNotSupported)
		CASE(GFSDK_Aftermath_Result_FAIL_Disabled)
#undef CASE
	}

	return "UNKNOWN";
}

#define AM_CHECK(F) \
do { \
	GFSDK_Aftermath_Result result = F; \
	if (!GFSDK_Aftermath_SUCCEED(result)) { \
		gEngine.Con_Printf( S_ERROR "%s:%d " #F " failed (%#x): %s\n", \
			__FILE__, __LINE__, result, aftermathErrorName(result)); \
	} \
} while (0)

static qboolean writeFile(const char *filename, const void *data, size_t size) {
	FILE *f = fopen(filename, "wb");
	qboolean result = false;
	if (!f)
		return result;
	result = fwrite(data, 1, size, f) == size;
	fclose(f);
	return result;
}

static void callbackGpuCrashDump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData) {
	gEngine.Con_Printf(S_ERROR "AFTERMATH GPU CRASH DUMP: %p, size=%d\n", pGpuCrashDump, gpuCrashDumpSize);
	writeFile("ref_vk.nv-gpudmp", pGpuCrashDump, gpuCrashDumpSize);
}

static void callbackShaderDebugInfo(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData) {
    GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {0};
	gEngine.Con_Printf(S_ERROR "AFTERMATH Shader Debug Info: %p, size=%d\n", pShaderDebugInfo, shaderDebugInfoSize);

    AM_CHECK(GFSDK_Aftermath_GetShaderDebugInfoIdentifier(
        GFSDK_Aftermath_Version_API,
        pShaderDebugInfo,
        shaderDebugInfoSize,
        &identifier));

	char filename[64];
	Q_snprintf(filename, sizeof(filename), "shader-%016llX-%016llX.nvdbg", identifier.id[0], identifier.id[1]);
	writeFile(filename, pShaderDebugInfo, shaderDebugInfoSize);
}

static void callbackGpuCrashDumpDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addValue, void* pUserData) {
	gEngine.Con_Printf(S_ERROR "AFTERMATH asks for crash dump description\n");
	addValue(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "xash3d-fwgs-ref-vk");
	addValue(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, "v0.0.1");
}

static qboolean initialized = false;
qboolean VK_AftermathInit() {
    AM_CHECK(GFSDK_Aftermath_EnableGpuCrashDumps(
        GFSDK_Aftermath_Version_API,
        GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
        GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
        callbackGpuCrashDump,
        callbackShaderDebugInfo,
        callbackGpuCrashDumpDescription,
        NULL));

	initialized = true;
	return true;
}

void VK_AftermathShutdown() {
	if (initialized) {
		GFSDK_Aftermath_DisableGpuCrashDumps();
	}
}
#endif //ifdef USE_AFTERMATH
