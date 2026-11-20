// Fuzz the public descriptor-list deserializers: nixlSerDes::importStr followed by
// the nixlDescList(nixlSerDes*) constructors for transfer lists (nixlBasicDesc)
// and registration lists (nixlBlobDesc, with per-descriptor metaInfo strings).
// These are what peers' metadata and the Python bindings' pickled dlists go
// through. The first input byte selects the list type; the rest is the
// serialized buffer. Successfully parsed lists are re-serialized and re-parsed.
#include <cstddef>
#include <cstdint>
#include <string>

#include "nixl_descriptors.h"
#include "serdes/serdes.h"

template <class L>
static void roundTrip(const std::string &buf) {
    nixlSerDes sd;
    if (sd.importStr(buf) != NIXL_SUCCESS) return;
    L list(&sd);
    if (list.descCount() == 0) return;
    (void)list.getType();
    nixlSerDes out;
    if (list.serialize(&out) != NIXL_SUCCESS) return;
    nixlSerDes in;
    if (in.importStr(out.exportStr()) != NIXL_SUCCESS) return;
    L again(&in);
    (void)again.descCount();
    for (int i = 0; i < list.descCount(); ++i) {
        (void)list.getIndex(list[i]);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    const std::string buf(reinterpret_cast<const char *>(data) + 1, size - 1);
    if (data[0] & 1) {
        roundTrip<nixl_reg_dlist_t>(buf);
    } else {
        roundTrip<nixl_xfer_dlist_t>(buf);
    }
    return 0;
}
