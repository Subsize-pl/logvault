qt_add_library(lv_core STATIC
    AppInfo.cpp
    AppInfo.h
    Database.cpp
    Database.h
    Record.cpp
    Record.h
)

target_include_directories(lv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lv_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network Qt6::Concurrent)
target_compile_definitions(lv_core PRIVATE LV_VERSION="${PROJECT_VERSION}")
lv_enable_warnings(lv_core)
