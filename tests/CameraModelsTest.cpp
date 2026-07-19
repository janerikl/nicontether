#include "camera/CameraModels.h"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace cammodel;

    // The list is non-empty and includes the sentinel "custom" entry.
    assert(!models().empty());
    assert(byId("custom") != nullptr);

    // D7500 is present with positive default dimensions.
    const Model* m = byId("d7500");
    assert(m != nullptr);
    assert(m->afFrameW > 0 && m->afFrameH > 0);

    // Every model has positive defaults and a non-empty id/display.
    for (const Model& e : models()) {
        assert(e.id && e.id[0]);
        assert(e.display && e.display[0]);
        assert(e.afFrameW > 0 && e.afFrameH > 0);
    }

    // Name matching against gphoto2-style camera names.
    assert(matchModel("Nikon DSC D7500") == "d7500");
    assert(matchModel("Nikon DSC D750 (PTP mode)") == "d750");
    assert(matchModel("Canon EOS 5D") == "");
    assert(matchModel("") == "");

    // Unknown id -> nullptr.
    assert(byId("nope") == nullptr);

    std::puts("CameraModelsTest: all assertions passed");
    return 0;
}
