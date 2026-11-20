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
    if (glyph == 10) {
        return digitW * 3 / 8; // colon
    }
    return (glyph == 1) ? digitW * 7 / 16 : digitW * 5 / 8;
}

static void Panel_NotebookTimeGlyphs(s32 hours, s32 minutes, s32 glyphs[5]) {
    glyphs[0] = (hours / 10) % 10;
    glyphs[1] = hours % 10;
    glyphs[2] = 10;
    glyphs[3] = minutes / 10;
    glyphs[4] = minutes % 10;
}

static s32 Panel_NotebookTimeWidth(s32 hours, s32 minutes, s32 digitW) {
    s32 glyphs[5];
    s32 width = 0;
    s32 i;

    Panel_NotebookTimeGlyphs(hours, minutes, glyphs);
    for (i = 0; i < 5; i++) {
        width += Panel_NotebookGlyphAdvance(glyphs[i], digitW);
    }
    return width;
}

static Gfx* Panel_DrawNotebookTime(Gfx* gfx, s32 hours, s32 minutes, s32 x, s32 y, s32 digitW, s32 digitH) {
    s32 glyphs[5];
    s32 i;

    Panel_NotebookTimeGlyphs(hours, minutes, glyphs);
    for (i = 0; i < 5; i++) {
        s32 advance = Panel_NotebookGlyphAdvance(glyphs[i], digitW);

        // Glyphs are centred in their cells, so centre each cell on its advance
        gfx = Panel_DrawTex4b(gfx, sNotebookDigitTextures[glyphs[i]], G_IM_FMT_I, 16, 17,
                              x + (advance - digitW) / 2, y, digitW, digitH);
        x += advance;
    }
    return gfx;
}

// #endregion

static void Panel_UpdateNotebookScroll(s32 swipe) {
    BombersNotebook* nb = &sPanelNotebook;

    if (swipe == DUAL_SCREEN_SWIPE_UP) {
        sNotebookTargetPage = CLAMP_MAX(sNotebookTargetPage + 4, NOTEBOOK_MAX_PAGE);
    } else if (swipe == DUAL_SCREEN_SWIPE_DOWN) {
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
// (sized for the widest reading, so the bar doesn't shift as the digits change)
#define TIME_BAR_MOON_X (DUAL_SCREEN_CANVAS_WIDTH - 24 - (4 * 21 * 5 / 8 + 21 * 3 / 8) - 40)
#define TIME_BAR_SEGMENTS_X 200
#define TIME_BAR_SEGMENT_W ((TIME_BAR_MOON_X - 24 - TIME_BAR_SEGMENTS_X - 2 * 6) / 3)

static Gfx* Panel_DrawTimeBar(Gfx* gfx, s32 canvasHeight) {
    s32 day = CURRENT_DAY;
    s32 minutes = (s32)TIME_TO_MINUTES_F(CURRENT_TIME);
    s32 elapsed;
    s32 remaining;
    s32 barY = NOTEBOOK_HEIGHT + (canvasHeight - NOTEBOOK_HEIGHT) / 2;
    s32 i;

    // Days start at dawn, so the moon falls at 6:00 on the morning after the Final Day
    if (day == 0) {
        elapsed = 0;
    } else {
        elapsed = (day - 1) * 24 * 60 + ((minutes - 6 * 60 + 24 * 60) % (24 * 60));
    }
    elapsed = CLAMP(elapsed, 0, MOON_FALL_MINUTES);
    remaining = MOON_FALL_MINUTES - elapsed;

    gfx = Panel_FillRect(gfx, 20, NOTEBOOK_HEIGHT + 2, DUAL_SCREEN_CANVAS_WIDTH - 40, 2, 90, 50, 20, 160);

    // Day label from the three-day clock, then the time in the notebook's hand
    gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    gfx = Panel_DrawTex8b(gfx, sDayTextures[CLAMP(day, 0, 4)], G_IM_FMT_I, 48, 27, 24, barY - 20, 72, 40);
    gfx = Panel_DrawNotebookTime(gfx, minutes / 60, minutes % 60, 104, barY - 16, 24, 32);

    // Three-day progress: one segment per day, filled up to now, between the clock and the moon
    for (i = 0; i < 3; i++) {
        s32 segX = TIME_BAR_SEGMENTS_X + i * (TIME_BAR_SEGMENT_W + 6);
        s32 segFill = CLAMP(elapsed - i * 24 * 60, 0, 24 * 60) * TIME_BAR_SEGMENT_W / (24 * 60);

        gfx = Panel_FillRect(gfx, segX, barY - 4, TIME_BAR_SEGMENT_W, 8, 90, 50, 20, 60);
        if (segFill > 0) {
            gfx = Panel_FillRect(gfx, segX, barY - 4, segFill, 8, (i == 2) ? 190 : 120, (i == 2) ? 40 : 70, 30,
                                 220);
        }
    }

    // Time left before the moon falls; it burns red in the final hours
    if (remaining <= 6 * 60) {
        gfx = Panel_SetTexturedMode(gfx, 255, 60, 30, 255);
    } else {
        gfx = Panel_SetTexturedMode(gfx, 200, 170, 120, 255);
    }
    gfx = Panel_DrawTex8b(gfx, gThreeDayClockMoonHourTex, G_IM_FMT_IA, 24, 24, TIME_BAR_MOON_X, barY - 18, 36, 36);
    if (remaining <= 6 * 60) {
        gfx = Panel_SetTexturedMode(gfx, 200, 30, 20, 255);
    } else {
        gfx = Panel_SetTexturedMode(gfx, 90, 50, 20, 255);
    }
    gfx = Panel_DrawNotebookTime(gfx, remaining / 60, remaining % 60,
                                 DUAL_SCREEN_CANVAS_WIDTH - 24 - Panel_NotebookTimeWidth(remaining / 60, remaining % 60, 21),
                                 barY - 14, 21, 28);
    return gfx;
}

void DualScreen_Draw(PlayState* play) {
    Gfx* start;
    Gfx* gfx;
    u8 tapped;
    s32 swipe;
    s32 canvasHeight = DualScreen_GetCanvasHeight();
    s32 hasNotebook = CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK) != 0;
    s32 frameBufferId = DualScreen_GetFrameBufferId();

    if (frameBufferId < 0) {
        return;
    }
    DualScreen_ConsumeInput(&tapped, &swipe);
    if (tapped && hasNotebook) {
        sPage = (sPage + 1) % DUAL_SCREEN_PAGE_MAX;
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    }
    // The schedule only exists once Link has the notebook, as in the original
    if (!hasNotebook) {
        sPage = DUAL_SCREEN_PAGE_STATUS;
    }
    Panel_UpdateNotebookScroll((sPage == DUAL_SCREEN_PAGE_NOTEBOOK) ? swipe : DUAL_SCREEN_SWIPE_NONE);

    start = gfx = sPanelGfx[sPanelGfxIndex];
    sPanelGfxIndex ^= 1;

    // Keep the HUD editor's per-element overrides away from these draws
    HudEditor_SetActiveElement(HUD_EDITOR_ELEMENT_NONE);

    gsSPSetFB(gfx++, frameBufferId);
    gfx = Gfx_SetupDL39(gfx);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, DUAL_SCREEN_CANVAS_WIDTH, canvasHeight);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);

    gfx = Panel_DrawBackground(gfx, canvasHeight);
    if (sPage == DUAL_SCREEN_PAGE_NOTEBOOK) {
        gfx = Panel_DrawNotebook(gfx, canvasHeight);
    } else {
        gfx = Panel_DrawStatus(gfx);
    }
    gfx = Panel_DrawTimeBar(gfx, canvasHeight);

    gDPPipeSync(gfx++);
    gsSPResetFB(gfx++);
    gSPEndDisplayList(gfx++);

    if (gfx - start > PANEL_GFX_SIZE) {
        // Already past the end of the buffer; nothing safe left to do but stop
        abort();
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPDisplayList(OVERLAY_DISP++, start);
    // Restore the main screen's scissor for the HUD drawn after us
    gSPDisplayList(OVERLAY_DISP++, D_0E000000_TO_SEGMENTED(setScissor));
    CLOSE_DISPS(play->state.gfxCtx);
}
