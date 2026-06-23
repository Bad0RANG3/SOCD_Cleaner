cmake_minimum_required(VERSION 3.14)
project(SOCDCleaner VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Win32 GUI 应用（无控制台窗口）
add_executable(socd WIN32
    main.cpp
    socd_engine.h
    socd_engine.cpp
)

target_link_libraries(socd PRIVATE
    user32
    gdi32
    shell32
)

# 编译选项
if(MSVC)
    target_compile_options(socd PRIVATE /W4 /O2 /std:c++17 /utf-8 /DUNICODE /D_UNICODE)
else()
    target_compile_options(socd PRIVATE -Wall -Wextra -O2 -std=c++17)
endif()

# 输出设置
set_target_properties(socd PROPERTIES
    OUTPUT_NAME "socd"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
