# MediaKit Android Helper

This Android helper provides JNI bridge functionality for the media_kit Flutter plugin.

## Recent Updates

### Memory Leak Fixes (2024)

Fixed critical memory leaks that were causing crashes after approximately 20 minutes of video playback:

- **Global Reference Management**: Proper cleanup of JNI global references
- **Thread Safety**: Eliminated problematic async thread creation
- **String Resource Management**: Safe string operations with proper cleanup
- **Memory Allocation**: Proper cleanup of static memory allocations
- **Error Handling**: Comprehensive exception handling and resource cleanup

### Usage

#### Basic Integration
```java
// Initialize the helper (typically in Application.onCreate)
MediaKitAndroidHelper.setApplicationContextJava(getApplicationContext());
```

#### Cleanup (Important for Memory Management)
```java
// Call this when your app is being destroyed or video player is disposed
MediaKitAndroidHelper.cleanupJava();
```

#### File Descriptor Operations
```java
// Open file descriptor for media files
int fd = MediaKitAndroidHelper.openFileDescriptorNative(uri);

// Close when done
MediaKitAndroidHelper.closeFileDescriptor(fd);
```

### Memory Management Best Practices

1. **Always call cleanup**: Ensure `MediaKitAndroidHelper.cleanupJava()` is called when:
   - App is being destroyed
   - Video player is disposed
   - Switching between different media sessions

2. **Monitor memory usage**: Use Android Profiler to verify memory usage remains stable during extended playback

3. **Handle errors gracefully**: The helper now includes comprehensive error handling, but always check return values

### Testing Extended Playback

To verify the memory leak fixes:
```bash
# Monitor memory usage
adb shell dumpsys meminfo <your.package.name> -d

# Run the validation script
./validate_memory_fixes.sh
```

### Integration with Flutter Plugin

For Flutter plugin developers using this helper:

```dart
// In your plugin's dispose method
@override
void dispose() {
  // Call cleanup before disposing
  MediaKitAndroidHelper.cleanupJava();
  super.dispose();
}
```

## Files

- `MediaKitAndroidHelper.java` - Java JNI interface
- `native-lib.cpp` - C++ implementation with memory leak fixes
- `MEMORY_LEAK_ANALYSIS.md` - Detailed analysis of fixes
- `validate_memory_fixes.sh` - Testing validation script

## Build

Requires Android SDK and NDK. The project uses CMake for native code compilation.

```bash
./gradlew build
```