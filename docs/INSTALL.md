# Installing OJU

One installer per computer. It puts the plugin where every DAW looks, so there's nothing to copy by hand.

## Windows 10 / 11

1. Close your DAW.
2. Double-click **OJU-x.y.z-Windows-Setup.exe**, then **Next** and **Install**.
3. Open your DAW. OJU is under VST3 effects, by **Made by Joseph**.

If it doesn't show up, rescan plugins:
- **Ableton Live:** Settings > Plug-ins > Rescan. "Use VST3 Plug-in System Folders" must be on.
- **FL Studio:** Options > Manage plugins > Find more plugins.
- **Reaper:** Options > Preferences > Plug-ins > VST > Re-scan.

**"Windows protected your PC"?** Click **More info**, then **Run anyway**. Windows shows this for any installer that isn't code-signed yet. It goes away once OJU is signed for release.

The installer puts the plugin in `C:\Program Files\Common Files\VST3\OJU.vst3` and the Standalone app in `C:\Program Files\Made by Joseph\OJU`. Installing a newer OJU replaces the old one, so there's no need to uninstall first. To remove it: Settings > Apps > OJU > Uninstall.

## Mac (Apple Silicon and Intel)

1. Close your DAW.
2. Double-click **OJU-x.y.z-macOS.pkg**, then **Continue** and **Install**. Enter your Mac password when asked.
3. Open your DAW:
   - **Logic Pro / GarageBand:** Audio FX > Audio Units > Made by Joseph > OJU
   - **Ableton Live / Reaper / FL Studio:** VST3 (or Audio Units) > Made by Joseph > OJU

**"Apple could not verify..." or "unidentified developer"?** The installer isn't notarised by Apple yet, so macOS asks once:
- **macOS 15 Sequoia and newer:** click **Done**. Open **System Settings > Privacy & Security**, scroll down to *"OJU-x.y.z-macOS.pkg" was blocked*, click **Open Anyway**, and enter your password.
- **macOS 14 and older:** Control-click (or right-click) the .pkg, choose **Open**, then **Open** again.

This step goes away once OJU is signed and notarised for release. The plugins it installs load normally either way.

The installer puts OJU in:
- `/Library/Audio/Plug-Ins/Components/OJU.component` (Audio Unit)
- `/Library/Audio/Plug-Ins/VST3/OJU.vst3`
- `/Applications/OJU.app`

To remove OJU, drag those three to the Trash.

**Logic doesn't list OJU?** Restart Logic. If it's still missing, open Logic Pro > Settings > Plug-in Manager, select OJU, and click **Reset & Rescan Selection**.

## After installing

Put OJU on your vocal track, press the brass eye (**Auto**), and sing or play the take. OJU reads the tempo from your DAW and shows it in the right-hand panel.

## Updating

Run the new installer over the old one. Your presets and saved sessions are kept: v1 presets still load in 2.0.
