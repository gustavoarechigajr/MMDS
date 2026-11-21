#ifndef DUAL_SCREEN_H
#define DUAL_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The second screen is drawn in Bombers' Notebook (hi-res) units: 640 wide, with the height
// following the secondary display's aspect ratio (557 on the AYN Thor's 1240x1080 screen).
#define DUAL_SCREEN_CANVAS_WIDTH 640

typedef enum DualScreenPage {
    DUAL_SCREEN_PAGE_ITEMS,
    DUAL_SCREEN_PAGE_MASKS,
    DUAL_SCREEN_PAGE_QUEST,
    DUAL_SCREEN_PAGE_MAP,
    DUAL_SCREEN_PAGE_NOTEBOOK,
    DUAL_SCREEN_PAGE_STATUS,
    DUAL_SCREEN_PAGE_RANDO,
    DUAL_SCREEN_PAGE_MAX
} DualScreenPage;

typedef enum DualScreenTouchType {
    DUAL_SCREEN_TOUCH_DOWN,
    DUAL_SCREEN_TOUCH_MOVE,
    DUAL_SCREEN_TOUCH_UP
} DualScreenTouchType;

// One touch event, in canvas units
typedef struct DualScreenTouchEvent {
    int32_t type;
    float x;
    float y;
    // Second finger, valid when pointers >= 2 (pinch)
    float x2;
    float y2;
    int32_t pointers;
} DualScreenTouchEvent;

// True while a secondary display surface exists and the feature is enabled
uint8_t DualScreen_IsActive(void);
int32_t DualScreen_GetFrameBufferId(void);
int32_t DualScreen_GetCanvasHeight(void);
// Pops the oldest queued touch event; returns 0 when the queue is empty
uint8_t DualScreen_PollTouch(DualScreenTouchEvent* event);
// Plays an ocarina song: for the next few frames C-Left counts as the ocarina, and the staff that opens
// plays this song back on its own (the same path as 2S2H's Song Items). The caller presses C-Left.
void DualScreen_RequestSong(uint8_t song);
// Called after the first interpolated replay of a frame: the panels are already drawn, so later replays skip them
void DualScreen_SkipPanelReplays(void);
// Copies the panel framebuffer onto the secondary display; called after the frame is rendered
void DualScreen_PresentFrame(void);

// Counts one rendered top-screen frame; DualScreen_GetFPS reports the rate over the last second
void DualScreen_CountFrame(void);
int32_t DualScreen_GetFPS(void);

// Turns frame interpolation's matrix recording on or off; returns whether it was on
uint8_t DualScreen_SetInterpolationRecording(uint8_t on);

struct Actor;
// The health an actor started with (2S2H's enemy health data), for the target card
uint8_t DualScreen_ActorMaxHealth(struct Actor* actor);

// Randomizer checks grouped by area (DualScreenRando.cpp); all empty outside a rando save
uint8_t DualScreen_RandoActive(void);
int32_t DualScreen_RandoAreaCount(void);
const char* DualScreen_RandoAreaName(int32_t area);
void DualScreen_RandoAreaProgress(int32_t area, int32_t* done, int32_t* total);
void DualScreen_RandoTotalProgress(int32_t* done, int32_t* total);
int32_t DualScreen_RandoAreaForScene(int16_t sceneId);
int32_t DualScreen_RandoCheckCount(int32_t area);
const char* DualScreen_RandoCheckName(int32_t area, int32_t index);
uint8_t DualScreen_RandoCheckDone(int32_t area, int32_t index);

struct PlayState;
struct GraphicsContext;
// Handles touch input; runs in Play_Update so actions can safely change game state (DualScreenPanels.c)
void DualScreen_Update(struct PlayState* play);
// Draws the current page into the panel framebuffer
void DualScreen_Draw(struct PlayState* play);
// Draws the title page while no gameplay is running (title screen, file select); called every frame
void DualScreen_DrawIdle(struct GraphicsContext* gfxCtx);

#ifdef __cplusplus
}
#endif

#endif
