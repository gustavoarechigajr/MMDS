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
#include "2s2h/GameInteractor/GameInteractor.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <stdlib.h>

// Bombers' Notebook pieces from z_play_hireso.c
void BombersNotebook_DrawHeaders(Gfx** gfxP);
void BombersNotebook_DrawColumns(Gfx** gfxP);
void BombersNotebook_DrawRows(BombersNotebook* this, Gfx** gfxP);
void BombersNotebook_DrawTimeOfDay(Gfx** gfxP);
void BombersNotebook_DrawArrows(BombersNotebook* this, Gfx** gfxP);

// Pause menu equip logic (z_kaleido_item.c / z_kaleido_mask.c), shared so touch equips follow the same rules
void KaleidoScope_ApplyItemEquip(PlayState* play);
void KaleidoScope_ApplyMaskEquip(PlayState* play);
extern u8 gPlayerFormSlotRestrictions[PLAYER_FORM_MAX][ITEM_NUM_SLOTS];
extern u8 sPlayerFormItems[PLAYER_FORM_MAX];
extern u16 sMasksGivenOnMoonBits[];
#define CHECK_GIVEN_MASK_ON_MOON(maskIndex) \
    (gSaveContext.masksGivenOnMoon[sMasksGivenOnMoonBits[maskIndex] >> 8] & (u8)sMasksGivenOnMoonBits[maskIndex])

#define PANEL_GFX_SIZE 0x4000
#define NOTEBOOK_HEIGHT 480
#define NOTEBOOK_MAX_PAGE (BOMBERS_NOTEBOOK_PERSON_MAX - 4) // Same scroll range as the notebook itself
#define NOTEBOOK_ROW_HEIGHT 48
#define MOON_FALL_MINUTES (3 * 24 * 60)

static Gfx sPanelGfx[2][PANEL_GFX_SIZE];
static s32 sPanelGfxIndex = 0;
static s32 sPage = DUAL_SCREEN_PAGE_ITEMS;
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
    s32 began;    // went down this update
    f32 x;
    f32 y;
    f32 startX;
    f32 startY;
} PanelTouch;

static PanelTouch sTouch;

static void Panel_PollTouch(void) {
    DualScreenTouchEvent event;

    sTouch.released = false;
    sTouch.began = false;
    while (DualScreen_PollTouch(&event)) {
        if (event.type == DUAL_SCREEN_TOUCH_DOWN) {
            sTouch.down = true;
            sTouch.began = true;
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
    u8 iconItem;
} PanelTab;

static PanelTab sTabs[] = {
    { DUAL_SCREEN_PAGE_ITEMS, ITEM_OCARINA_OF_TIME },
    { DUAL_SCREEN_PAGE_MASKS, ITEM_MASK_DEKU },
    { DUAL_SCREEN_PAGE_NOTEBOOK, ITEM_BOMBERS_NOTEBOOK },
    { DUAL_SCREEN_PAGE_STATUS, ITEM_REMAINS_ODOLWA },
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
        sPage = DUAL_SCREEN_PAGE_ITEMS;
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
        gfx = Panel_DrawTexRGBA32(gfx, gItemIcons[sTabs[i].iconItem], 32, 32, x + (STRIP_TAB_SIZE - STRIP_TAB_ICON) / 2 - grow,
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

// #region Items and masks: the pause menu's grids, equipped by tapping or dragging onto a button

#define GRID_COLS 6
#define GRID_ROWS 4
#define GRID_SLOT 70
#define GRID_X 16
#define GRID_Y ((STRIP_Y - GRID_ROWS * GRID_SLOT) / 2)
#define GRID_ICON 64

// Same values as the pause menu's PauseEquipCButton / PauseEquipDButton
#define EQUIP_TARGET_C_LEFT 0
#define EQUIP_TARGET_C_DOWN 1
#define EQUIP_TARGET_C_RIGHT 2
#define EQUIP_TARGET_D_RIGHT 3
#define EQUIP_TARGET_D_UP 6

typedef struct PanelEquipTarget {
    s32 target; // EQUIP_TARGET_*
    s32 x;      // centre
    s32 y;
    s32 size;
} PanelEquipTarget;

// The face buttons in the Thor's Nintendo layout: X (C-Left) on top, Y (C-Right) left, A right, B bottom,
// RB (C-Down) on the shoulder, and the D-pad below
#define FACE_X 540
#define FACE_Y 150
#define FACE_GAP 62
#define DPAD_Y 368

static PanelEquipTarget sEquipTargets[] = {
    { EQUIP_TARGET_C_LEFT, FACE_X, FACE_Y - FACE_GAP, 70 },
    { EQUIP_TARGET_C_RIGHT, FACE_X - FACE_GAP, FACE_Y, 70 },
    { EQUIP_TARGET_C_DOWN, FACE_X + 70, FACE_Y - FACE_GAP - 44, 56 },
    { EQUIP_TARGET_D_RIGHT, FACE_X + 60, DPAD_Y, 60 },
    { EQUIP_TARGET_D_RIGHT + 1, FACE_X - 60, DPAD_Y, 60 }, // D-Left
    { EQUIP_TARGET_D_RIGHT + 2, FACE_X, DPAD_Y + 60, 60 }, // D-Down
    { EQUIP_TARGET_D_UP, FACE_X, DPAD_Y - 60, 60 },
};

static s32 sSelectedSlot = -1; // inventory slot picked by a tap, waiting for a button
static s32 sDragSlot = -1;     // inventory slot under the finger since it went down
static s32 sUseTarget = -1;    // button held down on the screen, pressed in-game for as long as it's held

static u16 sTargetButtons[] = { BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT, BTN_DRIGHT, BTN_DLEFT, BTN_DDOWN, BTN_DUP };

static s32 Panel_DpadEquipsOn(void) {
    return CVarGetInteger("gEnhancements.Dpad.DpadEquips", 0);
}

static s32 Panel_TargetVisible(s32 target) {
    return (target < EQUIP_TARGET_D_RIGHT) || Panel_DpadEquipsOn();
}

static u8 Panel_TargetItem(s32 target) {
    if (target < EQUIP_TARGET_D_RIGHT) {
        return BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT + target) & 0xFF;
    }
    return DPAD_BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_D_RIGHT + target - EQUIP_TARGET_D_RIGHT) & 0xFF;
}

static TexturePtr Panel_TargetLabel(s32 target) {
    // With the button-letter pack these are the X / RB / Y letters
    switch (target) {
        case EQUIP_TARGET_C_LEFT:
            return gEmptyCLeftArrowTex;
        case EQUIP_TARGET_C_DOWN:
            return gEmptyCDownArrowTex;
        case EQUIP_TARGET_C_RIGHT:
            return gEmptyCRightArrowTex;
        default:
            return NULL;
    }
}

static s32 Panel_GridSlot(s32 firstSlot, f32 x, f32 y) {
    s32 col = (s32)(x - GRID_X) / GRID_SLOT;
    s32 row = (s32)(y - GRID_Y) / GRID_SLOT;

    if ((x < GRID_X) || (y < GRID_Y) || (col >= GRID_COLS) || (row >= GRID_ROWS)) {
        return -1;
    }
    return firstSlot + row * GRID_COLS + col;
}

static s32 Panel_TargetAt(f32 x, f32 y) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sEquipTargets); i++) {
        PanelEquipTarget* t = &sEquipTargets[i];
        s32 half = t->size / 2 + 6; // a little forgiving for fingers

        if (Panel_TargetVisible(t->target) && Panel_InRect(x, y, t->x - half, t->y - half, half * 2, half * 2)) {
            return t->target;
        }
    }
    return -1;
}

// Same rules as the pause menu: owned, usable in the current form, and not a mask given away on the moon
static s32 Panel_SlotEquippable(s32 slot) {
    u8 item = gSaveContext.save.saveInfo.inventory.items[slot];

    if (item == ITEM_NONE) {
        return false;
    }
    if (slot >= ITEM_NUM_SLOTS) {
        return !CHECK_GIVEN_MASK_ON_MOON(slot - ITEM_NUM_SLOTS);
    }
    return gPlayerFormSlotRestrictions[GET_PLAYER_FORM][slot];
}

static s32 Panel_SlotEquipped(s32 slot) {
    u8 item = gSaveContext.save.saveInfo.inventory.items[slot];
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sEquipTargets); i++) {
        u8 equipped = Panel_TargetItem(sEquipTargets[i].target);

        if (Panel_TargetVisible(sEquipTargets[i].target) &&
            ((equipped == item) || ((slot == SLOT_BOW) && (equipped >= ITEM_BOW_FIRE) && (equipped <= ITEM_BOW_LIGHT)))) {
            return true;
        }
    }
    return false;
}

// Equips like the pause menu does, without the pause menu: only while Link is free to act
static void Panel_Equip(PlayState* play, s32 slot, s32 target) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u8 item = gSaveContext.save.saveInfo.inventory.items[slot];

    if (!Panel_SlotEquippable(slot) || (pauseCtx->state != PAUSE_STATE_OFF) ||
        (play->msgCtx.msgMode != MSGMODE_NONE) || Player_InCsMode(play) ||
        (play->transitionTrigger != TRANS_TRIGGER_OFF) ||
        // A transformation mask can't be taken off the button while it's being worn
        ((GET_PLAYER_FORM != PLAYER_FORM_HUMAN) && (Panel_TargetItem(target) == sPlayerFormItems[GET_PLAYER_FORM]))) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return;
    }
    if ((slot < ITEM_NUM_SLOTS) && !GameInteractor_Should(VB_KALEIDO_EQUIP_ITEM_TO_BUTTON, true, slot, item)) {
        return;
    }

    pauseCtx->equipTargetItem = item;
    pauseCtx->equipTargetSlot = slot;
    pauseCtx->equipTargetCBtn = target;
    if ((item >= ITEM_ARROW_FIRE) && (item <= ITEM_ARROW_LIGHT)) {
        // Magic arrows go on the bow, as at the end of the pause menu's arrow animation
        pauseCtx->equipTargetItem = ITEM_BOW_FIRE + (item - ITEM_ARROW_FIRE);
        pauseCtx->equipTargetSlot = SLOT_BOW;
        Audio_PlaySfx(NA_SE_SY_SET_FIRE_ARROW + (item - ITEM_ARROW_FIRE));
    } else {
        Audio_PlaySfx(NA_SE_SY_DECIDE);
    }

    if (slot >= ITEM_NUM_SLOTS) {
        KaleidoScope_ApplyMaskEquip(play);
    } else {
        KaleidoScope_ApplyItemEquip(play);
    }
}

// Holding an equipped button presses it in-game, MM3D style: pressed on touch, held while the finger stays,
// released with the finger (so the bow aims while held and fires on release)
static void Panel_UpdateTouchUse(PlayState* play) {
    Input* input = &play->state.input[0];
    u16 button;

    if (sTouch.began && (sSelectedSlot < 0) && CVarGetInteger("gEnhancements.DualScreen.TouchUse", 1)) {
        s32 target = Panel_TargetAt(sTouch.startX, sTouch.startY);

        if ((target >= 0) && (Panel_TargetItem(target) < ITEM_FD)) {
            sUseTarget = target;
            input->press.button |= sTargetButtons[target];
        }
    }
    if (sUseTarget < 0) {
        return;
    }
    button = sTargetButtons[sUseTarget];
    if (sTouch.down) {
        input->cur.button |= button;
    } else {
        input->rel.button |= button;
        sUseTarget = -1;
    }
}

static void Panel_UpdateEquipPage(PlayState* play, s32 firstSlot) {
    s32 slot;
    s32 target;

    if (sTouch.began) {
        slot = Panel_GridSlot(firstSlot, sTouch.startX, sTouch.startY);
        sDragSlot = ((slot >= 0) && Panel_SlotEquippable(slot)) ? slot : -1;
    }
    Panel_UpdateTouchUse(play);
    if (!sTouch.released) {
        return;
    }

    target = Panel_TargetAt(sTouch.x, sTouch.y);
    if (sTouch.moved) {
        // Dragged from the grid onto a button
        if ((sDragSlot >= 0) && (target >= 0)) {
            Panel_Equip(play, sDragSlot, target);
            sSelectedSlot = -1;
        }
    } else if (target >= 0) {
        // Tapped a button with an item picked
        if (sSelectedSlot >= 0) {
            Panel_Equip(play, sSelectedSlot, target);
            sSelectedSlot = -1;
        }
    } else {
        // Tapped the grid: pick an item, or put it back down
        slot = Panel_GridSlot(firstSlot, sTouch.startX, sTouch.startY);
        if ((slot >= 0) && Panel_SlotEquippable(slot) && (slot != sSelectedSlot)) {
            sSelectedSlot = slot;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        } else {
            sSelectedSlot = -1;
        }
    }
    sDragSlot = -1;
}

static Gfx* Panel_DrawAmmo(Gfx* gfx, s32 slot, s32 rightX, s32 y) {
    s32 ammo;

    switch (slot) {
        case SLOT_BOW:
        case SLOT_BOMB:
        case SLOT_BOMBCHU:
        case SLOT_DEKU_STICK:
        case SLOT_DEKU_NUT:
        case SLOT_MAGIC_BEANS:
        case SLOT_POWDER_KEG:
            break;
        default:
            return gfx;
    }
    ammo = gSaveContext.save.saveInfo.inventory.ammo[slot];
    // A soft shadow, then the count in the pause menu's white
    gfx = Panel_SetTexturedMode(gfx, 0, 0, 0, 150);
    gfx = Panel_DrawCounter(gfx, ammo, rightX + 2, y + 2, 13, 26);
    gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
    return Panel_DrawCounter(gfx, ammo, rightX, y, 13, 26);
}

static Gfx* Panel_DrawItemIcon(Gfx* gfx, u8 item, s32 cx, s32 cy, s32 size) {
    return Panel_DrawTexRGBA32(gfx, gItemIcons[item], 32, 32, cx - size / 2, cy - size / 2, size, size);
}

static Gfx* Panel_DrawEquipTargets(Gfx* gfx) {
    s32 i;

    // A and B as they sit on the controller, for orientation (not equip targets)
    gfx = Panel_SetTexturedMode(gfx, 100, 200, 255, 120);
    gfx = Panel_DrawTex8b(gfx, gButtonBackgroundTex, G_IM_FMT_IA, 32, 32, FACE_X + FACE_GAP - 34, FACE_Y - 34, 68,
                          68);
    gfx = Panel_SetTexturedMode(gfx, 100, 255, 120, 120);
    gfx = Panel_DrawTex8b(gfx, gButtonBackgroundTex, G_IM_FMT_IA, 32, 32, FACE_X - 34, FACE_Y + FACE_GAP - 34, 68,
                          68);
    if (GET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_B) < ITEM_FD) {
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 160);
        gfx = Panel_DrawItemIcon(gfx, GET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_B), FACE_X - 1, FACE_Y + FACE_GAP - 1, 64);
    }

    for (i = 0; i < ARRAY_COUNT(sEquipTargets); i++) {
        PanelEquipTarget* t = &sEquipTargets[i];
        u8 item;
        s32 highlight;

        if (!Panel_TargetVisible(t->target)) {
            continue;
        }
        item = Panel_TargetItem(t->target);
        // Buttons light up while an item is picked or dragged over them
        highlight = ((sSelectedSlot >= 0) || (sDragSlot >= 0 && sTouch.moved)) ? 1 : 0;
        if ((sTouch.down && sTouch.moved && (sDragSlot >= 0) && (Panel_TargetAt(sTouch.x, sTouch.y) == t->target)) ||
            (sUseTarget == t->target)) {
            highlight = 2;
        }

        // C buttons in the HUD's yellow; D-pad slots in a neutral grey
        if (t->target < EQUIP_TARGET_D_RIGHT) {
            gfx = Panel_SetTexturedMode(gfx, 255, 240, 0, (highlight == 2) ? 255 : (highlight ? 200 : 140));
        } else {
            gfx = Panel_SetTexturedMode(gfx, 200, 200, 190, (highlight == 2) ? 255 : (highlight ? 200 : 130));
        }
        gfx = Panel_DrawTex8b(gfx, gButtonBackgroundTex, G_IM_FMT_IA, 32, 32, t->x - t->size / 2 - highlight * 2,
                              t->y - t->size / 2 - highlight * 2, t->size + highlight * 4, t->size + highlight * 4);
        if (item < ITEM_FD) {
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            // Fill the button face; the face sits a touch up-left of the texture's centre because of its shadow
            gfx = Panel_DrawItemIcon(gfx, item, t->x - 1, t->y - 1, t->size * 19 / 20);
        } else if (Panel_TargetLabel(t->target) != NULL) {
            gfx = Panel_SetTexturedMode(gfx, 255, 240, 0, 255);
            gfx = Panel_DrawTex8b(gfx, Panel_TargetLabel(t->target), G_IM_FMT_IA, 32, 32, t->x - t->size / 2,
                                  t->y - t->size / 2, t->size, t->size);
        }
        // Small letter badge so a filled button still says which one it is
        if ((item < ITEM_FD) && (Panel_TargetLabel(t->target) != NULL)) {
            gfx = Panel_SetTexturedMode(gfx, 255, 240, 0, 255);
            gfx = Panel_DrawTex8b(gfx, Panel_TargetLabel(t->target), G_IM_FMT_IA, 32, 32, t->x + t->size / 2 - 16,
                                  t->y - t->size / 2 - 6, 22, 22);
        }
    }
    return gfx;
}

static Gfx* Panel_DrawEquipPage(Gfx* gfx, PlayState* play, s32 firstSlot) {
    s32 i;
    s32 pulse = 160 + (s32)(Math_SinS(play->state.frames * 0x800) * 80.0f);

    for (i = 0; i < GRID_COLS * GRID_ROWS; i++) {
        s32 slot = firstSlot + i;
        u8 item = gSaveContext.save.saveInfo.inventory.items[slot];
        s32 cx = GRID_X + (i % GRID_COLS) * GRID_SLOT + GRID_SLOT / 2;
        s32 cy = GRID_Y + (i / GRID_COLS) * GRID_SLOT + GRID_SLOT / 2;
        s32 shown = (item != ITEM_NONE) && !((slot >= ITEM_NUM_SLOTS) && CHECK_GIVEN_MASK_ON_MOON(i));

        if (slot == sSelectedSlot) {
            gfx = Panel_SetTexturedMode(gfx, 255, 200, 60, pulse);
            gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, cx - 35, cy - 35, 70, 70);
        } else {
            gfx = Panel_SetTexturedMode(gfx, 90, 60, 30, shown ? 70 : 35);
            gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, cx - 32, cy - 32, 64, 64);
        }
        if (!shown || ((slot == sDragSlot) && sTouch.moved)) {
            continue;
        }
        // Equipped items sit in the pause menu's frame, drawn behind the icon
        if (Panel_SlotEquipped(slot)) {
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            gfx = Panel_DrawTex8b(gfx, gEquippedItemOutlineTex, G_IM_FMT_IA, 32, 32, cx - 34, cy - 34, 68, 68);
        }
        // Items this form can't use are greyed out, as in the pause menu
        if (Panel_SlotEquippable(slot)) {
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
        } else {
            gfx = Panel_SetTexturedMode(gfx, 120, 120, 120, 170);
        }
        gfx = Panel_DrawItemIcon(gfx, item, cx, cy, GRID_ICON);
        gfx = Panel_DrawAmmo(gfx, slot, cx + 33, cy + 8);
    }

    gfx = Panel_DrawEquipTargets(gfx);

    // The dragged item follows the finger, drawn last so it sits on top
    if ((sDragSlot >= 0) && sTouch.down && sTouch.moved) {
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 230);
        gfx = Panel_DrawItemIcon(gfx, gSaveContext.save.saveInfo.inventory.items[sDragSlot], (s32)sTouch.x,
                                 (s32)sTouch.y - 20, 64);
    }
    return gfx;
}

// #endregion

void DualScreen_Update(PlayState* play) {
    s32 canvasHeight = DualScreen_GetCanvasHeight();

    if (!DualScreen_IsActive()) {
        return;
    }
    Panel_PollTouch();
    Panel_UpdateTabs(canvasHeight);
    if (sPage == DUAL_SCREEN_PAGE_ITEMS) {
        Panel_UpdateEquipPage(play, 0);
    } else if (sPage == DUAL_SCREEN_PAGE_MASKS) {
        Panel_UpdateEquipPage(play, ITEM_NUM_SLOTS);
    } else {
        sSelectedSlot = sDragSlot = sUseTarget = -1;
    }
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
    if (sPage == DUAL_SCREEN_PAGE_ITEMS) {
        gfx = Panel_DrawEquipPage(gfx, play, 0);
    } else if (sPage == DUAL_SCREEN_PAGE_MASKS) {
        gfx = Panel_DrawEquipPage(gfx, play, ITEM_NUM_SLOTS);
    } else if (sPage == DUAL_SCREEN_PAGE_NOTEBOOK) {
        gfx = Panel_DrawNotebook(gfx, canvasHeight);
    } else {
        gfx = Panel_DrawStatus(gfx);
    }
    gfx = Panel_DrawStrip(gfx, canvasHeight);
    Panel_End(start, gfx, play->state.gfxCtx);
}

// Big RGBA32 images (the title logo) don't fit a normal texture load; load the whole image as one clamped tile,
// as 2S2H's own title screen does for HD textures
static Gfx* Panel_DrawLargeRGBA32(Gfx* gfx, TexturePtr tex, s32 texW, s32 texH, s32 x, s32 y, s32 w, s32 h) {
    gDPSetTileCustom(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                     G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTextureImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, tex);
    gDPLoadSync(gfx++);
    gDPLoadTile(gfx++, G_TX_LOADTILE, 0, 0, (texW - 1) << 2, (texH - 1) << 2);
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0, TEX_SCALE(texW, w),
                        TEX_SCALE(texH, h));
    return gfx;
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
    gfx = Panel_DrawLargeRGBA32(gfx, gTitleScreenZeldaLogoTex, 144, 64, 320 - 216, canvasHeight / 2 - 120, 432, 192);
    gfx = Panel_SetTexturedMode(gfx, 120, 60, 20, 255);
    gfx = Panel_DrawTex8b(gfx, gTitleScreenMajorasMaskSubtitleTex, G_IM_FMT_I, 104, 16, 320 - 156,
                          canvasHeight / 2 + 84, 312, 48);
    Panel_End(start, gfx, gfxCtx);
}
