// This file is a part of media_kit (https://github.com/alexmercerind/media_kit).
//
// Copyright © 2021 & onwards, Hitesh Kumar Saini <saini123hitesh@gmail.com>.
// All rights reserved.
// Use of this source code is governed by MIT license that can be found in the LICENSE file.

#include <jni.h>
#include <sys/stat.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <unistd.h>

#include <string>
#include <memory>
#include <algorithm>

// ------------------------------------------------------------
// Globals. For sharing necessary values between Java & Dart.
// ------------------------------------------------------------

JavaVM* g_jvm = NULL;
char* g_files_dir = NULL;
int8_t g_is_emulator = -1;
AAssetManager* g_asset_manager = NULL;
jclass g_media_kit_android_helper_class = NULL;
jobject g_asset_manager_global_ref = NULL;

// ------------------------------------------------------------
// Native. For access through Dart FFI.
// ------------------------------------------------------------

extern "C" __attribute__ ((visibility ("default"))) void MediaKitAndroidHelperCopyAssetToFilesDir(const char* asset_name,  char* result) {
    strcpy(result, "");

    if (g_jvm == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "JavaVM* is nullptr.");
        return;
    }
    if (g_asset_manager == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "AAssetManager* is nullptr.");
        return;
    }
    if (g_files_dir == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Files directory is nullptr.");
        return;
    }

    AAsset* asset = AAssetManager_open(g_asset_manager, asset_name, AASSET_MODE_BUFFER);

    if (asset == NULL) {
        __android_log_print(ANDROID_LOG_WARNING, "media_kit", "Asset not found: %s", asset_name);
        return;
    }

    off_t length = AAsset_getLength(asset);
    if (length <= 0) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Invalid asset length: %ld", length);
        AAsset_close(asset);
        return;
    }

    auto buffer = std::make_unique<uint8_t[]>(length);
    if (!buffer) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to allocate buffer for asset");
        AAsset_close(asset);
        return;
    }

    int32_t size = AAsset_read(asset, buffer.get(), length);

    __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset name: %s", asset_name);
    __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset size: %d", size);

    AAsset_close(asset);

    if (size != length) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to read complete asset. Expected: %ld, Read: %d", length, size);
        return;
    }

    std::string directory_out = g_files_dir;
    directory_out += "/com.alexmercerind.media_kit/";
    std::string file_name = asset_name;
    std::replace(file_name.begin(), file_name.end(), '/', '_');
    std::string output_file = directory_out + file_name;

    __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset directory: %s", directory_out.c_str());

    struct stat st;
    int32_t stat_result = stat(directory_out.c_str(), &st);
    if (stat_result == -1) {
        __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Creating asset directory...");
        if (mkdir(directory_out.c_str(), 0777) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to create asset directory");
            return;
        }
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset directory exists.");
    }

    __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset file: %s", output_file.c_str());

    // Check if file already exists and has correct size
    FILE* existing_file = fopen(output_file.c_str(), "rb");
    if (existing_file != NULL) {
        fseek(existing_file, 0, SEEK_END);
        long existing_size = ftell(existing_file);
        fclose(existing_file);
        
        if (existing_size == size) {
            __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset file exists with correct size.");
            strncpy(result, output_file.c_str(), 2047);
            result[2047] = '\0';
            return;
        } else {
            __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "Asset file exists but size mismatch, recreating...");
        }
    }

    // Create/overwrite the file
    FILE* file = fopen(output_file.c_str(), "wb");
    if (file == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to create asset file: %s", output_file.c_str());
        return;
    }

    size_t written = fwrite(buffer.get(), sizeof(uint8_t), size, file);
    fclose(file);

    if (written != (size_t)size) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to write complete asset. Expected: %d, Written: %zu", size, written);
        return;
    }

    strncpy(result, output_file.c_str(), 2047);
    result[2047] = '\0';
}

extern "C" __attribute__ ((visibility ("default"))) void* MediaKitAndroidHelperGetJavaVM() {
    return g_jvm;
}

extern "C" __attribute__ ((visibility ("default"))) char* MediaKitAndroidHelperGetFilesDir() {
    return g_files_dir;
}

extern "C" __attribute__ ((visibility ("default"))) int8_t MediaKitAndroidHelperIsEmulator() {
    return g_is_emulator;
}

extern "C" __attribute__ ((visibility ("default"))) int32_t MediaKitAndroidHelperGetAPILevel() {
    return android_get_device_api_level();
}

extern "C" __attribute__ ((visibility ("default"))) int32_t MediaKitAndroidHelperOpenFileDescriptor(const char* uri) {
    if (g_jvm == NULL || g_media_kit_android_helper_class == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "MediaKitAndroidHelperOpenFileDescriptor: Missing initialization");
        return -1;
    }
    
    __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "MediaKitAndroidHelperOpenFileDescriptor: %s", uri);
    
    JNIEnv* env = NULL;
    bool attached = false;
    jint get_env_result = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);

    __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "get_env_result = %d", get_env_result);

    if (get_env_result != JNI_OK) {
        if (g_jvm->AttachCurrentThread(&env, NULL) == JNI_OK) {
            attached = true;
            __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "JavaVM::AttachCurrentThread Success");
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "JavaVM::AttachCurrentThread Failure");
            return -1;
        }
    }

    if (env == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "env = NULL");
        return -1;
    }
    
    int32_t file_descriptor = -1;
    jstring uri_jstring = NULL;
    
    try {
        uri_jstring = env->NewStringUTF(uri);
        if (uri_jstring == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to create URI string");
        } else {
            jmethodID open_file_descriptor_method_id = env->GetStaticMethodID(g_media_kit_android_helper_class, "openFileDescriptorJava", "(Ljava/lang/String;)I");
            if (open_file_descriptor_method_id != NULL) {
                file_descriptor = env->CallStaticIntMethod(g_media_kit_android_helper_class, open_file_descriptor_method_id, uri_jstring);
                __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "file_descriptor = %d", file_descriptor);
            } else {
                __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get method ID for openFileDescriptorJava");
            }
        }
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Exception in MediaKitAndroidHelperOpenFileDescriptor");
        file_descriptor = -1;
    }
    
    // Cleanup
    if (uri_jstring != NULL) {
        env->DeleteLocalRef(uri_jstring);
    }
    
    // Check for exceptions
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        file_descriptor = -1;
    }

    if (attached) {
        g_jvm->DetachCurrentThread();
    }

    return file_descriptor;
}

extern "C" __attribute__ ((visibility ("default"))) void MediaKitAndroidHelperCloseFileDescriptor(int32_t file_descriptor) {
    close(file_descriptor);
}

extern "C" __attribute__ ((visibility ("default"))) void MediaKitAndroidHelperCleanup() {
    if (g_files_dir != NULL) {
        delete[] g_files_dir;
        g_files_dir = NULL;
    }
    
    if (g_jvm != NULL && g_asset_manager_global_ref != NULL) {
        JNIEnv* env = NULL;
        bool attached = false;
        jint get_env_result = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        
        if (get_env_result != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&env, NULL) == JNI_OK) {
                attached = true;
            }
        }
        
        if (env != NULL) {
            if (g_asset_manager_global_ref != NULL) {
                env->DeleteGlobalRef(g_asset_manager_global_ref);
                g_asset_manager_global_ref = NULL;
            }
            
            if (g_media_kit_android_helper_class != NULL) {
                env->DeleteGlobalRef(g_media_kit_android_helper_class);
                g_media_kit_android_helper_class = NULL;
            }
            
            if (attached) {
                g_jvm->DetachCurrentThread();
            }
        }
    }
    
    g_asset_manager = NULL;
    g_is_emulator = -1;
}

// ------------------------------------------------------------
// JNI. For access through platform channels.
// ------------------------------------------------------------

extern "C" JNIEXPORT jlong JNICALL
Java_com_alexmercerind_mediakitandroidhelper_MediaKitAndroidHelper_newGlobalObjectRef(JNIEnv *env, jclass, jobject obj) {
    return (jlong) (intptr_t) env->NewGlobalRef(obj);
}

extern "C" JNIEXPORT void JNICALL
Java_com_alexmercerind_mediakitandroidhelper_MediaKitAndroidHelper_deleteGlobalObjectRef(JNIEnv *env, jclass, jlong ref) {
    env->DeleteGlobalRef((jobject) (intptr_t) ref);
}

extern "C" JNIEXPORT void JNICALL
Java_com_alexmercerind_mediakitandroidhelper_MediaKitAndroidHelper_setApplicationContextNative(JNIEnv *env, jclass, jobject context) {
    // g_jvm

    if (g_jvm == NULL) {
        env->GetJavaVM(&g_jvm);
    }

    // g_is_emulator

    if (g_is_emulator == -1) {

        // https://github.com/fluttercommunity/plus_plugins/blob/ff54dc49230ee5f8b772a3326d4ff3758618df80/packages/device_info_plus/device_info_plus/android/src/main/kotlin/dev/fluttercommunity/plus/device_info/MethodCallHandlerImpl.kt#L110-L125

        g_is_emulator = 0;

        jclass build_class = env->FindClass("android/os/Build");
        if (build_class == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to find Build class");
            return;
        }

        char brand_chars[1024];
        char device_chars[1024];
        char fingerprint_chars[1024];
        char hardware_chars[1024];
        char model_chars[1024];
        char manufacturer_chars[1024];
        char product_chars[1024];

        memset(brand_chars, '\0', 1024);
        memset(device_chars, '\0', 1024);
        memset(fingerprint_chars, '\0', 1024);
        memset(hardware_chars, '\0', 1024);
        memset(model_chars, '\0', 1024);
        memset(manufacturer_chars, '\0', 1024);
        memset(product_chars, '\0', 1024);

        // Helper lambda for safe string extraction
        auto safeGetString = [&](const char* fieldName) -> bool {
            jfieldID field_id = env->GetStaticFieldID(build_class, fieldName, "Ljava/lang/String;");
            if (field_id == NULL) {
                __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get field ID for %s", fieldName);
                return false;
            }
            
            jstring jstr = (jstring)env->GetStaticObjectField(build_class, field_id);
            if (jstr == NULL) {
                __android_log_print(ANDROID_LOG_WARNING, "media_kit", "Field %s is null", fieldName);
                return false;
            }
            
            const char* str_chars = env->GetStringUTFChars(jstr, NULL);
            if (str_chars == NULL) {
                env->DeleteLocalRef(jstr);
                return false;
            }
            
            char* target_buffer = nullptr;
            if (strcmp(fieldName, "BRAND") == 0) target_buffer = brand_chars;
            else if (strcmp(fieldName, "DEVICE") == 0) target_buffer = device_chars;
            else if (strcmp(fieldName, "FINGERPRINT") == 0) target_buffer = fingerprint_chars;
            else if (strcmp(fieldName, "HARDWARE") == 0) target_buffer = hardware_chars;
            else if (strcmp(fieldName, "MODEL") == 0) target_buffer = model_chars;
            else if (strcmp(fieldName, "MANUFACTURER") == 0) target_buffer = manufacturer_chars;
            else if (strcmp(fieldName, "PRODUCT") == 0) target_buffer = product_chars;
            
            if (target_buffer != nullptr) {
                strncpy(target_buffer, str_chars, 1023);
                target_buffer[1023] = '\0';  // Ensure null termination
            }
            
            __android_log_print(ANDROID_LOG_DEBUG, "media_kit", "%s: %s", fieldName, target_buffer ? target_buffer : "null");
            
            env->ReleaseStringUTFChars(jstr, str_chars);
            env->DeleteLocalRef(jstr);
            
            return true;
        };

        // Extract all build properties safely
        safeGetString("BRAND");
        safeGetString("DEVICE");
        safeGetString("FINGERPRINT");
        safeGetString("HARDWARE");
        safeGetString("MODEL");
        safeGetString("MANUFACTURER");
        safeGetString("PRODUCT");

        if (
                (strncmp(brand_chars, "generic", strlen("generic")) == 0 && strncmp(device_chars, "generic", strlen("generic")) == 0)
                || strncmp(fingerprint_chars, "generic", strlen("generic")) == 0
                || strncmp(fingerprint_chars, "unknown", strlen("unknown")) == 0
                || strstr(hardware_chars, "goldfish") != NULL
                || strstr(hardware_chars, "ranchu") != NULL
                || strstr(model_chars, "google_sdk") != NULL
                || strstr(model_chars, "Emulator") != NULL
                || strstr(model_chars, "Android SDK built for x86") != NULL
                || strstr(manufacturer_chars, "Genymotion") != NULL
                || strstr(product_chars, "sdk_google") != NULL
                || strstr(product_chars, "google_sdk") != NULL
                || strstr(product_chars, "sdk") != NULL
                || strstr(product_chars, "sdk_x86") != NULL
                || strstr(product_chars, "vbox86p") != NULL
                || strstr(product_chars, "emulator") != NULL
                || strstr(product_chars, "simulator") != NULL) {
            g_is_emulator = 1;
        }

        env->DeleteLocalRef(build_class);
    }

    // g_files_dir

    if (g_files_dir == NULL) {

        g_files_dir = new(std::nothrow) char[2048];
        if (g_files_dir == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to allocate memory for files directory");
            return;
        }
        memset(g_files_dir, '\0', 2048);

        jclass context_class = env->GetObjectClass(context);
        if (context_class == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get context class");
            delete[] g_files_dir;
            g_files_dir = NULL;
            return;
        }
        
        jmethodID get_files_dir_method_id = env->GetMethodID(context_class, "getFilesDir", "()Ljava/io/File;");
        if (get_files_dir_method_id == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get getFilesDir method");
            env->DeleteLocalRef(context_class);
            delete[] g_files_dir;
            g_files_dir = NULL;
            return;
        }
        
        jobject files_dir_jobject = env->CallObjectMethod(context, get_files_dir_method_id);

        if (env->IsSameObject(files_dir_jobject, NULL)) {
            if (android_get_device_api_level() >= 24) {
                jmethodID get_data_dir_method_id = env->GetMethodID(context_class, "getDataDir", "()Ljava/io/File;");
                if (get_data_dir_method_id != NULL) {
                    files_dir_jobject = env->CallObjectMethod(context, get_data_dir_method_id);
                }
            } else {
                jmethodID get_application_info_method_id = env->GetMethodID(context_class, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info_method_id != NULL) {
                    jobject application_info_jobject = env->CallObjectMethod(context, get_application_info_method_id);
                    if (application_info_jobject != NULL) {
                        jclass application_info_class = env->GetObjectClass(application_info_jobject);
                        if (application_info_class != NULL) {
                            jfieldID data_dir_field = env->GetFieldID(application_info_class, "dataDir", "Ljava/lang/String;");
                            if (data_dir_field != NULL) {
                                jobject data_dir_jobject = env->GetObjectField(application_info_jobject, data_dir_field);

                                jclass file_class = env->FindClass("java/io/File");
                                if (file_class != NULL) {
                                    jmethodID file_constructor = env->GetMethodID(file_class, "<init>", "(Ljava/lang/String;)V");
                                    if (file_constructor != NULL) {
                                        files_dir_jobject = env->NewObject(file_class, file_constructor, data_dir_jobject);
                                    }
                                    env->DeleteLocalRef(file_class);
                                }

                                if (data_dir_jobject != NULL) {
                                    env->DeleteLocalRef(data_dir_jobject);
                                }
                            }
                            env->DeleteLocalRef(application_info_class);
                        }
                        env->DeleteLocalRef(application_info_jobject);
                    }
                }
            }
        }

        if (files_dir_jobject != NULL) {
            jclass file_class = env->FindClass("java/io/File");
            if (file_class != NULL) {
                jmethodID get_absolute_path_method_id = env->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
                if (get_absolute_path_method_id != NULL) {
                    jstring files_dir_jstring = (jstring)env->CallObjectMethod(files_dir_jobject, get_absolute_path_method_id);
                    if (files_dir_jstring != NULL) {
                        const char* files_dir_chars = env->GetStringUTFChars(files_dir_jstring, NULL);
                        if (files_dir_chars != NULL) {
                            strncpy(g_files_dir, files_dir_chars, 2047);
                            g_files_dir[2047] = '\0';  // Ensure null termination
                            env->ReleaseStringUTFChars(files_dir_jstring, files_dir_chars);
                        }
                        env->DeleteLocalRef(files_dir_jstring);
                    }
                }
                env->DeleteLocalRef(file_class);
            }
            env->DeleteLocalRef(files_dir_jobject);
        }
        
        env->DeleteLocalRef(context_class);
        
        // If we failed to get the files directory, clean up
        if (strlen(g_files_dir) == 0) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get files directory");
            delete[] g_files_dir;
            g_files_dir = NULL;
        }
    }

    // g_asset_manager

    if (g_asset_manager == NULL) {
        jclass context_class = env->GetObjectClass(context);
        if (context_class == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get context class");
            return;
        }
        
        jmethodID asset_manager_id = env->GetMethodID(context_class, "getAssets", "()Landroid/content/res/AssetManager;");
        if (asset_manager_id == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get getAssets method");
            env->DeleteLocalRef(context_class);
            return;
        }
        
        jobject asset_manager_jobject = env->CallObjectMethod(context, asset_manager_id);
        if (asset_manager_jobject == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "media_kit", "Failed to get asset manager");
            env->DeleteLocalRef(context_class);
            return;
        }

        // Store global reference for proper cleanup
        g_asset_manager_global_ref = env->NewGlobalRef(asset_manager_jobject);
        g_asset_manager = AAssetManager_fromJava(env, g_asset_manager_global_ref);

        env->DeleteLocalRef(asset_manager_jobject);
        env->DeleteLocalRef(context_class);
    }

    // g_media_kit_android_helper_class

    if (g_media_kit_android_helper_class == NULL) {
        g_media_kit_android_helper_class = (jclass)env->NewGlobalRef(env->FindClass("com/alexmercerind/mediakitandroidhelper/MediaKitAndroidHelper"));
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_alexmercerind_mediakitandroidhelper_MediaKitAndroidHelper_copyAssetToFilesDir(JNIEnv *env, jclass, jstring asset_name) {
    const char* asset_name_chars = env->GetStringUTFChars(asset_name, NULL);
    char result[2048];
    MediaKitAndroidHelperCopyAssetToFilesDir(asset_name_chars, result);
    env->ReleaseStringUTFChars(asset_name, asset_name_chars);
    return env->NewStringUTF(result);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_alexmercerind_mediakitandroidhelper_MediaKitAndroidHelper_openFileDescriptorNative(
        JNIEnv *env, jclass clazz, jstring uri) {
    if (g_media_kit_android_helper_class != NULL) {
        jmethodID open_file_descriptor_method_id = env->GetStaticMethodID(g_media_kit_android_helper_class, "openFileDescriptorJava", "(Ljava/lang/String;)I");
        if (open_file_descriptor_method_id != NULL) {
            jint file_descriptor = env->CallStaticIntMethod(g_media_kit_android_helper_class, open_file_descriptor_method_id, uri);
            
            // Check for exceptions
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                return -1;
            }
            
            return file_descriptor;
        }
    }
    return -1;
}

extern "C" JNIEXPORT void JNICALL
Java_com_alexmercerind_mediakitandroidhelper_MediaKitAndroidHelper_cleanup(JNIEnv *env, jclass clazz) {
    MediaKitAndroidHelperCleanup();
}
