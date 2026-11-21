# 2S2H Dual Screen — Roadmap

> What is built so far is documented in [2S2H-DualScreen-Features.md](FEATURES.md).

Goal: make the Thor's bottom screen feel like MM3D's touch screen, a real part of the game.
Everything is drawn with Majora's Mask's own art, and the top screen never slows down.

Status key: `[ ]` todo · `[~]` in progress · `[x]` done and verified on the Thor · `[-]` dropped

---

## Resume protocol (read first after any interruption)

1. **Find the next task:** the first `[~]` or `[ ]` item below. The "Progress log" at the bottom says what happened last.
2. **Find the code:** source is `~/android-dev/2ship`, branch `dualscreen`. Every finished task is one git commit.
   - `git log --oneline` shows what's done.
   - `git status` shows any half-finished work.
3. **Dev loop (token-light):** `~/android-dev/tools/ds.sh build|install|shot [bot|top]|tap X Y|swipe X1 Y1 X2 Y2|log`. Coordinates are canvas units; screenshots come out at half size. `install` refuses while the app is in front unless `force` is passed. The steps below are the manual equivalents.
4. **Build:** `bash ~/android-dev/build.sh` (about 2–5 min).
   - The APK lands in `~/android-dev/2ship/Android/app/build/outputs/apk/release/app-release.apk`.
   - Copy it to `~/android-dev/2Ship-DualScreen-vN.apk`.
4. **Connect:** find the Thor with `adb mdns services`; its IP changes.
   - **Never** force-stop either 2S2H app while the user may be playing. Ask first.
5. **Install:** `adb install -r <apk>`.
6. **Capture screenshots:**
   - Bottom: `adb exec-out screencap -p -d 4630946482288158084 > bot.png`
   - Top: `adb exec-out screencap -p -d 4630946441858561667 > top.png`
7. **Simulate touch on the bottom screen:** `adb shell input -d 4 tap X Y` or `adb shell input -d 4 swipe X1 Y1 X2 Y2 150`.
   - Screen pixels are 1240x1080. Canvas units are 640x557 (1 unit ≈ 1.94 px).
8. **Read logs:** `adb logcat -d -s DualScreen`.
9. **Close out each task:** update this file (checkbox plus a log line), then commit.

### Key facts

- **Panel framebuffer:** created at 640xH canvas units with `resize=true`. Native dimensions are chosen so it renders at the panel's real 1240x1080. libultraship maps draw coordinates against the framebuffer's `orig_width`/`orig_height`.
- **Files:**
  - `mm/2s2h/Enhancements/DualScreen/DualScreen.cpp`: EGL present, touch, framebuffer.
  - `mm/2s2h/Enhancements/DualScreen/DualScreenPanels.c`: pages.
  - `.../DualScreen.h`
  - Java side: `DualScreenManager` and `DualScreenPresentation`.
- **Hooks:**
  - `Play_PostWorldDraw` in `z_play.c` calls `DualScreen_Draw`.
  - `BenPort.cpp` calls `DualScreen_PresentFrame` after the frame.
- **Shared data:** both 2S2H apps share `/sdcard/2S2H`. Saves are backed up in `~/Backups/2S2H_saves`.
- **Game-state changes go in update, not draw.** Anything that changes game state (equipping, warping, playing songs) must run in the game's update step (Phase 1 adds that hook).

---

## Phase 0 — Polish what exists

- [x] 0.1 Time digits aligned: fixed-width clock digits, centred colon, both clocks the same size on one baseline.
- [x] 0.0 Colon in the time bar sits slightly left of centre; shift it right by about digitW/10 in `Panel_DrawNotebookTime`.
- [x] 0.2 Bottom screen outside gameplay: title, file select and pause show a parchment "Majora's Mask" page instead of a frozen frame.
- [x] 0.3 Remove the periodic debug log, keep error logs.

## Phase 1 — Touch foundation (needed by everything below)

- [x] 1.1 Touch events in canvas units: press, move, release, hold. Each is queued from the UI thread and drained once per game update.
- [x] 1.2 Game-update hook: run touch actions in `Play_Update`, so equips and warps are safe.
- [x] 1.3 MM3D-style tab bar along the bottom: Items · Masks · Map · Quest · Notebook. It uses item icons, with a press highlight and the game's cursor/select sounds. The time bar moves into a slim strip above it.
- [x] 1.4 Hit-testing helpers, press feedback (scale/glow, as on the pause menu's cursor) and drag rendering (icon follows the finger).

## Phase 2 — Items and masks (the MM3D core)

- [x] 2.1 **Items page:** the 24-slot item grid from the pause menu, with ammo counts, using the real slot art.
- Plan for 2.2: the pause menu's equip logic (swaps between C and D-pad slots, bow and magic arrows, form restrictions in `gPlayerFormSlotRestrictions`) lives in `KaleidoScope_UpdateItemEquip` (z_kaleido_item.c ~1483) and is tied to its fly-to-button animation. Pull the "apply" block that runs when the animation ends out into `KaleidoScope_ApplyItemEquip(play, item, slot, cBtn)`, and call it from both the pause menu and the panel. D-pad slots go through `KaleidoScope_UpdateDpadItemEquip` (~892) and `SwapDpadItemToCItem` (~818); the same extraction applies.
- [x] 2.2 **Equip by touch:** tap an item, then tap a button, or drag an item onto a button icon. The targets are:
  - the X, Y and RB (C-button) slots
  - the 2S2H D-pad slots
  - It uses the game's own equip path, so the HUD and transformations update correctly.
- [x] 2.3 Use items directly from the screen. A tap on the "use" area triggers it, MM3D style; this is optional and goes behind a toggle.
- [x] 2.4 **Masks page:** a 24-mask grid, with the same equip/drag. Transformation masks are highlighted.
- [x] 2.5 Equipped-button preview: the current X, Y, RB and D-pad items mirrored at the top of the page.

## Phase 3 — Quest status and music guide

- [x] 3.1 **Quest page:**
  - remains (Odolwa, Goht, Gyorg, Twinmold)
  - sword, shield, quiver, bomb bag, wallet
  - Bomber's Notebook and Lens
  - heart pieces as the pause-menu heart
- [x] 3.2 **Music guide:** each learned song as a staff with the game's ocarina note icons (A, ▲▼◀▶ shown as your X/Y/RB letters where the button mapping applies). Unlearned songs are greyed out.
- [x] 3.3 Tap a song to play it, using 2S2H "Song Items" or the ocarina playback path. Only songs that are learned and usable here can be played.

## Phase 3B — Quest log (requested after Phase 3)

- [x] 3B.1 **Text in the game's font:** draw strings with the message-box glyphs (`fontTbl`, 16x16 I4) and the game's width table (`sNESFontWidths`), with word wrapping.
- [x] 3B.2 **Song names:** each song's name in the music guide, above its notes.
- [x] 3B.3 **Quest Log tab** (replaces Collection) with three views: Main · Masks · Collect. A Lens of Truth toggle reveals hints.
- [x] 3B.4 **Main view:** Clock Town, Woodfall, Snowhead, Great Bay, Ikana and the Moon, each with step dots from save flags (masks, songs, remains), the next objective, and a hint when the Lens is on.
- [x] 3B.5 **Masks view:** a 24-mask grid with found masks in colour. Tap one for its name and status; the Lens shows where to get it.
- [x] 3B.6 **Collect view:**
  - heart pieces
  - stray fairies per temple plus Clock Town
  - Swamp and Ocean Skulltula houses
  - people met in the Bombers' Notebook
  - masks
  - bank

  Each line gets a tip when the Lens is on.
- [x] 3B.7 **Later:** in-cycle side quests (Anju and Kafei, Romani Ranch...) using week-event flags.

## Phase 4 — Map and owl warps

- [x] 4.1 **Map page:** the Termina world map from the pause menu, with region names and Link's current region marked.
- [x] 4.2 Activated owl statues drawn on the map.
- [x] 4.3 Tap an owl, confirm, and warp. This reuses the 2S2H owl-warp logic (`Songs/BetterOwlWarpMenu`, `PauseOwlWarp`), and only works where Song of Soaring is allowed.
- [x] 4.4 Dungeon map: inside a temple the Map tab shows the pause menu's floor map (rooms in its palettes, current room pulsing, doors; chests and boss door with the compass), Link's position, tappable floor buttons (Link's floor and the boss floor marked) and the temple's map/compass/boss key/keys/fairies.

## Phase 5 — Notebook depth

- [x] 5.1 Tap a person: they get the highlight, and their events appear as tooltip-style descriptions in the game's font.
- [x] 5.2 A "next up" strip: the upcoming notebook events in the next few in-game hours.
- [x] 5.3 The time bar shows Inverted Song of Time / Double Time state.

## Phase 6 — Extras

- [~] 6.1 Target info: while Z-targeting an enemy, a card at the top of the bottom screen shows its name (Tatl's hint names) and health (2S2H's stored max health). Option "Second Screen Target Info", default on. Built, not yet seen on the device (no enemy reachable during the test).
- [x] 6.2 FPS and input display: a bar above the strip with the real top-screen FPS, the stick and the held buttons (Thor names). Option "Second Screen FPS and Input", default off.
- [x] 6.3 Randomizer Checks tab (chest icon), only in rando saves: opens on Link's area, "< Areas" lists every area with done/total, drag to scroll, tap an area for its checks (stamped when collected). In-logic colouring left to the ImGui tracker (its logic search is too heavy per frame).
- [x] 6.4 Settings: "Second Screen Tab: …" hides any tab; "Second Screen Left-Handed" puts the controller on the left of Items/Masks, the grid on the right, and reverses the tabs.

---

## Progress log

- 2026-11-20: v2 working on the Thor: notebook page, status page, time bar, tap/swipe. Committed `aeeee29` on branch `dualscreen`. Roadmap created.
- 2026-11-20: 0.1 built as v3 (~/android-dev/2Ship-DualScreen-v3.apk), waiting to install: the app was open on the Thor.
- 2026-11-20: v3 installed and verified (0.1 done). Paused by the user. Next: 0.0 colon nudge, then 0.2. Lead for 0.2: `GameState_Draw` in `mm/src/code/game.c` runs for every game state; draw a parchment page there when the state isn't Play (the Play path already draws via `Play_PostWorldDraw`).
- 2026-11-20: v4 `f5a50cd`. Phase 0 and Phase 1 core done:
  - touch queue
  - `DualScreen_Update` in `Play_Update`
  - bottom strip: clock, three-day timeline, Notebook/Collection tabs, moon countdown
  - tab taps verified on the Thor
  - **Still open:** 0.2 (the idle title page in `GameState_Draw`) is built but not seen on the device yet, because the app loads straight into the save. 1.4 has hit-testing and press growth done; drag rendering moves to 2.2.
  - **Next:** Phase 2 Items page.
- 2026-11-20: v5 `3564004`: title logo drawn as one clamped tile (it had been wrapping). Verified on the Thor; 0.2 done. Next: Phase 2.
- 2026-11-20: v6/v7 `3b9c9a6`. Phase 2 equip:
  - Items and Masks tabs added; equip by tap-then-button or by drag
  - A Nintendo-layout button cluster shows X, Y, RB and the D-pad slots
  - Pause equip logic split out as `KaleidoScope_ApplyItemEquip` and `KaleidoScope_ApplyMaskEquip`
  - Verified on v6: drag equip (Beans to X) and tap equip (Ocarina to Y, which swapped off RB) both reflect on the HUD
  - **v7** (frame drawn behind icons, bigger ammo, grey D-pad slots) is built but not installed yet
  - **Untested:** the Masks page on the device, transformation-mask rules
  - 2.3 (use an item by tap) not started
- 2026-11-20: v8 `b7913e0` installed. Button icons are larger and centred. Masks page verified: Deku Mask dragged to X swapped off D-Up, and the HUD matched. Remaining in Phase 2: 2.3 (use an item by tap, optional) and testing the transformation-mask rule while transformed.
- 2026-11-20: v9 `2c09e7e`. Grid and buttons enlarged. 2.3 done: holding an equipped button presses it in-game (press, hold, release), toggled by `gEnhancements.DualScreen.TouchUse`. Verified: tapping D-Up wore the Deku Mask; dragging the Ocarina onto D-Up while Deku was refused; tapping D-Up again took the mask off. **Phase 2 complete. Next: Phase 3.**
- 2026-11-20: v10 `09066bc` built but not installed: the Thor dropped off ADB (connection refused). Includes:
  - Quest tab with remains and gear
  - Music guide: 12 songs, notes shown as X/Y/RB/A/C-Up buttons
  - Tap a song to play it: `DualScreen_RequestSong` plus VB hooks in DualScreen.cpp make C-Left act as the ocarina and auto-play the staff
  - Collection tab icon is now the Giant Wallet

  **Next:** install and test 3.1–3.3 on the device.
- 2026-11-20: v11 `b2c2da9` installed. Phase 3 verified on the Thor:
  - Quest gear shows; songs are written with the correct buttons
  - Tapping Song of Healing made Link play it on the ocarina
  - Note icons are coloured by alpha (the HD note texture is dark)
  - `ds.sh` now pins `ANDROID_SERIAL`, because mDNS can list the Thor twice

  **Next:** Phase 4 (map and owl warps).
- 2026-11-20: v12–v16. Phase 3B (3B.1–3B.6) done:
  - text in the game's font
  - song names in the music guide
  - Quest Log tab (Main Quest / Masks / Collection) with Lens of Truth hints (CVar `gEnhancements.DualScreen.QuestHints`); it replaces Collection
  - Songs tab icon is now a brown note

  Verified on the Thor: Main view and the hint box. **Not yet checked on the device:** the Masks and Collection views.

  **Performance:**
  - Interpolation replayed the panel display list for every interpolated frame. `DualScreen_SkipPanelReplays` now ends it after the first replay, and the present happens right after that replay.
  - The user still heard choppy audio. Raised `DesiredBuffered` from 1680 to 2400 in `BenPort.cpp` and log `slow present` when the copy takes over 3 ms. Measured the game at about 57% of one core, CPU at about 74°C.
- 2026-11-20: v17 `38f7da7`:
  - The second-screen copy moved to its own thread: an EGL fence plus a condition variable, swap interval 1 on that thread. The game thread now only creates a fence and flushes.
  - Measured 116 FPS on the top screen with 0 janky frames.
  - Quest Log mask icons now map slot to item (item IDs are not in slot order).
  - **Waiting on the user:** does the audio still sound choppy?
- 2026-11-20: v18. Tab-switch audio chop traced to texture reloads. The texture cache held only 1024 LRU entries, which the HD pack overflows; raised to 4096 in libultraship (branch `dualscreen` in the submodule). Every page is also warmed up once. Slow frames while switching tabs went from 59 at 16 ms + 10 at 25 ms down to 12 at 16 ms.
- 2026-11-20: **OPEN ISSUE:** the user still hears choppy audio on v18. Circle back later. Leads:
  - test the original `com.twoshipfork.mm` at the same settings for a baseline
  - check whether `gInterpolationFPS` 120 vs 60 changes it
  - consider a larger audio `SampleLength`, or Android AAudio low-latency flags

  Moving on to Phase 4.
- 2026-11-20: v19–v21 `0b80ba9`. Phase 4 Map tab:
  - The pause world map (the MM Reloaded HD pack already paints it) with Tingle clouds, a pulsing marker for Link's region, and activated owls
  - Tap an owl, then "Warp" (same steps as 2S2H `PauseOwlWarp`)
  - Pinch, drag, +/- and recentre (Google Maps style); Java now sends a second pointer
  - Verified zoom, drag and recentre on the device
  - **Owl warp is untested**, because the user hasn't learned Song of Soaring yet
  - **Open question:** the user may want the clouds optional
- 2026-11-20: v22–v23. Map clouds toggle: a cloud button on the map plus a menu option, CVar `gEnhancements.DualScreen.MapClouds` (default on). Verified that the full HD map shows with clouds off.
- 2026-11-20: v24–v25. Clouds showed on only half the map: libultraship flips `gDPSetScissor` vertically inside off-screen framebuffers. Added a `Panel_SetScissor` helper that mirrors the rows. The notebook rows scissor now runs to 468, so its 7th row shows in full.
- 2026-11-20: Documented everything up to v25 in `2S2H-DualScreen-Features.md`.
  - **Remaining:** 4.3 warp test, 4.4 dungeon maps, Phase 5 (notebook descriptions, next-up strip, Inverted/Double Time), 3B.7 side quests, Phase 6 extras, and the audio issue.
- 2026-11-20: v26–v29 `9e513df`. Phases 3B, 4 and 5 done:
  - **5.1:** tap a portrait or stamp in the notebook to read the game's own text (from `msgCtx.messageTableNES`). Verified with the Bombers' description and the notebook ribbon.
  - **5.2:** a "Next" line shows the soonest unfinished appointment today for people you've met. Coded but nothing was due during the test.
  - **5.3:** the strip's timeline and countdown turn blue while the Inverted Song of Time is active (`timeSpeedOffset == -2`). Coded, not seen on the device.
  - **4.4:** inside a temple the Map tab shows a dungeon card (title, map, compass, boss key, small keys, fairies x/15). Verified with the hidden `gEnhancements.DualScreen.PreviewDungeon` CVar (1–4); the user's config was restored afterwards. The full floor-map rendering (`z_map_disp`) is deferred.
  - **3B.7:** Quest Log "Side Quests" view (Anju and Kafei, Romani Ranch, the Old Lady, the Postman) from the notebook's event flags, with Lens hints. Verified. The Masks and Collection views are verified too.
  - `ds.sh btn a|b|x|y|start` presses the Thor's controller buttons via evdev (A = BTN_SOUTH 304), so the dev loop can load the save.
  - **Still untestable:** 4.3 owl warp (needs Song of Soaring). Open issue: audio.
- 2026-11-20: v30–v32 `a0e9f52`. Phase 6:
  - Verified on the Thor: left-handed layout, reversed tabs, hiding the Notebook tab, FPS/input bar (119 FPS), and the Checks page (area list, scrolling, area detail) on a throwaway rando file in slot 2.
  - The test rando file was moved off the Thor to `~/Backups/2S2H_saves/1120_phase6/rando_test`; the user's config was restored from `~/Backups/2S2H_config_before_phase6.json`.
  - D-pad on Items/Masks moved up 16 (DPAD_Y 352) so the FPS bar doesn't cover D-Down.
  - `ds.sh btn up|down|left|right` added, but the Thor's D-pad keys don't reach the file select; a short left-stick pulse (`sendevent ... 3 1 ±32000`, 0.08 s) moves one row.
  - **Not seen yet:** 6.1 target card (needs an enemy). Open: audio, 4.3 owl warp, dungeon floor map.
- 2026-11-20: v33–v38 `671f297`. **Audio fixed** (measured, not by ear):
  - Tooling: `AudioDiag.cpp` (CVar `gDebug.AudioDiag`, logcat tag `DSAudio`) polls the SDL queue every 2 ms and logs every time it runs dry, plus 5 s summaries (samples made vs 160000 played, game frames, render time, dropped frames). `gDebug.AudioCycleTabs` (frames) switches tabs from inside the game. `tools/audiorun.sh <label> '<cvar json>'` runs one 40 s measurement on File 1. Debug switches: `gDebug.DualScreenSkipDraw`, `gDebug.DualScreenSkipPresent`, `gDebug.AudioNoCatchUp`, `gDebug.NoFrameSkip`; A/B knobs read at start: `gDebug.Audio.SampleLength/DesiredBuffered/Driver`.
  - Cause: at 120 FPS each game frame is 6 vsync slots. Drawing the panel (not the present thread) made replay 0 miss its slot, so game frames ran 52–57 ms instead of 50. Audio is synthesized once per game frame with only ~5% speed-up room (560 vs 528 samples), so the queue drained to empty, and the device played silence.
  - Results (40 s, tabs cycling every 1.5 s, 120 FPS): second screen off = 0 dropouts; on = 4–60 dropouts at 95 frames/5 s; 60 FPS = 0; panel draw skipped = 0; present skipped = 60, so the cost is the draw itself.
  - Fix 1 (`BenPort.cpp` audio thread): under a frame's worth queued, synthesize a second batch (the music keeps real time).
  - Fix 2 (`RunCommands`): keep game frames on a 50 ms schedule by dropping an in-between interpolated frame when behind; the final frame always renders.
  - After: 0 dropouts, 100 frames/5 s, queue never below ~2000 samples; ~30 dropped interpolated frames per 5 s while switching tabs, ~6 idle.
  - Later option: redraw the panel less often when nothing changes, to cut the dropped frames further.
  - User's config and save restored from `~/Backups/2S2H_saves/1120_before_audio_tests` after the tests.
- 2026-11-20: v39–v40 `526f7d9`. **4.4 dungeon floor map** done:
  - `z_map_disp.c`: `MapDisp_GetPanelInfo` / `MapDisp_DrawPanelDungeonMap` draw the pause menu's dungeon map into any display list at any zoom (same palettes, flips, mirror mode, doors, chests, boss icon), using the port's texture paths, so nothing has to be loaded outside the pause menu.
  - `DualScreenPanels.c`: the dungeon card became a map page (floor fitted to a 384x416 board, floor buttons on the left, items on the right). Tapping a floor shows it; the page follows Link again when he changes floor. The `PreviewDungeon` test CVar is gone.
  - Verified in Woodfall Temple on a temporary slot-2 copy of the user's save (owl save with `pauseSaveEntrance` = 0x3000 and the map + compass), then deleted. 1F/2F switching, chests, boss skull and Link's marker all show. Perf on this page: 0 audio dropouts, 100 game frames/5 s.
  - Note for testing: the owl save's start entrance comes from `shipSaveInfo.pauseSaveEntrance`; a regular save always starts in South Clock Town.
- 2026-11-20: v41 `03ae003`. Verification round on temporary slot-2 copies of the user's save (deleted afterwards; File 1 and config checked byte-identical to the backup):
  - **4.3 owl warp verified:** with Song of Soaring and all owls set, tapped Woodfall then Warp, and Link landed at the Woodfall owl (Tatl's Deku Scrub mark line), with the map marker moved too.
  - **5.3 Inverted Song tint verified:** with `timeSpeedOffset` -2, the countdown digits read (53, 89, 183) and the timeline (86, 125, 207), both blue.
  - **5.2 Next line verified,** and fixed: an appointment already under way now reads "Now  until 18:00  Romani" instead of its past start time.
  - **6.1 target card still unseen:** Z (left trigger, `sendevent ... 3 10 32767`) engages, but no enemy was in reach from the temple entrance. Handed to the user together with bow hold-to-aim.
  - Deleted APKs v1–v39 (~1.4 GB); v41 is current.
- 2026-11-20: v42–v51. **Target card frame + 3D portrait, and Thor gyro aiming:**
  - Target card: the dialogue-box background (`gMessageDefaultBackgroundTex`, mirrored) as its frame; the targeted actor's own model drawn again into the card (`Panel_DrawTargetPortrait`: OPA/XLU/overlay pointers redirected into the panel list, interpolation recording paused, own camera/lights, fog pushed out). Viewports are flipped vertically in off-screen framebuffers, like scissors. After the portrait, `Gfx_SetupDL39` restores the 2D state (the actor's draw left it broken). Option "Second Screen Target Portrait". Verified on Link via `gDebug.DualScreenPortraitPlayer`, and on a real enemy (Black Boe) for the name and health.
  - Gyro: 2S2H only feeds the device gyro once a gyro mapping exists. `AndroidGyro.cpp` maps it automatically when gyro aiming is on.
  - Root cause of the drift: SDL (and `enableSensor`) left the Thor's gyro at **5 Hz** (sensor service: `samplingPeriod=200000us`). libultraship now opens its own `ASensorEventQueue` with `ASensorEventQueue_registerSensor` (dlsym, API 26) at 200 Hz, and averages every event per game-frame poll.
  - Yaw is "relaxed player space": the size from the non-pitch rotation (capped at 1.75x the part around gravity), the direction from gravity (accelerometer).
  - Verified: a 95° turn flat on a table came back within 1°. The remaining in-hand mismatch is arm-vs-wrist motion (gyros can't see translation).
  - Gyro sensitivity set to 0.72 (1:1; the game's default factor is 1.38x). First-person stick Invert Y turned off (user preference). Diagnostics: `gDebug.GyroLog` (logcat `DSGyro`: rates, running yaw total, Link's aim angle).
- 2026-11-20: v52. **Resting gyro creep fixed:** with the Thor still, the gyro total held at -0.1° while Link's aim slid about 0.05°/s. `Ship_HandleFirstPersonAiming` (z_player.c) added each frame's fractional gyro turn to s16 angles, truncating toward zero, so noise walked the aim to one side (left or right depending on which side of the body you aim). The fraction now carries over to the next frame. Verified: the aim held at 173.7° for 20 s, and the user confirms no creep.
- 2026-11-20: v53–v57. **Gyro under hard motion** (test: C-Up view, Thor flat against a table edge, then rotate/shake and put it back; `gDebug.GyroLog` logs totals per stage and Link's yaw/pitch):
  - v53: "up" tracked by the gyro, corrected slowly by the accelerometer (ignoring jolts). A shake still ended **58°** off.
  - v54 diagnostics: the gyro clips at ~3.4 rad/s (~195°/s). The "relaxed" player-space formula turned clipping into +39°, against -5.5° for plain player space. v55: back to plain player space; the shake now ends 8–12° off.
  - v56–v57: turn and tilt are read from the sensor hub's **game rotation vector** (type 15, 200 Hz) as frame-to-frame heading/tilt changes (passed as per-game-frame amounts ×20), so the same orientation gives the same aim. **Pitch is verified** (-14.9° → -15.0° after rotating and shaking). **Yaw:** a table rotation comes back exactly, but a violent shake past the clip limit still ends ~30° off: the game rotation vector has no heading reference.
  - **NEXT (resume here):** correct the heading slowly toward the compass. The Thor has a qmc6308 magnetometer and a QTI "Rotation Vector" (type 11, with magnetometer). Plan: register type 11 too; each poll, ease the game-rotation heading offset toward the magnetic heading (time constant ~2–3 s), and ignore the compass while its heading changes faster than the gyro says (magnetic disturbance). Then rerun the table+shake test.
  - The user's config still has `gDebug.GyroLog` on (harmless). File 2 is still the bow test copy (Woodfall Temple).
- 2026-11-21: v58 (built, **not installed yet**, because the user was playing). **Stray fairy tracker** (user request):
  - `DualScreenFairies.cpp`, on the first look at a temple's map:
    - takes that temple's 15 stray fairies from the rando static check list (item `RI_<DUNGEON>_STRAY_FAIRY`, with flag type and flag)
    - loads every room resource (`play->roomList.romFiles[i].fileName`) and matches flags in each room's actor list: En_Elforg switch flag `(params>>9)&0x7F`, En_Box chest flag `params&0x1F`, Obj_Tsubo collectible `(params>>9)&0x7F`, Obj_Comb / Obj_Kibako / Obj_Kibako2 collectible `(params>>8)&0x7F`.
  - Collected status comes from this cycle's switch/treasure/collectible flags.
  - Map page: a pulsing icon of that temple's stray fairy at each uncollected one on the shown floor (`MapDisp_PanelWorldPoint`), a small fairy on floor buttons with some left, and "N unmarked" if any couldn't be placed.
  - Shown with the compass. Not in rando saves. Option "Second Screen Fairy Tracker" (default on).
  - **To test:** install, load the Woodfall test setup (slot-2 copy with `pauseSaveEntrance` 0x3000 and the compass), open Map. Expect up to 15 icons across 1F/2F, and check whether the enemy-held ones (Deku Baba, Skulltula) are placed or counted as unmarked.
- 2026-11-21: v60–v61.
  - **Fairy tracker verified:** all 15 Woodfall fairies placed (bubble fairies are En_Elfbub; room actor ids carry spawn flags in their top bits, so mask with 0x1FFF), on both floors, on a slot-2 copy with Woodfall's flags cleared.
  - **Timeline** (user found the thin bar lacking and asked for all four ideas):
    - New Timeline tab (sun icon): remains, today and hours left, three day rows (6:00–6:00, day and night, past dimmed, sun/moon marker for now). The notebook's appointments for met people are lane-packed bars with portraits, gold when done; tap one to read it.
    - Tap a future hour for "Skip to …" plus a **Double Time** button. It plays the song and hands the hour to 2S2H's Better Song of Double Time through `BetterSongOfDoubleTime_SetPendingTarget` (confirmed once no text box is open; expires after ~10 s). Verified: Day 2 5:08 → Final Day 20:17.
    - Strip: 8 px day/night timeline with a red "now" marker; tapping it opens the Timeline. The moon icon grows from 26 to 44 px as time runs out. Tabs shrink to 44 px when there are 8.
  - Testing rule from the user: only load the slot-2 test copy (`tools/loadtest.sh` checks File 2 is highlighted first).
- 2026-11-21: v62–v63.
  - **Timeline for a full notebook:** with all 20 people met, a day has 17–25 appointments needing up to 17 lanes. The tab now shows one day at a time (1st/2nd/Final selector, opens on today, red dot on today), sorted and packed into lanes, with drag-to-scroll and a scroll bar. Verified with all 20 met on a slot-2 copy.
  - **Quest guides** (user request): tapping a Main Quest region or a Side Quest opens its guide. It lists every step with a numbered stamp (gold when done), the next step highlighted, when it happens (`sQuestStepExtras` / `sSideStepExtras`), and up to 3 needed items (full colour once owned, faded if missing; bottles check slot 1). With the Lens on, hints appear inline (up to 4 steps), or for longer quests in a box for the tapped/next step.
  - Fixed: the Lens toggle wasn't saved (`CVarSave`).
- 2026-11-21: v64. Map tab follows Link (the user found the region-only marker too vague; asked for an arrow showing his facing, and for the switch to be a zoom level rather than a button):
  - Area view: `MapDisp_DrawPanelArea` (z_map_disp.c) draws the current room's minimap texture (`sMapDisp.minimapCurTex`, with the minimap's colour, draw type and flips) fitted to the board, then Link as a red arrow at his exact position (the red compass icon's maths), pointing his `shape.rot.y`.
  - The arrow is two shaded triangles under an orthographic projection of the panel. Panel 2D state and the view matrices are restored afterwards.
  - Zooming in (+ or pinch) follows Link (centring blends in from 1x to 1.5x, max 3x). Zooming out below 1x switches to the Termina map, zoomed on Link's region; zooming Termina past 3x switches back to the area. Areas without a map (most interiors) show Termina. Items tab icon is now the Hero's Bow (the ocarina read as Songs).
