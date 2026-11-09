# CPS by Rafu

A lightweight Windows desktop click automation tool with a modern control panel. Choose a rate from **1 to 60 clicks per second (CPS)**, arm the clicker, then hold the left mouse button while pointing at the target app.

> CPS by Rafu sends standard Windows mouse input. It does not increase your mouse's hardware capability, and some apps may handle simulated input differently. Use it only where automated clicking is allowed.

## Features

- Adjustable click rate from 1 to 60 CPS
- Quick rate presets: 10, 13, and 17 CPS
- Start, Stop, and emergency-stop controls
- Customizable global hotkeys
- Live session click counter and ready/armed/clicking status
- Clicks pause while the pointer is over the CPS by Rafu window
- Saves the selected CPS and hotkeys between sessions
- No extra drivers required

## Install

1. Download **CPS by Rafu Setup.exe** from the project's GitHub Releases page.
2. Run the setup program.
3. Choose whether to create a desktop shortcut, then select **Install**.

The installer installs for the current Windows account in:

```text
%LOCALAPPDATA%\Programs\CPS by Rafu
```

It creates a Start Menu shortcut and adds an uninstall entry in Windows Settings. Administrator access is not required. The installer can launch the app after installation.

## Use

1. Open CPS by Rafu and choose a CPS value with the slider or a quick preset.
2. Select **Start** or press the Start hotkey. The app is now armed.
3. Move the pointer to the target app and hold the physical left mouse button. The app sends clicks at the selected rate while you hold it.
4. Release the left button or select **Stop** to pause clicking. Use the emergency-stop hotkey to halt it immediately.

The selected CPS is the requested rate. Actual clicks received by another app can vary with Windows scheduling and that app's input handling.

### Default hotkeys

| Action | Default |
| --- | --- |
| Start / arm | `F6` |
| Stop / pause | `F7` |
| Emergency stop | `F8` |

Change hotkeys in the **Shortcuts** section. If another program already uses a hotkey, choose a different combination. Settings are saved under `%APPDATA%\CPSByRafu\settings.ini` when the app closes.

## Build the app from source

### Requirements

- Windows
- MinGW-w64 with `g++` and `windres` available on `PATH`

From the project directory, run:

```bat
build.bat
```

The script builds `CPS by Rafu.exe` from `CPS by Rafu.cpp` and the Windows resources in `app.rc`.

## Build the setup executable

The `InstallerProject` folder contains the setup program's C++ source, resources, build script, and copies of the app, icon, and readme that it embeds.

### Requirements

- Windows
- Visual Studio Build Tools with the **Desktop development with C++** workload

Open Command Prompt in `InstallerProject` and run:

```bat
build_setup.bat
```

The script creates `CPS by Rafu Setup.exe` in that folder. It uses the Visual Studio Build Tools path configured in `build_setup.bat`; update the `VCVARS` path there if Build Tools are installed elsewhere. If you rebuild the app, copy the updated `CPS by Rafu.exe` into `InstallerProject` before rebuilding the setup so the installer embeds the new version.

## Project files

| File or folder | Purpose |
| --- | --- |
| `CPS by Rafu.cpp` | Application source code |
| `build.bat` | Builds the application with MinGW-w64 |
| `app.rc`, `app.manifest`, `resource.h`, `icon.ico` | App resources and icon |
| `InstallerProject/` | Setup executable source, resources, and build script |
| `CPS by Rafu Setup.exe` | Built installer, when included in the repository or a release |

## License

No license has been selected for this project yet. Add a `LICENSE` file when you decide how others may use or distribute the code.
