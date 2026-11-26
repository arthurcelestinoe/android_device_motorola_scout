/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.scout"

#include "include/UdfpsHandler.h"

#include <aidl/com/motorola/hardware/biometric/fingerprint/IMotoFingerPrint.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

using aidl::com::motorola::hardware::biometric::fingerprint::IMotoEventResult;
using aidl::com::motorola::hardware::biometric::fingerprint::IMotoFingerPrint;
using aidl::com::motorola::hardware::biometric::fingerprint::IMotoFodEventType;

namespace {

// The Goodix HAL reports halHandlesDisplayTouches, so the framework never forwards
// onPointerDown to it. On stock, Motorola's system_server tells the HAL about the
// finger through IMotoFingerPrint::sendFodEvent; the HAL then raises HBM itself
// through the Motorola display panel HAL and starts the capture. SurfaceFlinger
// calls this handler when the UDFPS layer is touched, so it forwards the same
// events from here.
const std::string kMotoFingerprintInstance =
        std::string(IMotoFingerPrint::descriptor) + "/default";

}  // namespace

class ScoutUdfpsHandler : public UdfpsHandler {
  public:
    ~ScoutUdfpsHandler() override {
        {
            std::lock_guard<std::mutex> lock(mLock);
            mStop = true;
        }
        mCv.notify_all();
        if (mWorker.joinable()) mWorker.join();
    }

    void init(fingerprint_device_t* /* device */) override {
        mWorker = std::thread([this] { workerLoop(); });
        LOG(INFO) << "Scout UDFPS handler initialized";
    }

    // SurfaceFlinger reaches this hook only after SystemUI has already sent
    // onUiReady, and it never reports the finger leaving, so it cannot drive the
    // Goodix HAL. vendor.motorola.fod_bridge forwards the touchscreen's FOD keys
    // instead; these hooks only log.
    void onFingerDown(uint32_t /* x */, uint32_t /* y */, float /* minor */,
                      float /* major */) override {
        LOG(DEBUG) << "UDFPS layer touched";
    }

    void onFingerUp() override { LOG(DEBUG) << "UDFPS layer released"; }

    void onAcquired(int32_t result, int32_t vendorCode) override {
        LOG(DEBUG) << "onAcquired: result=" << result << " vendorCode=" << vendorCode;
    }

    void cancel() override {
        mFingerDown = false;
        queue(IMotoFodEventType::CANCEL);
    }

  private:
    std::mutex mLock;
    std::condition_variable mCv;
    std::deque<IMotoFodEventType> mEvents;
    std::thread mWorker;
    bool mStop = false;
    bool mFingerDown = false;  // Only touched from the SurfaceFlinger caller thread.
    std::shared_ptr<IMotoFingerPrint> mService;

    // Never block the compositor on binder: hand the event to the worker thread.
    void queue(IMotoFodEventType event) {
        {
            std::lock_guard<std::mutex> lock(mLock);
            mEvents.push_back(event);
        }
        mCv.notify_one();
    }

    std::shared_ptr<IMotoFingerPrint> getService() {
        if (mService) return mService;
        // checkService does not wait; if the HAL is not up yet the event is dropped
        // and the next touch tries again.
        ndk::SpAIBinder binder(AServiceManager_checkService(kMotoFingerprintInstance.c_str()));
        mService = IMotoFingerPrint::fromBinder(binder);
        if (!mService) LOG(ERROR) << "IMotoFingerPrint service not available";
        return mService;
    }

    void workerLoop() {
        std::unique_lock<std::mutex> lock(mLock);
        while (true) {
            mCv.wait(lock, [this] { return mStop || !mEvents.empty(); });
            if (mStop) return;
            IMotoFodEventType event = mEvents.front();
            mEvents.pop_front();
            lock.unlock();
            send(event);
            lock.lock();
        }
    }

    void send(IMotoFodEventType event) {
        auto service = getService();
        if (!service) return;
        IMotoEventResult result = IMotoEventResult::RESULT_ERR;
        // The vendor implementation dereferences eventId unconditionally (a null one
        // aborts the HAL with bad_optional_access) and only forwards its bytes to the
        // Goodix extension command 0x600 + event, so send a present, empty payload.
        const std::optional<std::vector<uint8_t>> eventId = std::vector<uint8_t>{};
        ndk::ScopedAStatus status = service->sendFodEvent(event, eventId, &result);
        if (!status.isOk()) {
            LOG(ERROR) << "sendFodEvent(" << toString(event)
                       << ") failed: " << status.getDescription();
            mService.reset();  // Reconnect on the next event, e.g. after a HAL restart.
            return;
        }
        LOG(INFO) << "sendFodEvent(" << toString(event) << ") -> " << toString(result);
    }
};

static UdfpsHandler* create() {
    return new ScoutUdfpsHandler();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
        .create = create,
        .destroy = destroy,
};
