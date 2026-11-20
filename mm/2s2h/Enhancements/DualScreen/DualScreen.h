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
    DUAL_SCREEN_PAGE_NOTEBOOK,
    DUAL_SCREEN_PAGE_STATUS,
    DUAL_SCREEN_PAGE_MAX
} DualScreenPage;

typedef enum DualScreenSwipe {
    DUAL_SCREEN_SWIPE_NONE,
    DUAL_SCREEN_SWIPE_UP,
    DUAL_SCREEN_SWIPE_DOWN
} DualScreenSwipe;

// True while a secondary display surface exists and the feature is enabled
uint8_t DualScreen_IsActive(void);
int32_t DualScreen_GetFrameBufferId(void);
int32_t DualScreen_GetCanvasHeight(void);
// Touch gestures collected since the last call; called once per game frame
void DualScreen_ConsumeInput(uint8_t* tapped, int32_t* swipe);
// Copies the panel framebuffer onto the secondary display; called after the frame is rendered
void DualScreen_PresentFrame(void);

struct PlayState;
// Draws the current page into the panel framebuffer (DualScreenPanels.c)
void DualScreen_Draw(struct PlayState* play);

#ifdef __cplusplus
}
#endif

#endif
