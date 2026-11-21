#ifndef AUDIO_DIAG_H
#define AUDIO_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio diagnostics, enabled with the CVar gDebug.AudioDiag (AudioDiag.cpp); all calls are cheap no-ops otherwise
void AudioDiag_ConfigureBackend(int32_t* sampleLength, int32_t* desiredBuffered);
void AudioDiag_Start(void);
void AudioDiag_OnPush(int32_t bufferedBefore, int32_t samples, double synthMs);
void AudioDiag_OnFrame(double renderMs);
void AudioDiag_OnSkippedFrame(void);
// Marks an event (a static string) so later dry spells can be traced back to it
void AudioDiag_Mark(const char* what);

#ifdef __cplusplus
}
#endif

#endif
