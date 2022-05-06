#include "../Unmanaged.hpp"
#include "PixelTransform.h"
#include "vulkan/vulkan_android.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>
#include <media/NdkImageReader.h>
#include "../haishinkit.hpp"
#include "DynamicLoader.h"

using namespace Graphics;

void *PixelTransform::OnRunning(void *data) {
    reinterpret_cast<PixelTransform *>(data)->OnRunning();
    return (void *) nullptr;
}

void PixelTransform::OnFrame(long frameTimeNanos, void *data) {
    reinterpret_cast<PixelTransform *>(data)->OnFrame(frameTimeNanos);
}

PixelTransform::PixelTransform() :
        kernel(new Kernel()),
        textures(std::vector<Texture *>(0)),
        imageReader(new ImageReader()),
        fpsController(new FpsController()) {
}

PixelTransform::~PixelTransform() {
    StopRunning();
    delete fpsController;
    delete imageReader;
    delete kernel;
}

void PixelTransform::SetFrameRate(int frameRate) {
    fpsController->SetFrameRate(frameRate);
}

void PixelTransform::SetImageExtent(int32_t width, int32_t height) {
    kernel->SetImageExtent(width, height);
}

ANativeWindow *PixelTransform::GetInputSurface() {
    return imageReader->GetWindow();
}

void PixelTransform::SetVideoGravity(VideoGravity newVideoGravity) {
    videoGravity = newVideoGravity;
    for (auto &texture: textures) {
        texture->videoGravity = newVideoGravity;
    }
}

void PixelTransform::SetImageOrientation(ImageOrientation newImageOrientation) {
    imageOrientation = newImageOrientation;
    for (auto &texture : textures) {
        texture->SetImageOrientation(newImageOrientation);
    }
}

void PixelTransform::SetResampleFilter(ResampleFilter newResampleFilter) {
    resampleFilter = newResampleFilter;
    for (auto &texture : textures) {
        texture->resampleFilter = newResampleFilter;
    }
}

void PixelTransform::SetImageReader(int32_t width, int32_t height, int32_t format) {
    auto texture = new Texture(vk::Extent2D(width, height), format);
    texture->videoGravity = videoGravity;
    texture->resampleFilter = resampleFilter;
    texture->SetImageOrientation(imageOrientation);
    textures.clear();
    textures.push_back(texture);
    imageReader->SetUp(width, height, format);
    if (kernel->surface) {
        StartRunning();
    }
}

void PixelTransform::SetDeviceOrientation(SurfaceRotation surfaceRotation) {
    kernel->SetDeviceOrientation(surfaceRotation);
}

void PixelTransform::SetAssetManager(AAssetManager *assetManager) {
    kernel->SetAssetManager(assetManager);
}

void PixelTransform::SetNativeWindow(ANativeWindow *nativeWindow) {
    if (nativeWindow == nullptr) {
        StopRunning();
    }
    kernel->SetNativeWindow(nativeWindow);
    if (nativeWindow != nullptr && !textures.empty()) {
        StartRunning();
    }
}

std::string PixelTransform::InspectDevices() {
    return kernel->InspectDevices();
}

bool PixelTransform::HasFeatures() {
    return kernel->HasFeatures();
}

void PixelTransform::SetExpectedOrientationSynchronize(bool expectedOrientationSynchronize) {
    kernel->SetExpectedOrientationSynchronize(expectedOrientationSynchronize);
}

void PixelTransform::StartRunning() {
    if (running) {
        return;
    }
    running = true;
    pthread_create(&pthread, nullptr, &OnRunning, this);
}

void PixelTransform::StopRunning() {
    if (!running) {
        return;
    }
    running = false;
    pthread_join(pthread, nullptr);
}

void PixelTransform::OnRunning() {
    looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ALooper_acquire(looper);
    choreographer = AChoreographer_getInstance();
    if (choreographer == nullptr) {
        LOGI("failed get an AChoreographer instance.");
        return;
    }
    AChoreographer_postFrameCallback(choreographer, OnFrame, this);
    while (running && ALooper_pollOnce(-1, nullptr, nullptr, nullptr));
    ALooper_release(looper);
    looper = nullptr;
    choreographer = nullptr;
}

void PixelTransform::OnFrame(long frameTimeNanos) {
    if (choreographer) {
        AChoreographer_postFrameCallback(choreographer, OnFrame, this);
    }
    if (fpsController->Advanced(frameTimeNanos)) {
        AHardwareBuffer *buffer = imageReader->GetLatestBuffer();
        if (!kernel->IsAvailable() || buffer == nullptr) {
            return;
        }
        const auto &texture = textures[0];
        texture->SetUp(*kernel, buffer);
        kernel->DrawFrame([=](uint32_t index) {
            texture->UpdateAt(*kernel, index, buffer);
            texture->LayoutAt(*kernel, index);
        });
    }
}

extern "C"
{
JNIEXPORT jboolean JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_00024Companion_isSupported(JNIEnv *env,
                                                                       jobject thiz) {
    return Graphics::DynamicLoader::GetInstance().Load();
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetVideoGravity(JNIEnv *env, jobject thiz,
                                                                  jint value) {
    Unmanaged<PixelTransform>::fromOpaque(env,
                                          thiz)->takeRetainedValue()->SetVideoGravity(
            static_cast<VideoGravity>(value));
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetSurface(JNIEnv *env, jobject thiz,
                                                             jobject surface) {
    ANativeWindow *window = nullptr;
    if (surface != nullptr) {
        window = ANativeWindow_fromSurface(env, surface);
    }
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetNativeWindow(window);
    });
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetImageOrientation(JNIEnv *env, jobject thiz,
                                                                      jint value) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetImageOrientation(static_cast<ImageOrientation>(value));
    });
}

JNIEXPORT jobject JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeCreateInputSurface(JNIEnv *env, jobject thiz,
                                                                     jint width,
                                                                     jint height, jint format) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetImageReader(width, height, format);
    });
    return ANativeWindow_toSurface(env, Unmanaged<PixelTransform>::fromOpaque(env,
                                                                              thiz)->takeRetainedValue()->GetInputSurface());
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetDeviceOrientation(JNIEnv *env, jobject thiz,
                                                                       jint value) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetDeviceOrientation(static_cast<SurfaceRotation>(value));
    });
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetResampleFilter(JNIEnv *env, jobject thiz,
                                                                    jint value) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetResampleFilter(static_cast<ResampleFilter>(value));
    });
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetAssetManager(JNIEnv *env, jobject thiz,
                                                                  jobject asset_manager) {
    AAssetManager *manager = AAssetManager_fromJava(env, asset_manager);
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetAssetManager(manager);
    });
}

JNIEXPORT jstring JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_inspectDevices(JNIEnv *env, jobject thiz) {
    std::string string = Unmanaged<PixelTransform>::fromOpaque(env,
                                                               thiz)->takeRetainedValue()->InspectDevices();
    return env->NewStringUTF(string.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeIsSupported(JNIEnv *env, jobject thiz) {
    if (!DynamicLoader::GetInstance().Load()) {
        return false;
    }
    return Unmanaged<PixelTransform>::fromOpaque(env, thiz)->takeRetainedValue()->HasFeatures();
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeDispose(JNIEnv *env, jobject thiz) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->release();
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetImageExtent(JNIEnv *env, jobject thiz,
                                                                 jint width,
                                                                 jint height) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetImageExtent(width, height);
    });
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetExpectedOrientationSynchronize(JNIEnv *env,
                                                                                    jobject thiz,
                                                                                    jboolean expectedOrientationSynchronize) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetExpectedOrientationSynchronize(expectedOrientationSynchronize);
    });
}

JNIEXPORT void JNICALL
Java_com_haishinkit_vulkan_VkPixelTransform_nativeSetFrameRate(JNIEnv *env, jobject thiz,
                                                               jint frameRate) {
    Unmanaged<PixelTransform>::fromOpaque(env, thiz)->safe([=](PixelTransform *self) {
        self->SetFrameRate(frameRate);
    });
}
}