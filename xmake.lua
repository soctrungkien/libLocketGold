-- Locket Gold Native Hook Library - xmake.lua
-- Targets: ARM64, no root required, uses Dobby for method hooking

set_project("locket_gold_hook")
set_version("1.0.0")

-- Minimum xmake version
set_xmakever("2.7.0")

-- Target configurations
set_allowedplats("android")
set_allowedarchs("arm64-v8a")

-- Default mode (release recommended for library)
set_default_mode("release")

-- Global optimization flags
add_cflags("-O3", "-fvisibility=hidden")
add_cxxflags("-O3", "-fvisibility=hidden", "-std=c++17")
add_ldflags("-s")  -- Strip symbols in release

-- Include Dobby and common headers
set_languages("c17", "cxx17")

includes("dobby")

-- ==================== DEPENDENCIES ====================

-- Download/find Dobby hook framework
function download_dobby()
    local dobby_dir = path.join(os.getenv("XMAKE_GLOBALDIR"), "dobby")
    if not os.exists(dobby_dir) then
        print("Downloading Dobby...")
        os.exec("git clone --depth=1 https://github.com/jmpews/Dobby " .. dobby_dir)
    end
    return dobby_dir
end

-- ==================== MAIN LIBRARY TARGET ====================

target("locket_gold")
    set_kind("shared")
    set_filename("liblocket_gold.so")
    
    -- Source files
    add_files(
        "src/main.cpp",
        "src/hermes_patcher.cpp",
        "src/hook_manager.cpp",
        "src/firestore_hook.cpp",
        "src/firebase_config_hook.cpp",
        "src/mmkv_hook.cpp",
        "src/networking_hook.cpp",
        "src/revenuecat_hook.cpp",
        "src/user_data_mutator.cpp",
        "src/json_utils.cpp",
        "src/log_util.cpp"
    )
    
    -- Include directories
    add_includedirs("include", "$(env:ANDROID_NDK)/sysroot/usr/include")
    
    -- Dobby integration
    local dobby_dir = download_dobby()
    add_includedirs(path.join(dobby_dir, "include"))
    add_linkdirs(path.join(dobby_dir, "lib/arm64-v8a"))
    add_links("dobby")
    
    -- Android/NDK specific
    add_links("log", "android")
    
    -- Compiler flags for production
    set_symbols("hidden")  -- No debug symbols in .so
    set_warnings("all")
    
    on_load(function(target)
        -- Detect NDK path
        local ndk_path = os.getenv("ANDROID_NDK")
        if not ndk_path then
            raise("ANDROID_NDK environment variable not set")
        end
        target:add("ldflags", "-fPIC")
    end)

-- ==================== JNI LOADER (Optional) ====================

target("locket_loader")
    set_kind("shared")
    set_filename("liblocket_loader.so")
    set_dependenceof("locket_gold")
    
    add_files("src/jni_loader.cpp")
    add_includedirs("include", "$(env:ANDROID_NDK)/sysroot/usr/include")
    add_links("log", "android")
    
    on_load(function(target)
        local ndk_path = os.getenv("ANDROID_NDK")
        if not ndk_path then
            raise("ANDROID_NDK environment variable not set")
        end
    end)

-- ==================== BUILD MODES ====================

-- Debug: symbols + logs
if is_mode("debug") then
    add_cxxflags("-g", "-DDEBUG_LOG")
    set_symbols("debug")
end

-- Release: optimized + stripped
if is_mode("release") then
    add_cxxflags("-DRELEASE_MODE", "-flto")
    add_ldflags("-flto")
end

-- ==================== OUTPUT ====================

set_targetdir("$(buildir)/$(mode)/$(arch)")

-- ==================== INSTALL ====================

install_target("locket_gold", {installdir = "libs/$(arch)"})
install_target("locket_loader", {installdir = "libs/$(arch)"})

