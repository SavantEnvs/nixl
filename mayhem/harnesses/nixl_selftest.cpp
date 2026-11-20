// Known-answer self-test for the code the fuzz targets exercise (run by mayhem/test.sh):
// nixlSerDes, descriptor-list (de)serialization and the agent metadata exchange
// (getLocalMD -> loadRemoteMD -> checkRemoteMD -> invalidateRemoteMD).
// Prints "PASS <name>" / "FAIL <name>" per check; with -o DIR also writes the
// metadata blobs it produced (used to make the committed fuzz seeds).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "nixl.h"
#include "serdes/serdes.h"

static int failures = 0;
static void check(bool ok, const char *name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

static std::string outDir;
static void save(const std::string &name, const std::string &prefix, const std::string &blob) {
    if (outDir.empty()) return;
    std::ofstream(outDir + "/" + name, std::ios::binary) << prefix << blob;
}

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "-o") outDir = argv[2];

    // --- serdes known answers
    {
        nixlSerDes sd;
        sd.addStr("Agent", "alice");
        const uint64_t v = 0x1122334455667788ULL;
        sd.addBuf("num", &v, sizeof(v));
        const std::string wire = sd.exportStr();
        check(wire.rfind("nixlSerDes|", 0) == 0, "serdes_header");
        nixlSerDes in;
        check(in.importStr(wire) == NIXL_SUCCESS, "serdes_import");
        check(in.getStr("Agent") == "alice", "serdes_getStr");
        uint64_t r = 0;
        check(in.getBufLen("num") == 8 && in.getBuf("num", &r, 8) == NIXL_SUCCESS && r == v,
              "serdes_getBuf");
        nixlSerDes bad;
        check(bad.importStr("garbage") != NIXL_SUCCESS, "serdes_reject_bad_header");
    }

    // --- descriptor lists round trip
    {
        nixl_xfer_dlist_t x(DRAM_SEG);
        x.addDesc(nixlBasicDesc(0x1000, 64, 0));
        x.addDesc(nixlBasicDesc(0x2000, 128, 1));
        nixlSerDes sd;
        check(x.serialize(&sd) == NIXL_SUCCESS, "xfer_dlist_serialize");
        const std::string wire = sd.exportStr();
        save("xfer_dlist.bin", std::string(1, '\0'), wire);
        nixlSerDes in;
        in.importStr(wire);
        nixl_xfer_dlist_t y(&in);
        check(y.getType() == DRAM_SEG && y.descCount() == 2 && y[1].addr == 0x2000 &&
                  y[1].len == 128 && y[1].devId == 1 && x == y,
              "xfer_dlist_roundtrip");

        nixl_reg_dlist_t r(VRAM_SEG);
        r.addDesc(nixlBlobDesc(0x3000, 32, 2, "meta-a"));
        r.addDesc(nixlBlobDesc(0x4000, 16, 3, std::string("m\0b", 3)));
        nixlSerDes sr;
        r.serialize(&sr);
        const std::string rwire = sr.exportStr();
        save("reg_dlist.bin", std::string(1, '\1'), rwire);
        nixlSerDes rin;
        rin.importStr(rwire);
        nixl_reg_dlist_t r2(&rin);
        check(r2.getType() == VRAM_SEG && r2.descCount() == 2 && r2[0].metaInfo == "meta-a" &&
                  r2[1].metaInfo == std::string("m\0b", 3) && r2 == r,
              "reg_dlist_roundtrip");
    }

    // --- agent metadata exchange
    {
        nixlAgent a("alice", nixlAgentConfig(false));
        nixlAgent b("bob", nixlAgentConfig(false));
        nixlBackendH *ha = nullptr, *hb = nullptr;
        check(a.createBackend("FUZZ", nixl_b_params_t(), ha) == NIXL_SUCCESS &&
                  b.createBackend("FUZZ", nixl_b_params_t(), hb) == NIXL_SUCCESS,
              "agent_create_backend");
        std::vector<char> buf(4096);
        nixl_reg_dlist_t reg(DRAM_SEG);
        reg.addDesc(nixlBlobDesc(reinterpret_cast<uintptr_t>(buf.data()), buf.size(), 0, "k1"));
        check(a.registerMem(reg) == NIXL_SUCCESS, "agent_registerMem");
        nixl_blob_t md;
        check(a.getLocalMD(md) == NIXL_SUCCESS && !md.empty(), "agent_getLocalMD");
        save("agent_md.bin", "", md);

        std::string name;
        check(b.loadRemoteMD(md, name) == NIXL_SUCCESS && name == "alice", "agent_loadRemoteMD");
        nixl_xfer_dlist_t inside(DRAM_SEG);
        inside.addDesc(nixlBasicDesc(reinterpret_cast<uintptr_t>(buf.data()) + 16, 64, 0));
        check(b.checkRemoteMD("alice", inside) == NIXL_SUCCESS, "agent_checkRemoteMD_inside");
        nixl_xfer_dlist_t outside(DRAM_SEG);
        outside.addDesc(nixlBasicDesc(reinterpret_cast<uintptr_t>(buf.data()) + 4090, 64, 0));
        check(b.checkRemoteMD("alice", outside) != NIXL_SUCCESS, "agent_checkRemoteMD_outside");

        check(b.invalidateRemoteMD("alice") == NIXL_SUCCESS, "agent_invalidate");
        check(b.checkRemoteMD("alice", inside) != NIXL_SUCCESS, "agent_checkRemoteMD_after_invalidate");
        std::string junk_name;
        check(b.loadRemoteMD(md.substr(0, md.size() / 2), junk_name) != NIXL_SUCCESS,
              "agent_reject_truncated_md");
        a.deregisterMem(reg);
    }

    std::printf("SUMMARY failures=%d\n", failures);
    return failures ? 1 : 0;
}
