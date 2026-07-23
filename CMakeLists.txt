cmake_minimum_required(VERSION 3.10)
project(yabatman C CXX)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(YABATMAN_AGGRESSIVE_FLAGS "Enable aggressive -O2/LTO compile flags" ON)

if(YABATMAN_AGGRESSIVE_FLAGS)
    # Global flags optimized for minimal size + max inter-procedural speed
    set(_YABATMAN_C_FLAGS "-O2 -flto=auto -fstrict-aliasing -ffunction-sections -fdata-sections -fno-ident -fno-plt -fomit-frame-pointer -fno-stack-protector -fno-math-errno -fvisibility=hidden -fmerge-all-constants -fno-asynchronous-unwind-tables -DNDEBUG")
    set(_YABATMAN_CXX_EXTRA "-std=c++03 -fno-threadsafe-statics -fno-exceptions -fmerge-all-constants -Wno-deprecated-declarations -fpermissive")
    set(CMAKE_C_FLAGS "${_YABATMAN_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${_YABATMAN_C_FLAGS} ${_YABATMAN_CXX_EXTRA}")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,-O1 -Wl,--gc-sections -Wl,--as-needed -Wl,--build-id=none -Wl,-z,norelro -flto=auto -s")
    set(CMAKE_C_FLAGS_DEBUG "")
    set(CMAKE_C_FLAGS_RELEASE "")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
    set(CMAKE_C_FLAGS_MINSIZEREL "")
    set(CMAKE_CXX_FLAGS_DEBUG "")
    set(CMAKE_CXX_FLAGS_RELEASE "")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "")
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++03>)
endif()

find_package(PkgConfig REQUIRED)

# Packages for Daemon
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

# Packages for GUI
pkg_check_modules(TQT REQUIRED tqt-mt)
pkg_check_modules(UDEV REQUIRED libudev)
pkg_check_modules(NOTIFY REQUIRED libnotify)

# Find X11 and its extensions
find_package(X11 REQUIRED)
find_library(XSS_LIB Xss)
find_library(XEXT_LIB Xext)
find_library(XTST_LIB Xtst)

# Find tqmoc
find_program(TQMOC_EXECUTABLE NAMES tqmoc moc-tqt)
if (NOT TQMOC_EXECUTABLE)
    message(FATAL_ERROR "tqmoc (or moc-tqt) not found")
endif()

# Find sstrip for aggressive binary stripping
find_program(SSTRIP_EXECUTABLE sstrip)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${TQT_INCLUDE_DIRS}
    ${UDEV_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${SYSTEMD_INCLUDE_DIRS}
    ${NOTIFY_INCLUDE_DIRS}
    /usr/include/tqt
    /opt/trinity/include
    /opt/trinity/include/tqt
)

link_directories(
    ${TQT_LIBRARY_DIRS}
    ${UDEV_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS}
    ${SYSTEMD_LIBRARY_DIRS}
    /opt/trinity/lib
)

# 1. Compile yabatmand (Daemon in C)
add_executable(yabatmand src/yabatmand.c)
if(NOT YABATMAN_AGGRESSIVE_FLAGS)
    target_compile_definitions(yabatmand PRIVATE CONSOLE_DEBUG)
endif()
target_link_libraries(yabatmand
    ${GLIB_LIBRARIES}
    ${SYSTEMD_LIBRARIES}
    udev
)

# 2. Compile yabatman (GUI in C++/TQt3)
# We will define the source files and generate MOC files for classes with Q_OBJECT

# Custom command to run tqmoc on headers
macro(tqt_wrap_cpp out_files)
    foreach(it ${ARGN})
        get_filename_component(name ${it} NAME_WE)
        set(moc_file ${CMAKE_CURRENT_BINARY_DIR}/moc_${name}.cpp)
        add_custom_command(
            OUTPUT ${moc_file}
            COMMAND ${TQMOC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/${it} -o ${moc_file}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${it}
        )
        list(APPEND ${out_files} ${moc_file})
    endforeach()
endmacro()

# GUI Headers that need MOC
set(MOC_HEADERS
    yabatman_gui.h
    battery_info_dialog.h
    battery_history_dialog.h
    config_dialog.h
    calibration_manager.h
    screensavers.h
    inactivity_manager.h
)

tqt_wrap_cpp(MOC_SOURCES ${MOC_HEADERS})

# Custom command to run tqmoc on main.cpp
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.moc
    COMMAND ${TQMOC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/main.moc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

set(GUI_SOURCES
    src/main.cpp
    src/yabatman_gui.cpp
    src/battery_logger.cpp
    src/battery_info_dialog.cpp
    src/battery_history_dialog.cpp
    src/config_manager.cpp
    src/config_dialog.cpp
    src/calibration_manager.cpp
    src/screensavers.cpp
    src/inactivity_manager.cpp
    src/mpris_detection.c
    src/tqtaapainter.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/main.moc
)

add_executable(yabatman
    ${GUI_SOURCES}
    ${MOC_SOURCES}
)

option(YABATMAN_STATIC_TQT "Build GUI statically with standalone TQt3 (no TDE dependency)" OFF)

if(YABATMAN_STATIC_TQT)
    add_definitions(-DPURE_TQT3)
endif()

target_compile_options(yabatman PRIVATE ${TQT_CFLAGS_OTHER})

find_library(XFT_LIB Xft)
find_library(XRENDER_LIB Xrender)
find_library(XRANDR_LIB Xrandr)
find_library(XCURSOR_LIB Xcursor)
find_library(XINERAMA_LIB Xinerama)

if(YABATMAN_STATIC_TQT)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/libs/libtqt-mt.a" AND EXISTS "${CMAKE_SOURCE_DIR}/libs/libtqt-mt.a.xz")
        message(STATUS "Decompressing libs/libtqt-mt.a.xz...")
        execute_process(COMMAND xz -d -k "${CMAKE_SOURCE_DIR}/libs/libtqt-mt.a.xz")
    endif()
    target_link_libraries(yabatman
        ${CMAKE_SOURCE_DIR}/libs/libtqt-mt.a
        ${UDEV_LIBRARIES}
        ${GIO_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${X11_LIBRARIES}
        ${XSS_LIB}
        ${XEXT_LIB}
        ${XTST_LIB}
        ${XFT_LIB}
        ${XRENDER_LIB}
        ${XRANDR_LIB}
        ${XCURSOR_LIB}
        ${XINERAMA_LIB}
        ${NOTIFY_LIBRARIES}

        fontconfig
        freetype
        png
        z
        jpeg
        pthread
        dl
        m
    )
else()
    target_link_libraries(yabatman
        ${TQT_LIBRARIES}
        ${UDEV_LIBRARIES}
        ${GIO_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${X11_LIBRARIES}
        ${XSS_LIB}
        ${XEXT_LIB}
        ${XTST_LIB}
        ${NOTIFY_LIBRARIES}

        tdecore
        tdeui
        pthread
    )
endif()

# 3. Compile test_screensaver
add_executable(test_screensaver
    src/test_screensaver.cpp
    src/screensavers.cpp
    src/tqtaapainter.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/moc_screensavers.cpp
)

if(NOT YABATMAN_AGGRESSIVE_FLAGS)
    target_compile_definitions(test_screensaver PRIVATE CONSOLE_DEBUG)
endif()
target_compile_options(test_screensaver PRIVATE ${TQT_CFLAGS_OTHER})

target_link_libraries(test_screensaver
    ${TQT_LIBRARIES}
    ${X11_LIBRARIES}
    ${XSS_LIB}
    ${XEXT_LIB}
    pthread
)

# 4. Aggressive size optimizations for UI elements and daemon
if(YABATMAN_AGGRESSIVE_FLAGS)
    set_source_files_properties(
        src/yabatmand.c
        src/main.cpp
        src/yabatman_gui.cpp
        src/battery_info_dialog.cpp
        src/battery_history_dialog.cpp
        src/config_manager.cpp
        src/config_dialog.cpp
        src/calibration_manager.cpp
        PROPERTIES COMPILE_FLAGS "-Os"
    )
    
    # sstrip on binaries
    if(SSTRIP_EXECUTABLE)
        add_custom_command(TARGET yabatmand POST_BUILD
            COMMAND ${SSTRIP_EXECUTABLE} $<TARGET_FILE:yabatmand>
            COMMENT "Aggressively stripping yabatmand with sstrip"
        )
        add_custom_command(TARGET yabatman POST_BUILD
            COMMAND ${SSTRIP_EXECUTABLE} $<TARGET_FILE:yabatman>
            COMMENT "Aggressively stripping yabatman with sstrip"
        )
    endif()
endif()
