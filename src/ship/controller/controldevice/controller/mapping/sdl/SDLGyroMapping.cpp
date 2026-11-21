#include "ship/controller/controldevice/controller/mapping/sdl/SDLGyroMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerGyroMapping.h"
#include <spdlog/spdlog.h>
#include <cmath>
#ifdef __ANDROID__
#include <android/log.h>
#include <android/sensor.h>
#include <android/looper.h>
#include <dlfcn.h>
#include <chrono>
#endif
#include "ship/Context.h"

#include "ship/config/ConsoleVariable.h"
#include "ship/utils/StringHelper.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
#ifdef __ANDROID__
static SDL_Sensor* sAndroidGyroSensor = nullptr;

static bool OpenAndroidGyroSensor() {
    if (sAndroidGyroSensor != nullptr) {
        return true;
    }

    if ((SDL_WasInit(SDL_INIT_SENSOR) & SDL_INIT_SENSOR) == 0) {
        SDL_InitSubSystem(SDL_INIT_SENSOR);
    }

    for (int32_t i = 0; i < SDL_NumSensors(); i++) {
        if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_GYRO) {
            sAndroidGyroSensor = SDL_SensorOpen(i);
            break;
        }
    }

    return sAndroidGyroSensor != nullptr;
}

// The device's accelerometer, to know which way is up for player-space gyro
static SDL_Sensor* sAndroidAccelSensor = nullptr;
static float sAndroidUp[3] = { 0.0f, 0.0f, 0.0f };

static bool OpenAndroidAccelSensor() {
    if (sAndroidAccelSensor != nullptr) {
        return true;
    }
    for (int32_t i = 0; i < SDL_NumSensors(); i++) {
        if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_ACCEL) {
            sAndroidAccelSensor = SDL_SensorOpen(i);
            break;
        }
    }
    return sAndroidAccelSensor != nullptr;
}

// Every gyro event since the last poll, not just the latest. The game reads the gyro once a game frame (20 a
// second) while the sensor reports hundreds of times a second; one instantaneous sample per frame misjudges any
// motion between frames, so turns don't add back up to where they started. Averaging all events over the frame
// gives the true rotation.
static ASensorEventQueue* sGyroQueue = nullptr;
static const ASensor* sQueueGyro = nullptr;
static const ASensor* sQueueAccel = nullptr;
// The sensor hub's fused orientation (gyro + accelerometer, no compass). Turning is read from its heading, frame to
// frame, rather than by adding up gyro steps: added-up steps keep every small error (hard shaking left the aim tens
// of degrees off), while a heading comes back to the same value whenever the device does.
static const ASensor* sQueueRotation = nullptr;
static bool sHasHeading = false;
static float sHeading = 0.0f;     // latest heading of the device's Y axis around the vertical, radians
static float sLastHeading = 0.0f; // heading at the previous poll
static bool sHeadingValid = false;
static bool sLastHeadingValid = false;
// Tilt around the device's Y axis (up/down aim in landscape), from the same fused orientation
static float sTilt = 0.0f;
static float sLastTilt = 0.0f;
static bool sTiltValid = false;
static bool sLastTiltValid = false;
static float sLastGyro[3] = { 0.0f, 0.0f, 0.0f };
static int32_t sGyroEventCount = 0; // for the diagnostics log
// Diagnostics: exact running totals (degrees) per event: raw axes, turn around "up", and the relaxed turn
static double sRawTotal[3] = { 0.0, 0.0, 0.0 };
static double sWorldYawTotal = 0.0;
static double sRelaxedTotal = 0.0;
static float sPeakRate = 0.0f;
static int64_t sLastGyroTimestamp = 0;

static bool OpenAndroidGyroQueue() {
    if (sGyroQueue != nullptr) {
        return true;
    }
    // getInstanceForPackage needs API 26; this build still targets older devices
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    ASensorManager* manager = ASensorManager_getInstance();
#pragma clang diagnostic pop
    if (manager == nullptr) {
        return false;
    }
    sQueueGyro = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_GYROSCOPE);
    sQueueAccel = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_ACCELEROMETER);
    sQueueRotation = ASensorManager_getDefaultSensor(manager, 15); // ASENSOR_TYPE_GAME_ROTATION_VECTOR
    ALooper* looper = ALooper_forThread();
    if (looper == nullptr) {
        looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }
    if (sQueueGyro == nullptr || looper == nullptr) {
        return false;
    }
    sGyroQueue = ASensorManager_createEventQueue(manager, looper, 7, nullptr, nullptr);
    if (sGyroQueue == nullptr) {
        return false;
    }
    // enableSensor + setEventRate leaves the sensor at Android's default 5 Hz on the Thor (the sensor service lists
    // a 200000 us period), which is far too slow for aiming. registerSensor (API 26, looked up at run time since
    // this build targets older Android) asks for the rate up front.
    using RegisterSensorFn = int (*)(ASensorEventQueue*, const ASensor*, int32_t, int64_t);
    static RegisterSensorFn registerSensor =
        reinterpret_cast<RegisterSensorFn>(dlsym(RTLD_DEFAULT, "ASensorEventQueue_registerSensor"));
    if (registerSensor != nullptr) {
        registerSensor(sGyroQueue, sQueueGyro, 5000, 0); // 200 Hz, no batching
        if (sQueueAccel != nullptr) {
            registerSensor(sGyroQueue, sQueueAccel, 10000, 0);
        }
        if (sQueueRotation != nullptr) {
            registerSensor(sGyroQueue, sQueueRotation, 5000, 0);
        }
    } else {
        ASensorEventQueue_enableSensor(sGyroQueue, sQueueGyro);
        ASensorEventQueue_setEventRate(sGyroQueue, sQueueGyro, 5000);
        if (sQueueAccel != nullptr) {
            ASensorEventQueue_enableSensor(sGyroQueue, sQueueAccel);
            ASensorEventQueue_setEventRate(sGyroQueue, sQueueAccel, 10000);
        }
    }
    return true;
}

// The mean rate over every gyro event since the last call (and the latest single event, for comparison);
// accelerometer events update the smoothed up vector
static bool ReadAndroidGyroQueue(float mean[3], float latest[3]) {
    if (!OpenAndroidGyroQueue()) {
        return false;
    }
    ASensorEvent events[64];
    ssize_t count;
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    float sumDt = 0.0f;
    while ((count = ASensorEventQueue_getEvents(sGyroQueue, events, 64)) > 0) {
        for (ssize_t i = 0; i < count; i++) {
            const ASensorEvent& e = events[i];
            if (e.type == ASENSOR_TYPE_GYROSCOPE) {
                float dt = (sLastGyroTimestamp != 0) ? (e.timestamp - sLastGyroTimestamp) * 1e-9f : 0.0f;
                if (dt < 0.0f || dt > 0.1f) {
                    dt = 0.0f; // first event, or after a long pause
                }
                sLastGyroTimestamp = e.timestamp;
                for (int32_t k = 0; k < 3; k++) {
                    sum[k] += e.data[k] * dt;
                    sLastGyro[k] = e.data[k];
                }
                sumDt += dt;
                // Keep "up" turning with the device: a fixed world direction seen from the device changes by
                // -(rate x up) as it rotates
                float cross[3] = { e.data[1] * sAndroidUp[2] - e.data[2] * sAndroidUp[1],
                                   e.data[2] * sAndroidUp[0] - e.data[0] * sAndroidUp[2],
                                   e.data[0] * sAndroidUp[1] - e.data[1] * sAndroidUp[0] };
                for (int32_t k = 0; k < 3; k++) {
                    sAndroidUp[k] -= cross[k] * dt;
                }
                sGyroEventCount++;
                {
                    float upLen = sqrtf(sAndroidUp[0] * sAndroidUp[0] + sAndroidUp[1] * sAndroidUp[1] +
                                        sAndroidUp[2] * sAndroidUp[2]);
                    float w = (upLen > 1.0f) ? (e.data[0] * sAndroidUp[0] + e.data[1] * sAndroidUp[1] +
                                                e.data[2] * sAndroidUp[2]) / upLen
                                             : 0.0f;
                    float plane = sqrtf(e.data[0] * e.data[0] + e.data[2] * e.data[2]);
                    float relaxed = fminf(fabsf(w) * 1.75f, plane) * ((w < 0.0f) ? -1.0f : 1.0f);
                    for (int32_t k = 0; k < 3; k++) {
                        sRawTotal[k] += e.data[k] * dt * 57.2958;
                        sPeakRate = fmaxf(sPeakRate, fabsf(e.data[k]));
                    }
                    sWorldYawTotal += w * dt * 57.2958;
                    sRelaxedTotal += relaxed * dt * 57.2958;
                }
            } else if (e.type == 15) { // game rotation vector: x, y, z (and w) of the device-to-world quaternion
                float qx = e.data[0];
                float qy = e.data[1];
                float qz = e.data[2];
                float qw = e.data[3];
                if (qw == 0.0f) {
                    qw = sqrtf(fmaxf(0.0f, 1.0f - qx * qx - qy * qy - qz * qz));
                }
                // The device's Y axis in world space; it stays near level however the device is held in landscape
                float vx = 2.0f * (qx * qy - qw * qz);
                float vy = 1.0f - 2.0f * (qx * qx + qz * qz);
                float vz = 2.0f * (qy * qz + qw * qx);
                sHeadingValid = fabsf(vz) < 0.9f;
                // World up seen from the device; its angle in the device's X-Z plane is the tilt around Y
                float ux = 2.0f * (qx * qz - qw * qy);
                float uz = 1.0f - 2.0f * (qx * qx + qy * qy);
                sTiltValid = (ux * ux + uz * uz) > 0.1f;
                if (sTiltValid) {
                    sTilt = atan2f(ux, uz);
                }
                if (sHeadingValid) {
                    sHeading = atan2f(vy, vx);
                    sHasHeading = true;
                }
            } else if (e.type == ASENSOR_TYPE_ACCELEROMETER) {
                // The accelerometer only nudges "up" back toward gravity, slowly (about 2 s), and not at all while
                // the device is being jolted: shaking tilts its reading in step with the rotation, and those errors
                // would add up to a turn in one direction instead of cancelling out
                float g = sqrtf(e.data[0] * e.data[0] + e.data[1] * e.data[1] + e.data[2] * e.data[2]);
                float upLength = sqrtf(sAndroidUp[0] * sAndroidUp[0] + sAndroidUp[1] * sAndroidUp[1] +
                                       sAndroidUp[2] * sAndroidUp[2]);
                float blend = (upLength < 1.0f) ? 1.0f : ((g > 8.3f && g < 11.3f) ? 0.005f : 0.0f);
                for (int32_t k = 0; k < 3; k++) {
                    sAndroidUp[k] += (e.data[k] - sAndroidUp[k]) * blend;
                }
            }
        }
    }
    for (int32_t k = 0; k < 3; k++) {
        mean[k] = (sumDt > 0.0f) ? sum[k] / sumDt : sLastGyro[k];
        latest[k] = sLastGyro[k];
    }
    return true;
}
#endif

SDLGyroMapping::SDLGyroMapping(uint8_t portIndex, float sensitivity, float neutralPitch, float neutralYaw,
                               float neutralRoll)
    : ControllerInputMapping(PhysicalDeviceType::SDLGamepad),
      ControllerGyroMapping(PhysicalDeviceType::SDLGamepad, portIndex, sensitivity), mNeutralPitch(neutralPitch),
      mNeutralYaw(neutralYaw), mNeutralRoll(neutralRoll) {
}

void SDLGyroMapping::Recalibrate() {
    for (const auto& [instanceId, gamepad] : Context::GetRawInstance()
                                                 ->GetControlDeck()
                                                 ->GetConnectedPhysicalDeviceManager()
                                                 ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
        if (!SDL_GameControllerHasSensor(gamepad, SDL_SENSOR_GYRO)) {
            continue;
        }

        // just use gyro on the first gyro supported device we find
        float gyroData[3];
#ifdef __ANDROID__
        GetAndroidGyroData(gamepad, gyroData);
#else
        SDL_GameControllerSetSensorEnabled(gamepad, SDL_SENSOR_GYRO, SDL_TRUE);
        SDL_GameControllerGetSensorData(gamepad, SDL_SENSOR_GYRO, gyroData, 3);
#endif

        mNeutralPitch = gyroData[0];
        mNeutralYaw = gyroData[1];
        mNeutralRoll = gyroData[2];
        return;
    }

#ifdef __ANDROID__
    float gyroData[3];
    GetAndroidGyroData(nullptr, gyroData);
    mNeutralPitch = gyroData[0];
    mNeutralYaw = gyroData[1];
    mNeutralRoll = gyroData[2];
    return;
#endif

    // if we didn't find a gyro device zero everything out
    mNeutralPitch = 0;
    mNeutralYaw = 0;
    mNeutralRoll = 0;
}

void SDLGyroMapping::UpdatePad(float& x, float& y) {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        x = 0;
        y = 0;
        return;
    }

    for (const auto& [instanceId, gamepad] : Context::GetRawInstance()
                                                 ->GetControlDeck()
                                                 ->GetConnectedPhysicalDeviceManager()
                                                 ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
        if (!SDL_GameControllerHasSensor(gamepad, SDL_SENSOR_GYRO)) {
            continue;
        }

        // just use gyro on the first gyro supported device we find
        float gyroData[3];
#ifdef __ANDROID__
        GetAndroidGyroData(gamepad, gyroData);
#else
        SDL_GameControllerSetSensorEnabled(gamepad, SDL_SENSOR_GYRO, SDL_TRUE);
        SDL_GameControllerGetSensorData(gamepad, SDL_SENSOR_GYRO, gyroData, 3);
#endif

        x = (gyroData[0] - mNeutralPitch) * mSensitivity;
        y = (gyroData[1] - mNeutralYaw) * mSensitivity;
        return;
    }

#ifdef __ANDROID__
    float gyroData[3];
    GetAndroidGyroData(nullptr, gyroData);
    // Diagnostics (gDebug.GyroLog): the yaw sent to the game, and the turn it adds up to since the app started
    static std::chrono::steady_clock::time_point sLastPoll = std::chrono::steady_clock::now();
    static double sTotalYawDeg = 0.0;
    static int32_t sLogCounter = 0;
    auto now = std::chrono::steady_clock::now();
    double pollDt = std::chrono::duration<double>(now - sLastPoll).count();
    sLastPoll = now;
    if (pollDt < 0.2) {
        sTotalYawDeg += (gyroData[1] - mNeutralYaw) * pollDt * 57.2958;
    }
    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger("gDebug.GyroLog", 0) &&
        (++sLogCounter % 4) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "DSGyro", "pitch %+.3f yaw %+.3f roll %+.3f totalYaw %+.1f deg events %d | raw %+.1f %+.1f %+.1f world %+.1f relaxed %+.1f peak %.1f",
                            gyroData[0], gyroData[1], gyroData[2], sTotalYawDeg, sGyroEventCount, sRawTotal[0],
                            sRawTotal[1], sRawTotal[2], sWorldYawTotal, sRelaxedTotal, sPeakRate);
    }
    x = (gyroData[0] - mNeutralPitch) * mSensitivity;
    y = (gyroData[1] - mNeutralYaw) * mSensitivity;
    return;
#endif

    // if we didn't find a gyro device zero everything out
    x = 0;
    y = 0;
}

std::string SDLGyroMapping::GetGyroMappingId() {
    return StringHelper::Sprintf("P%d", mPortIndex);
}

void SDLGyroMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".GyroMappings." + GetGyroMappingId();

    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.GyroMappingClass", mappingCvarKey.c_str()).c_str(), "SDLGyroMapping");
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.Sensitivity", mappingCvarKey.c_str()).c_str(), mSensitivity);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.NeutralPitch", mappingCvarKey.c_str()).c_str(), mNeutralPitch);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.NeutralYaw", mappingCvarKey.c_str()).c_str(), mNeutralYaw);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.NeutralRoll", mappingCvarKey.c_str()).c_str(), mNeutralRoll);

    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void SDLGyroMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".GyroMappings." + GetGyroMappingId();

    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.GyroMappingClass", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Sensitivity", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.NeutralPitch", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.NeutralYaw", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.NeutralRoll", mappingCvarKey.c_str()).c_str());

    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

#ifdef __ANDROID__
void SDLGyroMapping::GetAndroidGyroData(SDL_GameController* gamepad, float gyroData[3]) {
    if (gamepad != nullptr && SDL_GameControllerHasSensor(gamepad, SDL_SENSOR_GYRO)) {
        SDL_GameControllerSetSensorEnabled(gamepad, SDL_SENSOR_GYRO, SDL_TRUE);
        SDL_GameControllerGetSensorData(gamepad, SDL_SENSOR_GYRO, gyroData, 3);
        return;
    }

    float latest[3];
    bool fromQueue = ReadAndroidGyroQueue(gyroData, latest);
    if (!fromQueue && !OpenAndroidGyroSensor()) {
        gyroData[0] = 0.0f;
        gyroData[1] = 0.0f;
        gyroData[2] = 0.0f;
        return;
    }

    if (!fromQueue) {
        SDL_SensorUpdate();
        SDL_SensorGetData(sAndroidGyroSensor, gyroData, 3);
    }
    if (fromQueue && Context::GetRawInstance()->GetConsoleVariables()->GetInteger("gDebug.GyroLatestOnly", 0)) { // A/B: the old single-sample reading
        gyroData[0] = latest[0];
        gyroData[1] = latest[1];
        gyroData[2] = latest[2];
    }

    // Player space: handhelds are held tilted back, so turning left or right spins the device partly around the
    // screen's axis too. Measure the turn around the real vertical (from the accelerometer, smoothed) instead of a
    // fixed device axis, or a turn registers differently going out than coming back. Pitch stays on the device's
    // left-right axis, which stays level however it's held.
    float playerYaw = 0.0f;
    bool hasPlayerYaw = false;
    if (fromQueue || OpenAndroidAccelSensor()) {
        if (!fromQueue) {
            float accel[3];
            SDL_SensorGetData(sAndroidAccelSensor, accel, 3);
            for (int32_t i = 0; i < 3; i++) {
                sAndroidUp[i] += (accel[i] - sAndroidUp[i]) * 0.1f;
            }
        }
        float length = sqrtf(sAndroidUp[0] * sAndroidUp[0] + sAndroidUp[1] * sAndroidUp[1] +
                             sAndroidUp[2] * sAndroidUp[2]);
        if (length > 4.0f) { // roughly 1 g: a real reading, not a sensor that hasn't started yet
            float worldYaw =
                (gyroData[0] * sAndroidUp[0] + gyroData[1] * sAndroidUp[1] + gyroData[2] * sAndroidUp[2]) / length;
            // Player space: the turn around the real vertical. ("Relaxed" player space, which takes the size from
            // the device's own axes, was tried: under hard shaking the gyro clips at ~3.4 rad/s and that variant
            // turned the clipping into a one-way turn of 40-60 degrees, against ~5 for this one.)
            playerYaw = worldYaw;
            // Prefer the change in fused heading since the last poll. The game adds this value once per game frame
            // (20 a second), so pass the change as a per-frame amount, not divided by the real time between polls.
            if (sHasHeading && sHeadingValid && sLastHeadingValid) {
                float delta = sHeading - sLastHeading;
                if (delta > (float)M_PI) {
                    delta -= 2.0f * (float)M_PI;
                } else if (delta < -(float)M_PI) {
                    delta += 2.0f * (float)M_PI;
                }
                playerYaw = delta * 20.0f;
            }
            sLastHeading = sHeading;
            sLastHeadingValid = sHasHeading && sHeadingValid;
            hasPlayerYaw = true;
        }
    }

    const float gyroX = gyroData[0];
    const float gyroY = gyroData[1];
    switch (SDL_GetDisplayOrientation(0)) {
        case SDL_ORIENTATION_PORTRAIT:
            break;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED:
            gyroData[0] = -gyroX;
            gyroData[1] = -gyroY;
            break;
        case SDL_ORIENTATION_LANDSCAPE:
            gyroData[0] = -gyroY;
            gyroData[1] = gyroX;
            break;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
            gyroData[0] = gyroY;
            gyroData[1] = -gyroX;
            break;
        case SDL_ORIENTATION_UNKNOWN:
            break;
    }
    if (hasPlayerYaw) {
        gyroData[1] = playerYaw;
    }
    // Up/down aim from the change in fused tilt, per game frame like the turn above. Rotating by w around Y moves
    // the tilt by -w, matching the landscape mapping's -gyroY (and +gyroY when flipped).
    {
        SDL_DisplayOrientation orientation = SDL_GetDisplayOrientation(0);
        bool landscape =
            (orientation == SDL_ORIENTATION_LANDSCAPE) || (orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED);
        if (fromQueue && landscape && sTiltValid && sLastTiltValid) {
            float delta = sTilt - sLastTilt;
            if (delta > (float)M_PI) {
                delta -= 2.0f * (float)M_PI;
            } else if (delta < -(float)M_PI) {
                delta += 2.0f * (float)M_PI;
            }
            gyroData[0] = ((orientation == SDL_ORIENTATION_LANDSCAPE) ? delta : -delta) * 20.0f;
        }
        sLastTilt = sTilt;
        sLastTiltValid = sTiltValid;
    }
}
#endif

std::string SDLGyroMapping::GetPhysicalDeviceName() {
    return "SDL Gamepad";
}
} // namespace Ship
