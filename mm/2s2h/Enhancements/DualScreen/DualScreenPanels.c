// Second-screen pages, drawn with the game's own art so they read as part of Majora's Mask:
//  - Notebook: the real Bombers' Notebook schedule, live, scrolled by swiping the second screen
//  - Status: masks collected, hearts, wallet and bank, and stray fairies per dungeon
// Both share a bottom bar with the day, the time and the countdown to the moon's fall.
// Tapping the second screen switches pages.
#include "global.h"
#include "z64bombers_notebook.h"
#include "interface/schedule_static/schedule_static.h"
#include "interface/parameter_static/parameter_static.h"
#include "interface/week_static/week_static.h"
#include "archives/icon_item_static/icon_item_static_yar.h"
#include "objects/object_mag/object_mag.h"
#include "BenPort.h"
#include "2s2h/BenGui/HudEditor.h"
#include "DualScreen.h"
#include <stdlib.h>

// Bombers' Notebook pieces from z_play_hireso.c
void BombersNotebook_DrawHeaders(Gfx** gfxP);
void BombersNotebook_DrawColumns(Gfx** gfxP);
void BombersNotebook_DrawRows(BombersNotebook* this, Gfx** gfxP);
void BombersNotebook_DrawTimeOfDay(Gfx** gfxP);
void BombersNotebook_DrawArrows(BombersNotebook* this, Gfx** gfxP);

#define PANEL_GFX_SIZE 0x4000
#define NOTEBOOK_HEIGHT 480
#define NOTEBOOK_MAX_PAGE (BOMBERS_NOTEBOOK_PERSON_MAX - 4) // Same scroll range as the notebook itself
#define NOTEBOOK_ROW_HEIGHT 48
#define MOON_FALL_MINUTES (3 * 24 * 60)

static Gfx sPanelGfx[2][PANEL_GFX_SIZE];
static s32 sPanelGfxIndex = 0;
static s32 sPage = DUAL_SCREEN_PAGE_NOTEBOOK;
static BombersNotebook sPanelNotebook;
static s32 sNotebookTargetPage = 0;

static TexturePtr sNotebookDigitTextures[] = {
    gBombersNotebookDigit0Tex, gBombersNotebookDigit1Tex, gBombersNotebookDigit2Tex, gBombersNotebookDigit3Tex,
    gBombersNotebookDigit4Tex, gBombersNotebookDigit5Tex, gBombersNotebookDigit6Tex, gBombersNotebookDigit7Tex,
    gBombersNotebookDigit8Tex, gBombersNotebookDigit9Tex, gBombersNotebookColonTex,
};

static TexturePtr sCounterDigitTextures[] = {
    gCounterDigit0Tex, gCounterDigit1Tex, gCounterDigit2Tex, gCounterDigit3Tex, gCounterDigit4Tex,
    gCounterDigit5Tex, gCounterDigit6Tex, gCounterDigit7Tex, gCounterDigit8Tex, gCounterDigit9Tex,
};

static TexturePtr sDayTextures[] = { gClockDay1stTex, gClockDay1stTex, gClockDay2ndTex, gClockDayFinalTex,
                                     gClockDayFinalTex };

static TexturePtr sStrayFairyTextures[] = {
    gStrayFairyWoodfallIconTex,
    gStrayFairySnowheadIconTex,
    gStrayFairyGreatBayIconTex,
    gStrayFairyStoneTowerIconTex,
};

static TexturePtr sHeartPieceTextures[] = {
    gHeartEmptyTex,
    gHeartQuarterTex,
    gHeartHalfTex,
    gHeartThreeQuarterTex,
};

// #region Drawing helpers (canvas units, scaled to any size)

#define TEX_SCALE(texSize, rectSize) ((u16)(((texSize) << 10) / (rectSize)))

static Gfx* Panel_DrawTex4b(Gfx* gfx, TexturePtr tex, s32 fmt, s32 texW, s32 texH, s32 x, s32 y, s32 w, s32 h) {
    gDPLoadTextureBlock_4b(gfx++, tex, fmt, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                           G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0,
                        TEX_SCALE(texW, w), TEX_SCALE(texH, h));
    return gfx;
}

static Gfx* Panel_DrawTex8b(Gfx* gfx, TexturePtr tex, s32 fmt, s32 texW, s32 texH, s32 x, s32 y, s32 w, s32 h) {
    gDPLoadTextureBlock(gfx++, tex, fmt, G_IM_SIZ_8b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0,
                        TEX_SCALE(texW, w), TEX_SCALE(texH, h));
    return gfx;
}

static Gfx* Panel_DrawTexRGBA32(Gfx* gfx, TexturePtr tex, s32 texW, s32 texH, s32 x, s32 y, s32 w, s32 h) {
    gDPLoadTextureBlock(gfx++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0,
                        TEX_SCALE(texW, w), TEX_SCALE(texH, h));
    return gfx;
}

static Gfx* Panel_SetTexturedMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    return gfx;
}

static Gfx* Panel_FillRect(Gfx* gfx, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillRectangle(gfx++, x, y, x + w, y + h);
    return gfx;
}

static s32 Panel_CountDigits(s32 value) {
    s32 count = 1;
    while (value >= 10) {
        value /= 10;
        count++;
    }
    return count;
}

// Right-aligned HUD counter digits (the rupee/minigame counter font)
static Gfx* Panel_DrawCounter(Gfx* gfx, s32 value, s32 rightX, s32 y, s32 digitW, s32 digitH) {
    s32 i;
    s32 numDigits = Panel_CountDigits(value);

    for (i = 0; i < numDigits; i++) {
        s32 x = rightX - (i + 1) * digitW;
        gfx = Panel_DrawTex8b(gfx, sCounterDigitTextures[value % 10], G_IM_FMT_I, 8, 16, x, y, digitW, digitH);
        value /= 10;
    }
    return gfx;
}

// "HH:MM" in the Bombers' Notebook digits
// The digit glyphs sit in wide cells; advance by the inked width so "HH:MM" reads as one word
static s32 Panel_NotebookGlyphAdvance(s32 glyph, s32 digitW) {
    // Fixed-width digits, like a clock face, so the time doesn't jitter as it changes
    return (glyph == 10) ? digitW / 2 : digitW * 5 / 8;
}

static void Panel_NotebookTimeGlyphs(s32 hours, s32 minutes, s32 glyphs[5]) {
    glyphs[0] = (hours / 10) % 10;
    glyphs[1] = hours % 10;
    glyphs[2] = 10;
    glyphs[3] = minutes / 10;
    glyphs[4] = minutes % 10;
}

static Gfx* Panel_DrawNotebookTime(Gfx* gfx, s32 hours, s32 minutes, s32 x, s32 y, s32 digitW, s32 digitH) {
    s32 glyphs[5];
    s32 i;

    Panel_NotebookTimeGlyphs(hours, minutes, glyphs);
    for (i = 0; i < 5; i++) {
        s32 advance = Panel_NotebookGlyphAdvance(glyphs[i], digitW);

        // Glyphs are centred in their cells, so centre each cell on its advance
        gfx = Panel_DrawTex4b(gfx, sNotebookDigitTextures[glyphs[i]], G_IM_FMT_I, 16, 17,
                              x + (advance - digitW) / 2 + ((glyphs[i] == 10) ? digitW / 10 : 0), y, digitW,
                              digitH);
        x += advance;
    }
    return gfx;
}

// #endregion

static void Panel_UpdateNotebookScroll(s32 swipe) {
    BombersNotebook* nb = &sPanelNotebook;

    if (swipe > 0) {
        sNotebookTargetPage = CLAMP_MAX(sNotebookTargetPage + 4, NOTEBOOK_MAX_PAGE);
    } else if (swipe < 0) {
        sNotebookTargetPage = CLAMP_MIN(sNotebookTargetPage - 4, 0);
    }

    // Same row-by-row slide as scrolling the notebook with the stick
    if (nb->scrollAmount == 0) {
        if (nb->cursorPage < sNotebookTargetPage) {
            nb->scrollAmount = -24;
            nb->scrollOffset = 0;
        } else if (nb->cursorPage > sNotebookTargetPage) {
            nb->cursorPage--;
            nb->scrollAmount = 24;
            nb->scrollOffset = -NOTEBOOK_ROW_HEIGHT;
        }
    } else if (nb->scrollAmount < 0) {
        nb->scrollOffset += nb->scrollAmount;
        if (ABS_ALT(nb->scrollOffset) >= NOTEBOOK_ROW_HEIGHT) {
            Audio_PlaySfx(NA_SE_SY_CURSOR);
            nb->scrollOffset = 0;
            nb->scrollAmount = 0;
            nb->cursorPage++;
        }
    } else {
        nb->scrollOffset += nb->scrollAmount;
        if (nb->scrollOffset >= 0) {
            Audio_PlaySfx(NA_SE_SY_CURSOR);
            nb->scrollOffset = 0;
            nb->scrollAmount = 0;
        }
    }
}

static Gfx* Panel_DrawBackground(Gfx* gfx, s32 canvasHeight) {
    // The notebook's parchment, tiled across the whole screen
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 215, 255);
    gDPSetEnvColor(gfx++, 100, 100, 100, 255);
    gDPLoadTextureBlock(gfx++, gBombersNotebookBackgroundTex, G_IM_FMT_I, G_IM_SIZ_8b, 32, 32, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(gfx++, 0, 0, DUAL_SCREEN_CANVAS_WIDTH << 2, canvasHeight << 2, G_TX_RENDERTILE, 0, 0, 0x200,
                        0x200);
    return gfx;
}

static Gfx* Panel_DrawNotebook(Gfx* gfx, s32 canvasHeight) {
    sPanelNotebook.loadState = BOMBERS_NOTEBOOK_LOAD_STATE_DONE;
    // No cursor on the panel: keep the selected-row highlight off every row
    sPanelNotebook.cursorPageRow = BOMBERS_NOTEBOOK_PERSON_MAX;

    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    BombersNotebook_DrawHeaders(&gfx);
    BombersNotebook_DrawColumns(&gfx);

    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 86, 600, 450);
    BombersNotebook_DrawRows(&sPanelNotebook, &gfx);
    gDPPipeSync(gfx++);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, DUAL_SCREEN_CANVAS_WIDTH, canvasHeight);

    BombersNotebook_DrawTimeOfDay(&gfx);
    BombersNotebook_DrawArrows(&sPanelNotebook, &gfx);
    return gfx;
}

// Counters start just right of their 44-unit icons at x 404
#define STATUS_COUNTER_X 462

static Gfx* Panel_DrawStatus(Gfx* gfx) {
    s32 i;
    s32 x;
    s32 y;

    // Masks, laid out like the pause menu's mask page; empty slots stay as faint rings
    for (i = 0; i < 24; i++) {
        u8 item = gSaveContext.save.saveInfo.inventory.items[SLOT_MASK_POSTMAN + i];

        x = 30 + (i % 6) * 58;
        y = 36 + (i / 6) * 58;
        gfx = Panel_SetTexturedMode(gfx, 90, 60, 30, (item == ITEM_NONE) ? 35 : 110);
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x, y, 52, 52);
        if (item != ITEM_NONE) {
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            gfx = Panel_DrawTexRGBA32(gfx, gItemIcons[item], 32, 32, x + 4, y + 4, 44, 44);
        }
    }

    // Hearts: containers, plus the pieces toward the next one
    gfx = Panel_SetTexturedMode(gfx, 255, 70, 50, 255);
    gfx = Panel_DrawTex8b(gfx, gHeartFullTex, G_IM_FMT_IA, 16, 16, 404, 40, 44, 44);
    gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    x = STATUS_COUNTER_X + Panel_CountDigits(gSaveContext.save.saveInfo.playerData.healthCapacity / 0x10) * 16;
    gfx = Panel_DrawCounter(gfx, gSaveContext.save.saveInfo.playerData.healthCapacity / 0x10, x, 46, 16, 32);
    gfx = Panel_SetTexturedMode(gfx, 255, 70, 50, 255);
    gfx = Panel_DrawTex8b(gfx, sHeartPieceTextures[GET_QUEST_HEART_PIECE_COUNT % 4], G_IM_FMT_IA, 16, 16, x + 14,
                          46, 32, 32);

    // Wallet and bank
    gfx = Panel_SetTexturedMode(gfx, 200, 255, 100, 255);
    gfx = Panel_DrawTex8b(gfx, gRupeeCounterIconTex, G_IM_FMT_IA, 16, 16, 404, 108, 44, 44);
    gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    gfx = Panel_DrawCounter(gfx, gSaveContext.save.saveInfo.playerData.rupees,
                            STATUS_COUNTER_X + Panel_CountDigits(gSaveContext.save.saveInfo.playerData.rupees) * 16,
                            114, 16, 32);

    gfx = Panel_SetTexturedMode(gfx, 255, 210, 80, 255);
    gfx = Panel_DrawTex8b(gfx, gRupeeCounterIconTex, G_IM_FMT_IA, 16, 16, 404, 176, 44, 44);
    gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    gfx = Panel_DrawCounter(gfx, HS_GET_BANK_RUPEES(), STATUS_COUNTER_X + Panel_CountDigits(HS_GET_BANK_RUPEES()) * 16,
                            182, 16, 32);

    // Stray fairies rescued per dungeon; a full set of 15 turns gold
    for (i = 0; i < ARRAY_COUNT(sStrayFairyTextures); i++) {
        s32 count = gSaveContext.save.saveInfo.inventory.strayFairies[i];

        x = 46 + i * 148;
        y = 318;
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
        gfx = Panel_DrawTexRGBA32(gfx, sStrayFairyTextures[i], 32, 24, x, y, 80, 60);
        if (count >= 15) {
            gfx = Panel_SetTexturedMode(gfx, 220, 150, 20, 255);
        } else {
            gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
        }
        gfx = Panel_DrawCounter(gfx, CLAMP(count, 0, 99), x + 40 + Panel_CountDigits(CLAMP(count, 0, 99)) * 8,
                                y + 70, 16, 32);
    }
    return gfx;
}

// Moon countdown is right-aligned to the same 24-unit margin as the day label on the left
// #region Touch

// A drag shorter than this (canvas units) still counts as a tap
#define TOUCH_SLOP 14
#define SWIPE_MIN 40

typedef struct PanelTouch {
    s32 down;
    s32 moved;    // left the slop radius since going down
    s32 released; // went up this update
    f32 x;
    f32 y;
    f32 startX;
    f32 startY;
} PanelTouch;

static PanelTouch sTouch;

static void Panel_PollTouch(void) {
    DualScreenTouchEvent event;

    sTouch.released = false;
    while (DualScreen_PollTouch(&event)) {
        if (event.type == DUAL_SCREEN_TOUCH_DOWN) {
            sTouch.down = true;
            sTouch.moved = false;
            sTouch.startX = event.x;
            sTouch.startY = event.y;
        } else if (!sTouch.down) {
            continue;
        }
        sTouch.x = event.x;
        sTouch.y = event.y;
        if ((ABS_ALT((s32)(sTouch.x - sTouch.startX)) > TOUCH_SLOP) ||
            (ABS_ALT((s32)(sTouch.y - sTouch.startY)) > TOUCH_SLOP)) {
            sTouch.moved = true;
        }
        if (event.type == DUAL_SCREEN_TOUCH_UP) {
            sTouch.down = false;
            sTouch.released = true;
            // Release the rest next update, so a quick tap is seen as a tap and not lost
            break;
        }
    }
}

static s32 Panel_InRect(f32 x, f32 y, s32 rx, s32 ry, s32 rw, s32 rh) {
    return (x >= rx) && (x < rx + rw) && (y >= ry) && (y < ry + rh);
}

// Released this update without moving, inside the rect (judged where the finger went down)
static s32 Panel_Tapped(s32 rx, s32 ry, s32 rw, s32 rh) {
    return sTouch.released && !sTouch.moved && Panel_InRect(sTouch.startX, sTouch.startY, rx, ry, rw, rh);
}

// Held down without moving, inside the rect: used to show press feedback
static s32 Panel_Pressing(s32 rx, s32 ry, s32 rw, s32 rh) {
    return sTouch.down && !sTouch.moved && Panel_InRect(sTouch.startX, sTouch.startY, rx, ry, rw, rh);
}

// Vertical swipe this update: +1 for a swipe up (move down the list), -1 for a swipe down
static s32 Panel_VerticalSwipe(void) {
    f32 dx = sTouch.x - sTouch.startX;
    f32 dy = sTouch.y - sTouch.startY;

    if (!sTouch.released || (ABS_ALT((s32)dy) < SWIPE_MIN) || (ABS_ALT((s32)dy) <= ABS_ALT((s32)dx))) {
        return 0;
    }
    return (dy < 0) ? 1 : -1;
}

// #endregion

// #region Strip: clock, tabs and moon countdown along the bottom

#define STRIP_Y NOTEBOOK_HEIGHT
#define STRIP_DIGIT_W 22
#define STRIP_DIGIT_H 30
#define STRIP_TIME_W (4 * (STRIP_DIGIT_W * 5 / 8) + STRIP_DIGIT_W / 2)
#define STRIP_TAB_SIZE 60
#define STRIP_TAB_ICON 44

typedef struct PanelTab {
    s32 page;
    TexturePtr icon;
} PanelTab;

static PanelTab sTabs[] = {
    { DUAL_SCREEN_PAGE_NOTEBOOK, gItemIconBombersNotebookTex },
    { DUAL_SCREEN_PAGE_STATUS, gItemIconOdolwasRemainsTex },
};

static s32 Panel_TabAvailable(s32 page) {
    // The schedule only exists once Link has the notebook, as in the original
    return (page != DUAL_SCREEN_PAGE_NOTEBOOK) || CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK);
}

static s32 Panel_TabCount(void) {
    s32 i;
    s32 count = 0;

    for (i = 0; i < ARRAY_COUNT(sTabs); i++) {
        count += Panel_TabAvailable(sTabs[i].page);
    }
    return count;
}

static s32 Panel_StripCenterY(s32 canvasHeight) {
    return STRIP_Y + 4 + (canvasHeight - STRIP_Y - 4) / 2;
}

// Tabs sit centred between the clock and the moon; visible tabs are packed together
static s32 Panel_TabX(s32 index) {
    return DUAL_SCREEN_CANVAS_WIDTH / 2 - (Panel_TabCount() * STRIP_TAB_SIZE) / 2 + index * STRIP_TAB_SIZE;
}

static void Panel_UpdateTabs(s32 canvasHeight) {
    s32 i;
    s32 index = 0;
    s32 tabY = Panel_StripCenterY(canvasHeight) - STRIP_TAB_SIZE / 2;

    for (i = 0; i < ARRAY_COUNT(sTabs); i++) {
        if (!Panel_TabAvailable(sTabs[i].page)) {
            continue;
        }
        if (Panel_Tapped(Panel_TabX(index), tabY, STRIP_TAB_SIZE, STRIP_TAB_SIZE) && (sPage != sTabs[i].page)) {
            sPage = sTabs[i].page;
            Audio_PlaySfx(NA_SE_SY_DECIDE);
        }
        index++;
    }
    if (!Panel_TabAvailable(sPage)) {
        sPage = DUAL_SCREEN_PAGE_STATUS;
    }
}

static Gfx* Panel_DrawTabs(Gfx* gfx, s32 canvasHeight) {
    s32 i;
    s32 index = 0;
    s32 tabY = Panel_StripCenterY(canvasHeight) - STRIP_TAB_SIZE / 2;

    for (i = 0; i < ARRAY_COUNT(sTabs); i++) {
        s32 x;
        s32 selected = (sTabs[i].page == sPage);
        s32 grow;

        if (!Panel_TabAvailable(sTabs[i].page)) {
            continue;
        }
        x = Panel_TabX(index++);
        grow = Panel_Pressing(x, tabY, STRIP_TAB_SIZE, STRIP_TAB_SIZE) ? 4 : 0;

        // Gold ring behind the open page, like the pause menu's cursor
        if (selected) {
            gfx = Panel_SetTexturedMode(gfx, 255, 200, 60, 230);
        } else {
            gfx = Panel_SetTexturedMode(gfx, 90, 60, 30, 50);
        }
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x + 4 - grow, tabY + 4 - grow,
                              STRIP_TAB_SIZE - 8 + grow * 2, STRIP_TAB_SIZE - 8 + grow * 2);
        if (selected) {
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
        } else {
            gfx = Panel_SetTexturedMode(gfx, 190, 180, 160, 210);
        }
        gfx = Panel_DrawTexRGBA32(gfx, sTabs[i].icon, 32, 32, x + (STRIP_TAB_SIZE - STRIP_TAB_ICON) / 2 - grow,
                                  tabY + (STRIP_TAB_SIZE - STRIP_TAB_ICON) / 2 - grow, STRIP_TAB_ICON + grow * 2,
                                  STRIP_TAB_ICON + grow * 2);
    }
    return gfx;
}

static Gfx* Panel_DrawStrip(Gfx* gfx, s32 canvasHeight) {
    s32 day = CURRENT_DAY;
    s32 minutes = (s32)TIME_TO_MINUTES_F(CURRENT_TIME);
    s32 elapsed;
    s32 remaining;
    s32 centerY = Panel_StripCenterY(canvasHeight);
    s32 segW = (DUAL_SCREEN_CANVAS_WIDTH - 40 - 2 * 4) / 3;
    s32 i;

    // Days start at dawn, so the moon falls at 6:00 on the morning after the Final Day
    if (day == 0) {
        elapsed = 0;
    } else {
        elapsed = (day - 1) * 24 * 60 + ((minutes - 6 * 60 + 24 * 60) % (24 * 60));
    }
    elapsed = CLAMP(elapsed, 0, MOON_FALL_MINUTES);
    remaining = MOON_FALL_MINUTES - elapsed;

    // The three days as a timeline across the top of the strip, filled up to now
    for (i = 0; i < 3; i++) {
        s32 segX = 20 + i * (segW + 4);
        s32 segFill = CLAMP(elapsed - i * 24 * 60, 0, 24 * 60) * segW / (24 * 60);

        gfx = Panel_FillRect(gfx, segX, STRIP_Y, segW, 4, 90, 50, 20, 60);
        if (segFill > 0) {
            gfx = Panel_FillRect(gfx, segX, STRIP_Y, segFill, 4, (i == 2) ? 190 : 120, (i == 2) ? 40 : 70, 30, 220);
        }
    }

    // Day label from the three-day clock, then the time in the notebook's hand
    gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    gfx = Panel_DrawTex8b(gfx, sDayTextures[CLAMP(day, 0, 4)], G_IM_FMT_I, 48, 27, 16, centerY - 17, 60, 34);
    gfx = Panel_DrawNotebookTime(gfx, minutes / 60, minutes % 60, 82, centerY - STRIP_DIGIT_H / 2, STRIP_DIGIT_W,
                                 STRIP_DIGIT_H);

    // Time left before the moon falls; it burns red in the final hours
    if (remaining <= 6 * 60) {
        gfx = Panel_SetTexturedMode(gfx, 255, 60, 30, 255);
    } else {
        gfx = Panel_SetTexturedMode(gfx, 200, 170, 120, 255);
    }
    gfx = Panel_DrawTex8b(gfx, gThreeDayClockMoonHourTex, G_IM_FMT_IA, 24, 24,
                          DUAL_SCREEN_CANVAS_WIDTH - 16 - STRIP_TIME_W - 38, centerY - 16, 32, 32);
    if (remaining <= 6 * 60) {
        gfx = Panel_SetTexturedMode(gfx, 200, 30, 20, 255);
    } else {
        gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    }
    gfx = Panel_DrawNotebookTime(gfx, remaining / 60, remaining % 60, DUAL_SCREEN_CANVAS_WIDTH - 16 - STRIP_TIME_W,
                                 centerY - STRIP_DIGIT_H / 2, STRIP_DIGIT_W, STRIP_DIGIT_H);

    return Panel_DrawTabs(gfx, canvasHeight);
}

// #endregion

void DualScreen_Update(PlayState* play) {
    s32 canvasHeight = DualScreen_GetCanvasHeight();

    if (!DualScreen_IsActive()) {
        return;
    }
    Panel_PollTouch();
    Panel_UpdateTabs(canvasHeight);
    // Swipes above the strip scroll the schedule
    Panel_UpdateNotebookScroll(((sPage == DUAL_SCREEN_PAGE_NOTEBOOK) && (sTouch.startY < STRIP_Y))
                                   ? Panel_VerticalSwipe()
                                   : 0);
}

static Gfx* Panel_Begin(Gfx** start, s32 frameBufferId, s32 canvasHeight) {
    Gfx* gfx;

    *start = gfx = sPanelGfx[sPanelGfxIndex];
    sPanelGfxIndex ^= 1;

    // Keep the HUD editor's per-element overrides away from these draws
    HudEditor_SetActiveElement(HUD_EDITOR_ELEMENT_NONE);

    gsSPSetFB(gfx++, frameBufferId);
    gfx = Gfx_SetupDL39(gfx);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, DUAL_SCREEN_CANVAS_WIDTH, canvasHeight);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);
    return Panel_DrawBackground(gfx, canvasHeight);
}

static void Panel_End(Gfx* start, Gfx* gfx, GraphicsContext* gfxCtx) {
    gDPPipeSync(gfx++);
    gsSPResetFB(gfx++);
    gSPEndDisplayList(gfx++);

    if (gfx - start > PANEL_GFX_SIZE) {
        // Already past the end of the buffer; nothing safe left to do but stop
        abort();
    }

    OPEN_DISPS(gfxCtx);
    gSPDisplayList(OVERLAY_DISP++, start);
    // Restore the main screen's scissor for whatever is drawn after us
    gSPDisplayList(OVERLAY_DISP++, D_0E000000_TO_SEGMENTED(setScissor));
    CLOSE_DISPS(gfxCtx);
}

// Frames since gameplay last drew the panels; the title page takes over once it's clearly gone
static s32 sFramesSincePlayDraw = 0;

void DualScreen_Draw(PlayState* play) {
    Gfx* start;
    Gfx* gfx;
    s32 canvasHeight = DualScreen_GetCanvasHeight();
    s32 frameBufferId = DualScreen_GetFrameBufferId();

    if (frameBufferId < 0) {
        return;
    }
    sFramesSincePlayDraw = 0;

    gfx = Panel_Begin(&start, frameBufferId, canvasHeight);
    if (sPage == DUAL_SCREEN_PAGE_NOTEBOOK) {
        gfx = Panel_DrawNotebook(gfx, canvasHeight);
    } else {
        gfx = Panel_DrawStatus(gfx);
    }
    gfx = Panel_DrawStrip(gfx, canvasHeight);
    Panel_End(start, gfx, play->state.gfxCtx);
}

void DualScreen_DrawIdle(GraphicsContext* gfxCtx) {
    Gfx* start;
    Gfx* gfx;
    s32 canvasHeight;
    s32 frameBufferId;

    // Gameplay can skip a frame or two (transitions); only take over once it has really stopped
    if (!DualScreen_IsActive() || (++sFramesSincePlayDraw < 4)) {
        return;
    }
    frameBufferId = DualScreen_GetFrameBufferId();
    if (frameBufferId < 0) {
        return;
    }
    canvasHeight = DualScreen_GetCanvasHeight();

    // The title screen's logo on the notebook's parchment
    gfx = Panel_Begin(&start, frameBufferId, canvasHeight);
    gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
    gfx = Panel_DrawTexRGBA32(gfx, gTitleScreenZeldaLogoTex, 144, 64, 320 - 216, canvasHeight / 2 - 120, 432, 192);
    gfx = Panel_SetTexturedMode(gfx, 120, 60, 20, 255);
    gfx = Panel_DrawTex8b(gfx, gTitleScreenMajorasMaskSubtitleTex, G_IM_FMT_I, 104, 16, 320 - 156,
                          canvasHeight / 2 + 84, 312, 48);
    Panel_End(start, gfx, gfxCtx);
}
