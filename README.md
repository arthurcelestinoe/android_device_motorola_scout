# Device tree for the Motorola edge 60 (scout)

```
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
```

AxionOS (Android 16, `lineage-23.2` base) device tree for the Motorola edge 60
(`scout`, XT2505, MediaTek Dimensity 7400 / MT6878).

| Component | Value |
|-----------|-------|
| SoC       | MediaTek MT6878 |
| Display   | Tianma 6.67" pOLED, 120 Hz, under-display fingerprint (Goodix) |
| Kernel    | Stock prebuilt GKI (`device/motorola/scout-kernel`) |
| Base firmware | `A171VCH.41-15` (RETBR) |

## Status

Working:

- Boot, display at 120 Hz, adaptive brightness, auto-rotate
- Calls, SMS/MMS, mobile data, VoLTE (IMS)
- Wi-Fi 2.4/5 GHz with WPA3, hotspot, Bluetooth (including call audio)
- GPS, NFC, sensors
- Camera (Aperture), photo and video, screen recording
- Fingerprint with the screen on, in AOD and with the screen off
- Double tap to wake
- Vibration, audio, fast charging, USB file transfer (MTP)
- Low idle drain (about 2 % overnight)

Known limitations:

- Play Integrity passes BASIC only. DEVICE needs hardware attestation, which
  reports the unlocked bootloader. Google Wallet tap-to-pay does not work.
- The Moto Camera app is not included (see "Optional Moto Camera" below).

## Building

1. Initialize AxionOS and add the local manifest from
   [`local_manifests/scout.xml`](local_manifests/scout.xml) to
   `.repo/local_manifests/`, then `repo sync`.
2. Apply the platform patches from [`patches/`](patches):

   | Patch | Repository | Purpose |
   |-------|-----------|---------|
   | `wpa_supplicant_8-no-wpa-version-3.patch` | `external/wpa_supplicant_8` | The Wi-Fi driver rejects `NL80211_WPA_VERSION_3`; selected by the `wifi_no_wpa_version_3` Soong flag |
   | `frameworks_base-fp-force-pointer-events.patch` | `frameworks/base` | Forward pointer events to the fingerprint HAL and drive the panel FOD HBM through the Motorola `IDisplayPanel` HAL |
   | `frameworks_base-doze-moto-gestures.patch` | `frameworks/base` | Accept the Motorola gesture sensor (not one-shot) for screen-off fingerprint and double tap |

   ```
   git -C external/wpa_supplicant_8 apply device/motorola/scout/patches/wpa_supplicant_8-no-wpa-version-3.patch
   git -C frameworks/base apply device/motorola/scout/patches/frameworks_base-fp-force-pointer-events.patch
   git -C frameworks/base apply device/motorola/scout/patches/frameworks_base-doze-moto-gestures.patch
   ```
3. Extract the proprietary blobs into `vendor/motorola/scout` by running
   `./extract-files.py` against the `A171VCH.41-15` firmware (a dump of its
   partitions or a running device on that firmware).
4. Build:

   ```
   source build/envsetup.sh
   axion scout user pico
   m bacon
   ```

Without `vendor/lineage-priv/keys` the build signs with the AOSP test keys.
Release builds use private keys from that directory.

## Installation

The bootloader must be unlocked. The port expects the stock `A171VCH.41-15`
firmware on the slot that will boot it, and it keeps the stock `boot`
partition (GKI kernel). Do not relock the bootloader while running a custom
build.

The images go to one slot: the logical partitions (`system`, `system_ext`,
`system_dlkm`, `product`, `vendor`, `vendor_dlkm`) from fastbootd, then
`init_boot`, `dtbo`, `vendor_boot`, `vbmeta` and `vbmeta_system` from the
bootloader. [`fastboot-info.txt`](fastboot-info.txt) lists the order.

After a Motorola stock rescue, boot stock and check that the IMEI and mobile
service work before going back to AxionOS. The rescue XML can erase `nvdata`.

## Optional Moto Camera

The Moto Camera app, the Motorola framework it needs and the MediaTek
`system_ext` libraries only it loads live in a separate package,
`device/motorola/scout-motcamera` with `vendor/motorola/scout-motcamera`.
The device includes it only when that directory exists, so the tree builds
without it.

## Credits

- [abdulla-li](https://github.com/abdulla-li/android_device_motorola_scout)
  and [RidgewayPlus](https://github.com/RidgewayPlus/android_device_motorola_scout)
  for the original scout tree
- [noelle](https://noelle.bar)
- LineageOS and AxionOS
