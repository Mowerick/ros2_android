#include "jni/bitmap_utils.h"
#include <android/bitmap.h>
#include <cstring>
#include "core/log.h"

namespace ros2_android {
namespace jni {

jobject CreateBitmapFromRGB(JNIEnv* env,
                           const uint8_t* data,
                           int width,
                           int height) {
    if (!env || !data || width <= 0 || height <= 0) {
        LOGE("CreateBitmapFromRGB: Invalid parameters");
        return nullptr;
    }

    // Find Bitmap.Config class and get ARGB_8888 constant
    jclass bitmapConfigClass = env->FindClass("android/graphics/Bitmap$Config");
    if (!bitmapConfigClass) {
        LOGE("Failed to find Bitmap.Config class");
        return nullptr;
    }

    jfieldID argb8888Field = env->GetStaticFieldID(
        bitmapConfigClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    if (!argb8888Field) {
        LOGE("Failed to find ARGB_8888 field");
        env->DeleteLocalRef(bitmapConfigClass);
        return nullptr;
    }

    jobject bitmapConfig = env->GetStaticObjectField(bitmapConfigClass, argb8888Field);
    env->DeleteLocalRef(bitmapConfigClass);

    if (!bitmapConfig) {
        LOGE("Failed to get ARGB_8888 config");
        return nullptr;
    }

    // Find Bitmap class and createBitmap method
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    if (!bitmapClass) {
        LOGE("Failed to find Bitmap class");
        env->DeleteLocalRef(bitmapConfig);
        return nullptr;
    }

    jmethodID createBitmapMethod = env->GetStaticMethodID(
        bitmapClass, "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    if (!createBitmapMethod) {
        LOGE("Failed to find createBitmap method");
        env->DeleteLocalRef(bitmapClass);
        env->DeleteLocalRef(bitmapConfig);
        return nullptr;
    }

    // Create the Bitmap object
    jobject bitmap = env->CallStaticObjectMethod(
        bitmapClass, createBitmapMethod, width, height, bitmapConfig);
    env->DeleteLocalRef(bitmapClass);
    env->DeleteLocalRef(bitmapConfig);

    if (!bitmap) {
        LOGE("Failed to create Bitmap object");
        return nullptr;
    }

    // Lock pixels for writing
    AndroidBitmapInfo info;
    void* pixels = nullptr;

    int result = AndroidBitmap_getInfo(env, bitmap, &info);
    if (result < 0) {
        LOGE("AndroidBitmap_getInfo failed: %d", result);
        env->DeleteLocalRef(bitmap);
        return nullptr;
    }

    result = AndroidBitmap_lockPixels(env, bitmap, &pixels);
    if (result < 0 || !pixels) {
        LOGE("AndroidBitmap_lockPixels failed: %d", result);
        env->DeleteLocalRef(bitmap);
        return nullptr;
    }

    // Data is in RGBA8 format: [R G B A] [R G B A] ...
    // Android ARGB_8888 bitmap expects BGRA ordering in little-endian memory
    // Direct memcpy works because RGBA in memory becomes ABGR in 32-bit word (little-endian)
    // which Android interprets as ARGB_8888
    const int byte_count = width * height * 4;
    std::memcpy(pixels, data, byte_count);

    AndroidBitmap_unlockPixels(env, bitmap);

    return bitmap;
}

} // namespace jni
} // namespace ros2_android
