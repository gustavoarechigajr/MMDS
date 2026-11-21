// Gyro aiming on handhelds like the AYN Thor: the gyroscope belongs to the device, not to a game controller, so
// the Input Editor's "press a button to map gyro" step is easy to miss and aiming silently does nothing. When
// first-person gyro aiming is on and port 1 has no gyro mapping yet, map the device's own gyro (SDLGyroMapping
// already falls back to it on Android) and calibrate it once at rest.
#include "2s2h/ShipInit.hpp"
#include "2s2h/GameInteractor/GameInteractor.h"
#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/controller/controldevice/controller/mapping/sdl/SDLGyroMapping.h>

#if defined(__ANDROID__)
#include <SDL2/SDL.h>

namespace {

#define CVAR_GYRO_AIMING "gEnhancements.Camera.FirstPerson.GyroEnabled"

bool DeviceHasGyro() {
    if ((SDL_WasInit(SDL_INIT_SENSOR) & SDL_INIT_SENSOR) == 0) {
        SDL_InitSubSystem(SDL_INIT_SENSOR);
    }
    for (int32_t i = 0; i < SDL_NumSensors(); i++) {
        if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_GYRO) {
            return true;
        }
    }
    return false;
}

void MapDeviceGyro() {
    auto controller = Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0);
    if (controller == nullptr || controller->GetGyro()->GetGyroMapping() != nullptr || !DeviceHasGyro()) {
        return;
    }
    auto mapping = std::make_shared<Ship::SDLGyroMapping>(0, 1.0f, 0.0f, 0.0f, 0.0f);
    mapping->Recalibrate();
    controller->GetGyro()->SetGyroMapping(mapping);
    mapping->SaveToConfig();
    controller->GetGyro()->SaveGyroMappingIdToConfig();
    CVarSave();
}

void RegisterAndroidGyro() {
    // Checked once gameplay starts (controllers are loaded by then), and again whenever the option is turned on
    COND_HOOK(OnSceneInit, CVarGetInteger(CVAR_GYRO_AIMING, 0), [](s8 sceneId, s8 spawnNum) { MapDeviceGyro(); });
}

static RegisterShipInitFunc sAndroidGyroInit(RegisterAndroidGyro, { CVAR_GYRO_AIMING });

} // namespace
#endif
