# Shipping OJU: what's left before you sell it

The plugin, the builds and the validation are done. Here is what stands between the code and a paying customer, roughly in order.

## 1. Licences for what OJU is built on

- **JUCE**: this is required. JUCE is AGPLv3 unless you hold a commercial licence. For a closed-source paid plugin, register a JUCE licence at juce.com. The free *Starter* tier applies below its revenue cap; check the current limits. Keep the licence email with your business records.
- **VST3**: the bundled SDK (3.8) is MIT-licensed, so you don't need an agreement to ship VST3. To use the **VST logo** on your site or box, sign Steinberg's free trademark usage agreement.
- **AU**: no licence is needed.
- **RNNoise** (Denoise, OJU 2.0): BSD-3-Clause, so commercial use is fine. Include its copyright notice and licence text (`third_party/rnnoise/COPYING`) in your manual or installer.
- **Trademarks**: never call Tune "Auto-Tune" (an Antares trademark). Use "Tune" or "pitch correction".

## 2. Code signing

Unsigned plugins trigger security warnings, and on macOS they won't even load for most users.

**macOS** (required):
1. Join the Apple Developer Program ($99 a year).
2. Create two certificates: **Developer ID Application**, which signs the `.vst3`, `.component` and `.app`, and **Developer ID Installer**, which signs the `.pkg`.
3. Sign with the hardened runtime, notarise with `notarytool`, then staple. `installer/mac/make-pkg.sh` does all of this once your certificates and notary profile exist.

**Windows** (strongly recommended, otherwise SmartScreen shows "Windows protected your PC"):
- Option A: **Azure Trusted Signing**. This is a cheap monthly cloud signing service. Eligibility has been limited by region and business history, so check whether you qualify.
- Option B: an **OV or EV code-signing certificate** from a CA such as Sectigo, DigiCert or SSL.com. Since 2023 these are delivered on a hardware token or cloud HSM.
- Sign `OJU.vst3\Contents\x86_64-win\OJU.vst3`, `OJU.exe` and the installer with `signtool sign /fd sha256 /tr <timestamp-url> /td sha256 ...`.

## 3. Installers

CI builds both installers on every push (artifacts *OJU-Windows-Installer* and *OJU-macOS-Installer*). Pushing a tag such as `v2.0.1` publishes them on the GitHub Releases page.

- **Windows (Inno Setup)**: `installer/windows/OJU.iss` installs the VST3 into `C:\Program Files\Common Files\VST3` and the app into Program Files, with an uninstaller. A new version installs over the old one. To sign it, set `SignTool` in the script, or sign the output `.exe` with `signtool`.
- **macOS (pkg)**: `installer/mac/make-pkg.sh` builds one component package per format:
  - VST3 → `/Library/Audio/Plug-Ins/VST3`
  - AU → `/Library/Audio/Plug-Ins/Components`, then refreshes the AU cache
  - app → `/Applications`

  It wraps them in a product archive with OJU's welcome and finish pages. With `DEV_APP` / `DEV_INST` set, it also signs with your Developer ID, then notarises and staples. Without them it builds an unsigned test installer.
- Once both are signed, the one-time security prompts in `docs/INSTALL.md` disappear. Remove those paragraphs then.
- Ship a **PDF manual** or a manual page, and an **EULA**. Both installers can show the EULA (Inno `LicenseFile=`, pkg `<license>`).

## 4. Licence keys and copy protection

Keep it light: vocalists and producers hate dongles and always-online checks.

Recommended setup:
1. **Sell through a store that issues licence keys**:
   - *Moonbase*: built for audio plugins, handles trials and activations, and has a JUCE SDK.
   - *Lemon Squeezy* or *Paddle*: merchant of record, so they handle EU and UK VAT for you, and they have a licence-key API.
   - *Gumroad*: simple, with licence keys.
2. **Activate once, then verify offline.** The customer pastes a key, the plugin calls the store's API once, and gets back a small signed licence file. After that the plugin checks it offline with an RSA or Ed25519 public key. JUCE's `juce_product_unlocking` module (`OnlineUnlockStatus`, `KeyGeneration`) provides this pattern if you'd rather self-host.
3. **Trial or demo**, pick one:
   - A 14-day full trial.
   - Or everything free except **Listen**. The brain is the product, and manual knobs still work.

   Avoid silence gaps or noise bursts: they get into people's bounces and generate refunds.

Code hooks to add when you choose a vendor:
- An "Activate" panel in the editor.
- A licence check that runs **on the message thread only**, never in `processBlock`.
- A flag the editor reads to enable Listen.

## 5. Product page (madebyjoseph.com/oju)

What sells a plugin like this:
- **Hero**: the brass eye animating, with the line *"It listens. Then it sets itself up."*
- **Before and after audio**: one dry vocal per style (Afrobeats, Trap, R&B, Pop, Soul), plus an Extreme version. Use A/B players; this is the most persuasive thing on the page.
- **A 60-second video**: press Listen, the ring fills, The Read appears, the vocal sits in the beat.
- **Screenshot of The Read**, because it shows the product explaining itself.
- **Feature list**: auto setup, the six modules, Natural vs Extreme, tempo-synced Space, A/B, about 2 % CPU per instance, and the fact that audio never leaves the computer (analysis is local).
- **System requirements**: Windows 10/11 x64 with VST3; macOS 10.13+ on Apple Silicon or Intel with VST3/AU. Tested hosts: Reaper, FL Studio, Ableton Live, Logic Pro.
- **Price and trial button**, an FAQ (installing, activation, refunds) and a support email.
- **Launch channels**: KVR Audio product listing and news, Plugin Boutique or ADSR marketplace, YouTube producers in the Afrobeats and Trap scenes, and short-form videos of the eye listening.

## 6. Before release

- Run your final release candidate through the CI workflow, which runs the tests, pluginval (strictness 10) and auval.
- Then check it by hand in each host:
  - Reaper, FL Studio and Ableton on Windows.
  - Logic, Ableton and Reaper on macOS (Apple Silicon **and** Intel/Rosetta).
  - In each host: load, Listen, automate a knob, save and reload the project, undo a Listen, render or bounce offline.
- Test mono and stereo tracks at 44.1, 48 and 96 kHz.
- Bump the version in `CMakeLists.txt` (`project(OJU VERSION x.y.z)`) for every release. Hosts use it.
- Later: **AAX** for Pro Tools requires an Avid developer account and PACE signing. JUCE supports it with the AAX SDK.
