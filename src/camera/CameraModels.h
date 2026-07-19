#pragma once

#include <string>
#include <vector>
#include <cctype>

// Pure, Qt-free catalog of camera bodies and their nominal AF coordinate frame
// sizes. The true AF frame is not exposed by libgphoto2, so these defaults are
// calibration starting points; the UI remembers per-model overrides.
namespace cammodel {

struct Model {
    const char *id;       // stable settings key, e.g. "d7500"
    const char *display;  // "Nikon D7500"
    int afFrameW;         // nominal default
    int afFrameH;
};

inline const std::vector<Model>& models() {
    static const std::vector<Model> kModels = {
        {"d7500", "Nikon D7500", 640, 426},
        {"d750",  "Nikon D750",  640, 426},
        {"d780",  "Nikon D780",  640, 426},
        {"d850",  "Nikon D850",  640, 426},
        {"d500",  "Nikon D500",  640, 426},
        {"d5600", "Nikon D5600", 640, 426},
        {"d3500", "Nikon D3500", 640, 426},
        {"z6",    "Nikon Z6 / Z6II", 640, 426},
        {"z7",    "Nikon Z7 / Z7II", 640, 426},
        {"custom", "Other / Custom", 640, 426},
    };
    return kModels;
}

inline const Model* byId(const std::string& id) {
    for (const Model& m : models())
        if (id == m.id) return &m;
    return nullptr;
}

// Case-insensitive substring match of each model's id token in the camera name.
// Longer ids are preferred so "d7500" wins over a "d750" substring. "custom" is
// never auto-matched. Returns "" when nothing matches.
inline std::string matchModel(const std::string& cameraName) {
    std::string hay = cameraName;
    for (char& c : hay) c = char(std::tolower((unsigned char)c));

    std::string best;
    for (const Model& m : models()) {
        std::string id = m.id;
        if (id == "custom") continue;
        if (hay.find(id) != std::string::npos && id.size() > best.size())
            best = id;
    }
    return best;
}

} // namespace cammodel
