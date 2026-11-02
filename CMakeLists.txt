cmake_minimum_required(VERSION 3.16)

project(DeejMix VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Sql SerialPort Network REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Sql SerialPort Network REQUIRED)

add_executable(DeejMix
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/mainwindow.ui
  src/databasemanager.h
  src/databasemanager.cpp
  src/hardwaremanager.h
  src/hardwaremanager.cpp
  src/audiomanager.h
  src/audiomanager.cpp
  src/channelstrip.h
  src/channelstrip.cpp
  src/knobwidget.h
  src/knobwidget.cpp
  resources/app.qrc
)

target_link_libraries(DeejMix PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::SerialPort Qt${QT_VERSION_MAJOR}::Network)
if(WIN32)
    target_link_libraries(DeejMix PRIVATE ole32 uuid psapi shell32)
elseif(UNIX AND NOT APPLE)
    # Linux audio backend: PulseAudio (also used by PipeWire via its pulse-compat server)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE REQUIRED libpulse)
    target_include_directories(DeejMix PRIVATE ${PULSE_INCLUDE_DIRS})
    target_compile_options(DeejMix PRIVATE ${PULSE_CFLAGS_OTHER})
    target_link_libraries(DeejMix PRIVATE ${PULSE_LIBRARIES})
endif()

# Set icon (to be added later)
# if(WIN32)
#   target_sources(DeejMix PRIVATE resources/app.rc)
# endif()

option(BUILD_TESTING "Build DeejMix regression tests (requires Qt Test)" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)
    get_target_property(DEEJMIX_TEST_SOURCES DeejMix SOURCES)
    list(REMOVE_ITEM DEEJMIX_TEST_SOURCES src/main.cpp)
    get_target_property(DEEJMIX_TEST_LIBRARIES DeejMix LINK_LIBRARIES)
    add_executable(DeejMixTests tests/tst_deejmix.cpp ${DEEJMIX_TEST_SOURCES})
    target_include_directories(DeejMixTests PRIVATE src ${PULSE_INCLUDE_DIRS})
    target_compile_options(DeejMixTests PRIVATE ${PULSE_CFLAGS_OTHER})
    target_link_libraries(DeejMixTests PRIVATE
        ${DEEJMIX_TEST_LIBRARIES} Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME DeejMixRegression COMMAND DeejMixTests)
    set_tests_properties(DeejMixRegression PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion"
        TIMEOUT 30)
endif()
