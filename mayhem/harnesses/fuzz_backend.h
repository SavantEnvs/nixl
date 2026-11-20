// Minimal remote-capable backend for the fuzz harnesses, built as the plugin
// libplugin_FUZZ.so (fuzz_backend_plugin.cpp) in the plugin directory next to libnixl.so.
// nixlAgent only stores and validates a peer's memory sections for backends that
// support remote transfers (POSIX does not), so without one loadRemoteMD would skip
// nixlRemoteSection::loadRemoteData's descriptor handling. This engine accepts any
// connection info and wraps any public-metadata blob; it does no transfers.
#pragma once
#include <string>

#include "backend/backend_engine.h"
#include "backend/backend_plugin.h"

namespace nixl_fuzz {

class FuzzMD : public nixlBackendMD {
public:
    explicit FuzzMD(std::string b) : nixlBackendMD(false), blob(std::move(b)) {}
    std::string blob;
};

class FuzzEngine : public nixlBackendEngine {
public:
    explicit FuzzEngine(const nixlBackendInitParams *p)
        : nixlBackendEngine(p, nixl_backend_capability_t::REMOTE | nixl_backend_capability_t::NOTIF) {}

    bool supportsNotif() const override { return true; }
    nixl_mem_list_t getSupportedMems() const override {
        return {DRAM_SEG, VRAM_SEG, BLK_SEG, OBJ_SEG, FILE_SEG};
    }
    nixl_status_t registerMem(const nixlBlobDesc &mem, const nixl_mem_t &, nixlBackendMD *&out) override {
        out = new FuzzMD(mem.metaInfo);
        return NIXL_SUCCESS;
    }
    nixl_status_t deregisterMem(nixlBackendMD *meta) override { delete meta; return NIXL_SUCCESS; }
    nixl_status_t connect(const std::string &) override { return NIXL_SUCCESS; }
    nixl_status_t disconnect(const std::string &) override { return NIXL_SUCCESS; }
    nixl_status_t unloadMD(nixlBackendMD *input) override { delete input; return NIXL_SUCCESS; }
    nixl_status_t prepXfer(const nixl_xfer_op_t &, const nixl_meta_dlist_t &, const nixl_meta_dlist_t &,
                           const std::string &, nixlBackendReqH *&, const nixl_opt_b_args_t *) const override {
        return NIXL_ERR_NOT_SUPPORTED;
    }
    nixl_status_t postXfer(const nixl_xfer_op_t &, const nixl_meta_dlist_t &, const nixl_meta_dlist_t &,
                           const std::string &, nixlBackendReqH *&, const nixl_opt_b_args_t *) const override {
        return NIXL_ERR_NOT_SUPPORTED;
    }
    nixl_status_t checkXfer(nixlBackendReqH *) const override { return NIXL_ERR_NOT_SUPPORTED; }
    nixl_status_t releaseReqH(nixlBackendReqH *) const override { return NIXL_SUCCESS; }
    nixl_status_t getPublicData(const nixlBackendMD *meta, std::string &str) const override {
        str = static_cast<const FuzzMD *>(meta)->blob;
        return NIXL_SUCCESS;
    }
    nixl_status_t getConnInfo(std::string &str) const override {
        str = "fuzz-conn-info";
        return NIXL_SUCCESS;
    }
    nixl_status_t loadRemoteConnInfo(const std::string &, const std::string &) override {
        return NIXL_SUCCESS;
    }
    nixl_status_t loadRemoteMD(const nixlBlobDesc &input, const nixl_mem_t &, const std::string &,
                               nixlBackendMD *&output) override {
        output = new FuzzMD(input.metaInfo);
        return NIXL_SUCCESS;
    }
    nixl_status_t getNotifs(notif_list_t &) override { return NIXL_SUCCESS; }
    nixl_status_t genNotif(const std::string &, const std::string &) const override { return NIXL_SUCCESS; }
};

} // namespace nixl_fuzz
