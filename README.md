 #Android Device tree for the Motorola Edge 60 Fusion (scout)

```
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
```
Credits to 
[abdulla-li](https://github.com/abdulla-li/android_device_motorola_scout)
[noelle](https://noelle.bar)

## XT2505-3 stock firmware prerequisite

The AxionOS package expects the RETBR `A171VCH.41-15` platform firmware
in every slot that will boot the port. Check the destination slot before
installation. The build running on the active slot only identifies that slot.
The OTA and fastboot plan install the Android
partitions, the boot images, DTBO, and AVB metadata. The original service
archive is used as a source for the port. Its Motorola flashing XMLs have their
own write and erase instructions and are separate from the AxionOS package.

## First boot on the test device

The verified fastboot ZIP uses the signed target-files images as one set.
The plan preserves the already-installed stock `boot` partition and its
signed GKI payload. It writes the remaining boot images and AVB metadata
before entering fastbootd. The OTA likewise omits `boot`.
The archive checks the SKU, baseband, bootloader lock, active slot, and
snapshot state before running the plan.
For this unit, the verified stock firmware is on slot `b`. When installation
is authorized, use the checkout's fastboot with `--slot=b`,
`--skip-secondary`, `--disable-super-optimization`, and `--skip-reboot` so the
recipe uses fastbootd for the logical partitions and leaves the phone available
for checks. The `update-super` operation updates shared logical partition
metadata. The unverified slot `a` is not a recovery plan.

Keep the bootloader unlocked when running AxionOS. After a Motorola stock
rescue, boot stock and check that the IMEI and mobile service are available
before returning to AxionOS. The rescue XML can erase `nvdata`.

The local userdebug build enables authenticated ADB for diagnostics and
includes the workstation's public ADB key when configured in the local keys
directory. Treat that build as private.
