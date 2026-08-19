cmake_minimum_required(VERSION 3.16)
project(AudioMonitor VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

set(AUDIOMONITOR_TRANSLATIONS
    translations/audiomonitor_en.ts
    translations/audiomonitor_zh_CN.ts
)

set(AUDIOMONITOR_SOURCES
    src/main.cpp
    src/appicon.cpp
    src/appicon.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/core/audiorouter.cpp
    src/core/audiorouter.h
    resources/resources.qrc
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
    message(FATAL_ERROR "AudioMonitor supports Windows and Linux only")
endif()

# WIN32: Windows GUI subsystem (no console window).
add_executable(audiomonitor WIN32 ${AUDIOMONITOR_SOURCES})

target_include_directories(audiomonitor PRIVATE src)
target_link_libraries(audiomonitor PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(BUILD_TESTING)
    # Exercise GUI session/reconnect state with a fake router. The production
    # platform backend is linked but never constructed by this test.
    set(MAINWINDOW_TEST_SOURCES ${AUDIOMONITOR_SOURCES})
    list(REMOVE_ITEM MAINWINDOW_TEST_SOURCES src/main.cpp)
    add_executable(mainwindow_tests
        tests/mainwindow_test.cpp
        ${MAINWINDOW_TEST_SOURCES}
    )
    target_include_directories(mainwindow_tests PRIVATE src)
    target_link_libraries(mainwindow_tests PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
    if(WIN32)
        target_link_libraries(mainwindow_tests PRIVATE ole32 avrt)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(mainwindow_tests PRIVATE PkgConfig::PIPEWIRE)
    endif()
endif()

set(AUDIOMONITOR_TRANSLATION_TARGETS audiomonitor)
if(BUILD_TESTING)
    list(APPEND AUDIOMONITOR_TRANSLATION_TARGETS mainwindow_tests)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    # Qt 6.4 (used by Ubuntu 24.04) accepts the target as a positional
    # argument.  Newer Qt releases also support TARGETS, but using the
    # compatible form keeps the CI and local builds on the same code path.
    # Compile and embed the catalogs in the application first.
    qt_add_translations(audiomonitor
        TS_FILES ${AUDIOMONITOR_TRANSLATIONS}
        RESOURCE_PREFIX "/translations"
    )

    # Reuse the generated catalogs for the GUI test target.  Generating the
    # same .qm files a second time creates duplicate-output rules with Ninja.
    if(BUILD_TESTING)
        set(AUDIOMONITOR_QM_FILES
            "${CMAKE_CURRENT_BINARY_DIR}/audiomonitor_en.qm"
            "${CMAKE_CURRENT_BINARY_DIR}/audiomonitor_zh_CN.qm"
        )
        set_source_files_properties(${AUDIOMONITOR_QM_FILES}
            PROPERTIES GENERATED TRUE
        )
        qt_add_resources(mainwindow_tests mainwindow_tests_translations
            PREFIX "/translations"
            BASE "${CMAKE_CURRENT_BINARY_DIR}"
            FILES ${AUDIOMONITOR_QM_FILES}
        )
    endif()
else()
    # Qt 5 does not provide qt_add_translations(). Generate the catalogs once
    # and share one resource source with each target using the same prefix.
    qt5_add_translation(AUDIOMONITOR_QM_FILES ${AUDIOMONITOR_TRANSLATIONS})
    set(AUDIOMONITOR_QRC "${CMAKE_CURRENT_BINARY_DIR}/audiomonitor_translations.qrc")
    set(AUDIOMONITOR_QRC_CONTENT "<RCC>\n  <qresource prefix=\"/translations\">\n")
    foreach(QM_FILE IN LISTS AUDIOMONITOR_QM_FILES)
        get_filename_component(QM_NAME "${QM_FILE}" NAME)
        set_source_files_properties("${QM_FILE}" PROPERTIES GENERATED TRUE)
        string(APPEND AUDIOMONITOR_QRC_CONTENT
            "    <file alias=\"${QM_NAME}\">${QM_FILE}</file>\n")
    endforeach()
    string(APPEND AUDIOMONITOR_QRC_CONTENT "  </qresource>\n</RCC>\n")
    file(WRITE "${AUDIOMONITOR_QRC}" "${AUDIOMONITOR_QRC_CONTENT}")
    qt5_add_resources(AUDIOMONITOR_TRANSLATION_RESOURCES "${AUDIOMONITOR_QRC}")
    foreach(AUDIOMONITOR_TARGET IN LISTS AUDIOMONITOR_TRANSLATION_TARGETS)
        target_sources(${AUDIOMONITOR_TARGET} PRIVATE
            ${AUDIOMONITOR_QM_FILES}
            ${AUDIOMONITOR_TRANSLATION_RESOURCES}
        )
    endforeach()
endif()

if(WIN32)
    target_link_libraries(audiomonitor PRIVATE ole32 avrt)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(audiomonitor PRIVATE PkgConfig::PIPEWIRE)
endif()

# For packaging (use windeployqt on Windows):
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

    add_test(NAME mainwindow COMMAND mainwindow_tests)
endif()
