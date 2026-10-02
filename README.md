# KEK Mod

A bug-fix and quality-of-life mod for **Civilization V multiplayer**. It
replaces the game's core DLL to fix crashes, desyncs and long-standing engine
bugs. It also adds small UI and convenience improvements. Vanilla gameplay is
left alone.

Works with or without [EUI](https://forums.civfanatics.com/resources/enhanced-user-interface.25370/)
(Enhanced User Interface). KEK Mod adapts its UI files to whichever one you have.

Built on [ImmoSS/Civ5-Patch](https://github.com/ImmoSS/Civ5-Patch).

> Everyone in a multiplayer game needs the **same KEK Mod version**.

---

## Windows

### Installer (recommended)

1. Download [`KekModInstaller.exe`](https://github.com/OBLASTWAR/kek-mod/raw/main/installer/KekModInstaller.exe).
2. Run it and click **INSTALL** next to KEK Mod. Optionally install EUI from
   the same window.
3. Start Civ V.

The installer finds your Steam library and updates itself. When a new KEK Mod
release is out, the **UPDATE** button turns red.

### Manual

1. Download `kekmod.<version>.zip` from the
   [latest release](https://github.com/OBLASTWAR/kek-mod/releases/latest).
2. Extract it into your game's DLC folder. The default location is

   ```
   C:\Program Files (x86)\Steam\steamapps\common\Sid Meier's Civilization V\Assets\DLC\
   ```

   You should end up with a `KEK Mod v<version>` folder there.
3. Delete any older `KEK Mod v*` folders.
4. If you use EUI, install it **first** so that a `UI_bc1` folder exists in
   the same DLC folder.
5. Run `ui_check.bat` from inside the `KEK Mod v<version>` folder. Run it
   again whenever you add or remove EUI.

---

## Linux (Steam + Proton)

KEK Mod is a Windows DLL, so Civ V must run through Proton. In Steam, right-click
**Civilization V → Properties → Compatibility** and tick **Force the use of a
specific Steam Play compatibility tool**. Then launch the game once so Proton
can set it up.

### Installer (recommended)

The installer only needs `python3`, which comes with almost every distro and
with SteamOS. Install it with one line:

```bash
curl -fsSL https://raw.githubusercontent.com/OBLASTWAR/kek-mod/main/installer/linux/civ5-mod-installer | python3 - install-self
```

After that, open **Civ V Mod Installer** from your app menu or run:

```bash
civ5-mod-installer            # full-screen installer
civ5-mod-installer status     # what's installed and whether updates exist
civ5-mod-installer --help     # all commands
```

It finds Civ V in any of your Steam libraries, including Flatpak Steam. It
installs KEK Mod and EUI, sets up the UI files and clears the game's cache.
It also updates itself. To remove the installer, run
`civ5-mod-installer uninstall-self`. Your installed mods are kept.

### Manual

1. Download `kekmod.<version>.zip` from the
   [latest release](https://github.com/OBLASTWAR/kek-mod/releases/latest).
2. Extract it into the DLC folder, usually

   ```
   ~/.local/share/Steam/steamapps/common/Sid Meier's Civilization V/Assets/DLC/
   ```

   Delete any older `KEK Mod v*` folders.
3. If you use EUI, extract it there **first** so `UI_bc1` sits next to the
   KEK Mod folder.
4. Instead of `ui_check.bat`, run the Linux version on the KEK Mod folder:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/OBLASTWAR/kek-mod/main/ui_check.sh \
     | bash -s -- "$HOME/.local/share/Steam/steamapps/common/Sid Meier's Civilization V/Assets/DLC/KEK Mod v<version>"
   ```

---

## Troubleshooting

- **Black map or missing UI after an update:** clear the cache. On Linux, run
  `civ5-mod-installer clear-cache`. On Windows, click **CLEAR GFX
  CACHE** in the installer.
- **Mod not showing in game:** make sure there's only one `KEK Mod v*` folder
  in `Assets/DLC`, then re-run `ui_check`.
- **Crashes:** KEK Mod has a built-in crash reporter. Send the report when the
  game asks you to.
