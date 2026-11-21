// Where a temple's stray fairies are, for the dungeon map's fairy tracker. The rando check list names every dungeon
// stray fairy with its flag (switch for floating/bubble/enemy fairies, collectible for pots, hives and crates, chest
// for chests); the room files give the position of the object carrying that flag. Read once per scene, straight
// from the game's own resources, and checked against this cycle's flags each frame (stray fairies, like their
// count, reset with the Song of Time).
#include "DualScreen.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/resource/type/Scene.h"
#include "2s2h/resource/type/scenecommand/SetActorList.h"
#include <libultraship/libultraship.h>
#include <vector>
#if defined(__ANDROID__)
#include <android/log.h>
#define FAIRY_LOG(...) __android_log_print(ANDROID_LOG_INFO, "DSFairy", __VA_ARGS__)
#else
#define FAIRY_LOG(...)
#endif

extern "C" {
#include "z64.h"
s16 Play_GetOriginalSceneId(s16 sceneId);
}

namespace {

struct FairySpot {
    float x;
    float y;
    float z;
    FlagType flagType;
    int32_t flag;
};

std::vector<FairySpot> sSpots;
int32_t sUnplaced = 0;
int16_t sBuiltForScene = -1;

const RandoItemId kDungeonFairyItems[] = { RI_WOODFALL_STRAY_FAIRY, RI_SNOWHEAD_STRAY_FAIRY, RI_GREAT_BAY_STRAY_FAIRY,
                                           RI_STONE_TOWER_STRAY_FAIRY };

// The flag an object in a room carries, of the kind that would mark a stray fairy in it; false if it can't hold one
bool ActorFairyFlag(const SOH::ActorEntry& entry, FlagType* flagType, int32_t* flag) {
    uint16_t params = (uint16_t)entry.params;

    // The top bits of a room's actor id are spawn flags (as Actor_SpawnEntry masks them off)
    switch (entry.id & 0x1FFF) {
        case ACTOR_EN_ELFORG: // floating, or held by an enemy
        case ACTOR_EN_ELFBUB: // in a bubble
            *flagType = FLAG_CYCL_SCENE_SWITCH;
            *flag = (params & 0xFE00) >> 9;
            return true;
        case ACTOR_EN_BOX:
            *flagType = FLAG_CYCL_SCENE_CHEST;
            *flag = params & 0x1F;
            return true;
        case ACTOR_OBJ_TSUBO: // pots
            *flagType = FLAG_CYCL_SCENE_COLLECTIBLE;
            *flag = (params >> 9) & 0x7F;
            return true;
        case ACTOR_OBJ_COMB: // beehives
        case ACTOR_OBJ_KIBAKO:
        case ACTOR_OBJ_KIBAKO2: // crates
            *flagType = FLAG_CYCL_SCENE_COLLECTIBLE;
            *flag = (params >> 8) & 0x7F;
            return true;
        default:
            return false;
    }
}

void Build(PlayState* play, int32_t dungeon) {
    SceneId sceneId = (SceneId)Play_GetOriginalSceneId(play->sceneId);
    std::vector<std::pair<FlagType, int32_t>> wanted;

    sSpots.clear();
    sUnplaced = 0;
    sBuiltForScene = play->sceneId;

    for (auto& [id, check] : Rando::StaticData::Checks) {
        if (check.randoItemId == kDungeonFairyItems[dungeon] && check.sceneId == sceneId) {
            wanted.push_back({ check.flagType, check.flag });
        }
    }
    std::vector<bool> found(wanted.size(), false);

    for (int32_t room = 0; room < play->roomList.count; room++) {
        auto resource = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
            play->roomList.romFiles[room].fileName);
        auto roomScene = std::dynamic_pointer_cast<SOH::Scene>(resource);
        if (roomScene == nullptr) {
            continue;
        }
        for (auto& command : roomScene->commands) {
            if (command == nullptr || command->cmdId != SOH::SceneCommandID::SetActorList) {
                continue;
            }
            auto actorList = std::static_pointer_cast<SOH::SetActorList>(command);
            for (const SOH::ActorEntry& entry : actorList->actorList) {
                FlagType flagType;
                int32_t flag;
                if (!ActorFairyFlag(entry, &flagType, &flag)) {
                    continue;
                }
                for (size_t i = 0; i < wanted.size(); i++) {
                    if (!found[i] && wanted[i].first == flagType && wanted[i].second == flag) {
                        found[i] = true;
                        sSpots.push_back({ (float)entry.pos.x, (float)entry.pos.y, (float)entry.pos.z, flagType, flag });
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < found.size(); i++) {
        sUnplaced += found[i] ? 0 : 1;
        if (!found[i]) {
            FAIRY_LOG("unplaced: flag type %d flag 0x%02X", (int)wanted[i].first, wanted[i].second);
        }
    }
    FAIRY_LOG("scene %d: %d fairies wanted, %d placed", (int)sceneId, (int)wanted.size(), (int)sSpots.size());
}

bool Collected(PlayState* play, const FairySpot& spot) {
    switch (spot.flagType) {
        case FLAG_CYCL_SCENE_SWITCH:
            return Flags_GetSwitch(play, spot.flag);
        case FLAG_CYCL_SCENE_CHEST:
            return Flags_GetTreasure(play, spot.flag);
        case FLAG_CYCL_SCENE_COLLECTIBLE:
            return Flags_GetCollectible(play, spot.flag);
        default:
            return false;
    }
}

} // namespace

extern "C" int32_t DualScreen_FairySpotCount(PlayState* play, int32_t dungeon) {
    if (dungeon < 0 || dungeon >= (int32_t)(sizeof(kDungeonFairyItems) / sizeof(kDungeonFairyItems[0]))) {
        return 0;
    }
    if (sBuiltForScene != play->sceneId) {
        Build(play, dungeon);
    }
    return (int32_t)sSpots.size();
}

extern "C" uint8_t DualScreen_FairySpot(PlayState* play, int32_t index, float* x, float* y, float* z) {
    if (index < 0 || index >= (int32_t)sSpots.size()) {
        return false;
    }
    *x = sSpots[index].x;
    *y = sSpots[index].y;
    *z = sSpots[index].z;
    return !Collected(play, sSpots[index]);
}

extern "C" int32_t DualScreen_FairyUnplacedCount(void) {
    return sUnplaced;
}
