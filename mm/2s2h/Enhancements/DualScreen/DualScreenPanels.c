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
#include "interface/icon_item_field_static/icon_item_field_static.h"
#include "archives/icon_item_24_static/icon_item_24_static_yar.h"
#include "z64horse.h"
#include "BenPort.h"
#include "2s2h/BenGui/HudEditor.h"
#include "DualScreen.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <stdlib.h>
#include <string.h>
#include "message_data_static.h"
#include <math.h>

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
// The message box font (z_kanfont.c, z_message_nes.c)
extern const char* fontTbl[];
extern f32 sNESFontWidths[];
#define CHECK_GIVEN_MASK_ON_MOON(maskIndex) \
    (gSaveContext.masksGivenOnMoon[sMasksGivenOnMoonBits[maskIndex] >> 8] & (u8)sMasksGivenOnMoonBits[maskIndex])

#define PANEL_GFX_SIZE 0x8000
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

// Solid colour shaped by the texture's alpha: for glyphs whose HD replacement is dark (the song note)
static Gfx* Panel_SetSilhouetteMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    return gfx;
}

// #region Text in the message-box font

// Line height for a text scale, in canvas units (glyphs are 16x16)
#define TEXT_LINE_H(scale) ((s32)(16.0f * (scale) + 3.0f))

static f32 Panel_GlyphAdvance(char c, f32 scale) {
    u8 index = (u8)c - ' ';
    return ((u8)c < ' ' || index >= 160) ? 0.0f : sNESFontWidths[index] * scale;
}

static s32 Panel_TextWidth(const char* str, s32 len, f32 scale) {
    f32 width = 0.0f;
    s32 i;

    for (i = 0; (i < len) && (str[i] != '\0'); i++) {
        width += Panel_GlyphAdvance(str[i], scale);
    }
    return (s32)width;
}

// Draws up to len characters on one line; the colour comes from the current prim (silhouette mode)
static Gfx* Panel_DrawTextN(Gfx* gfx, const char* str, s32 len, s32 x, s32 y, f32 scale) {
    f32 penX = x;
    s32 size = (s32)(16.0f * scale + 0.5f);
    s32 i;

    for (i = 0; (i < len) && (str[i] != '\0'); i++) {
        u8 c = (u8)str[i];

        if ((c > ' ') && (c - ' ' < 156)) {
            gDPLoadTextureBlock_4b(gfx++, fontTbl[c - ' '], G_IM_FMT_I, 16, 16, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPTextureRectangle(gfx++, (s32)penX << 2, y << 2, ((s32)penX + size) << 2, (y + size) << 2,
                                G_TX_RENDERTILE, 0, 0, (16 << 10) / size, (16 << 10) / size);
        }
        penX += Panel_GlyphAdvance(c, scale);
    }
    return gfx;
}

static Gfx* Panel_DrawText(Gfx* gfx, const char* str, s32 x, s32 y, f32 scale) {
    return Panel_DrawTextN(gfx, str, 0x7FFF, x, y, scale);
}

// Word-wrapped text; returns the number of lines drawn (at most maxLines)
static s32 Panel_DrawTextWrapped(Gfx** gfxP, const char* str, s32 x, s32 y, s32 maxW, f32 scale, s32 maxLines) {
    s32 lines = 0;

    while ((*str != '\0') && (lines < maxLines)) {
        s32 len = 0;
        s32 fit = 0;

        // Take whole words while they fit
        while (str[len] != '\0') {
            s32 end = len;

            while ((str[end] != ' ') && (str[end] != '\0')) {
                end++;
            }
            if ((fit > 0) && (Panel_TextWidth(str, end, scale) > maxW)) {
                break;
            }
            fit = end;
            len = (str[end] == ' ') ? end + 1 : end;
        }
        if (fit == 0) {
            fit = len;
        }
        *gfxP = Panel_DrawTextN(*gfxP, str, fit, x, y + lines * TEXT_LINE_H(scale), scale);
        lines++;
        str += fit;
        while (*str == ' ') {
            str++;
        }
    }
    return lines;
}

// #endregion

// libultraship flips scissor rectangles vertically inside off-screen framebuffers (asking for rows 12-368 clips to
// 189-545 on a 557-tall canvas), so ask for the mirrored rows to get the ones we mean
static Gfx* Panel_SetScissor(Gfx* gfx, s32 x0, s32 y0, s32 x1, s32 y1) {
    s32 canvasHeight = DualScreen_GetCanvasHeight();

    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, x0, canvasHeight - y1, x1, canvasHeight - y0);
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

    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    BombersNotebook_DrawHeaders(&gfx);
    BombersNotebook_DrawColumns(&gfx);

    // The panel is taller than the notebook's 480 rows, so let the last visible row show in full
    gfx = Panel_SetScissor(gfx, 0, 86, 600, 468);
    BombersNotebook_DrawRows(&sPanelNotebook, &gfx);
    gDPPipeSync(gfx++);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, DUAL_SCREEN_CANVAS_WIDTH, canvasHeight);

    BombersNotebook_DrawTimeOfDay(&gfx);
    BombersNotebook_DrawArrows(&sPanelNotebook, &gfx);
    return gfx;
}

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
    s32 pointers; // fingers down; the second one is (x2, y2)
    f32 x2;
    f32 y2;
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
        sTouch.x2 = event.x2;
        sTouch.y2 = event.y2;
        sTouch.pointers = event.pointers;
        if (event.pointers >= 2) {
            // A second finger makes this a gesture, never a tap
            sTouch.moved = true;
        }
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
#define STRIP_TAB_SIZE 56
#define STRIP_TAB_ICON 44

typedef struct PanelTab {
    s32 page;
    u8 iconItem;
} PanelTab;

static PanelTab sTabs[] = {
    { DUAL_SCREEN_PAGE_ITEMS, ITEM_OCARINA_OF_TIME },
    { DUAL_SCREEN_PAGE_MASKS, ITEM_MASK_DEKU },
    { DUAL_SCREEN_PAGE_QUEST, ITEM_SONG_TIME },
    { DUAL_SCREEN_PAGE_MAP, ITEM_COMPASS }, // ITEM_COMPASS's icon is the dungeon map
    { DUAL_SCREEN_PAGE_STATUS, ITEM_REMAINS_ODOLWA },
    { DUAL_SCREEN_PAGE_NOTEBOOK, ITEM_BOMBERS_NOTEBOOK },
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
        if (sTabs[i].iconItem == ITEM_COMPASS) {
            // A 24x24 quest icon rather than a 32x32 item icon
            gfx = Panel_DrawTexRGBA32(gfx, gItemIcons[ITEM_COMPASS], 24, 24,
                                      x + (STRIP_TAB_SIZE - STRIP_TAB_ICON) / 2 - grow,
                                      tabY + (STRIP_TAB_SIZE - STRIP_TAB_ICON) / 2 - grow, STRIP_TAB_ICON + grow * 2,
                                      STRIP_TAB_ICON + grow * 2);
            continue;
        }
        if (sTabs[i].iconItem == ITEM_SONG_TIME) {
            // The song note is a small IA glyph; draw it as a brown note instead of a full-colour icon
            gfx = Panel_SetSilhouetteMode(gfx, 110, 70, 35, selected ? 255 : 170);
            gfx = Panel_DrawTex8b(gfx, gItemIconSongNoteTex, G_IM_FMT_IA, 16, 24, x + STRIP_TAB_SIZE / 2 - 13 - grow,
                                  tabY + STRIP_TAB_SIZE / 2 - 20 - grow, 26 + grow * 2, 39 + grow * 2);
            continue;
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

    // While the Inverted Song of Time slows time, the timeline and countdown turn a cool blue
    s32 slowed = (gSaveContext.save.timeSpeedOffset == -2);

    // The three days as a timeline across the top of the strip, filled up to now
    for (i = 0; i < 3; i++) {
        s32 segX = 20 + i * (segW + 4);
        s32 segFill = CLAMP(elapsed - i * 24 * 60, 0, 24 * 60) * segW / (24 * 60);

        gfx = Panel_FillRect(gfx, segX, STRIP_Y, segW, 4, 90, 50, 20, 60);
        if (segFill > 0) {
            if (slowed) {
                gfx = Panel_FillRect(gfx, segX, STRIP_Y, segFill, 4, 60, 120, 220, 230);
            } else {
                gfx = Panel_FillRect(gfx, segX, STRIP_Y, segFill, 4, (i == 2) ? 190 : 120, (i == 2) ? 40 : 70, 30,
                                     220);
            }
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
    } else if (slowed) {
        gfx = Panel_SetTexturedMode(gfx, 40, 90, 190, 255);
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

static void Panel_UpdateMapPage(PlayState* play);
static void Panel_UpdateNotebookDetails(PlayState* play);
static Gfx* Panel_DrawNotebookDetails(Gfx* gfx, PlayState* play);
static void Panel_SetNotebookHighlight(void);
static Gfx* Panel_DrawMapPage(Gfx* gfx, PlayState* play);

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

// #region Quest: remains, gear and the music guide

#define QUEST_ICON 56
#define QUEST_ROW_Y 14
#define SONG_TOP 100
#define SONG_ROW_H 62
#define SONG_COL_W 304
#define SONG_NOTE 33
#define SONG_NOTE_ADVANCE 37

typedef struct PanelSong {
    u8 song; // OCARINA_SONG_*
    const char* name;
    u8 r;
    u8 g;
    u8 b;
} PanelSong;

// Two columns: the songs of time and travel, then the songs that wake each region
static PanelSong sPanelSongs[] = {
    { OCARINA_SONG_TIME, "Song of Time", 110, 70, 35 },          { OCARINA_SONG_STORMS, "Song of Storms", 110, 70, 35 },
    { OCARINA_SONG_INVERTED_TIME, "Inverted Song of Time", 110, 70, 35 }, { OCARINA_SONG_SONATA, "Sonata of Awakening", 150, 255, 100 },
    { OCARINA_SONG_DOUBLE_TIME, "Song of Double Time", 110, 70, 35 },   { OCARINA_SONG_GORON_LULLABY, "Goron Lullaby", 255, 80, 40 },
    { OCARINA_SONG_HEALING, "Song of Healing", 110, 70, 35 },       { OCARINA_SONG_NEW_WAVE, "New Wave Bossa Nova", 100, 150, 255 },
    { OCARINA_SONG_SOARING, "Song of Soaring", 110, 70, 35 },       { OCARINA_SONG_ELEGY, "Elegy of Emptiness", 255, 160, 0 },
    { OCARINA_SONG_EPONAS, "Epona's Song", 110, 70, 35 },        { OCARINA_SONG_OATH, "Oath to Order", 255, 100, 255 },
};

// Which song (if any) can be played for this row: Inverted and Double Time come with the Song of Time, and
// the Goron Lullaby can be its intro until the whole song is learned
static s32 Panel_LearnedSong(u8 song) {
    if ((song == OCARINA_SONG_INVERTED_TIME) || (song == OCARINA_SONG_DOUBLE_TIME)) {
        return CHECK_QUEST_ITEM(QUEST_SONG_TIME) ? song : -1;
    }
    if (CHECK_QUEST_ITEM(QUEST_SONG_SONATA + song)) {
        return song;
    }
    if ((song == OCARINA_SONG_GORON_LULLABY) && CHECK_QUEST_ITEM(QUEST_SONG_LULLABY_INTRO)) {
        return OCARINA_SONG_GORON_LULLABY_INTRO;
    }
    return -1;
}

static void Panel_SongRowRect(s32 index, s32* x, s32* y) {
    *x = 16 + (index % 2) * SONG_COL_W;
    *y = SONG_TOP + (index / 2) * SONG_ROW_H;
}

static void Panel_UpdateQuestPage(PlayState* play) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sPanelSongs); i++) {
        s32 x;
        s32 y;
        s32 song;

        Panel_SongRowRect(i, &x, &y);
        if (!Panel_Tapped(x, y, SONG_COL_W - 8, SONG_ROW_H)) {
            continue;
        }
        song = Panel_LearnedSong(sPanelSongs[i].song);
        // Same moments the pause menu would let you take out the ocarina
        if ((song < 0) || (INV_CONTENT(ITEM_OCARINA_OF_TIME) != ITEM_OCARINA_OF_TIME) ||
            (play->pauseCtx.state != PAUSE_STATE_OFF) || (play->msgCtx.msgMode != MSGMODE_NONE) ||
            Player_InCsMode(play) || (play->transitionTrigger != TRANS_TRIGGER_OFF)) {
            Audio_PlaySfx(NA_SE_SY_ERROR);
            return;
        }
        DualScreen_RequestSong(song);
        play->state.input[0].press.button |= BTN_CLEFT;
        play->state.input[0].cur.button |= BTN_CLEFT;
        return;
    }
}

// An ocarina note as the button you press for it: X / Y / RB letters for the C notes (from the button-letter
// pack when it's installed), the A and C-Up glyphs otherwise
static Gfx* Panel_DrawSongNote(Gfx* gfx, u8 button, s32 x, s32 y, u8 alpha) {
    TexturePtr glyph;
    s32 glyphSize = 32;

    if (button == OCARINA_BTN_A) {
        gfx = Panel_SetTexturedMode(gfx, 100, 200, 255, alpha);
    } else {
        gfx = Panel_SetTexturedMode(gfx, 255, 240, 0, alpha);
    }
    gfx = Panel_DrawTex8b(gfx, gButtonBackgroundTex, G_IM_FMT_IA, 32, 32, x, y, SONG_NOTE, SONG_NOTE);

    switch (button) {
        case OCARINA_BTN_C_DOWN:
            glyph = gEmptyCDownArrowTex;
            break;
        case OCARINA_BTN_C_RIGHT:
            glyph = gEmptyCRightArrowTex;
            break;
        case OCARINA_BTN_C_LEFT:
            glyph = gEmptyCLeftArrowTex;
            break;
        case OCARINA_BTN_C_UP:
            glyph = gOcarinaCUpTex;
            glyphSize = 16;
            break;
        default:
            glyph = gOcarinaATex;
            glyphSize = 16;
            break;
    }
    if (glyphSize == 16) {
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, alpha);
        return Panel_DrawTex8b(gfx, glyph, G_IM_FMT_IA, 16, 16, x + 5, y + 5, SONG_NOTE - 10, SONG_NOTE - 10);
    }
    return Panel_DrawTex8b(gfx, glyph, G_IM_FMT_IA, 32, 32, x, y, SONG_NOTE, SONG_NOTE);
}

static Gfx* Panel_DrawQuestIcon(Gfx* gfx, s32 item, s32 x, s32 y) {
    gfx = Panel_SetTexturedMode(gfx, 90, 60, 30, (item < 0) ? 35 : 70);
    gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x - 2, y - 2, QUEST_ICON + 4,
                          QUEST_ICON + 4);
    if (item >= 0) {
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
        gfx = Panel_DrawItemIcon(gfx, item, x + QUEST_ICON / 2, y + QUEST_ICON / 2, QUEST_ICON);
    }
    return gfx;
}

static Gfx* Panel_DrawQuestPage(Gfx* gfx) {
    s32 i;
    s32 x = 16;
    s32 value;

    // Boss remains, then the gear the pause menu's quest page shows
    for (i = 0; i < 4; i++, x += QUEST_ICON + 6) {
        gfx = Panel_DrawQuestIcon(gfx, CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA + i) ? ITEM_REMAINS_ODOLWA + i : -1, x,
                                  QUEST_ROW_Y);
    }
    x += 18;
    value = GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD);
    gfx = Panel_DrawQuestIcon(gfx, (value != EQUIP_VALUE_SWORD_NONE) ? ITEM_SWORD_KOKIRI + value - 1 : -1, x,
                              QUEST_ROW_Y);
    x += QUEST_ICON + 6;
    value = GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD);
    gfx = Panel_DrawQuestIcon(gfx, (value != EQUIP_VALUE_SHIELD_NONE) ? ITEM_SHIELD_HERO + value - 1 : -1, x,
                              QUEST_ROW_Y);
    x += QUEST_ICON + 6;
    value = CUR_UPG_VALUE(UPG_QUIVER);
    gfx = Panel_DrawQuestIcon(gfx, (value != 0) ? ITEM_QUIVER_30 + value - 1 : -1, x, QUEST_ROW_Y);
    x += QUEST_ICON + 6;
    value = CUR_UPG_VALUE(UPG_BOMB_BAG);
    gfx = Panel_DrawQuestIcon(gfx, (value != 0) ? ITEM_BOMB_BAG_20 + value - 1 : -1, x, QUEST_ROW_Y);
    x += QUEST_ICON + 6;
    gfx = Panel_DrawQuestIcon(gfx, ITEM_WALLET_DEFAULT + CUR_UPG_VALUE(UPG_WALLET), x, QUEST_ROW_Y);

    gfx = Panel_FillRect(gfx, 20, SONG_TOP - 14, DUAL_SCREEN_CANVAS_WIDTH - 40, 2, 90, 50, 20, 120);

    // The music guide: each learned song written out in the buttons that play it; tap one to play it
    for (i = 0; i < ARRAY_COUNT(sPanelSongs); i++) {
        PanelSong* entry = &sPanelSongs[i];
        s32 song = Panel_LearnedSong(entry->song);
        s32 rowX;
        s32 rowY;
        s32 n;

        Panel_SongRowRect(i, &rowX, &rowY);
        if (Panel_Pressing(rowX, rowY, SONG_COL_W - 8, SONG_ROW_H) && (song >= 0)) {
            gfx = Panel_FillRect(gfx, rowX, rowY + 4, SONG_COL_W - 8, SONG_ROW_H - 8, 255, 200, 60, 90);
        }
        gfx = Panel_SetSilhouetteMode(gfx, entry->r, entry->g, entry->b, (song >= 0) ? 255 : 50);
        gfx = Panel_DrawTex8b(gfx, gItemIconSongNoteTex, G_IM_FMT_IA, 16, 24, rowX + 4, rowY + 12, 26, 39);
        // The song's name, in the notebook's ink (faint until it's learned)
        gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, (song >= 0) ? 255 : 90);
        gfx = Panel_DrawText(gfx, entry->name, rowX + 38, rowY + 1, 1.0f);
        if (song < 0) {
            continue;
        }
        for (n = 0; n < gOcarinaSongButtons[song].numButtons; n++) {
            gfx = Panel_DrawSongNote(gfx, gOcarinaSongButtons[song].buttonIndex[n],
                                     rowX + 36 + n * SONG_NOTE_ADVANCE, rowY + SONG_ROW_H - SONG_NOTE - 2, 255);
        }
    }
    return gfx;
}

// #endregion

// #region Quest log: main quest progress, masks and collectibles, with hints behind the Lens of Truth

#define CVAR_HINTS "gEnhancements.DualScreen.QuestHints"
#define LOG_TOP 52
#define LOG_HINT_Y 418
#define LOG_TEXT 1.0f
#define LOG_SMALL 0.9f

typedef enum QuestLogView {
    QUEST_LOG_MAIN,
    QUEST_LOG_MASKS,
    QUEST_LOG_COLLECT,
    QUEST_LOG_VIEW_MAX
} QuestLogView;

typedef enum QuestStep {
    STEP_OCARINA,
    STEP_HEALING,
    STEP_MAGIC,
    STEP_NOTEBOOK,
    STEP_SONATA,
    STEP_ODOLWA,
    STEP_GORON_MASK,
    STEP_LULLABY,
    STEP_GOHT,
    STEP_ZORA_MASK,
    STEP_BOSSA_NOVA,
    STEP_GYORG,
    STEP_GARO_MASK,
    STEP_ELEGY,
    STEP_LIGHT_ARROWS,
    STEP_TWINMOLD,
    STEP_OATH,
    STEP_ALL_GIANTS
} QuestStep;

typedef struct QuestStepInfo {
    const char* label;
    const char* hint;
} QuestStepInfo;

static QuestStepInfo sQuestSteps[] = {
    { "Recover the Ocarina of Time",
      "Climb the Clock Tower when it opens at midnight on the Final Night and knock the Skull Kid's ocarina loose." },
    { "Learn the Song of Healing",
      "Once you have the ocarina back, return to the Mask Salesman inside the Clock Tower." },
    { "Receive the Great Fairy's magic",
      "Catch Clock Town's lost stray fairy (try the Laundry Pool) and bring it to the fountain in North Clock Town." },
    { "Get the Bombers' Notebook",
      "As Deku Link, find and catch all five Bombers kids hiding around Clock Town." },
    { "Learn the Sonata of Awakening",
      "Sneak through the Deku Palace gardens as Deku Link to reach the captured monkey. He teaches you the song." },
    { "Free the first Giant (Odolwa)",
      "Play the Sonata on the Deku platform in Woodfall to raise the temple. The Hero's Bow is inside." },
    { "Get the Goron Mask",
      "Follow the ghost of the Goron hero in the snowy Mountain Village and heal his soul with the Song of Healing." },
    { "Learn the Goron Lullaby",
      "The Goron Elder teaches the opening bars. Play them for his crying son in the Goron Shrine to learn the rest." },
    { "Free the second Giant (Goht)",
      "Lull the giant Goron on the path to Snowhead to sleep. The Fire Arrows are inside the temple." },
    { "Get the Zora Mask",
      "Push the wounded Zora floating off the Great Bay Coast to shore and play the Song of Healing." },
    { "Learn the New Wave Bossa Nova",
      "Return all seven Zora eggs to the Marine Research Lab's tank, then play your guitar beside it." },
    { "Free the third Giant (Gyorg)",
      "Play the Bossa Nova at the edge of Zora Cape to wake the turtle that carries you to the temple." },
    { "Get the Garo's Mask",
      "Beat the Gorman brothers in their horse race at Gorman Track. You'll need Epona." },
    { "Learn the Elegy of Emptiness",
      "Defeat the king of Ikana Castle. He teaches the Elegy after the battle." },
    { "Get the Light Arrows",
      "Climb Stone Tower using the Elegy's shells. The Light Arrows are inside its temple." },
    { "Free the fourth Giant (Twinmold)",
      "Shoot a Light Arrow at the red emblem at Stone Tower to flip the temple upside down." },
    { "Learn the Oath to Order",
      "The Giants teach it after you free the first of them." },
    { "Free all four Giants",
      "Clear all four temples, then play the Oath to Order atop the Clock Tower on the Final Night." },
};

typedef struct QuestRegion {
    const char* name;
    u8 icon;
    u8 firstStep;
    u8 numSteps;
} QuestRegion;

static QuestRegion sQuestRegions[] = {
    { "Clock Town", ITEM_OCARINA_OF_TIME, STEP_OCARINA, 4 },     { "Woodfall", ITEM_REMAINS_ODOLWA, STEP_SONATA, 2 },
    { "Snowhead", ITEM_REMAINS_GOHT, STEP_GORON_MASK, 3 },       { "Great Bay", ITEM_REMAINS_GYORG, STEP_ZORA_MASK, 3 },
    { "Ikana", ITEM_REMAINS_TWINMOLD, STEP_GARO_MASK, 4 },       { "The Moon", ITEM_MOONS_TEAR, STEP_OATH, 2 },
};

typedef struct QuestMaskInfo {
    const char* name;
    const char* hint;
} QuestMaskInfo;

// Mask items in inventory slot order (SLOT_MASK_POSTMAN onwards); item IDs run in a different order
static u8 sQuestMaskItems[] = {
    ITEM_MASK_POSTMAN, ITEM_MASK_ALL_NIGHT, ITEM_MASK_BLAST,    ITEM_MASK_STONE,         ITEM_MASK_GREAT_FAIRY,
    ITEM_MASK_DEKU,    ITEM_MASK_KEATON,    ITEM_MASK_BREMEN,   ITEM_MASK_BUNNY,         ITEM_MASK_DON_GERO,
    ITEM_MASK_SCENTS,  ITEM_MASK_GORON,     ITEM_MASK_ROMANI,   ITEM_MASK_CIRCUS_LEADER, ITEM_MASK_KAFEIS_MASK,
    ITEM_MASK_COUPLE,  ITEM_MASK_TRUTH,     ITEM_MASK_ZORA,     ITEM_MASK_KAMARO,        ITEM_MASK_GIBDO,
    ITEM_MASK_GARO,    ITEM_MASK_CAPTAIN,   ITEM_MASK_GIANT,    ITEM_MASK_FIERCE_DEITY,
};

// In inventory order (SLOT_MASK_POSTMAN onwards)
static QuestMaskInfo sQuestMasks[] = {
    { "Postman's Hat", "Hand the Postman the Express Mail to Mama on the Final Day. He flees town and leaves his hat." },
    { "All-Night Mask", "Sold at the Curiosity Shop on the Final Night, once you've saved the old lady from Sakon." },
    { "Blast Mask", "Stop the thief Sakon from robbing the old lady in North Clock Town at midnight on the first night." },
    { "Stone Mask", "Find the invisible soldier on the road to Ikana with the Lens of Truth and give him a red potion." },
    { "Great Fairy's Mask", "Return all 15 stray fairies of Woodfall Temple to the Great Fairy near Woodfall." },
    { "Deku Mask", "The Mask Salesman restores your form after you recover the ocarina." },
    { "Keaton Mask", "Meet Kafei in the Laundry Pool's back room on Day 2. The shop owner hands you this mask." },
    { "Bremen Mask", "Listen to the musician playing in the Laundry Pool at night." },
    { "Bunny Hood", "Wear the Bremen Mask and march the Cucco chicks at Romani Ranch until they all grow up." },
    { "Don Gero's Mask", "Bring a Rock Sirloin to the hungry Goron frozen near the Mountain Village." },
    { "Mask of Scents", "After clearing Woodfall Temple, beat the Deku Butler in his race near the palace." },
    { "Goron Mask", "Heal the ghost of the Goron hero in the Mountain Village with the Song of Healing." },
    { "Romani's Mask", "Help Cremia get her milk delivery to town safely on the second night." },
    { "Circus Leader's Mask", "In the Milk Bar, perform the Ballad of the Wind Fish on stage with all four forms." },
    { "Kafei's Mask", "Talk to Madame Aroma in the Mayor's Residence about her missing son." },
    { "Couple's Mask", "Reunite Anju and Kafei before dawn on the Final Day." },
    { "Mask of Truth", "Lift the curse on the Swamp Spider House by collecting all 30 Gold Skulltula tokens." },
    { "Zora Mask", "Heal the wounded Zora guitarist on the Great Bay Coast with the Song of Healing." },
    { "Kamaro's Mask", "At night, find the ghost dancing on a rock in Termina Field and play the Song of Healing." },
    { "Gibdo Mask", "Heal the father hiding in the music box house in Ikana Canyon with the Song of Healing." },
    { "Garo's Mask", "Win the Gorman brothers' horse race at Gorman Track." },
    { "Captain's Hat", "Wake the skeleton captain in the Ikana Graveyard with the Sonata and catch up with him." },
    { "Giant's Mask", "Hidden in Stone Tower Temple." },
    { "Fierce Deity's Mask", "Give every other mask to the children on the moon." },
};

typedef enum QuestCollect {
    COLLECT_HEART_PIECES,
    COLLECT_FAIRIES,
    COLLECT_SWAMP_SKULLS,
    COLLECT_OCEAN_SKULLS,
    COLLECT_NOTEBOOK,
    COLLECT_MASKS,
    COLLECT_BANK,
    COLLECT_MAX
} QuestCollect;

static const char* sCollectLabels[] = {
    "Pieces of Heart", "Stray fairies", "Swamp Spider House", "Ocean Spider House", "Bombers' Notebook",
    "Masks",           "Bank",
};

static const char* sCollectHints[] = {
    "52 in all. They come from minigames, chests, side quests and odd corners of every region.",
    "Each temple hides 15, and Clock Town has one more. Return them to that region's Great Fairy.",
    "In the Southern Swamp. Clearing it lifts a family's curse and earns the Mask of Truth.",
    "On the Great Bay Coast. Finish it on the first day for the best reward.",
    "People you've met who can be helped. Tap the Notebook tab to see their schedules.",
    "24 in all. The Masks view of this log lists where each one comes from.",
    "West Clock Town's bank rewards you at 200, 1000 and 5000 Rupees. Deposits survive time travel.",
};

static s32 sQuestLogView = QUEST_LOG_MAIN;
static s32 sQuestLogSelected[QUEST_LOG_VIEW_MAX] = { -1, -1, -1 };

static s32 Panel_HintsOn(void) {
    return CVarGetInteger(CVAR_HINTS, 0);
}

static s32 Panel_HasItem(u8 item) {
    return INV_CONTENT(item) == item;
}

static s32 Panel_CountRemains(void) {
    s32 i;
    s32 count = 0;

    for (i = 0; i < 4; i++) {
        count += CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA + i) ? 1 : 0;
    }
    return count;
}

static s32 Panel_QuestStepDone(s32 step) {
    switch (step) {
        case STEP_OCARINA:
            return Panel_HasItem(ITEM_OCARINA_OF_TIME);
        case STEP_HEALING:
            return CHECK_QUEST_ITEM(QUEST_SONG_HEALING);
        case STEP_MAGIC:
            return gSaveContext.save.saveInfo.playerData.isMagicAcquired;
        case STEP_NOTEBOOK:
            return CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK);
        case STEP_SONATA:
            return CHECK_QUEST_ITEM(QUEST_SONG_SONATA);
        case STEP_ODOLWA:
            return CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA);
        case STEP_GORON_MASK:
            return Panel_HasItem(ITEM_MASK_GORON);
        case STEP_LULLABY:
            return CHECK_QUEST_ITEM(QUEST_SONG_LULLABY);
        case STEP_GOHT:
            return CHECK_QUEST_ITEM(QUEST_REMAINS_GOHT);
        case STEP_ZORA_MASK:
            return Panel_HasItem(ITEM_MASK_ZORA);
        case STEP_BOSSA_NOVA:
            return CHECK_QUEST_ITEM(QUEST_SONG_BOSSA_NOVA);
        case STEP_GYORG:
            return CHECK_QUEST_ITEM(QUEST_REMAINS_GYORG);
        case STEP_GARO_MASK:
            return Panel_HasItem(ITEM_MASK_GARO);
        case STEP_ELEGY:
            return CHECK_QUEST_ITEM(QUEST_SONG_ELEGY);
        case STEP_LIGHT_ARROWS:
            return Panel_HasItem(ITEM_ARROW_LIGHT);
        case STEP_TWINMOLD:
            return CHECK_QUEST_ITEM(QUEST_REMAINS_TWINMOLD);
        case STEP_OATH:
            return CHECK_QUEST_ITEM(QUEST_SONG_OATH);
        case STEP_ALL_GIANTS:
            return Panel_CountRemains() == 4;
        default:
            return false;
    }
}

// First unfinished step of a region, or -1 when it's complete
static s32 Panel_RegionNextStep(QuestRegion* region) {
    s32 i;

    for (i = 0; i < region->numSteps; i++) {
        if (!Panel_QuestStepDone(region->firstStep + i)) {
            return region->firstStep + i;
        }
    }
    return -1;
}

static s32 Panel_MaskOwned(s32 i) {
    return gSaveContext.save.saveInfo.inventory.items[SLOT_MASK_POSTMAN + i] != ITEM_NONE;
}

static void Panel_CollectCount(s32 line, s32* have, s32* total) {
    s32 i;

    *have = 0;
    switch (line) {
        case COLLECT_HEART_PIECES:
            // Each boss leaves a Heart Container; every other heart beyond the starting three came from pieces
            *have = CLAMP_MIN((gSaveContext.save.saveInfo.playerData.healthCapacity / 0x10) - 3 - Panel_CountRemains(),
                              0) * 4 +
                    GET_QUEST_HEART_PIECE_COUNT % 4;
            *total = 52;
            break;
        case COLLECT_FAIRIES:
            for (i = 0; i < 4; i++) {
                *have += CLAMP_MAX(gSaveContext.save.saveInfo.inventory.strayFairies[i], 15);
            }
            *have += gSaveContext.save.saveInfo.playerData.isMagicAcquired ? 1 : 0;
            *total = 61;
            break;
        case COLLECT_SWAMP_SKULLS:
            *have = Inventory_GetSkullTokenCount(SCENE_KINSTA1);
            *total = 30;
            break;
        case COLLECT_OCEAN_SKULLS:
            *have = Inventory_GetSkullTokenCount(SCENE_KINDAN2);
            *total = 30;
            break;
        case COLLECT_NOTEBOOK:
            for (i = 0; i < BOMBERS_NOTEBOOK_PERSON_MAX; i++) {
                *have += CHECK_WEEKEVENTREG(gBombersNotebookWeekEventFlags[i]) ? 1 : 0;
            }
            *total = BOMBERS_NOTEBOOK_PERSON_MAX;
            break;
        case COLLECT_MASKS:
            for (i = 0; i < 24; i++) {
                *have += Panel_MaskOwned(i);
            }
            *total = 24;
            break;
        default:
            *have = HS_GET_BANK_RUPEES();
            *total = 5000;
            break;
    }
}

static char* Panel_FormatNumber(char* out, s32 value) {
    char digits[12];
    s32 n = 0;

    if (value < 0) {
        value = 0;
    }
    do {
        digits[n++] = '0' + (value % 10);
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        *out++ = digits[--n];
    }
    *out = '\0';
    return out;
}

// "have / total"
static void Panel_FormatFraction(char* out, s32 have, s32 total) {
    out = Panel_FormatNumber(out, have);
    *out++ = ' ';
    *out++ = '/';
    *out++ = ' ';
    Panel_FormatNumber(out, total);
}

// #region Layout

#define LOG_HEADER_W 128
#define LOG_LENS_X 580
#define LOG_ROW_H 60
#define LOG_MASK_SLOT 56
#define LOG_COLLECT_ROW_H 52

static void Panel_UpdateQuestLog(PlayState* play) {
    s32 i;

    for (i = 0; i < QUEST_LOG_VIEW_MAX; i++) {
        if (Panel_Tapped(16 + i * (LOG_HEADER_W + 8), 6, LOG_HEADER_W, 38) && (sQuestLogView != i)) {
            sQuestLogView = i;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
            return;
        }
    }
    if (Panel_Tapped(LOG_LENS_X - 6, 0, 56, 50)) {
        CVarSetInteger(CVAR_HINTS, !Panel_HintsOn());
        Audio_PlaySfx(Panel_HintsOn() ? NA_SE_SY_GLASSMODE_ON : NA_SE_SY_GLASSMODE_OFF);
        return;
    }

    if (sQuestLogView == QUEST_LOG_MAIN) {
        for (i = 0; i < ARRAY_COUNT(sQuestRegions); i++) {
            if (Panel_Tapped(0, LOG_TOP + i * LOG_ROW_H, DUAL_SCREEN_CANVAS_WIDTH, LOG_ROW_H)) {
                sQuestLogSelected[QUEST_LOG_MAIN] = i;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            }
        }
    } else if (sQuestLogView == QUEST_LOG_MASKS) {
        for (i = 0; i < 24; i++) {
            if (Panel_Tapped(16 + (i % 6) * LOG_MASK_SLOT, LOG_TOP + (i / 6) * LOG_MASK_SLOT, LOG_MASK_SLOT,
                             LOG_MASK_SLOT)) {
                sQuestLogSelected[QUEST_LOG_MASKS] = i;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            }
        }
    } else {
        for (i = 0; i < COLLECT_MAX; i++) {
            if (Panel_Tapped(0, LOG_TOP + i * LOG_COLLECT_ROW_H, DUAL_SCREEN_CANVAS_WIDTH, LOG_COLLECT_ROW_H)) {
                sQuestLogSelected[QUEST_LOG_COLLECT] = i;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            }
        }
    }
}

static Gfx* Panel_DrawLogHeader(Gfx* gfx) {
    static const char* sViewNames[] = { "Main Quest", "Masks", "Collection" };
    s32 i;

    for (i = 0; i < QUEST_LOG_VIEW_MAX; i++) {
        s32 x = 16 + i * (LOG_HEADER_W + 8);
        s32 selected = (sQuestLogView == i);

        gfx = Panel_FillRect(gfx, x, 8, LOG_HEADER_W, 34, 120, 80, 30, selected ? 150 : 45);
        gfx = Panel_SetSilhouetteMode(gfx, selected ? 255 : 70, selected ? 245 : 40, selected ? 210 : 15, 255);
        gfx = Panel_DrawText(gfx, sViewNames[i], x + (LOG_HEADER_W - Panel_TextWidth(sViewNames[i], 64, LOG_TEXT)) / 2,
                             16, LOG_TEXT);
    }

    // The Lens of Truth reveals hints
    if (Panel_HintsOn()) {
        gfx = Panel_SetTexturedMode(gfx, 255, 200, 60, 230);
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, LOG_LENS_X - 4, 0, 52, 52);
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
    } else {
        gfx = Panel_SetTexturedMode(gfx, 90, 60, 30, 50);
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, LOG_LENS_X - 4, 0, 52, 52);
        gfx = Panel_SetTexturedMode(gfx, 190, 180, 160, 210);
    }
    return Panel_DrawItemIcon(gfx, ITEM_LENS_OF_TRUTH, LOG_LENS_X + 22, 26, 42);
}

// The box along the bottom: the selected entry's hint while the Lens is on
static Gfx* Panel_DrawLogHint(Gfx* gfx, const char* hint) {
    gfx = Panel_FillRect(gfx, 12, LOG_HINT_Y, DUAL_SCREEN_CANVAS_WIDTH - 24, 58, 0, 0, 0, 150);
    gfx = Panel_SetSilhouetteMode(gfx, 255, 255, 255, 255);
    if (!Panel_HintsOn()) {
        Panel_DrawTextWrapped(&gfx, "Tap the Lens of Truth to reveal hints.", 24, LOG_HINT_Y + 18,
                              DUAL_SCREEN_CANVAS_WIDTH - 48, LOG_SMALL, 1);
    } else if (hint == NULL) {
        Panel_DrawTextWrapped(&gfx, "Tap an entry to see its hint.", 24, LOG_HINT_Y + 18, DUAL_SCREEN_CANVAS_WIDTH - 48,
                              LOG_SMALL, 1);
    } else {
        Panel_DrawTextWrapped(&gfx, hint, 24, LOG_HINT_Y + 6, DUAL_SCREEN_CANVAS_WIDTH - 48, LOG_SMALL, 2);
    }
    return gfx;
}

static Gfx* Panel_DrawLogMain(Gfx* gfx) {
    s32 i;
    s32 k;
    const char* hint = NULL;

    for (i = 0; i < ARRAY_COUNT(sQuestRegions); i++) {
        QuestRegion* region = &sQuestRegions[i];
        s32 y = LOG_TOP + i * LOG_ROW_H;
        s32 next = Panel_RegionNextStep(region);

        if (sQuestLogSelected[QUEST_LOG_MAIN] == i) {
            gfx = Panel_FillRect(gfx, 8, y + 2, DUAL_SCREEN_CANVAS_WIDTH - 16, LOG_ROW_H - 4, 255, 200, 60, 70);
            hint = (next >= 0) ? sQuestSteps[next].hint : "Done. Every step here is complete.";
        }
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, (next < 0) ? 255 : 200);
        gfx = Panel_DrawItemIcon(gfx, region->icon, 40, y + LOG_ROW_H / 2, 46);

        gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, 255);
        gfx = Panel_DrawText(gfx, region->name, 72, y + 6, LOG_TEXT);

        // One dot per step, filled gold when done
        for (k = 0; k < region->numSteps; k++) {
            s32 done = Panel_QuestStepDone(region->firstStep + k);

            gfx = Panel_SetTexturedMode(gfx, done ? 255 : 90, done ? 190 : 60, done ? 40 : 30, done ? 255 : 70);
            gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, 230 + k * 24, y + 8, 18, 18);
        }

        if (next < 0) {
            gfx = Panel_SetSilhouetteMode(gfx, 170, 120, 20, 255);
            gfx = Panel_DrawText(gfx, "Complete", 72, y + 30, LOG_SMALL);
        } else {
            gfx = Panel_SetSilhouetteMode(gfx, 110, 70, 35, 255);
            gfx = Panel_DrawText(gfx, sQuestSteps[next].label, 72, y + 30, LOG_SMALL);
        }
    }
    return Panel_DrawLogHint(gfx, hint);
}

static Gfx* Panel_DrawLogMasks(Gfx* gfx) {
    s32 i;
    s32 selected = sQuestLogSelected[QUEST_LOG_MASKS];

    for (i = 0; i < 24; i++) {
        s32 x = 16 + (i % 6) * LOG_MASK_SLOT;
        s32 y = LOG_TOP + (i / 6) * LOG_MASK_SLOT;
        s32 owned = Panel_MaskOwned(i);

        if (i == selected) {
            gfx = Panel_SetTexturedMode(gfx, 255, 200, 60, 230);
        } else {
            gfx = Panel_SetTexturedMode(gfx, 90, 60, 30, owned ? 70 : 35);
        }
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x + 2, y + 2, LOG_MASK_SLOT - 4,
                              LOG_MASK_SLOT - 4);
        // Missing masks show as dark outlines
        if (owned) {
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
        } else {
            gfx = Panel_SetSilhouetteMode(gfx, 60, 40, 20, 110);
        }
        gfx = Panel_DrawItemIcon(gfx, sQuestMaskItems[i], x + LOG_MASK_SLOT / 2, y + LOG_MASK_SLOT / 2, 48);
    }

    // Name and status of the picked mask beside the grid
    if (selected >= 0) {
        gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, 255);
        Panel_DrawTextWrapped(&gfx, sQuestMasks[selected].name, 368, LOG_TOP + 10, 256, 1.1f, 2);
        gfx = Panel_SetSilhouetteMode(gfx, Panel_MaskOwned(selected) ? 170 : 110, Panel_MaskOwned(selected) ? 120 : 70,
                                      Panel_MaskOwned(selected) ? 20 : 35, 255);
        gfx = Panel_DrawText(gfx, Panel_MaskOwned(selected) ? "Found" : "Not found yet", 368, LOG_TOP + 70, LOG_TEXT);
    } else {
        gfx = Panel_SetSilhouetteMode(gfx, 110, 70, 35, 255);
        Panel_DrawTextWrapped(&gfx, "Tap a mask to see what it is and where to find it.", 368, LOG_TOP + 10, 256,
                              LOG_SMALL, 4);
    }
    {
        char count[16];
        s32 have;
        s32 total;

        Panel_CollectCount(COLLECT_MASKS, &have, &total);
        Panel_FormatFraction(count, have, total);
        gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, 255);
        gfx = Panel_DrawText(gfx, count, 368, LOG_TOP + 4 * LOG_MASK_SLOT - 22, 1.2f);
    }
    return Panel_DrawLogHint(gfx, (selected >= 0) ? sQuestMasks[selected].hint : NULL);
}

static Gfx* Panel_DrawCollectIcon(Gfx* gfx, s32 line, s32 cx, s32 cy) {
    switch (line) {
        case COLLECT_HEART_PIECES:
            gfx = Panel_SetTexturedMode(gfx, 255, 70, 50, 255);
            return Panel_DrawTex8b(gfx, gHeartFullTex, G_IM_FMT_IA, 16, 16, cx - 18, cy - 18, 36, 36);
        case COLLECT_FAIRIES:
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            return Panel_DrawTexRGBA32(gfx, sStrayFairyTextures[0], 32, 24, cx - 24, cy - 18, 48, 36);
        case COLLECT_SWAMP_SKULLS:
        case COLLECT_OCEAN_SKULLS:
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            return Panel_DrawItemIcon(gfx, ITEM_SKULL_TOKEN, cx, cy, 40);
        case COLLECT_NOTEBOOK:
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            return Panel_DrawItemIcon(gfx, ITEM_BOMBERS_NOTEBOOK, cx, cy, 40);
        case COLLECT_MASKS:
            gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
            return Panel_DrawItemIcon(gfx, ITEM_MASK_KEATON, cx, cy, 40);
        default:
            gfx = Panel_SetTexturedMode(gfx, 255, 210, 80, 255);
            return Panel_DrawTex8b(gfx, gRupeeCounterIconTex, G_IM_FMT_IA, 16, 16, cx - 18, cy - 18, 36, 36);
    }
}

static Gfx* Panel_DrawLogCollect(Gfx* gfx) {
    s32 i;
    s32 selected = sQuestLogSelected[QUEST_LOG_COLLECT];

    for (i = 0; i < COLLECT_MAX; i++) {
        s32 y = LOG_TOP + i * LOG_COLLECT_ROW_H;
        char count[24];
        s32 have;
        s32 total;

        if (i == selected) {
            gfx = Panel_FillRect(gfx, 8, y + 2, DUAL_SCREEN_CANVAS_WIDTH - 16, LOG_COLLECT_ROW_H - 4, 255, 200, 60, 70);
        }
        gfx = Panel_DrawCollectIcon(gfx, i, 40, y + LOG_COLLECT_ROW_H / 2);
        gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, 255);
        gfx = Panel_DrawText(gfx, sCollectLabels[i], 72, y + 16, LOG_TEXT);

        Panel_CollectCount(i, &have, &total);
        Panel_FormatFraction(count, have, total);
        // Finished lines turn gold
        if (have >= total) {
            gfx = Panel_SetSilhouetteMode(gfx, 190, 130, 20, 255);
        }
        gfx = Panel_DrawText(gfx, count, DUAL_SCREEN_CANVAS_WIDTH - 24 - Panel_TextWidth(count, 24, LOG_TEXT), y + 16,
                             LOG_TEXT);
    }
    return Panel_DrawLogHint(gfx, (selected >= 0) ? sCollectHints[selected] : NULL);
}

static Gfx* Panel_DrawQuestLog(Gfx* gfx) {
    gfx = Panel_DrawLogHeader(gfx);
    if (sQuestLogView == QUEST_LOG_MAIN) {
        return Panel_DrawLogMain(gfx);
    }
    if (sQuestLogView == QUEST_LOG_MASKS) {
        return Panel_DrawLogMasks(gfx);
    }
    return Panel_DrawLogCollect(gfx);
}

// #endregion

// #endregion

void DualScreen_Update(PlayState* play) {
    s32 canvasHeight = DualScreen_GetCanvasHeight();

    if (!DualScreen_IsActive()) {
        return;
    }
    Panel_PollTouch();
    Panel_UpdateTabs(canvasHeight);
    // Touches on the strip belong to the tabs; pages only see touches that started above it
    if (sTouch.startY >= STRIP_Y) {
        sTouch.released = false;
        sTouch.began = false;
    }
    if (sPage == DUAL_SCREEN_PAGE_ITEMS) {
        Panel_UpdateEquipPage(play, 0);
    } else if (sPage == DUAL_SCREEN_PAGE_MASKS) {
        Panel_UpdateEquipPage(play, ITEM_NUM_SLOTS);
    } else if (sPage == DUAL_SCREEN_PAGE_QUEST) {
        sSelectedSlot = sDragSlot = sUseTarget = -1;
        Panel_UpdateQuestPage(play);
    } else if (sPage == DUAL_SCREEN_PAGE_STATUS) {
        sSelectedSlot = sDragSlot = sUseTarget = -1;
        Panel_UpdateQuestLog(play);
    } else if (sPage == DUAL_SCREEN_PAGE_MAP) {
        sSelectedSlot = sDragSlot = sUseTarget = -1;
        Panel_UpdateMapPage(play);
    } else {
        sSelectedSlot = sDragSlot = sUseTarget = -1;
    }
    // Swipes above the strip scroll the schedule
    Panel_UpdateNotebookScroll(((sPage == DUAL_SCREEN_PAGE_NOTEBOOK) && (sTouch.startY < STRIP_Y))
                                   ? Panel_VerticalSwipe()
                                   : 0);
    if (sPage == DUAL_SCREEN_PAGE_NOTEBOOK) {
        Panel_UpdateNotebookDetails(play);
    }
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

// This frame's panel display list, until its first replay has drawn it
static Gfx* sPendingPanelDL = NULL;

void DualScreen_SkipPanelReplays(void) {
    if (sPendingPanelDL != NULL) {
        // Interpolation replays the frame's display list several times; the panels only need drawing once
        gSPEndDisplayList(sPendingPanelDL);
        sPendingPanelDL = NULL;
    }
}

static void Panel_End(Gfx* start, Gfx* gfx, GraphicsContext* gfxCtx) {
    gDPPipeSync(gfx++);
    gsSPResetFB(gfx++);
    gSPEndDisplayList(gfx++);

    if (gfx - start > PANEL_GFX_SIZE) {
        // Already past the end of the buffer; nothing safe left to do but stop
        abort();
    }

    sPendingPanelDL = start;
    OPEN_DISPS(gfxCtx);
    gSPDisplayList(OVERLAY_DISP++, start);
    // Restore the main screen's scissor for whatever is drawn after us
    gSPDisplayList(OVERLAY_DISP++, D_0E000000_TO_SEGMENTED(setScissor));
    CLOSE_DISPS(gfxCtx);
}

// Frames since gameplay last drew the panels; the title page takes over once it's clearly gone
static s32 sFramesSincePlayDraw = 0;

// #region Notebook details: tap a portrait or a stamp to read it, and what's coming up next

// Notebook data (z_play_hireso.c): per person, entries of 3 u16s (day|event, start time, end time)
extern u16 sBombersNotebookEntries[BOMBERS_NOTEBOOK_PERSON_MAX][10 * 3];
extern u16 sBombersNotebookTextIds[];
extern s16 sBombersNotebookDayRectRectLeft[];

#define NB_ENTRY_END 0x9999
#define NB_ENTRY_POS_BELOW 0x4000
#define NB_ENTRY_POS_ABOVE 0x8000
#define NB_ENTRY_DAY(row, j) ((sBombersNotebookEntries[row][j] & 0xF00) >> 8)
#define NB_ENTRY_EVENT(row, j) (sBombersNotebookEntries[row][j] & 0xFF)
#define NB_ENTRY_START(row, j) (sBombersNotebookEntries[row][(j) + 1])
#define NB_ENTRY_END_TIME(row, j) (sBombersNotebookEntries[row][(j) + 2])
#define NB_ROWS_TOP 107
#define NB_PHOTO_LEFT 49
#define NB_PHOTO_RIGHT 113
#define NB_BOX_Y 404
#define NB_TEXT_MAX 320

static s32 sNotebookPerson = -1; // picked person, or -1
static s32 sNotebookEvent = -1;  // picked event (notebook event index), or -1 for the person's own description

// Plain text of a game message: control codes dropped, line breaks become spaces. With firstLine, stops at the
// first line break (the name or title line). Reads the message table only; never touches an open text box.
static s32 Panel_MessageText(PlayState* play, u16 textId, char* out, s32 outSize, s32 firstLine) {
    MessageTableEntry* entry = play->msgCtx.messageTableNES; // messageTable is only set while a text box is open
    const u8* text;
    s32 i;
    s32 len = 0;

    if (entry == NULL) {
        return 0;
    }
    while ((entry->textId != 0xFFFF) && (entry->textId != textId)) {
        entry++;
    }
    if (entry->textId == 0xFFFF) {
        out[0] = '\0';
        return 0;
    }
    text = (const u8*)entry->segment;
    // 11-byte header (box type, icon, next message, prices)
    for (i = 11; (i < (s32)entry->msgSize) && (len < outSize - 1); i++) {
        u8 c = text[i];

        if (c == 0xBF) { // end
            break;
        }
        if ((c >= 0x10) && (c <= 0x13)) { // line / box breaks
            if (firstLine && (len > 0)) {
                break;
            }
            if ((len > 0) && (out[len - 1] != ' ')) {
                out[len++] = ' ';
            }
            continue;
        }
        if ((c == 0x0A) || (c == 0x14)) { // one argument byte
            i++;
            continue;
        }
        if ((c >= 0x1B) && (c <= 0x1F)) { // two argument bytes
            i += 2;
            continue;
        }
        if ((c >= 0x20) && (c < 0x7F)) {
            out[len++] = c;
        }
    }
    while ((len > 0) && (out[len - 1] == ' ')) {
        len--;
    }
    out[len] = '\0';
    return len;
}

// Time of day as minutes after 6:00, so the notebook's day runs 0..1439
static s32 Panel_NotebookMinutes(u16 time) {
    return (s32)TIME_TO_MINUTES_F((u16)(time - CLOCK_TIME(6, 0)));
}

static s32 Panel_PersonMet(s32 person) {
    return CHECK_WEEKEVENTREG(gBombersNotebookWeekEventFlags[person]);
}

// Which notebook row is shown at this y, or -1 (rows only line up while not scrolling)
static s32 Panel_NotebookRowAt(f32 y) {
    s32 r;

    if ((sPanelNotebook.scrollAmount != 0) || (y < NB_ROWS_TOP)) {
        return -1;
    }
    r = ((s32)y - NB_ROWS_TOP) / NOTEBOOK_ROW_HEIGHT;
    return ((r < 7) && (sPanelNotebook.cursorPage + r < BOMBERS_NOTEBOOK_PERSON_MAX)) ? sPanelNotebook.cursorPage + r
                                                                                        : -1;
}

static void Panel_UpdateNotebookDetails(PlayState* play) {
    s32 person;
    s32 rowTop;
    s32 j;

    if (!Panel_Tapped(0, NB_ROWS_TOP, 600, NB_BOX_Y - NB_ROWS_TOP)) {
        return;
    }
    person = Panel_NotebookRowAt(sTouch.startY);
    if ((person < 0) || !Panel_PersonMet(person)) {
        sNotebookPerson = sNotebookEvent = -1;
        return;
    }
    rowTop = NB_ROWS_TOP + (person - sPanelNotebook.cursorPage) * NOTEBOOK_ROW_HEIGHT;

    // A known event's stamp on that row? (same box maths as the notebook)
    for (j = 0; sBombersNotebookEntries[person][j] != NB_ENTRY_END; j += 3) {
        s32 day = NB_ENTRY_DAY(person, j);
        s32 left = sBombersNotebookDayRectRectLeft[day] + Panel_NotebookMinutes(NB_ENTRY_START(person, j)) / 10;
        s32 right = sBombersNotebookDayRectRectLeft[day] + Panel_NotebookMinutes(NB_ENTRY_END_TIME(person, j)) / 10;
        s32 yOffset = (sBombersNotebookEntries[person][j] & NB_ENTRY_POS_ABOVE)
                          ? 8
                          : ((sBombersNotebookEntries[person][j] & NB_ENTRY_POS_BELOW) ? 24 : 16);

        if (right - left < 16) {
            // Short boxes get a finger-sized target
            left = (left + right) / 2 - 8;
            right = left + 16;
        }
        if (Panel_InRect(sTouch.startX, sTouch.startY, left - 2, rowTop + yOffset - 10, right - left + 4, 30) &&
            CHECK_WEEKEVENTREG(gBombersNotebookWeekEventFlags[NB_ENTRY_EVENT(person, j)])) {
            sNotebookPerson = person;
            sNotebookEvent = NB_ENTRY_EVENT(person, j);
            Audio_PlaySfx(NA_SE_SY_CURSOR);
            return;
        }
    }
    // Otherwise the person: their own description
    sNotebookEvent = -1;
    sNotebookPerson = (sNotebookPerson == person && sNotebookEvent < 0 &&
                       !Panel_InRect(sTouch.startX, sTouch.startY, NB_PHOTO_LEFT, 0, NB_PHOTO_RIGHT - NB_PHOTO_LEFT, 600))
                          ? -1
                          : person;
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

// The soonest appointment today (for people you've met) that hasn't happened yet this cycle
static s32 Panel_NextNotebookEntry(s32* outPerson, s32* outEntry) {
    s32 now = Panel_NotebookMinutes(CURRENT_TIME);
    s32 best = 0x7FFF;
    s32 person;
    s32 j;

    for (person = 0; person < BOMBERS_NOTEBOOK_PERSON_MAX; person++) {
        if (!Panel_PersonMet(person)) {
            continue;
        }
        for (j = 0; sBombersNotebookEntries[person][j] != NB_ENTRY_END; j += 3) {
            s32 start = Panel_NotebookMinutes(NB_ENTRY_START(person, j));
            s32 end = Panel_NotebookMinutes(NB_ENTRY_END_TIME(person, j));

            if ((NB_ENTRY_DAY(person, j) != CURRENT_DAY) || (end <= now) ||
                CHECK_WEEKEVENTREG(gBombersNotebookWeekEventFlags[NB_ENTRY_EVENT(person, j)])) {
                continue;
            }
            if (start < best) {
                best = start;
                *outPerson = person;
                *outEntry = j;
            }
        }
    }
    return best != 0x7FFF;
}

// Picked person: the row highlight the notebook's own cursor would give (none otherwise)
static void Panel_SetNotebookHighlight(void) {
    sPanelNotebook.cursorPageRow =
        ((sNotebookPerson >= sPanelNotebook.cursorPage) && (sNotebookPerson < sPanelNotebook.cursorPage + 7))
            ? sNotebookPerson - sPanelNotebook.cursorPage
            : BOMBERS_NOTEBOOK_PERSON_MAX;
}

static Gfx* Panel_DrawNotebookDetails(Gfx* gfx, PlayState* play) {
    char title[64];
    char body[NB_TEXT_MAX];
    s32 person;
    s32 j;

    if (sNotebookPerson >= 0) {
        // Text box in the message-box style: name, then the person's or event's description
        Panel_MessageText(play, sBombersNotebookTextIds[sNotebookPerson], title, sizeof(title), true);
        if (sNotebookEvent >= 0) {
            Panel_MessageText(play, sBombersNotebookTextIds[sNotebookEvent], body, sizeof(body), false);
        } else {
            Panel_MessageText(play, sBombersNotebookTextIds[sNotebookPerson], body, sizeof(body), false);
            // The description repeats the name first; drop it
            if (strncmp(body, title, strlen(title)) == 0) {
                memmove(body, body + strlen(title), strlen(body + strlen(title)) + 1);
            }
        }
        gfx = Panel_FillRect(gfx, 8, NB_BOX_Y, 584, 474 - NB_BOX_Y, 0, 0, 0, 190);
        gfx = Panel_SetSilhouetteMode(gfx, 255, 220, 120, 255);
        gfx = Panel_DrawText(gfx, title, 20, NB_BOX_Y + 4, LOG_TEXT);
        gfx = Panel_SetSilhouetteMode(gfx, 255, 255, 255, 255);
        Panel_DrawTextWrapped(&gfx, body[0] == ' ' ? body + 1 : body, 20, NB_BOX_Y + 24, 560, 0.85f, 3);
        return gfx;
    }

    // Nothing picked: what's next today
    if (Panel_NextNotebookEntry(&person, &j)) {
        s32 start = (s32)TIME_TO_MINUTES_F(NB_ENTRY_START(person, j));
        char line[160];
        char* p = line;

        Panel_MessageText(play, sBombersNotebookTextIds[person], title, sizeof(title), true);
        p = Panel_FormatNumber(p, start / 60);
        *p++ = ':';
        *p++ = '0' + (start % 60) / 10;
        *p++ = '0' + (start % 60) % 10;
        *p++ = ' ';
        *p++ = ' ';
        strcpy(p, title);
        gfx = Panel_FillRect(gfx, 8, 444, 584, 30, 0, 0, 0, 150);
        gfx = Panel_SetSilhouetteMode(gfx, 255, 220, 120, 255);
        gfx = Panel_DrawText(gfx, "Next", 20, 450, LOG_SMALL);
        gfx = Panel_SetSilhouetteMode(gfx, 255, 255, 255, 255);
        gfx = Panel_DrawText(gfx, line, 74, 450, LOG_SMALL);
    }
    return gfx;
}

// #endregion

// #region Map: the pause menu's world map, with Link's region and tap-to-warp owl statues

// Pause menu world map data (z_kaleido_map.c, z_kaleido_scope_NES.c, ShipUtils.cpp)
extern TexturePtr sCloudTextures[];
extern s16 sVtxPageMapWorldQuadsX[];
extern s16 sVtxPageMapWorldQuadsY[];
extern s16 gVtxPageMapWorldQuadsWidth[];
extern s16 gVtxPageMapWorldQuadsHeight[];
extern u16 sOwlWarpEntrancesForMods[];
extern s32 gHorseIsMounted;

// Index ranges in the tables above: 15 clouds, then 11 regions, then 10 owl statues
#define MAP_QUAD_CLOUDS 0
#define MAP_QUAD_REGIONS 15
#define MAP_QUAD_OWLS 26
#define MAP_NUM_CLOUDS 15
#define MAP_NUM_OWLS 10

// The 216x128 image's top-left corner in pause coordinates (y points up)
#define MAP_IMAGE_W 216
#define MAP_IMAGE_H 128
#define MAP_IMAGE_X0 (-109)
#define MAP_IMAGE_Y0 59

#define MAP_X 20
#define MAP_Y 12
#define MAP_W 600
#define MAP_H (MAP_W * MAP_IMAGE_H / MAP_IMAGE_W)
#define MAP_INFO_Y (MAP_Y + MAP_H + 10)
#define MAP_OWL_W 48
#define MAP_OWL_H 24
#define MAP_WARP_BTN_X 470
#define MAP_WARP_BTN_W 150

static const char* sRegionNames[] = {
    "Great Bay",       "Zora Hall",    "Romani Ranch",   "Deku Palace", "Woodfall",    "Clock Town",
    "Snowhead",        "Ikana Graveyard", "Ikana Canyon", "Goron Village", "Stone Tower",
};

static const char* sOwlNames[] = {
    "Great Bay Coast", "Zora Cape", "Snowhead",       "Mountain Village", "Clock Town",
    "Milk Road",       "Woodfall",  "Southern Swamp", "Ikana Canyon",     "Stone Tower",
};

static s32 sSelectedOwl = -1;

// #region Map view: zoom and pan, Google Maps style (pinch or the +/- buttons, drag to pan)

#define MAP_ZOOM_MAX 4.0f
#define CVAR_MAP_CLOUDS "gEnhancements.DualScreen.MapClouds"
#define MAP_BTN 44

static f32 sMapZoom = 1.0f;
// Centre of the view, in map image texels (= pause map units)
static f32 sMapCenterU = MAP_IMAGE_W / 2;
static f32 sMapCenterV = MAP_IMAGE_H / 2;
static f32 sMapDragX;
static f32 sMapDragY;
static f32 sMapPinchDist = 0.0f;

static f32 Panel_MapVisW(void) {
    return MAP_IMAGE_W / sMapZoom;
}

static f32 Panel_MapVisH(void) {
    return MAP_IMAGE_H / sMapZoom;
}

// Keeps the view inside the image
static void Panel_MapClampView(void) {
    sMapZoom = CLAMP(sMapZoom, 1.0f, MAP_ZOOM_MAX);
    sMapCenterU = CLAMP(sMapCenterU, Panel_MapVisW() / 2, MAP_IMAGE_W - Panel_MapVisW() / 2);
    sMapCenterV = CLAMP(sMapCenterV, Panel_MapVisH() / 2, MAP_IMAGE_H - Panel_MapVisH() / 2);
}

static f32 Panel_MapLeft(void) {
    return sMapCenterU - Panel_MapVisW() / 2;
}

static f32 Panel_MapTop(void) {
    return sMapCenterV - Panel_MapVisH() / 2;
}

static s32 Panel_MapX(s32 pauseX) {
    return MAP_X + (s32)(((pauseX - MAP_IMAGE_X0) - Panel_MapLeft()) * MAP_W / Panel_MapVisW());
}

static s32 Panel_MapY(s32 pauseY) {
    return MAP_Y + (s32)(((MAP_IMAGE_Y0 - pauseY) - Panel_MapTop()) * MAP_H / Panel_MapVisH());
}

// Zooms by a factor while keeping the texel under (screenX, screenY) where it is
static void Panel_MapZoomAt(f32 factor, f32 screenX, f32 screenY) {
    f32 u = Panel_MapLeft() + (screenX - MAP_X) * Panel_MapVisW() / MAP_W;
    f32 v = Panel_MapTop() + (screenY - MAP_Y) * Panel_MapVisH() / MAP_H;

    sMapZoom = CLAMP(sMapZoom * factor, 1.0f, MAP_ZOOM_MAX);
    sMapCenterU = u - (screenX - MAP_X) * Panel_MapVisW() / MAP_W + Panel_MapVisW() / 2;
    sMapCenterV = v - (screenY - MAP_Y) * Panel_MapVisH() / MAP_H + Panel_MapVisH() / 2;
    Panel_MapClampView();
}

static s32 Panel_InMap(f32 x, f32 y) {
    return Panel_InRect(x, y, MAP_X, MAP_Y, MAP_W, MAP_H);
}

// #endregion

// Centre of one of the pause map's quads on the panel
static void Panel_MapQuadCenter(s32 quad, s32* x, s32* y) {
    *x = Panel_MapX(sVtxPageMapWorldQuadsX[quad] + gVtxPageMapWorldQuadsWidth[quad] / 2);
    *y = Panel_MapY(sVtxPageMapWorldQuadsY[quad] - gVtxPageMapWorldQuadsHeight[quad] / 2);
}

static s32 Panel_CurrentRegion(PlayState* play) {
    s32 i;
    s32 j;

    for (i = 0; i < REGION_MAX; i++) {
        for (j = 0; gSceneIdsPerRegion[i][j] != 0xFFFF; j++) {
            if (gSceneIdsPerRegion[i][j] == play->sceneId) {
                return i;
            }
        }
    }
    return -1;
}

static s32 Panel_OwlActive(s32 owl) {
    return (gSaveContext.save.saveInfo.playerData.owlActivationFlags & (1 << owl)) != 0;
}

// Why an owl warp can't happen right now, or NULL if it can
static const char* Panel_OwlWarpBlocked(PlayState* play) {
    if (!CHECK_QUEST_ITEM(QUEST_SONG_SOARING) || (INV_CONTENT(ITEM_OCARINA_OF_TIME) != ITEM_OCARINA_OF_TIME)) {
        return "Learn the Song of Soaring to fly here.";
    }
    if ((play->interfaceCtx.restrictions.songOfSoaring != 0) || (play->pauseCtx.state != PAUSE_STATE_OFF) ||
        (play->msgCtx.msgMode != MSGMODE_NONE) || Player_InCsMode(play) ||
        (play->transitionTrigger != TRANS_TRIGGER_OFF)) {
        return "You can't warp from here right now.";
    }
    return NULL;
}

// Same steps as 2S2H's pause menu owl warp, without playing the song
static void Panel_OwlWarp(PlayState* play, s32 owl) {
    Player* player = GET_PLAYER(play);

    // Leave Epona where she is instead of bringing her along
    if (gHorseIsMounted && (player->stateFlags1 & PLAYER_STATE1_800000) && (player->rideActor != NULL)) {
        if (CHECK_QUEST_ITEM(QUEST_SONG_EPONA)) {
            gSaveContext.save.saveInfo.horseData.sceneId = play->sceneId;
            gSaveContext.save.saveInfo.horseData.pos.x = player->rideActor->world.pos.x;
            gSaveContext.save.saveInfo.horseData.pos.y = player->rideActor->world.pos.y;
            gSaveContext.save.saveInfo.horseData.pos.z = player->rideActor->world.pos.z;
            gSaveContext.save.saveInfo.horseData.yaw = player->rideActor->shape.rot.y;
        }
        gHorseIsMounted = false;
    }
    CLEAR_EVENTINF(EVENTINF_41);
    Audio_PlaySfx(NA_SE_SY_DECIDE);
    play->nextEntrance = sOwlWarpEntrancesForMods[owl];
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_WHITE;
    sSelectedOwl = -1;
}

#define MAP_BTN_X (MAP_X + MAP_W - MAP_BTN - 8)
#define MAP_BTN_Y(i) (MAP_Y + 8 + (i) * (MAP_BTN + 8))

static void Panel_UpdateMapView(PlayState* play) {
    // Pinch: zoom around the point between the fingers
    if (sTouch.down && (sTouch.pointers >= 2) && Panel_InMap(sTouch.startX, sTouch.startY)) {
        f32 dx = sTouch.x2 - sTouch.x;
        f32 dy = sTouch.y2 - sTouch.y;
        f32 dist = sqrtf(dx * dx + dy * dy);

        if ((sMapPinchDist > 0.0f) && (dist > 1.0f)) {
            Panel_MapZoomAt(dist / sMapPinchDist, (sTouch.x + sTouch.x2) / 2, (sTouch.y + sTouch.y2) / 2);
        }
        sMapPinchDist = dist;
        sMapDragX = sTouch.x;
        sMapDragY = sTouch.y;
        return;
    }
    sMapPinchDist = 0.0f;

    // One finger: drag the map
    if (sTouch.began) {
        sMapDragX = sTouch.startX;
        sMapDragY = sTouch.startY;
    }
    if (sTouch.down && sTouch.moved && Panel_InMap(sTouch.startX, sTouch.startY)) {
        sMapCenterU -= (sTouch.x - sMapDragX) * Panel_MapVisW() / MAP_W;
        sMapCenterV -= (sTouch.y - sMapDragY) * Panel_MapVisH() / MAP_H;
        Panel_MapClampView();
    }
    sMapDragX = sTouch.x;
    sMapDragY = sTouch.y;
}

static void Panel_UpdateMapPage(PlayState* play) {
    s32 owl;

    Panel_UpdateMapView(play);
    if (Panel_Tapped(MAP_BTN_X, MAP_BTN_Y(0), MAP_BTN, MAP_BTN)) {
        Panel_MapZoomAt(1.5f, MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2);
        Audio_PlaySfx(NA_SE_SY_CURSOR);
        return;
    }
    if (Panel_Tapped(MAP_BTN_X, MAP_BTN_Y(1), MAP_BTN, MAP_BTN)) {
        Panel_MapZoomAt(1.0f / 1.5f, MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2);
        Audio_PlaySfx(NA_SE_SY_CURSOR);
        return;
    }
    if (Panel_Tapped(MAP_BTN_X, MAP_BTN_Y(3), MAP_BTN, MAP_BTN)) {
        // The pause map hides regions until you buy their Tingle map; this shows the whole map instead
        CVarSetInteger(CVAR_MAP_CLOUDS, !CVarGetInteger(CVAR_MAP_CLOUDS, 1));
        Audio_PlaySfx(NA_SE_SY_CURSOR);
        return;
    }
    if (Panel_Tapped(MAP_BTN_X, MAP_BTN_Y(2), MAP_BTN, MAP_BTN)) {
        // Recentre on Link, zooming in if the whole map is showing
        s32 region = Panel_CurrentRegion(play);

        if (region >= 0) {
            s32 quad = MAP_QUAD_REGIONS + region;

            sMapZoom = MAX(sMapZoom, 2.0f);
            sMapCenterU = sVtxPageMapWorldQuadsX[quad] + gVtxPageMapWorldQuadsWidth[quad] / 2 - MAP_IMAGE_X0;
            sMapCenterV = MAP_IMAGE_Y0 - (sVtxPageMapWorldQuadsY[quad] - gVtxPageMapWorldQuadsHeight[quad] / 2);
            Panel_MapClampView();
        }
        Audio_PlaySfx(NA_SE_SY_CURSOR);
        return;
    }

    if ((sSelectedOwl >= 0) && Panel_Tapped(MAP_WARP_BTN_X, MAP_INFO_Y, MAP_WARP_BTN_W, 44)) {
        if (Panel_OwlWarpBlocked(play) == NULL) {
            Panel_OwlWarp(play, sSelectedOwl);
        } else {
            Audio_PlaySfx(NA_SE_SY_ERROR);
        }
        return;
    }
    for (owl = 0; owl < MAP_NUM_OWLS; owl++) {
        s32 x;
        s32 y;

        Panel_MapQuadCenter(MAP_QUAD_OWLS + owl, &x, &y);
        if (Panel_OwlActive(owl) && Panel_InMap(x, y) && Panel_Tapped(x - MAP_OWL_W / 2 - 6, y - MAP_OWL_H / 2 - 10, MAP_OWL_W + 12,
                                                 MAP_OWL_H + 20)) {
            sSelectedOwl = (sSelectedOwl == owl) ? -1 : owl;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
            return;
        }
    }
}

static Gfx* Panel_DrawMapPage(Gfx* gfx, PlayState* play) {
    s32 i;
    s32 x;
    s32 y;
    s32 region = Panel_CurrentRegion(play);
    s32 pulse = 140 + (s32)(Math_SinS(play->state.frames * 0x1000) * 110.0f);

    // The map itself: a paletted image, loaded whole like 2S2H's HD-aware pause menu does
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIDECALA_PRIM, G_CC_MODULATEIDECALA_PRIM);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, 255);
    gDPLoadTLUT_pal256(gfx++, gWorldMapImageTLUT);
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadMultiTile(gfx++, gWorldMapImageTex, 0, G_TX_RENDERTILE, G_IM_FMT_CI, G_IM_SIZ_8b, MAP_IMAGE_W, MAP_IMAGE_H,
                     0, 0, MAP_IMAGE_W - 1, MAP_IMAGE_H - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                     G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTileSize(gfx++, G_TX_RENDERTILE, 0, 0, (MAP_IMAGE_W - 1) << G_TEXTURE_IMAGE_FRAC,
                   (MAP_IMAGE_H - 1) << G_TEXTURE_IMAGE_FRAC);
    // Only the part of the image in view: start texel and texels-per-unit from the zoom
    gSPWideTextureRectangle(gfx++, MAP_X << 2, MAP_Y << 2, (MAP_X + MAP_W) << 2, (MAP_Y + MAP_H) << 2,
                            G_TX_RENDERTILE, (s32)(Panel_MapLeft() * 32.0f), (s32)(Panel_MapTop() * 32.0f),
                            (s32)(Panel_MapVisW() * 1024.0f / MAP_W), (s32)(Panel_MapVisH() * 1024.0f / MAP_H));
    gDPPipeSync(gfx++);
    gDPSetTextureLUT(gfx++, G_TT_NONE);

    // Everything on the map is clipped to it while zoomed
    gfx = Panel_SetScissor(gfx, MAP_X, MAP_Y, MAP_X + MAP_W, MAP_Y + MAP_H);

    // Clouds over regions whose Tingle map you haven't bought, in the pause menu's colours
    gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(gfx++, 0, 0, 215, 235, 235, 255);
    gDPSetEnvColor(gfx++, 40, 60, 100, 0);
    for (i = 0; i < MAP_NUM_CLOUDS; i++) {
        s32 quad = MAP_QUAD_CLOUDS + i;
        s32 texW = gVtxPageMapWorldQuadsWidth[quad];
        s32 texH = gVtxPageMapWorldQuadsHeight[quad];
        s32 w;
        s32 h;

        if (!CVarGetInteger(CVAR_MAP_CLOUDS, 1) || (gSaveContext.save.saveInfo.worldMapCloudVisibility & gBitFlags[i])) {
            continue;
        }
        x = Panel_MapX(sVtxPageMapWorldQuadsX[quad]);
        y = Panel_MapY(sVtxPageMapWorldQuadsY[quad]);
        w = Panel_MapX(sVtxPageMapWorldQuadsX[quad] + texW) - x;
        h = Panel_MapY(sVtxPageMapWorldQuadsY[quad] - texH) - y;
        gDPLoadTextureBlock(gfx++, sCloudTextures[i], G_IM_FMT_IA, G_IM_SIZ_8b, texW, texH, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        gSPWideTextureRectangle(gfx++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0,
                                (texW << 10) / MAX(w, 1), (texH << 10) / MAX(h, 1));
    }

    // Link's region, pulsing
    if (region >= 0) {
        Panel_MapQuadCenter(MAP_QUAD_REGIONS + region, &x, &y);
    }
    if ((region >= 0) && Panel_InMap(x, y)) {
        gfx = Panel_SetTexturedMode(gfx, 255, 60, 40, pulse);
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x - 14, y - 14, 28, 28);
        gfx = Panel_SetTexturedMode(gfx, 255, 240, 220, 255);
        gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x - 6, y - 6, 12, 12);
    }

    // Owl statues you've activated; the picked one gets the gold ring
    for (i = 0; i < MAP_NUM_OWLS; i++) {
        if (!Panel_OwlActive(i)) {
            continue;
        }
        Panel_MapQuadCenter(MAP_QUAD_OWLS + i, &x, &y);
        if (!Panel_InMap(x, y)) {
            continue;
        }
        if (i == sSelectedOwl) {
            gfx = Panel_SetTexturedMode(gfx, 255, 200, 60, pulse);
            gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, x - 34, y - 34, 68, 68);
        }
        gfx = Panel_SetTexturedMode(gfx, 255, 255, 255, 255);
        gfx = Panel_DrawTexRGBA32(gfx, gWorldMapOwlFaceTex, 24, 12, x - MAP_OWL_W / 2, y - MAP_OWL_H / 2, MAP_OWL_W,
                                  MAP_OWL_H);
    }

    gDPPipeSync(gfx++);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, DUAL_SCREEN_CANVAS_WIDTH, DualScreen_GetCanvasHeight());

    // Zoom in, zoom out and recentre on Link, stacked in the map's corner
    for (i = 0; i < 4; i++) {
        static const char* sMapButtonLabels[] = { "+", "-", "", "" };
        s32 pressed = Panel_Pressing(MAP_BTN_X, MAP_BTN_Y(i), MAP_BTN, MAP_BTN);

        gfx = Panel_SetTexturedMode(gfx, 255, 245, 215, pressed ? 255 : 220);
        gfx = Panel_DrawTex8b(gfx, gButtonBackgroundTex, G_IM_FMT_IA, 32, 32, MAP_BTN_X, MAP_BTN_Y(i), MAP_BTN,
                              MAP_BTN);
        if (i == 3) {
            // Clouds toggle: one of the map's own clouds, faded while the clouds are off
            s32 on = CVarGetInteger(CVAR_MAP_CLOUDS, 1);

            gDPPipeSync(gfx++);
            gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                              ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            gDPSetPrimColor(gfx++, 0, 0, on ? 215 : 150, on ? 235 : 150, on ? 235 : 150, on ? 255 : 150);
            gDPSetEnvColor(gfx++, 40, 60, 100, 0);
            gfx = Panel_DrawTex8b(gfx, sCloudTextures[8], G_IM_FMT_IA, gVtxPageMapWorldQuadsWidth[8],
                                  gVtxPageMapWorldQuadsHeight[8], MAP_BTN_X + 6, MAP_BTN_Y(i) + 10, MAP_BTN - 12,
                                  MAP_BTN - 20);
        } else if (i < 2) {
            gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, 255);
            gfx = Panel_DrawText(gfx, sMapButtonLabels[i],
                                 MAP_BTN_X + (MAP_BTN - Panel_TextWidth(sMapButtonLabels[i], 1, 1.6f)) / 2 - 1,
                                 MAP_BTN_Y(i) + 7, 1.6f);
        } else {
            // Recentre: the same red dot that marks Link
            gfx = Panel_SetTexturedMode(gfx, 255, 60, 40, 255);
            gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, MAP_BTN_X + 12, MAP_BTN_Y(i) + 11,
                                  20, 20);
            gfx = Panel_SetTexturedMode(gfx, 255, 240, 220, 255);
            gfx = Panel_DrawTex4b(gfx, gBombersNotebookCircleTex, G_IM_FMT_I, 16, 16, MAP_BTN_X + 18, MAP_BTN_Y(i) + 17,
                                  8, 8);
        }
    }

    // Below the map: where Link is, or the picked owl and its warp button
    gfx = Panel_SetSilhouetteMode(gfx, 70, 40, 15, 255);
    if (sSelectedOwl < 0) {
        gfx = Panel_DrawText(gfx, (region >= 0) ? sRegionNames[region] : "Termina", 20, MAP_INFO_Y + 4, 1.2f);
        gfx = Panel_SetSilhouetteMode(gfx, 110, 70, 35, 255);
        gfx = Panel_DrawText(gfx,
                             (gSaveContext.save.saveInfo.playerData.owlActivationFlags != 0)
                                 ? "Tap an owl statue to fly there."
                                 : "Strike owl statues to mark them on this map.",
                             20, MAP_INFO_Y + 32, LOG_SMALL);
    } else {
        const char* blocked = Panel_OwlWarpBlocked(play);

        gfx = Panel_DrawText(gfx, sOwlNames[sSelectedOwl], 20, MAP_INFO_Y + 4, 1.2f);
        if (blocked != NULL) {
            gfx = Panel_SetSilhouetteMode(gfx, 110, 70, 35, 255);
            gfx = Panel_DrawText(gfx, blocked, 20, MAP_INFO_Y + 32, LOG_SMALL);
        }
        gfx = Panel_FillRect(gfx, MAP_WARP_BTN_X, MAP_INFO_Y, MAP_WARP_BTN_W, 44, 120, 80, 30,
                             (blocked != NULL) ? 50 : (Panel_Pressing(MAP_WARP_BTN_X, MAP_INFO_Y, MAP_WARP_BTN_W, 44)
                                                           ? 230
                                                           : 160));
        gfx = Panel_SetSilhouetteMode(gfx, 255, 245, 210, (blocked != NULL) ? 120 : 255);
        gfx = Panel_DrawText(gfx, "Warp", MAP_WARP_BTN_X + (MAP_WARP_BTN_W - Panel_TextWidth("Warp", 8, 1.2f)) / 2,
                             MAP_INFO_Y + 10, 1.2f);
    }
    return gfx;
}

// #endregion

// Every page once, behind the real one, over the first frames: uploads all their textures up front so the
// first visit to a tab doesn't stall the game (and the audio) while textures load
typedef enum PanelWarmup {
    WARMUP_ITEMS,
    WARMUP_MASKS,
    WARMUP_SONGS,
    WARMUP_LOG_MAIN,
    WARMUP_LOG_MASKS,
    WARMUP_LOG_COLLECT,
    WARMUP_NOTEBOOK,
    WARMUP_MAP,
    WARMUP_DONE
} PanelWarmup;

static s32 sWarmupStep = WARMUP_ITEMS;

static Gfx* Panel_DrawWarmupPage(Gfx* gfx, PlayState* play, s32 canvasHeight) {
    s32 view = sQuestLogView;

    switch (sWarmupStep) {
        case WARMUP_ITEMS:
            gfx = Panel_DrawEquipPage(gfx, play, 0);
            break;
        case WARMUP_MASKS:
            gfx = Panel_DrawEquipPage(gfx, play, ITEM_NUM_SLOTS);
            break;
        case WARMUP_SONGS:
            gfx = Panel_DrawQuestPage(gfx);
            break;
        case WARMUP_LOG_MAIN:
        case WARMUP_LOG_MASKS:
        case WARMUP_LOG_COLLECT:
            sQuestLogView = QUEST_LOG_MAIN + (sWarmupStep - WARMUP_LOG_MAIN);
            gfx = Panel_DrawQuestLog(gfx);
            sQuestLogView = view;
            break;
        case WARMUP_NOTEBOOK:
            Panel_SetNotebookHighlight();
            gfx = Panel_DrawNotebook(gfx, canvasHeight);
            break;
        default:
            gfx = Panel_DrawMapPage(gfx, play);
            break;
    }
    sWarmupStep++;
    // Cover it up again; only the real page is shown
    return Panel_DrawBackground(gfx, canvasHeight);
}

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
    if (sWarmupStep < WARMUP_DONE) {
        gfx = Panel_DrawWarmupPage(gfx, play, canvasHeight);
    }
    if (sPage == DUAL_SCREEN_PAGE_ITEMS) {
        gfx = Panel_DrawEquipPage(gfx, play, 0);
    } else if (sPage == DUAL_SCREEN_PAGE_MASKS) {
        gfx = Panel_DrawEquipPage(gfx, play, ITEM_NUM_SLOTS);
    } else if (sPage == DUAL_SCREEN_PAGE_QUEST) {
        gfx = Panel_DrawQuestPage(gfx);
    } else if (sPage == DUAL_SCREEN_PAGE_MAP) {
        gfx = Panel_DrawMapPage(gfx, play);
    } else if (sPage == DUAL_SCREEN_PAGE_NOTEBOOK) {
        Panel_SetNotebookHighlight();
        gfx = Panel_DrawNotebook(gfx, canvasHeight);
        gfx = Panel_DrawNotebookDetails(gfx, play);
    } else {
        gfx = Panel_DrawQuestLog(gfx);
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
