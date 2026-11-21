// Audio diagnostics (CVar gDebug.AudioDiag, off by default). Finds out why audio crackles, from the numbers
// rather than by ear:
//  - a monitor thread polls the SDL audio queue every 2 ms and counts the times it runs dry, which is when
//    the device plays silence (a crackle)
//  - the audio thread reports how full the queue was before each push and how long synthesis took
//  - the game thread reports the time between game frames and how long rendering took
//  - pages switched on the second screen are marked, so a dry queue can be matched to what caused it
// Everything goes to logcat under the tag "DSAudio", one summary every 5 seconds plus a line per event.
#include "AudioDiag.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#include <SDL2/SDL.h>
#define DIAG_LOG(...) __android_log_print(ANDROID_LOG_INFO, "DSAudio", __VA_ARGS__)
#else
#include <cstdio>
#define DIAG_LOG(...) (printf(__VA_ARGS__), printf("\n"))
#endif

extern "C" int AudioPlayer_Buffered(void);

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

std::atomic<bool> sEnabled{ false };
std::atomic<bool> sMonitorRunning{ false };
std::thread sMonitor;
Clock::time_point sStart;

// Per-window stats, reset after each summary
std::mutex sStatsMutex;
struct Stats {
    int pushes = 0;
    long pushedSamples = 0;
    int minBufferedAtPush = 1 << 30;
    double maxSynthMs = 0.0;
    double totalSynthMs = 0.0;
    int frames = 0;
    double maxFrameGapMs = 0.0;
    double maxRenderMs = 0.0;
    int slowFrames = 0; // game frames more than 70 ms apart
    int skipped = 0;    // interpolated frames dropped to stay on schedule
    int dryEvents = 0;  // the queue was seen empty
    int minPolled = 1 << 30;
} sStats;

Clock::time_point sLastFrame;
Clock::time_point sLastPush;
std::atomic<int> sLastPushBuffered{ 0 };
std::atomic<double> sLastFrameGapMs{ 0.0 };
std::atomic<double> sLastRenderMs{ 0.0 };
std::atomic<double> sLastSynthMs{ 0.0 };
const char* sLastMark = "";
Clock::time_point sLastMarkTime;

double SecondsSinceStart() {
    return MsSince(sStart) / 1000.0;
}

void Summarize() {
    Stats s;
    {
        std::lock_guard<std::mutex> lock(sStatsMutex);
        s = sStats;
        sStats = Stats{};
    }
    // Made vs played: the device plays 32000 samples a second, 160000 per window
    DIAG_LOG("[%.1fs] 5s: dry=%d minQueue=%d made=%ld/160000 pushes=%d minAtPush=%d synth avg=%.2f max=%.2fms frames=%d "
             "maxGap=%.1fms slow=%d maxRender=%.1fms skipped=%d",
             SecondsSinceStart(), s.dryEvents, s.minPolled == (1 << 30) ? -1 : s.minPolled, s.pushedSamples, s.pushes,
             s.minBufferedAtPush == (1 << 30) ? -1 : s.minBufferedAtPush,
             s.pushes > 0 ? s.totalSynthMs / s.pushes : 0.0, s.maxSynthMs, s.frames, s.maxFrameGapMs, s.slowFrames,
             s.maxRenderMs, s.skipped);
}

void MonitorMain() {
    int prev = AudioPlayer_Buffered();
    bool dry = false;
    auto lastSummary = Clock::now();

    while (sMonitorRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        int cur = AudioPlayer_Buffered();
        {
            std::lock_guard<std::mutex> lock(sStatsMutex);
            sStats.minPolled = std::min(sStats.minPolled, cur);
        }
        // Empty after a pull: the device asked for more than was queued and played silence for the rest
        if (cur == 0 && !dry) {
            dry = true;
            {
                std::lock_guard<std::mutex> lock(sStatsMutex);
                sStats.dryEvents++;
            }
            std::lock_guard<std::mutex> lock(sStatsMutex);
            DIAG_LOG("[%.2fs] DRY prevQueue=%d lastPush %.1fms ago (queue %d before it) frameGap=%.1fms "
                     "render=%.1fms synth=%.2fms lastMark='%s' %.0fms ago",
                     SecondsSinceStart(), prev, MsSince(sLastPush), sLastPushBuffered.load(),
                     sLastFrameGapMs.load(), sLastRenderMs.load(), sLastSynthMs.load(), sLastMark,
                     MsSince(sLastMarkTime));
        } else if (cur > 0) {
            dry = false;
        }
        prev = cur;

        if (std::chrono::duration<double>(Clock::now() - lastSummary).count() >= 5.0) {
            lastSummary = Clock::now();
            Summarize();
        }
    }
}

} // namespace

extern "C" void AudioDiag_ConfigureBackend(int32_t* sampleLength, int32_t* desiredBuffered) {
    // A/B knobs, read once at start-up so a test only needs the config edited and the app restarted
    *sampleLength = CVarGetInteger("gDebug.Audio.SampleLength", *sampleLength);
    *desiredBuffered = CVarGetInteger("gDebug.Audio.DesiredBuffered", *desiredBuffered);
    const char* driver = CVarGetString("gDebug.Audio.Driver", "");
    if (driver != nullptr && driver[0] != '\0') {
        setenv("SDL_AUDIODRIVER", driver, 1);
    }
}

extern "C" void AudioDiag_Start(void) {
    sEnabled = CVarGetInteger("gDebug.AudioDiag", 0) != 0;
    sStart = sLastFrame = sLastPush = sLastMarkTime = Clock::now();
#if defined(__ANDROID__)
    const char* driver = SDL_GetCurrentAudioDriver();
    DIAG_LOG("audio driver=%s sampleLength=%d desired=%d diag=%d", driver ? driver : "?",
             CVarGetInteger("gDebug.Audio.SampleLength", 1024), CVarGetInteger("gDebug.Audio.DesiredBuffered", 2400),
             (int)sEnabled.load());
#endif
    if (sEnabled && !sMonitorRunning) {
        sMonitorRunning = true;
        sMonitor = std::thread(MonitorMain);
        sMonitor.detach();
    }
}

extern "C" void AudioDiag_OnPush(int32_t bufferedBefore, int32_t samples, double synthMs) {
    if (!sEnabled) {
        return;
    }
    sLastPushBuffered = bufferedBefore;
    sLastSynthMs = synthMs;
    std::lock_guard<std::mutex> lock(sStatsMutex);
    sLastPush = Clock::now();
    sStats.pushes++;
    sStats.pushedSamples += samples;
    sStats.minBufferedAtPush = std::min(sStats.minBufferedAtPush, (int)bufferedBefore);
    sStats.maxSynthMs = std::max(sStats.maxSynthMs, synthMs);
    sStats.totalSynthMs += synthMs;
}

extern "C" void AudioDiag_OnFrame(double renderMs) {
    if (!sEnabled) {
        return;
    }
    double gap = MsSince(sLastFrame);
    sLastFrame = Clock::now();
    sLastFrameGapMs = gap;
    sLastRenderMs = renderMs;
    std::lock_guard<std::mutex> lock(sStatsMutex);
    sStats.frames++;
    sStats.maxFrameGapMs = std::max(sStats.maxFrameGapMs, gap);
    sStats.maxRenderMs = std::max(sStats.maxRenderMs, renderMs);
    if (gap > 70.0) {
        sStats.slowFrames++;
        DIAG_LOG("[%.2fs] SLOW frame gap=%.1fms render=%.1fms queue=%d lastMark='%s' %.0fms ago", SecondsSinceStart(),
                 gap, renderMs, AudioPlayer_Buffered(), sLastMark, MsSince(sLastMarkTime));
    }
}

extern "C" void AudioDiag_Mark(const char* what) {
    if (!sEnabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(sStatsMutex);
    sLastMark = what;
    sLastMarkTime = Clock::now();
    DIAG_LOG("[%.2fs] mark %s queue=%d", SecondsSinceStart(), what, AudioPlayer_Buffered());
}

extern "C" void AudioDiag_OnSkippedFrame(void) {
    if (!sEnabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(sStatsMutex);
    sStats.skipped++;
}
