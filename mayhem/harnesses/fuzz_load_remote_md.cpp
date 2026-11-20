// Fuzz nixlAgent::loadRemoteMD: the public entry point that deserializes a peer
// agent's metadata blob (nixlSerDes tagged format: agent name, per-backend
// connection info, remote memory sections / descriptor lists). The same blob
// arrives from peers over the P2P listener, etcd or the TCP store.
//
// One agent (with the remote-capable FUZZ backend plugin, fuzz_backend.h) is kept
// across inputs because creating an agent costs ~0.2 s. State kept on purpose: the
// agent's per-remote caches. After every input the remote agent the blob names is
// invalidated (whether or not the load succeeded: a failed load can leave cached
// connection info behind), so each input starts from an empty remote-agent table.
#include <cstddef>
#include <cstdint>
#include <string>

#include "nixl.h"
#include "serdes/serdes.h"

static const char kLocalName[] = "fuzz-local";

static nixlAgent *agent() {
    static nixlAgent *a = [] {
        auto *ag = new nixlAgent(kLocalName, nixlAgentConfig(false));
        nixlBackendH *bh = nullptr;
        if (ag->createBackend("FUZZ", nixl_b_params_t(), bh) != NIXL_SUCCESS) {
            __builtin_trap();
        }
        return ag;
    }();
    return a;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    nixlAgent *a = agent();
    const nixl_blob_t blob(reinterpret_cast<const char *>(data), size);
    std::string name;
    if (a->loadRemoteMD(blob, name) == NIXL_SUCCESS) {
        nixl_xfer_dlist_t descs(DRAM_SEG);
        a->checkRemoteMD(name, descs);
    }

    // Reset: drop whatever this input made the agent cache.
    nixlSerDes sd;
    if (sd.importStr(blob) == NIXL_SUCCESS) {
        const std::string remote = sd.getStr("Agent");
        if (!remote.empty() && remote != kLocalName) {
            a->invalidateRemoteMD(remote);
        }
    }
    return 0;
}
