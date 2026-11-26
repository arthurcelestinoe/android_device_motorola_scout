#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from scout device
$(call inherit-product, device/motorola/scout/device.mk)

# AxionOS phone configuration.
TARGET_DISABLE_EPPE := true
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)
$(call inherit-product, vendor/lineage-priv/keys/scout-apex.mk)
PRODUCT_OTA_PUBLIC_KEYS := vendor/lineage-priv/keys/releasekey
PRODUCT_SYSTEM_PROPERTIES += apexd.config.dm_create.timeout=5000

ifeq ($(TARGET_BUILD_VARIANT),userdebug)
# This Make flag is true for any nonempty value.
PRODUCT_NOT_DEBUGGABLE_IN_USERDEBUG :=
PRODUCT_SYSTEM_PROPERTIES += persist.sys.usb.config=adb
# Keep the early-boot kernel log (SCP/ADSP bring-up) instead of losing it to the
# default 256 KiB ring while bringing up the port.
PRODUCT_SYSTEM_PROPERTIES += ro.logd.size.kernel=8M
PRODUCT_ADB_KEYS := $(wildcard vendor/lineage-priv/keys/scout-adbkey.pub)
endif

PRODUCT_DEVICE := scout
PRODUCT_NAME := lineage_scout
PRODUCT_BRAND := motorola
PRODUCT_MODEL := motorola edge 60
PRODUCT_MANUFACTURER := motorola

PRODUCT_GMS_CLIENTID_BASE := android-motorola
