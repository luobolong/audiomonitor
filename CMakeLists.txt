cmake_minimum_required(VERSION 3.16)
project(AudioMonitor VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(AUDIOMONITOR_SOURCES
    src/main.cpp
    src/appicon.cpp
    src/appicon.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/core/audiorouter.cpp
    src/core/audiorouter.h
)

# RingBuffer only used on Windows
if(WIN32)
    list(APPEND AUDIOMONITOR_SOURCES src/core/ringbuffer.h)
endif()

if(WIN32)
    list(APPEND AUDIOMONITOR_SOURCES
        src/core/audiorouter_win.cpp
        src/core/audiorouter_win.h
    )
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    list(APPEND AUDIOMONITOR_SOURCES
        src/core/audiorouter_linux.cpp
        src/core/audiorouter_linux.h
        src/core/realtime_audio.h
    )
else()
    message(FATAL_ERROR "AudioMonitor 仅支持 Windows 与 Linux")
endif()

# WIN32: Windows GUI 子系统（无控制台窗口）
add_executable(audiomonitor WIN32 ${AUDIOMONITOR_SOURCES})

target_include_directories(audiomonitor PRIVATE src)
target_link_libraries(audiomonitor PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(WIN32)
    target_link_libraries(audiomonitor PRIVATE ole32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(audiomonitor PRIVATE PkgConfig::PIPEWIRE)
endif()

# 打包发布时（Windows 用 windeployqt）:
#   cmake --install build --prefix dist
install(TARGETS audiomonitor
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

if(BUILD_TESTING)
    add_executable(realtime_audio_tests
        tests/realtime_audio_test.cpp
        src/core/realtime_audio.h
    )
    target_include_directories(realtime_audio_tests PRIVATE src)
    set_target_properties(realtime_audio_tests PROPERTIES AUTOMOC OFF)
    add_test(NAME realtime_audio COMMAND realtime_audio_tests)

    # The production Linux backend does not use RingBuffer. Building its
    # platform-independent tests here keeps the strict SPSC invariants covered
    # on every CI host without adding the queue to the Linux application.
    find_package(Threads REQUIRED)
    add_executable(ringbuffer_tests
        tests/ringbuffer_test.cpp
        src/core/ringbuffer.h
    )
    target_include_directories(ringbuffer_tests PRIVATE src)
    target_link_libraries(ringbuffer_tests PRIVATE Threads::Threads)
    set_target_properties(ringbuffer_tests PROPERTIES AUTOMOC OFF)
    add_test(NAME ringbuffer COMMAND ringbuffer_tests)
endif()
