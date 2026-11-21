# MMDS: Majora's Mask on two screens

**MMDS** turns the second screen of a dual-screen Android handheld into a Majora's Mask 3D-style touch screen, built on [2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian) (2S2H). It was made for and tested on the **AYN Thor**.

Everything on the second screen is drawn with the game's own art and font:

- **Items and Masks:** tap or drag to equip; touch a button to use it.
- **Songs:** a music guide; tap a song to play it.
- **Map:** the area you're in, with an arrow for Link; zoom out to Termina, tap an owl to warp. Inside temples, the floor map with chests, the boss room and a stray fairy tracker.
- **Quest Log:** main and side quests with step-by-step guides, masks and collectibles, and hints behind the Lens of Truth.
- **Bombers' Notebook:** the real notebook, scrollable, with every entry readable.
- **Timeline:** each day's appointments; tap a later hour to skip there with the Song of Double Time.
- **Target info:** the Z-targeted enemy's name, health and a live 3D portrait.
- **Also:** a moon countdown, gyro aiming on the device's own gyroscope, an FPS/input display, and a randomizer check tracker.

**You need your own Majora's Mask ROM.** No game data is included in this repository or in its releases.

- **Install, mods and settings:** [docs/dualscreen/SETUP.md](docs/dualscreen/SETUP.md)
- **Every feature:** [docs/dualscreen/FEATURES.md](docs/dualscreen/FEATURES.md)
- **Development history and open items:** [docs/dualscreen/ROADMAP.md](docs/dualscreen/ROADMAP.md)

The app installs as **"2 Ship Dual Screen"** (`com.twoshipfork.mm.dualscreen`), next to the regular 2S2H app, and shares its `/sdcard/2S2H` folder (saves, settings, mods).

### Credits

- [HarbourMasters](https://github.com/HarbourMasters/2ship2harkinian): 2 Ship 2 Harkinian and libultraship
- [Waterdish](https://github.com/Waterdish/2ship2harkinian-Android): the original Android port
- [linkzenic](https://github.com/linkzenic/2ship2harkinian-Android): the Android fork this builds on
- [GhostlyDark](https://github.com/GhostlyDark/MM-Reloaded-2S2H): MM Reloaded, the HD texture pack the second screen was designed around (installed separately)

The upstream Android port's README follows.

---

# 2 Ship 2 Harkinian Android

Android port of 2 Ship 2 Harkinian, based on the HarbourMasters project and forked from Waterdish's original Android port.

Original repository: https://github.com/HarbourMasters/2ship2harkinian

Original port: https://github.com/Waterdish/2ship2harkinian-Android

Current Android release: **v5.0.1-android.2**

Supported: Android 7+ with OpenGL ES 3.0+

Tested on: Android 13

## Installation

1. Install the APK from the releases page: https://github.com/linkzenic/2ship2harkinian-Android/releases
2. Open the app once so it can create the data folder and copy bundled support files.
3. When prompted, select your legally obtained `MM.z64` ROM so the app can generate `mm.o2r`.
4. Subsequent launches should start directly into the game.

Use the Back, Select, or minus controller button, or the Android back gesture/button, to open the 2 Ship 2 Harkinian menu. Use touch controls or a controller to navigate menus.

## Data Folder

The app stores user data in the selected 2S2H data folder. You can view the current folder and change it from Settings > General.

Mods and user preset files should be placed in the relevant folders inside the selected data folder.

## FAQs

**What is different with this fork?**
Special attention to Android specific needs.
 - Move the data folder to an SD card
 - Turn touch controls on or off
 - Scalable menu sizes

**Why is it immediately crashing?**

Try deleting and regenerating `mm.o2r` from your own ROM.

**My controller is not doing anything.**

Open the menu and check Settings > Controls to confirm the controller is detected and mapped.

**Can I hide the on-screen touch controls?**

Yes. Use Settings > Touch Controls > Disable Touch Controls.

**Can I resize the menu?**

Yes. Use Settings > General > Menu Scale.

## Known Issues

Orientation lock is limited by SDL behavior on Android: https://github.com/libsdl-org/SDL/issues/6090

Near-plane clipping can occur when the camera is close to walls.
