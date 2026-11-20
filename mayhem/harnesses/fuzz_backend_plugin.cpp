// Plugin entry points for the FUZZ backend (see fuzz_backend.h).
#include "fuzz_backend.h"

extern "C" NIXL_PLUGIN_EXPORT nixlBackendPlugin *
nixl_plugin_init() {
    return nixlBackendPluginCreator<nixl_fuzz::FuzzEngine>::create(
        NIXL_PLUGIN_API_VERSION, "FUZZ", "0.1.0", nixl_b_params_t(),
        {DRAM_SEG, VRAM_SEG, BLK_SEG, OBJ_SEG, FILE_SEG});
}

extern "C" NIXL_PLUGIN_EXPORT void
nixl_plugin_fini() {}
