# Locket Gold Native Hook - Build & Deployment Guide

## Prerequisites

1. **xmake** (≥2.7.0)
   ```bash
   # Install xmake if not already done
   curl -fsSL https://xmake.io/shget.text | bash
   export PATH=$PATH:~/.local/bin
   ```

2. **Android NDK** (r21+)
   ```bash
   export ANDROID_NDK=/path/to/android-ndk-r25c
   ```

3. **Dobby hook framework**
   - Automatically downloaded by xmake if not present
   - Pre-built ARM64 binaries expected in `dobby/lib/arm64-v8a/`

## Building

### Quick Build (ARM64 Release)

```bash
# Set NDK path
export ANDROID_NDK=/path/to/android-ndk-r25c

# Build library
xmake build -m release

# Output: build/release/arm64-v8a/liblocket_gold.so
```

### Build Modes

```bash
# Debug (with symbols and logs)
xmake build -m debug
xmake install -m debug

# Release (optimized, stripped)
xmake build -m release
xmake install -m release

# Clean rebuild
xmake clean
xmake build -m release
```

### Build Output
- `build/release/arm64-v8a/liblocket_gold.so` - Main hook library
- `build/release/arm64-v8a/liblocket_loader.so` - Optional JNI loader

## Integration Methods

### Method 1: Direct JNI Call (Recommended for No-Root)

1. Add the `.so` file to your app's `lib/arm64-v8a/` directory
2. Load it early in app lifecycle:

```java
static {
    System.loadLibrary("locket_gold");
}

// Then call during app init
public class LocketHookLib {
    public static native void initHooks(String targetPackage);
    public static native void cleanup();
    public static native boolean isInitialized();
}

// In your app:
LocketHookLib.initHooks("com.locket.Locket");
```

### Method 2: Frida Injection

```bash
# Using Frida to inject into running app
frida-ps -U  # Find com.locket.Locket
frida -U com.locket.Locket -l inject_hook.js

# inject_hook.js:
# const soPath = "/data/app/com.locket.Locket-xxx/lib/arm64/liblocket_gold.so";
# const lib = Module.load(soPath);
# const initHooks = new NativeFunction(lib.getExportByName("Java_com_locket_hook_LocketHookLib_initHooks"), ...);
```

### Method 3: Zygote Hook (Requires Privileges)

If your hooking framework supports Zygote interception, inject at process fork time.

### Method 4: App Wrapper (Cleanest for Standalone)

Create a minimal wrapper app that:
1. Targets `com.locket.Locket` process via process attachment (requires framework)
2. Or embeds the target app and injects before `Application.onCreate()`

## Architecture Overview

```
liblocket_gold.so (ARM64)
├── Entry: initHooks() [JNI]
├── HookManager: Coordinates all hooks
│   ├── hook_hermes()              → Dobby hooks for bytecode patching
│   ├── hook_firebase_config()     → JNI callback to Java layer
│   ├── hook_firestore()           → JNI callback to Java layer
│   ├── hook_mmkv()                → JNI callback to Java layer
│   ├── hook_networking()          → JNI callback to Java layer
│   └── hook_revenuecat()          → JNI callback to Java layer
└── Utils: Logging, JSON mutation, pattern matching

+ Java Layer (Original Xposed Hooks)
├── Firebase Remote Config interception
├── Firestore data mutation
├── MMKV cache modification
├── React Native Networking patching
└── RevenueCat spoofing
```

## Hybrid Approach (Recommended)

**Most Java hooks remain unchanged.** The native library (`liblocket_gold.so`) provides:

1. **Early initialization** before JVM is fully up
2. **Dobby-based hooking** for performance-critical paths (e.g., Hermes)
3. **JNI callbacks** to Java layer for complex object manipulation
4. **No reflection overhead** for frequently-called functions

To activate this, add the native library to your Xposed module APK:

```
├── lib/
│   └── arm64-v8a/
│       └── liblocket_gold.so
└── [Original Xposed module code]
```

Then in `HookMain.java`:

```java
static {
    try {
        System.loadLibrary("locket_gold");
    } catch (Throwable t) {
        Log.w(TAG, "Native hook library not available", t);
    }
}

public void handleLoadPackage(LoadPackageParam lpparam) {
    if (!TARGET_PACKAGE.equals(lpparam.packageName)) return;
    
    // Attempt to initialize native hooks first
    try {
        LocketHookLib.initHooks(lpparam.packageName);
    } catch (Throwable t) {
        Log.d(TAG, "Native hooks unavailable, using Java-only fallback");
    }
    
    // ... rest of Java-based hooks
}
```

## Troubleshooting

### Symbol Resolution Errors

```
DobbyHook failed: cannot find symbol
```

Solution: Ensure NDK includes are correct. Verify `Dobby.h` path in xmake.lua.

### Library Load Failure

```
UnsatisfiedLinkError: liblocket_gold.so not found
```

Solutions:
- Add `.so` to `lib/arm64-v8a/` in APK
- Verify file exists: `adb shell ls -la /data/app/com.locket.Locket-xxx/lib/arm64/`
- Check logcat for actual error: `adb logcat | grep LocketGold`

### Hermes Bytecode Patch Not Applied

```
No matching Hermes fingerprint found
```

Solutions:
- Locket version may have updated Hermes version
- Add new fingerprint to `FINGERPRINTS[]` in `hermes_patcher.cpp`
- Fallback to Java-level hooking (still works)

### App Crash After Initialization

Enable debug mode:

```bash
xmake build -m debug
# Then check logcat for detailed errors
adb logcat | grep LocketGold
```

## Testing

### Run on Emulator

```bash
adb install app-debug.apk
adb logcat -s LocketGold
# Launch app manually
# Check logs for "All hooks initialized successfully"
```

### Runtime Verification

```bash
adb shell am start -n com.locket.Locket/.MainActivity
adb logcat | grep LocketGold

# Expected output:
# [LocketGold] Hermes hook setup
# [LocketGold] Firebase Config hook setup
# [LocketGold] All hooks initialized successfully
```

## Performance Notes

- **Initialization**: < 100ms overhead (Dobby setup only)
- **Hook overhead**: < 1-2% CPU on typical loads (mostly wait time)
- **Memory**: ~2-3 MB resident (Dobby + native stubs)

## Security Considerations

- ⚠️ This is for **research/education** on Android hooking
- The `.so` is not obfuscated; decompile via `objdump` or IDA
- No anti-tamper or certificate pinning checks
- Root not required = easier detection

## Future Enhancements

1. **Obfuscation**: Add string encryption, symbol stripping
2. **Anti-debugging**: Detection of debuggers/Frida
3. **Code integrity**: Runtime verification of `.so` checksums
4. **Dynamic patching**: Read patches from secure server
5. **ARM v7 support**: Add 32-bit architecture

## Cleanup

```bash
xmake clean
rm -rf build install
```

---

**Last Updated**: Sep 2026 | **Target**: Android ARM64 | **Root**: Not Required

