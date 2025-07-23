# Memory Leak Analysis and Fixes

## Problem
The Flutter video player plugin's Android helper was experiencing crashes after approximately 20 minutes of video playback, particularly on specific device models. This indicated memory leaks accumulating over time during extended playback sessions.

## Root Cause Analysis

### 1. Global Reference Leaks
**Problem**: JNI global references were being created but never cleaned up
- `AAssetManager_fromJava(env, env->NewGlobalRef(asset_manager_jobject))` - Global ref never deleted
- `g_media_kit_android_helper_class` global reference never cleaned up

**Impact**: Each initialization created permanent references that accumulated in memory

### 2. Thread Safety Issues
**Problem**: `MediaKitAndroidHelperOpenFileDescriptor` created detached threads
- Threads were detached immediately after creation
- No thread lifecycle management
- Potential race conditions and resource leaks

**Impact**: Thread resources could accumulate over long sessions

### 3. String Memory Leaks
**Problem**: JNI string operations were not properly cleaned up
- Multiple `GetStringUTFChars` calls in device detection
- Some `ReleaseStringUTFChars` calls could be skipped in error conditions
- No exception handling for string operations

**Impact**: String resources accumulated over time, especially during repeated device queries

### 4. Static Memory Allocation
**Problem**: `g_files_dir = new char[2048]` was never freed
**Impact**: Permanent memory allocation that couldn't be reclaimed

### 5. Missing Error Handling
**Problem**: Insufficient error checking in JNI operations
- No null pointer validation
- No exception checking
- Resource cleanup skipped on errors

## Solutions Implemented

### 1. Proper Global Reference Management
```cpp
// Added tracking variable
jobject g_asset_manager_global_ref = NULL;

// Proper cleanup function
extern "C" void MediaKitAndroidHelperCleanup() {
    if (g_asset_manager_global_ref != NULL) {
        env->DeleteGlobalRef(g_asset_manager_global_ref);
        g_asset_manager_global_ref = NULL;
    }
    if (g_media_kit_android_helper_class != NULL) {
        env->DeleteGlobalRef(g_media_kit_android_helper_class);
        g_media_kit_android_helper_class = NULL;
    }
}
```

### 2. Eliminated Thread Safety Issues
- Removed async thread creation
- Implemented synchronous file descriptor operations
- Added proper error handling and resource cleanup

### 3. Safe String Operations
```cpp
auto safeGetString = [&](const char* fieldName) -> bool {
    // Proper resource management with automatic cleanup
    const char* str_chars = env->GetStringUTFChars(jstr, NULL);
    // ... use string ...
    env->ReleaseStringUTFChars(jstr, str_chars);
    env->DeleteLocalRef(jstr);
    return true;
};
```

### 4. Memory Management
- Added `delete[] g_files_dir` in cleanup
- Added bounds checking for all buffer operations
- Added null pointer validation

### 5. Exception Handling
- Added `ExceptionCheck()` calls
- Added proper error logging
- Added resource cleanup on all error paths

## Testing Recommendations

### For Device-Specific Issues
1. Test on the specific device models that were experiencing crashes
2. Run extended playback sessions (30+ minutes)
3. Monitor memory usage with Android Profiler
4. Check for memory leaks using LeakCanary

### Memory Monitoring
```bash
# Monitor native heap
adb shell dumpsys meminfo <package_name> -d

# Monitor JNI global references
adb shell dumpsys meminfo <package_name> | grep -i "global"
```

### Integration Testing
1. Call `MediaKitAndroidHelper.cleanupJava()` when the video player is disposed
2. Monitor memory usage during repeated video playback cycles
3. Test with different video formats and durations

## Expected Results
- No memory accumulation during extended playback
- Stable memory usage patterns
- No crashes after 20+ minutes of playback
- Proper resource cleanup on app termination

## Additional Recommendations

### 1. Regular Cleanup
Consider calling cleanup functions periodically or after each video session:
```java
// In your Flutter plugin
@Override
public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
    MediaKitAndroidHelper.cleanupJava();
}
```

### 2. Memory Monitoring
Add memory usage logging to identify potential future issues:
```cpp
__android_log_print(ANDROID_LOG_DEBUG, "media_kit", 
    "Memory usage checkpoint: %s", checkpoint_name);
```

### 3. Error Recovery
Implement graceful degradation when resources are low:
- Check available memory before large allocations
- Implement fallback mechanisms for resource allocation failures

## File Changes Summary
- `native-lib.cpp`: 331 lines added, 154 lines removed (major refactoring)
- `MediaKitAndroidHelper.java`: 7 lines added (cleanup methods)

All changes maintain backward compatibility while significantly improving memory safety and stability.