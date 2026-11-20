cmake_minimum_required(VERSION 3.25)

project(LogVault VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 6.8: QStyleHints::setColorScheme() (the theme setting) and QDateTimeEdit::setTimeZone().
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Sql Network Concurrent Widgets Test)
qt_standard_project_setup()

enable_testing()

# Warnings are enabled per target, so they apply to our code only; Qt headers come in as system headers.
function(lv_enable_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/gui)
add_subdirectory(tests)
