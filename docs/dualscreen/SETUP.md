# MMDS: Majora's Mask on two screens (2S2H Dual Screen)

This guide covers installing the dual-screen build of 2 Ship 2 Harkinian, the mods it was built around, and the settings to use. It was written for the AYN Thor; any Android device that exposes its second screen as a presentation display should work the same way.

What the second screen does is described in [FEATURES.md](FEATURES.md). Development history and open items are in [ROADMAP.md](ROADMAP.md).

---

## 1. What you need

- **The device:** an AYN Thor (Android 13), or another Android device with a second display.
- **Your own Majora's Mask ROM:** the N64 game as a `.z64` file, dumped from a cartridge you own. Supported versions are listed in [`docs/supportedHashes.json`](../supportedHashes.json). No game data is included in this repository or the app.
- **The app:** "2 Ship Dual Screen" (`com.twoshipfork.mm.dualscreen`). Either use a built APK (`2Ship-DualScreen-vNN.apk`) or build it yourself (section 5).

---

## 2. Install

1. **Copy the APK to the device and install it.** You can use the device's file manager, or a PC with USB or wireless debugging:
   ```bash
   adb install -r 2Ship-DualScreen-v41.apk
   ```
   It installs **next to** the regular 2S2H app (`com.twoshipfork.mm`) and doesn't replace it.
2. **Open "2 Ship Dual Screen".** On first launch it asks for storage access, then for your ROM. Pick your `.z64` file. The app copies it to `/sdcard/2S2H/MM.z64` and builds its game data (`mm.o2r`) from it. This takes a minute or two the first time.
3. **Check the second screen.** The bottom screen should show the Zelda / Majora's Mask title page. If it stays black, see Troubleshooting.

### Coming from the regular 2S2H app

Both apps use the same folder, `/sdcard/2S2H`. Your saves, settings and mods carry over without copying anything.

**Run only one of the two apps at a time.** They write the same save and settings files.

### Where things live

| Path | What it is |
|---|---|
| `/sdcard/2S2H/MM.z64` | your ROM |
| `/sdcard/2S2H/mm.o2r`, `2ship.o2r` | game data built from the ROM, and the port's own assets |
| `/sdcard/2S2H/mods/` | mod packs (`.o2r` / `.otr`) |
| `/sdcard/2S2H/saves/` | saves (`file1.json` …) |
| `/sdcard/2S2H/2ship2harkinian.json` | all settings |

**Back up `saves/` now and then.** Copying `file1.json` somewhere safe takes seconds and saved this project once already.

---

## 3. Mods

Put every pack in `/sdcard/2S2H/mods/`, then turn on **Enhancements → Graphics → Use Alternate Assets** (`gEnhancements.Mods.AlternateAssets`). Packs load in alphabetical order, so a pack whose name starts with `zz_` wins over the others.

| Pack | Why | Where to get it |
|---|---|---|
| **MM Reloaded HD** (v11.0.3 tested, `MM_Reloaded_v11.0.3_HD.o2r`, ~2.9 GB) | HD textures for the whole game. The second screen uses its art too: its painted world map is what the Map tab shows, and its HUD and notebook textures appear on several pages. | GhostlyDark's [MM-Reloaded-2S2H](https://github.com/GhostlyDark/MM-Reloaded-2S2H) (the 2S2H `.o2r` build); project page: [evilgames.eu](https://evilgames.eu/texture-packs/mm-reloaded.htm) |
| **Button letters** (`zz_button_letters.o2r`, optional) | HUD button letters matching the Thor's Nintendo-style layout (X / Y / RB in place of the N64's C buttons). The second screen already uses Thor names on its own. | Your own pack. Keep the `zz_` prefix so it loads last. |

The build also runs without any mods. The pages then show the original N64 art instead.

### Optional: replacement music

2S2H can replace music, fanfares and sound effects with your own MP3 / OGG / FLAC / WAV files:
1. Pack the files into an `.o2r` with [Future](https://github.com/louist103/future/) (Create OTR/O2R → Custom Audio → Streamed).
2. Put the `.o2r` in `mods/`.
3. Assign tracks in **Enhancements → Audio Editor**.

The steps are in [`docs/CUSTOM_STREAMED_MUSIC.md`](../CUSTOM_STREAMED_MUSIC.md). Use only audio you have the rights to.

---

## 4. Settings

Open the 2S2H menu in game (the menu button in the top screen's touch overlay).

### Second screen (Enhancements, next to "Bow Reticle")

| Option | Default | Recommended |
|---|---|---|
| Second Screen Panels | on | **on**: everything on the bottom screen |
| Second Screen Map Clouds | on | on: hides regions until you buy their map from Tingle, as the pause menu does (also the map's cloud button) |
| Use Items From Second Screen | on | **on**: touching an equipped button presses it in game |
| Second Screen Target Info | on | on: name and health of the Z-targeted enemy |
| Second Screen FPS and Input | off | off: frame rate and held buttons on the bottom screen, for testing |
| Second Screen Left-Handed | off | your choice: puts the controller on the left of the Items and Masks pages and reverses the tabs |
| Second Screen Tab: Items / Masks / Songs / Map / Quest Log / Notebook / Checks | on | hide any tab you don't use (Checks only appears in randomizer saves) |

### Needed for parts of the second screen

| Setting | Why |
|---|---|
| **Enhancements → Dpad Equips** (`gEnhancements.Dpad.DpadEquips`) **on** | The four D-pad slots on the Items and Masks pages only exist with this on. |
| **Use Alternate Assets on**, with MM Reloaded installed | The HD world map on the Map tab. |

### Frame rate

**Settings → Graphics → Current FPS: 120** (the interpolation slider), matching the Thor's 120 Hz top screen. This is what the build was tuned and measured at.
- At 120, the game skips an in-between frame now and then to stay on time, so the game and its audio never fall behind.
- At 60, it never needs to skip, and battery life is better.

Older builds (before v38) had choppy audio at 120 FPS while the second screen was on; that's fixed.

### The setup this was developed on (optional, quality of life)

These aren't needed for the second screen; they're what the author plays with.

| Area | Settings |
|---|---|
| Saving | Autosave, Pause Save, Persistent Owl Saves |
| Speed | Fast Text, Faster Scene Transitions, Fast Mask Transformation, Skip to File Select |
| Ocarina and songs | D-Pad Ocarina, No Dropped Ocarina Input, Better Song of Double Time, Auto Bombers' Code |
| Camera and aiming | Free Look, Gyro Aiming in first person, Fix Targeting Camera Snap, Bow Reticle |
| Graphics | Fix Scene Geometry Seams, Actor Culling Accounts for Widescreen |
| HUD | The buttons rearranged with the HUD Editor to match the Thor's face buttons |

**One reminder with Autosave on:** progress made during a three-day cycle is only kept permanently once you play the **Song of Time**. If the moon falls, the game goes back to your last Song of Time, and autosave then saves that reset over your file. Keep backups of `saves/`.

---

## 5. Building from source

This repository is a fork of [linkzenic/2ship2harkinian-Android](https://github.com/linkzenic/2ship2harkinian-Android). The dual-screen work is on `main`. The `libultraship` submodule points at this repository's `libultraship-dualscreen` branch.

```bash
git clone --recursive git@github.com:gustavoarechigajr/MMDS.git 2ship
```

The build was done on Linux (a Steam Deck) with the scripts in [`tools/dualscreen/`](../../tools/dualscreen/). They expect the checkout at `~/android-dev/2ship`:

1. **One-time toolchain setup** (JDK 17, Android SDK 33, NDK 26.0.10792818, CMake 3.30.3, into `~/android-dev`): run `tools/dualscreen/setup.sh`.
2. **Build:** run `tools/dualscreen/build.sh`. It also prepares vcpkg's Android libraries on first run. The APK lands in `Android/app/build/outputs/apk/release/app-release.apk`, built as package `com.twoshipfork.mm.dualscreen` with the label "2 Ship Dual Screen".
3. **The dev loop:** `ds.sh` (`build`, `install`, `shot`, `tap`, `swipe`, `btn`, `log`). It builds, installs and captures screenshots of either screen over ADB. `audiorun.sh` runs a 40-second audio measurement (see the roadmap's progress log).

The code lives in `mm/2s2h/Enhancements/DualScreen/`:
- `DualScreen.cpp`: the display, touch input and presenting to the second screen
- `DualScreenPanels.c`: every page
- `DualScreenRando.cpp`: randomizer checks
- `AudioDiag.cpp`: audio diagnostics

It also adds small hooks in `BenPort.cpp`, `z_play.c`, `z_map_disp.c` and the pause menu, and Java changes in `Android/app/src/main/java/com/twoshipfork/mm/` (`DualScreenManager`, `DualScreenPresentation`).

---

## 6. Troubleshooting

| Problem | Try |
|---|---|
| Bottom screen black | Check **Second Screen Panels** is on, then fully close and reopen the app. Only one 2S2H app should be running. |
| Map tab shows plain N64 art | Install MM Reloaded HD and turn on **Use Alternate Assets**. |
| No D-pad slots on Items/Masks | Turn on **Dpad Equips**. |
| Notebook tab missing | It appears once Link has the Bombers' Notebook, as in the original. |
| Checks tab missing | It only shows in randomizer saves. |
| Audio crackles | Make sure you're on v38 or newer. To measure it, set `"gDebug": { "AudioDiag": 1 }` under `CVars` in `2ship2harkinian.json` and read `adb logcat -s DSAudio`: `dry=0` means no dropouts. |
| Lost progress after the moon fell | Expected Majora's Mask behaviour: restore a backup of `saves/file1.json`, then play the Song of Time to keep what you've earned. |
