#include "mobile_haptics.h"
#include "mobile_ui.h"

#include <android/log.h>
#include <chrono>
#include <mutex>

#define HAPTIC_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyHaptics", __VA_ARGS__)

namespace mobile_haptics {

namespace {

static JavaVM* sJavaVM = nullptr;
static jobject sVibratorGlobalRef = nullptr;
static jclass sVibrationEffectClass = nullptr;
static jmethodID sCancelMethod = nullptr;
static jmethodID sVibrateMillisMethod = nullptr;
static jmethodID sVibrateEffectMethod = nullptr;
static jmethodID sCreatePredefinedMethod = nullptr;
static jmethodID sCreateOneShotMethod = nullptr;
static bool sInitialized = false;

static std::mutex sHapticMutex;
static int sLastRumble = 0;
static std::chrono::steady_clock::time_point sLastRumbleTime;

static JNIEnv* getEnv() {
    if (!sJavaVM) return nullptr;
    JNIEnv* env = nullptr;
    jint res = sJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (sJavaVM->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    }
    return env;
}

// The touch controls' own clicks and ticks (Plus edition's option).
static bool isEnabled() {
    return mobile_ui::UIOverlay::get().isPlusEdition() &&
           mobile_ui::UIOverlay::get().settings().modAdvancedHaptics;
}

// The game's Wii Remote rumble on the phone.
static bool isRumbleEnabled() {
    return isEnabled() || (!mobile_ui::UIOverlay::get().isPlusEdition() && mobile_ui::UIOverlay::get().settings().vibration);
}

// The rumble is meant to go with the game, not to shake the phone: a soft
// start and a softer hold where the motor's strength can be set, sparse
// short pulses where it cannot (such a motor only runs at full strength).
static bool sHasAmplitude = false;
static const int kRumbleStartAmp = 70, kRumbleHoldAmp = 45;  // of 255

static void vibrateOneShotInternal(JNIEnv* env, int durationMs, int amplitude) {
    if (!sVibratorGlobalRef) return;

    if (sVibrateEffectMethod && sVibrationEffectClass && sCreateOneShotMethod) {
        jobject effect = env->CallStaticObjectMethod(sVibrationEffectClass, sCreateOneShotMethod, (jlong)durationMs, (jint)amplitude);
        if (!env->ExceptionCheck() && effect) {
            env->CallVoidMethod(sVibratorGlobalRef, sVibrateEffectMethod, effect);
            env->DeleteLocalRef(effect);
            if (!env->ExceptionCheck()) {
                return;
            }
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    if (sVibrateMillisMethod) {
        env->CallVoidMethod(sVibratorGlobalRef, sVibrateMillisMethod, (jlong)durationMs);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
}

static void vibratePredefinedInternal(JNIEnv* env, int effectId, int fallbackMs, int fallbackAmp) {
    if (!sVibratorGlobalRef) return;

    if (sVibrateEffectMethod && sVibrationEffectClass && sCreatePredefinedMethod) {
        jobject effect = env->CallStaticObjectMethod(sVibrationEffectClass, sCreatePredefinedMethod, (jint)effectId);
        if (!env->ExceptionCheck() && effect) {
            env->CallVoidMethod(sVibratorGlobalRef, sVibrateEffectMethod, effect);
            env->DeleteLocalRef(effect);
            if (!env->ExceptionCheck()) {
                return;
            }
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    vibrateOneShotInternal(env, fallbackMs, fallbackAmp);
}

static void cancelInternal(JNIEnv* env) {
    if (!sVibratorGlobalRef || !sCancelMethod) return;
    env->CallVoidMethod(sVibratorGlobalRef, sCancelMethod);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

} // namespace

void init(JavaVM* vm, jobject activity) {
    std::lock_guard<std::mutex> lock(sHapticMutex);
    sJavaVM = vm;
    if (!sJavaVM || !activity) return;

    JNIEnv* env = getEnv();
    if (!env) return;

    jclass activityClass = env->GetObjectClass(activity);
    if (!activityClass) return;

    jmethodID getSystemServiceMethod = env->GetMethodID(activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!getSystemServiceMethod) {
        env->DeleteLocalRef(activityClass);
        return;
    }

    jstring vibratorName = env->NewStringUTF("vibrator");
    jobject vibratorObj = env->CallObjectMethod(activity, getSystemServiceMethod, vibratorName);
    env->DeleteLocalRef(vibratorName);
    env->DeleteLocalRef(activityClass);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    if (vibratorObj) {
        sVibratorGlobalRef = env->NewGlobalRef(vibratorObj);
        env->DeleteLocalRef(vibratorObj);

        jclass vibratorClass = env->GetObjectClass(sVibratorGlobalRef);
        sCancelMethod = env->GetMethodID(vibratorClass, "cancel", "()V");
        if (env->ExceptionCheck()) env->ExceptionClear();

        sVibrateMillisMethod = env->GetMethodID(vibratorClass, "vibrate", "(J)V");
        if (env->ExceptionCheck()) env->ExceptionClear();

        jclass veClass = env->FindClass("android/os/VibrationEffect");
        if (veClass) {
            sVibrationEffectClass = (jclass)env->NewGlobalRef(veClass);
            env->DeleteLocalRef(veClass);

            sCreatePredefinedMethod = env->GetStaticMethodID(sVibrationEffectClass, "createPredefined", "(I)Landroid/os/VibrationEffect;");
            if (env->ExceptionCheck()) env->ExceptionClear();

            sCreateOneShotMethod = env->GetStaticMethodID(sVibrationEffectClass, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
            if (env->ExceptionCheck()) env->ExceptionClear();

            sVibrateEffectMethod = env->GetMethodID(vibratorClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
            if (env->ExceptionCheck()) env->ExceptionClear();

            jmethodID hasAmp = env->GetMethodID(vibratorClass, "hasAmplitudeControl", "()Z");
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (hasAmp) {
                sHasAmplitude = env->CallBooleanMethod(sVibratorGlobalRef, hasAmp) == JNI_TRUE;
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    sHasAmplitude = false;
                }
            }
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        env->DeleteLocalRef(vibratorClass);
        sInitialized = true;
        HAPTIC_LOG("Advanced Haptics initialized successfully (Vibrator: %p)", sVibratorGlobalRef);
    }
}

void shutdown() {
    std::lock_guard<std::mutex> lock(sHapticMutex);
    JNIEnv* env = getEnv();
    if (env) {
        cancelInternal(env);
        if (sVibratorGlobalRef) {
            env->DeleteGlobalRef(sVibratorGlobalRef);
            sVibratorGlobalRef = nullptr;
        }
        if (sVibrationEffectClass) {
            env->DeleteGlobalRef(sVibrationEffectClass);
            sVibrationEffectClass = nullptr;
        }
    }
    sInitialized = false;
}

void trigger(HapticType type) {
    if (!sInitialized || !isEnabled()) return;

    std::lock_guard<std::mutex> lock(sHapticMutex);
    JNIEnv* env = getEnv();
    if (!env) return;

    switch (type) {
    case HapticType::Click:
        // EFFECT_CLICK = 0
        vibratePredefinedInternal(env, 0, 22, 190);
        break;
    case HapticType::Tick:
        // EFFECT_TICK = 2
        vibratePredefinedInternal(env, 2, 12, 110);
        break;
    case HapticType::Spin:
        // EFFECT_HEAVY_CLICK = 5
        vibratePredefinedInternal(env, 5, 45, 255);
        break;
    case HapticType::RumblePulse:
        vibrateOneShotInternal(env, 120, 220);
        break;
    }
}

void updateRumble(int rumbleActive) {
    if (!sInitialized || !isRumbleEnabled()) {
        if (sLastRumble) {
            std::lock_guard<std::mutex> lock(sHapticMutex);
            JNIEnv* env = getEnv();
            if (env) cancelInternal(env);
            sLastRumble = 0;
        }
        return;
    }

    std::lock_guard<std::mutex> lock(sHapticMutex);
    JNIEnv* env = getEnv();
    if (!env) return;

    auto now = std::chrono::steady_clock::now();

    if (rumbleActive != sLastRumble) {
        if (rumbleActive) {
            // Rumble engaged
            if (sHasAmplitude) {
                vibrateOneShotInternal(env, 110, kRumbleStartAmp);
            } else {
                vibrateOneShotInternal(env, 12, 255);
            }
            sLastRumbleTime = now;
        } else {
            // Rumble disengaged: stop immediately
            cancelInternal(env);
        }
        sLastRumble = rumbleActive;
    } else if (rumbleActive) {
        // Keep renewing vibration while rumble motor remains active
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sLastRumbleTime).count();
        if (sHasAmplitude) {
            if (elapsedMs >= 90) {
                vibrateOneShotInternal(env, 130, kRumbleHoldAmp);
                sLastRumbleTime = now;
            }
        } else if (elapsedMs >= 85) {
            vibrateOneShotInternal(env, 10, 255);
            sLastRumbleTime = now;
        }
    }
}

} // namespace mobile_haptics
