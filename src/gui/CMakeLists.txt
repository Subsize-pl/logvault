# Everything but main.cpp is a library, so the tests can check the models and widgets too.
qt_add_library(lv_gui STATIC
    DeleteRecordsDialog.cpp
    DeleteRecordsDialog.h
    FilterBar.cpp
    FilterBar.h
    Format.cpp
    Format.h
    HistogramWidget.cpp
    HistogramWidget.h
    ImportDialog.cpp
    ImportDialog.h
    LiveUpdater.cpp
    LiveUpdater.h
    MainWindow.cpp
    MainWindow.h
    PeriodDialog.cpp
    PeriodDialog.h
    ReceiverPanel.cpp
    ReceiverPanel.h
    RecordDetailsPanel.cpp
    RecordDetailsPanel.h
    RecordTableModel.cpp
    RecordTableModel.h
    Settings.cpp
    Settings.h
    SettingsDialog.cpp
    SettingsDialog.h
    SourcesPanel.cpp
    SourcesPanel.h
    StatsView.cpp
    StatsView.h
    TaskStatus.cpp
    TaskStatus.h
    Theme.cpp
    Theme.h
)

target_include_directories(lv_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lv_gui PUBLIC lv_core Qt6::Widgets)
lv_enable_warnings(lv_gui)

qt_add_executable(LogVault
    LogVault.rc
    main.cpp
    resources.qrc
)

# A GUI application: no console window. Qt's entry point library provides WinMain and calls main().
# AUTORCC compiles resources.qrc (the window icon); LogVault.rc gives the .exe its icon.
set_target_properties(LogVault PROPERTIES WIN32_EXECUTABLE TRUE AUTORCC ON)

target_link_libraries(LogVault PRIVATE lv_gui)
lv_enable_warnings(LogVault)
