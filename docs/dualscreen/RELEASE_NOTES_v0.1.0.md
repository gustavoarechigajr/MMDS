# MMDS v0.1.0 (preview)

The first public build of MMDS: Majora's Mask with an MM3D-style second screen, for dual-screen Android handhelds. Built on 2 Ship 2 Harkinian 5.0.1 (linkzenic's Android fork, 5.0.1-android.3).

**You need your own Majora's Mask ROM (`.z64`).** No game data is included.

## Install

1. Download `MMDS-v0.1.0.apk` below and install it.
2. Open **2 Ship Dual Screen** and pick your ROM when asked.

It installs next to the regular 2S2H app and shares `/sdcard/2S2H` with it, so your saves, settings and mods carry over. **Run only one of the two at a time.**

The full guide (recommended mods and settings) is in [docs/dualscreen/SETUP.md](SETUP.md).

## What's on the second screen

- **Items and Masks:** tap or drag to equip; touch a button to use it. Left-handed layout available.
- **Songs:** a music guide; tap a song to play it.
- **Map:**
  - the area you're in, with Link's position and facing
  - zoom out to Termina; tap an owl statue to warp
  - in temples: the floor map, chests, the boss room and a stray fairy tracker
- **Quest Log:** main and side quests with step-by-step guides (when, and which items you still need), masks, collectibles, and hints behind the Lens of Truth.
- **Bombers' Notebook:** the real notebook; tap a portrait or stamp to read it.
- **Timeline:** each day's appointments; tap a later hour to skip there with the Song of Double Time.
- **Target info:** name, health and a live 3D portrait of the Z-targeted enemy.
- **Strip:** day, time, a day/night timeline and the moon countdown.
- **Randomizer saves:** a check tracker tab.

## Also in this build

- **Gyro aiming** uses the device's own gyroscope, read at 200 Hz with player-space turning. Set up automatically when First Person → Gyro Aiming is on.
- **Audio no longer crackles** at 120 FPS with the second screen on.
- **The main screen's minimap is hidden**, since the Map tab replaces it (option).

## Known issues

- **Gyro after very hard shaking:** shaking the device faster than its gyroscope can measure (about 195°/s) can leave left/right aim off. A compass correction is planned. Normal aiming is accurate.
- **Tested only on the AYN Thor.** Other devices that show their second screen as a presentation display should work, but are untested.
- **Diagnostics in the build:** audio and gyro diagnostics are compiled in, but off by default.
- **Upgrading from an earlier personal build:** this release is signed with a new key, so uninstall the old "2 Ship Dual Screen" first. Saves and settings live in `/sdcard/2S2H` and are not affected.
