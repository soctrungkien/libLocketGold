add_rules("mode.debug", "mode.release")

-- Khai báo package Dobby Hook từ xmake repository
add_requires("dobby")

target("locket_gold_native")
    set_kind("shared")
    set_plat("android")
    set_archs("arm64-v8a")

    -- Thêm dependency Dobby
    add_packages("dobby")

    -- Cấu hình C++ Standard
    set_languages("c++20")

    -- Thêm các file nguồn
    add_files("src/*.cpp")

    -- Thư viện hệ thống Android
    add_syslinks("log", "android")

    -- Flag tối ưu và ẩn symbols không cần thiết
    add_cxxflags("-fvisibility=hidden", "-fvisibility-inlines-hidden")
    add_ldflags("-Wl,--gc-sections")
