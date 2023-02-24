#include "vk_nv_aftermath.h"

#include "vk_common.h"
#include "vk_core.h"

#include "xash3d_types.h"

#ifdef USE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"
#endif // ifdef USE_AFTERMATH

#include <stdio.h>

#define MAX_NV_CHECKPOINTS 2048

typedef struct {
	unsigned sequence;
	char message[256];
} vk_nv_checkpoint_entry_t;

static struct {
	unsigned sequence;
	vk_nv_checkpoint_entry_t entries[MAX_NV_CHECKPOINTS];
} g_nv_checkpoint = {0};

#ifdef USE_AFTERMATH
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


static const char obsolete[] = "[OBSOLETE]";

static void callbackResolveMarkers(const void* pMarker, void* pUserData, void** resolvedMarkerData, uint32_t* markerSize) {
	const unsigned sequence = (uintptr_t)pMarker;
	const vk_nv_checkpoint_entry_t *const entry = g_nv_checkpoint.entries + (sequence % MAX_NV_CHECKPOINTS);

	const char *msg = entry->sequence == sequence ? entry->message : obsolete;
	gEngine.Con_Reportf(S_ERROR "resolved marker %u: msg: %s\n", sequence, msg);

	*resolvedMarkerData = (void*)msg;
	*markerSize = strlen(msg);
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
		callbackResolveMarkers,
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

void R_Vk_NV_CheckpointF(VkCommandBuffer cmdbuf, const char *fmt, ...) {
	ASSERT(vkCmdSetCheckpointNV);

	va_list argptr;
	++g_nv_checkpoint.sequence;

	vk_nv_checkpoint_entry_t *entry = g_nv_checkpoint.entries + (g_nv_checkpoint.sequence % MAX_NV_CHECKPOINTS);
	entry->sequence = g_nv_checkpoint.sequence;

	va_start( argptr, fmt );
	vsnprintf( entry->message, sizeof entry->message, fmt, argptr );
	va_end( argptr );

	const uintptr_t marker = entry->sequence;
	vkCmdSetCheckpointNV(cmdbuf, (const void*)marker);
}

void R_Vk_NV_Checkpoint_Dump(void) {
	ASSERT(vkGetQueueCheckpointDataNV);

	uint32_t checkpoints_count = 0;
	vkGetQueueCheckpointDataNV(vk_core.queue, &checkpoints_count, NULL);

	VkCheckpointDataNV checkpoints[32];
	if (checkpoints_count > COUNTOF(checkpoints))
		checkpoints_count = COUNTOF(checkpoints);

	for (int i = 0; i < checkpoints_count; ++i) {
		checkpoints[i].pNext = NULL;
		checkpoints[i].sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
	}

	vkGetQueueCheckpointDataNV(vk_core.queue, &checkpoints_count, checkpoints);

	gEngine.Con_Reportf(S_ERROR "Checkpoints: %d\n", checkpoints_count);
	for (int i = 0; i < checkpoints_count; ++i) {
		const VkCheckpointDataNV *const checkpoint = checkpoints + i;
		const unsigned sequence = (uintptr_t)checkpoint->pCheckpointMarker;
		const vk_nv_checkpoint_entry_t *const entry = g_nv_checkpoint.entries + (sequence % MAX_NV_CHECKPOINTS);
		gEngine.Con_Reportf(S_ERROR "\t%u: stage=%04x msg: %s\n", sequence, checkpoint->stage, entry->sequence == sequence ? entry->message : "[OBSOLETE]");
	}
}

