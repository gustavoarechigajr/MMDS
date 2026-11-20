// Randomizer check data for the second screen's Checks page: shuffled checks grouped by area, with what's been
// collected. Read-only; the logic search (in logic / out of logic) stays with the ImGui tracker, as it is too
// heavy to run every frame alongside the game.
#include "DualScreen.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/ShipUtils.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <vector>

extern "C" {
s16 Play_GetOriginalSceneId(s16 sceneId);
}

namespace {

struct RandoArea {
    SceneId sceneId;
    std::vector<RandoCheckId> checks;
};

std::vector<RandoArea> sAreas;
// Identifies the loaded file, so the lists are rebuilt only after loading another one
uint32_t sBuiltForSeed = 0;
int32_t sBuiltForFile = -1;

bool CheckDone(RandoCheckId id) {
    return RANDO_SAVE_CHECKS[id].obtained || RANDO_SAVE_CHECKS[id].skipped;
}

void BuildAreas() {
    if (gSaveContext.fileNum == sBuiltForFile && gSaveContext.save.shipSaveInfo.rando.finalSeed == sBuiltForSeed) {
        return;
    }
    sBuiltForFile = gSaveContext.fileNum;
    sBuiltForSeed = gSaveContext.save.shipSaveInfo.rando.finalSeed;
    sAreas.clear();

    for (auto& [id, check] : Rando::StaticData::Checks) {
        if (!RANDO_SAVE_CHECKS[id].shuffled) {
            continue;
        }
        SceneId sceneId = (SceneId)Play_GetOriginalSceneId(check.sceneId);
        auto area = std::find_if(sAreas.begin(), sAreas.end(), [&](RandoArea& a) { return a.sceneId == sceneId; });
        if (area == sAreas.end()) {
            sAreas.push_back({ sceneId, {} });
            area = sAreas.end() - 1;
        }
        area->checks.push_back(id);
    }
    std::sort(sAreas.begin(), sAreas.end(), [](const RandoArea& a, const RandoArea& b) {
        return strcmp(Ship_GetSceneName(a.sceneId), Ship_GetSceneName(b.sceneId)) < 0;
    });
}

bool ValidArea(int32_t area) {
    return area >= 0 && area < (int32_t)sAreas.size();
}

} // namespace

extern "C" uint8_t DualScreen_RandoActive(void) {
    return IS_RANDO;
}

extern "C" int32_t DualScreen_RandoAreaCount(void) {
    if (!IS_RANDO) {
        return 0;
    }
    BuildAreas();
    return (int32_t)sAreas.size();
}

extern "C" const char* DualScreen_RandoAreaName(int32_t area) {
    return ValidArea(area) ? Ship_GetSceneName(sAreas[area].sceneId) : "";
}

extern "C" void DualScreen_RandoAreaProgress(int32_t area, int32_t* done, int32_t* total) {
    *done = *total = 0;
    if (!ValidArea(area)) {
        return;
    }
    for (RandoCheckId id : sAreas[area].checks) {
        *done += CheckDone(id) ? 1 : 0;
    }
    *total = (int32_t)sAreas[area].checks.size();
}

extern "C" void DualScreen_RandoTotalProgress(int32_t* done, int32_t* total) {
    *done = *total = 0;
    for (int32_t i = 0; i < (int32_t)sAreas.size(); i++) {
        int32_t d;
        int32_t t;
        DualScreen_RandoAreaProgress(i, &d, &t);
        *done += d;
        *total += t;
    }
}

extern "C" int32_t DualScreen_RandoAreaForScene(int16_t sceneId) {
    SceneId original = (SceneId)Play_GetOriginalSceneId(sceneId);
    for (int32_t i = 0; i < (int32_t)sAreas.size(); i++) {
        if (sAreas[i].sceneId == original) {
            return i;
        }
    }
    return -1;
}

extern "C" int32_t DualScreen_RandoCheckCount(int32_t area) {
    return ValidArea(area) ? (int32_t)sAreas[area].checks.size() : 0;
}

extern "C" const char* DualScreen_RandoCheckName(int32_t area, int32_t index) {
    if (!ValidArea(area) || index < 0 || index >= (int32_t)sAreas[area].checks.size()) {
        return "";
    }
    RandoCheckId id = sAreas[area].checks[index];
    const std::string& name = Rando::StaticData::CheckNames[id];
    if (name.empty()) {
        return Rando::StaticData::Checks[id].name;
    }
    // "Astral Observatory Passage Chest" reads as "Passage Chest" under its area's heading
    const char* areaName = Ship_GetSceneName(sAreas[area].sceneId);
    size_t areaLen = strlen(areaName);
    if (name.size() > areaLen + 1 && strncasecmp(name.c_str(), areaName, areaLen) == 0 && name[areaLen] == ' ') {
        return name.c_str() + areaLen + 1;
    }
    return name.c_str();
}

extern "C" uint8_t DualScreen_RandoCheckDone(int32_t area, int32_t index) {
    if (!ValidArea(area) || index < 0 || index >= (int32_t)sAreas[area].checks.size()) {
        return false;
    }
    return CheckDone(sAreas[area].checks[index]);
}
