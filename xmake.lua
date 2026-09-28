set_project("locket_gold_hook")
set_version("1.0.0")

set_xmakever("2.7.0")

set_allowedplats("android")
set_allowedarchs("arm64-v8a")

set_defaultmode("release")

set_languages("c17", "cxx17")

add_cflags(
    "-O3",
    "-fvisibility=hidden",
    "-fPIC"
)

add_cxxflags(
    "-O3",
    "-fvisibility=hidden",
    "-std=c++17",
    "-fPIC"
)

add_ldflags("-s")

local dobby_dir = "dobby"

target("locket_gold")
    set_kind("shared")
    set_filename("liblocket_gold.so")

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

    add_includedirs(
        "include",
        path.join(dobby_dir, "include")
    )

    add_linkdirs(
        path.join(dobby_dir, "lib", "arm64-v8a")
    )

    add_links(
        "dobby",
        "log",
        "android"
    )

    set_symbols("hidden")
    set_warnings("all")

target("locket_loader")
    set_kind("shared")
    set_filename("liblocket_loader.so")

    add_deps("locket_gold")

    add_files(
        "src/jni_loader.cpp"
    )

    add_includedirs(
        "include"
    )

    add_links(
        "log",
        "android"
    )

if is_mode("debug") then
    add_cflags("-g")
    add_cxxflags(
        "-g",
        "-DDEBUG_LOG"
    )

    set_symbols("debug")
end

if is_mode("release") then
    add_cflags("-DRELEASE_MODE")
    add_cxxflags(
        "-DRELEASE_MODE",
        "-flto"
    )

    add_ldflags("-flto")
end

set_targetdir("$(buildir)/$(mode)/$(arch)")

on_load(function(target)
    local ndk_path = os.getenv("ANDROID_NDK")

    if not ndk_path or ndk_path == "" then
        raise("ANDROID_NDK environment variable not set")
    end
end)
