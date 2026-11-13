/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

#ifndef CHARGER_FORCE_NO_UI
#define CHARGER_FORCE_NO_UI 0
#endif

#if !CHARGER_FORCE_NO_UI
#include <health-impl/ChargerUtils.h>
#endif

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;

#if !CHARGER_FORCE_NO_UI
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;
#endif

static constexpr const char* gInstanceName = "default";
static constexpr std::string_view gChargerArg{"--charger"};

#include <android-base/file.h>
#include <android-base/strings.h>

namespace aidl::android::hardware::health {

class HealthImpl : public Health {
  public:
    using Health::Health;

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override {
        // Fix Transsion / MediaTek kernel fuelgauge capacity reporting
        std::string qmax_str;
        int32_t qmax_mah = 0;
        if (::android::base::ReadFileToString("/sys/devices/platform/tran_battery/BATTERY_QMAX", &qmax_str)) {
            qmax_mah = std::atoi(::android::base::Trim(qmax_str).c_str());
        }

        if (qmax_mah > 0) {
            int32_t qmax_uah = qmax_mah * 1000;
            health_info->batteryFullChargeDesignCapacityUah = qmax_uah;
            if (health_info->batteryFullChargeUah <= 0 || health_info->batteryFullChargeUah < (qmax_uah / 2)) {
                health_info->batteryFullChargeUah = qmax_uah;
            }
            if (health_info->batteryChargeCounterUah < (qmax_uah / 2) &&
                health_info->batteryCapacityLevel == BatteryCapacityLevel::FULL) {
                health_info->batteryChargeCounterUah = qmax_uah;
            }
        } else if (health_info->batteryFullChargeDesignCapacityUah > 0 &&
                   health_info->batteryFullChargeDesignCapacityUah < 1000000) {
            // Fallback: kernel reported tenths of mAh (e.g. 231000 instead of 2310000 or 5000000)
            health_info->batteryFullChargeDesignCapacityUah *= 10;
        }
    }
};

#if !CHARGER_FORCE_NO_UI
class ChargerCallbackImpl : public ChargerCallback {
  public:
    using ChargerCallback::ChargerCallback;
    bool ChargerEnableSuspend() override { return true; }
};
#endif

}  // namespace aidl::android::hardware::health

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif

    // make a default health service
    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    auto binder = ndk::SharedRefBase::make<aidl::android::hardware::health::HealthImpl>(
            gInstanceName, std::move(config));

    if (argc >= 2 && argv[1] == gChargerArg) {
#if !CHARGER_FORCE_NO_UI
        // If charger shouldn't have UI for your device, simply drop the line below
        // for your service implementation. This corresponds to
        // ro.charger.no_ui=true
        return ChargerModeMain(
                binder,
                std::make_shared<aidl::android::hardware::health::ChargerCallbackImpl>(binder));
#endif

        LOG(INFO) << "Starting charger mode without UI.";
    } else {
        LOG(INFO) << "Starting health HAL.";
    }

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
