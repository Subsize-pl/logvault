qt_add_executable(LogVault
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

# A GUI application: no console window. Qt's entry point library provides WinMain and calls main().
set_target_properties(LogVault PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(LogVault PRIVATE lv_core Qt6::Widgets)
lv_enable_warnings(LogVault)
