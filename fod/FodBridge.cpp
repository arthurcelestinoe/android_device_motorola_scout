/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// Forwards the under-display fingerprint touch to the Goodix HAL.
//
// The Goodix HAL reports halHandlesDisplayTouches, so the framework drops
// onPointerDown/onPointerUp. On stock, Motorola's system_server tells the HAL
// about the finger through IMotoFingerPrint::sendFodEvent as soon as the
// touchscreen reports it, before SystemUI illuminates the sensor and sends
// onUiReady. The goodix_ts touchscreen reports that moment as
// BTN_TRIGGER_HAPPY1 (finger down) and BTN_TRIGGER_HAPPY2 (finger up), so this
// daemon listens for those keys and forwards them in the same order.

#define LOG_TAG "fod_bridge"

#include <aidl/com/motorola/hardware/biometric/fingerprint/IMotoFingerPrint.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <android/binder_manager.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

using aidl::com::motorola::hardware::biometric::fingerprint::IMotoEventResult;
using aidl::com::motorola::hardware::biometric::fingerprint::IMotoFingerPrint;
using aidl::com::motorola::hardware::biometric::fingerprint::IMotoFodEventType;
using android::base::unique_fd;

namespace {

constexpr int kFingerDownKey = BTN_TRIGGER_HAPPY1;
constexpr int kFingerUpKey = BTN_TRIGGER_HAPPY2;
const std::string kInstance = std::string(IMotoFingerPrint::descriptor) + "/default";

bool testBit(int bit, const uint8_t* array) {
    return array[bit / 8] & (1 << (bit % 8));
}

// Finds the touchscreen that reports both FOD keys.
unique_fd openFodInput() {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/dev/input"), closedir);
    if (!dir) {
        PLOG(ERROR) << "opendir /dev/input";
        return {};
    }
    while (dirent* entry = readdir(dir.get())) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + entry->d_name;
        unique_fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
        if (fd < 0) continue;
        uint8_t keys[(KEY_MAX + 1) / 8] = {};
        if (ioctl(fd.get(), EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) continue;
        if (testBit(kFingerDownKey, keys) && testBit(kFingerUpKey, keys)) {
            char name[64] = {};
            ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name);
            LOG(INFO) << "Using " << path << " (" << name << ")";
            return fd;
        }
    }
    return {};
}

// FINGER_DOWN payload, as stock system_server (MotBiometricFingerprint
// setFodHalStateNative) builds it: one byte naming the display state the touch
// happened in. The Goodix TA ignores a FINGER_DOWN without it.
constexpr uint8_t kFodStateScreenOff = 2;  // AOD / screen off.
constexpr uint8_t kFodStateScreenOn = 3;   // Interactive.
constexpr char kBrightnessPath[] = "/sys/class/leds/lcd-backlight/brightness";

uint8_t currentFodState() {
    unique_fd fd(open(kBrightnessPath, O_RDONLY | O_CLOEXEC));
    char buf[16] = {};
    if (fd < 0 || read(fd.get(), buf, sizeof(buf) - 1) <= 0) {
        return kFodStateScreenOn;  // Enrollment, the case that matters most, is always interactive.
    }
    return atoi(buf) > 0 ? kFodStateScreenOn : kFodStateScreenOff;
}

class Bridge {
  public:
    void send(IMotoFodEventType event) {
        if (!mService) {
            ndk::SpAIBinder binder(AServiceManager_checkService(kInstance.c_str()));
            mService = IMotoFingerPrint::fromBinder(binder);
            if (!mService) {
                LOG(ERROR) << "IMotoFingerPrint not available";
                return;
            }
        }
        // The vendor implementation dereferences eventId unconditionally, so it is
        // always present; only FINGER_DOWN carries a payload on stock.
        std::vector<uint8_t> payload;
        if (event == IMotoFodEventType::FINGER_DOWN) payload.push_back(currentFodState());
        const std::optional<std::vector<uint8_t>> eventId = payload;
        IMotoEventResult result = IMotoEventResult::RESULT_ERR;
        ndk::ScopedAStatus status = mService->sendFodEvent(event, eventId, &result);
        if (!status.isOk()) {
            LOG(ERROR) << "sendFodEvent(" << toString(event) << ") failed: " << status.getDescription();
            mService.reset();  // Reconnect on the next event, e.g. after a HAL restart.
            return;
        }
        LOG(INFO) << "sendFodEvent(" << toString(event) << ", state="
                  << (payload.empty() ? -1 : payload[0]) << ") -> " << toString(result);
    }

  private:
    std::shared_ptr<IMotoFingerPrint> mService;
};

}  // namespace

int main() {
    unique_fd input;
    // The touchscreen driver may still be probing when init starts us.
    for (int i = 0; i < 60 && input < 0; i++) {
        input = openFodInput();
        if (input < 0) sleep(1);
    }
    if (input < 0) {
        LOG(ERROR) << "No input device reports the FOD keys";
        return 1;
    }

    // The panel's FOD high-brightness mode is raised by system_server only while an
    // enroll or authenticate client takes the touch (see FingerprintProvider); the
    // touchscreen reports these keys for any touch on the sensor area.
    Bridge bridge;
    bool fingerDown = false;
    pollfd pfd = {.fd = input.get(), .events = POLLIN};
    while (true) {
        if (poll(&pfd, 1, -1) < 0) {
            if (errno == EINTR) continue;
            PLOG(ERROR) << "poll";
            return 1;
        }
        input_event events[16];
        ssize_t n = read(input.get(), events, sizeof(events));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            PLOG(ERROR) << "read";
            return 1;
        }
        for (size_t i = 0; i < n / sizeof(input_event); i++) {
            const input_event& ev = events[i];
            if (ev.type != EV_KEY || ev.value != 1) continue;  // React on key press only.
            if (ev.code == kFingerDownKey && !fingerDown) {
                fingerDown = true;
                bridge.send(IMotoFodEventType::FINGER_DOWN);
            } else if (ev.code == kFingerUpKey && fingerDown) {
                fingerDown = false;
                bridge.send(IMotoFodEventType::FINGER_UP);
            }
        }
    }
}
