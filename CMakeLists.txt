cmake_minimum_required(VERSION 3.19)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CalendaeVersion)

project(calendae VERSION ${CALENDAE_VERSION} LANGUAGES CXX)

include(GNUInstallDirs)

# Reverse-DNS application id. The installed .desktop entry, the hicolor icon
# files and the AppStream metainfo are all named after it and generated from
# templates at configure time, so switching it is a one-line change here (or
# -DCALENDAE_APP_ID=...). Flatpak and software centres require the files to
# carry this exact basename.
set(CALENDAE_APP_ID "com.github.frutality.Calendae" CACHE STRING
    "Reverse-DNS application id (.desktop / icon / metainfo basename)")
set(CALENDAE_PROJECT_URL "https://github.com/frutality/calendae" CACHE STRING
    "Project homepage recorded in the AppStream metainfo")
# AppStream developer id: lowercase rDNS identifying *you*, not the app
# (e.g. com.github.<username>). Kept separate because it must be lowercase.
set(CALENDAE_DEVELOPER_ID "com.github.frutality" CACHE STRING
    "AppStream <developer id=...> (lowercase rDNS identifying the developer)")

# AppStream <release> entry for local builds; CI overrides both from the tag.
if(NOT DEFINED CALENDAE_RELEASE_VERSION)
    set(CALENDAE_RELEASE_VERSION "${CALENDAE_VERSION}")
endif()
if(NOT DEFINED CALENDAE_RELEASE_DATE)
    string(TIMESTAMP CALENDAE_RELEASE_DATE "%Y-%m-%d")
endif()

option(TINY_GCAL_BUILD_TESTS "Build unit tests" ON)

# The .ts -> .qm compilation needs Qt's LinguistTools component, which a
# stripped-down static Qt built from qtbase alone does not ship. Turning
# this OFF drops the single (English) catalogue; main.cpp just doesn't find
# :/i18n/ at runtime and shows the source strings, which are already
# English. Keep it ON for any normal Qt SDK build.
option(CALENDAE_BUILD_TRANSLATIONS "Compile bundled .ts translations (needs Qt LinguistTools)" ON)

set(_calendae_qt_components Core Network Widgets DBus)
if(CALENDAE_BUILD_TRANSLATIONS)
    list(APPEND _calendae_qt_components LinguistTools)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${_calendae_qt_components})

qt_standard_project_setup()

include(FetchContent)

set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)

# QtKeychain backend selection. The libsecret backend is the cross-desktop
# one: it drives ANY org.freedesktop.secrets provider (gnome-keyring,
# KeePassXC, KWallet 6, pass-secret-service, ...), so the app no longer
# depends on KWallet specifically. It needs the libsecret-1 headers at
# configure time; at runtime QtKeychain dlopen()s the library, so it stays a
# soft dependency -- an absent keyring daemon falls through to KWallet's own
# D-Bus protocol and finally to the on-disk fallback (see
# src/auth/keychainjob.h). Auto-detected; override with
# -DCALENDAE_WITH_LIBSECRET=ON or =OFF.
set(CALENDAE_WITH_LIBSECRET "AUTO" CACHE STRING
    "QtKeychain Secret Service (libsecret) backend: ON, OFF, or AUTO-detect")
if(CALENDAE_WITH_LIBSECRET STREQUAL "AUTO")
    set(_calendae_libsecret OFF)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(CALENDAE_LIBSECRET QUIET libsecret-1)
            if(CALENDAE_LIBSECRET_FOUND)
                set(_calendae_libsecret ON)
            endif()
        endif()
    endif()
else()
    set(_calendae_libsecret ${CALENDAE_WITH_LIBSECRET})
endif()

set(LIBSECRET_SUPPORT ${_calendae_libsecret} CACHE BOOL "" FORCE)
if(_calendae_libsecret)
    message(STATUS "calendae: QtKeychain Secret Service (libsecret) backend enabled")
else()
    message(STATUS "calendae: QtKeychain libsecret backend off -- install libsecret-1 "
        "development files for Secret Service support; KWallet + on-disk fallback still work")
endif()

FetchContent_Declare(
    qtkeychain
    GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
    GIT_TAG        v0.14.0
    GIT_SHALLOW    TRUE
)

# Build qtkeychain as a static library without permanently overriding this
# project's own BUILD_SHARED_LIBS default.
set(_tgc_saved_bsl ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(qtkeychain)
set(BUILD_SHARED_LIBS ${_tgc_saved_bsl})

# qt_add_translations() (below) auto-collects every eligible target in the
# whole CMake project when SOURCE_TARGETS isn't given explicitly -- without
# this, lupdate pulls qtkeychain's own internal strings (D-Bus/wallet error
# messages) into this app's .ts file, which isn't ours to translate and
# would silently break if the library's error text changes on a version
# bump.
set_property(TARGET qt6keychain PROPERTY QT_EXCLUDE_FROM_TRANSLATION ON)

# The OAuth/auth subsystem lives in its own static library so unit tests can
# link against it without recompiling it or duplicating its source list.
add_library(tgc_auth STATIC
    src/auth/pkce.h
    src/auth/pkce.cpp
    src/auth/tokenresponse.h
    src/auth/tokenresponse.cpp
    src/auth/credentialsprovider.h
    src/auth/credentialsprovider.cpp
    src/auth/keychainjob.h
    src/auth/keychainjob.cpp
    src/auth/oauthcredentialsdialog.h
    src/auth/oauthcredentialsdialog.cpp
    src/auth/oauthcredentialsdialog.ui
    src/auth/oauthloopbackserver.h
    src/auth/oauthloopbackserver.cpp
    src/auth/authmanager.h
    src/auth/authmanager.cpp
)

target_include_directories(tgc_auth
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # qtkeychain (via FetchContent) only exposes its public header
        # (keychain.h, included unqualified) and its generated export
        # header through its build-tree source/binary dirs, not via a
        # BUILD_INTERFACE include on the qt6keychain target itself.
        ${qtkeychain_SOURCE_DIR}
        ${qtkeychain_BINARY_DIR}
)

target_link_libraries(tgc_auth
    PUBLIC
        Qt::Core
        Qt::Network
        Qt::Widgets
        qt6keychain
)

# The Calendar REST subsystem lives in its own static library, same
# reasoning as tgc_auth: unit tests can link it without duplicating sources.
add_library(tgc_calendar STATIC
    src/calendar/calendar.h
    src/calendar/calendar.cpp
    src/calendar/googlecalendarapi.h
    src/calendar/googlecalendarapi.cpp
    src/calendar/calendarsidebarwidget.h
    src/calendar/calendarsidebarwidget.cpp
    src/calendar/calendarsidebarwidget.ui
    src/calendar/monthgrid.h
    src/calendar/monthgrid.cpp
    src/calendar/monthkeys.h
    src/calendar/monthkeys.cpp
    src/calendar/montheventstore.h
    src/calendar/montheventstore.cpp
    src/calendar/eventcachestore.h
    src/calendar/eventcachestore.cpp
    src/calendar/monthdaycellwidget.h
    src/calendar/monthdaycellwidget.cpp
    src/calendar/monthviewwidget.h
    src/calendar/monthviewwidget.cpp
    src/calendar/monthviewwidget.ui
    src/calendar/event.h
    src/calendar/event.cpp
    src/calendar/eventtimerange.h
    src/calendar/eventtimerange.cpp
    src/calendar/eventgrouping.h
    src/calendar/eventgrouping.cpp
    src/calendar/eventscontroller.h
    src/calendar/eventgridview.h
    src/calendar/montheventitem.h
    src/calendar/storebackedeventscontroller.h
    src/calendar/storebackedeventscontroller.cpp
    src/calendar/montheventscontroller.h
    src/calendar/montheventscontroller.cpp
    src/calendar/timegridrange.h
    src/calendar/timegridrange.cpp
    src/calendar/timegrideventscontroller.h
    src/calendar/timegrideventscontroller.cpp
    src/calendar/timegridviewwidget.h
    src/calendar/timegridviewwidget.cpp
    src/calendar/timegridviewwidget.ui
    src/calendar/timegriddaycolumnwidget.h
    src/calendar/timegriddaycolumnwidget.cpp
    src/calendar/timegridalldaycellwidget.h
    src/calendar/timegridalldaycellwidget.cpp
    src/calendar/timegridhourgutterwidget.h
    src/calendar/timegridhourgutterwidget.cpp
    src/calendar/neweventrequest.h
    src/calendar/eventdialog.h
    src/calendar/eventdialog.cpp
    src/calendar/eventdialog.ui
    src/calendar/eventpilllabel.h
    src/calendar/eventpilllabel.cpp
    src/calendar/reminderschedule.h
    src/calendar/reminderschedule.cpp
    src/calendar/reminderscheduler.h
    src/calendar/reminderscheduler.cpp
)

target_include_directories(tgc_calendar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tgc_calendar
    PUBLIC
        tgc_auth
)

qt_add_executable(calendae
    WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/mainwindow.ui
    src/app/processmemory.h
    src/app/processmemory.cpp
    src/app/desktopnotifier.h
    src/app/desktopnotifier.cpp
)

# Version header: CALENDAE_VERSION_STRING (from the git tag / describe) and
# CALENDAE_APP_ID, consumed by main.cpp for --version and setDesktopFileName().
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/calendae_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/calendae_version.h"
    @ONLY
)
target_include_directories(calendae PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Window icon, baked in so a dev build has an icon without `make install`.
qt_add_resources(calendae "app_icons"
    PREFIX "/icons"
    BASE resources/icons
    FILES
        resources/icons/calendae-16.png
        resources/icons/calendae-24.png
        resources/icons/calendae-32.png
        resources/icons/calendae-48.png
        resources/icons/calendae-64.png
        resources/icons/calendae-128.png
        resources/icons/calendae-256.png
)

if(CALENDAE_BUILD_TRANSLATIONS)
    qt_add_translations(
        TARGETS calendae
        TS_FILES translations/calendae_en_001.ts
    )
endif()

# Windows: embed the .ico (Explorer/taskbar icon -- the qt_add_resources
# block above only sets the in-app QWidget window icon) and a VERSIONINFO
# resource so File Properties > Details shows something other than
# "Unknown". calendae.rc.in references calendae.ico by relative path, so
# the icon must sit next to the generated .rc at configure time.
if(WIN32)
    set(_calendae_rc "${CMAKE_CURRENT_BINARY_DIR}/calendae.rc")
    configure_file(resources/calendae.rc.in "${_calendae_rc}" @ONLY)
    configure_file(resources/icons/calendae.ico "${CMAKE_CURRENT_BINARY_DIR}/calendae.ico" COPYONLY)
    target_sources(calendae PRIVATE "${_calendae_rc}")
endif()

# macOS: a generated Info.plist (bundle id = CALENDAE_APP_ID, versions from
# the git tag, LSMinimumSystemVersion) and the .icns bundled into
# Contents/Resources. macdeployqt + codesign happen later in
# packaging/macos/build-dmg.sh.
if(APPLE)
    set(_calendae_icns "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/calendae.icns")
    set(_calendae_plist "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    configure_file(resources/Info.plist.in "${_calendae_plist}" @ONLY)
    target_sources(calendae PRIVATE "${_calendae_icns}")
    set_source_files_properties("${_calendae_icns}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(calendae PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${_calendae_plist}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${CALENDAE_APP_ID}")
endif()

target_link_libraries(calendae
    PRIVATE
        tgc_auth
        tgc_calendar
        Qt::DBus
)

install(TARGETS calendae
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Freedesktop integration (Linux): a .desktop entry, an AppStream metainfo
# file, and a themed icon under hicolor. All three are named after
# CALENDAE_APP_ID and generated from templates so the id lives in exactly one
# place. The metainfo is what software centres and `flatpak-builder` read;
# keep it valid with scripts/lint-metadata.sh (the metadata_lint test).
# Harmless no-ops on the WIN32/macOS bundle builds.
if(UNIX AND NOT APPLE)
    set(_calendae_desktop "${CMAKE_CURRENT_BINARY_DIR}/${CALENDAE_APP_ID}.desktop")
    set(_calendae_metainfo "${CMAKE_CURRENT_BINARY_DIR}/${CALENDAE_APP_ID}.metainfo.xml")
    configure_file(resources/calendae.desktop.in "${_calendae_desktop}" @ONLY)
    configure_file(resources/calendae.metainfo.xml.in "${_calendae_metainfo}" @ONLY)

    install(FILES "${_calendae_desktop}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${_calendae_metainfo}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
    foreach(_icon_size IN ITEMS 16 22 24 32 48 64 128 256 512)
        install(FILES resources/icons/calendae-${_icon_size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_icon_size}x${_icon_size}/apps
            RENAME ${CALENDAE_APP_ID}.png
        )
    endforeach()
endif()

install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/calendae
)

# On `cmake --install`, Qt's deploy script also copies the Qt runtime (libs,
# plugins, translations) next to the binary. Useful for an ad-hoc local
# install; turned OFF by the packaging scripts, which bundle Qt themselves
# (linuxdeploy / windeployqt / macdeployqt) and don't want the extra dump.
option(CALENDAE_INSTALL_QT_RUNTIME
    "cmake --install also bundles the Qt runtime via Qt's deploy script" ON)
if(CALENDAE_INSTALL_QT_RUNTIME)
    qt_generate_deploy_app_script(
        TARGET calendae
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

# Validate the generated .desktop / metainfo against freedesktop + AppStream
# rules. Skipped (CTest return code 77) when neither validator is installed.
if(UNIX AND NOT APPLE)
    find_program(BASH_PROGRAM bash)
    if(BASH_PROGRAM)
        enable_testing()
        add_test(NAME metadata_lint
            COMMAND "${BASH_PROGRAM}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/lint-metadata.sh"
                    "${CMAKE_CURRENT_BINARY_DIR}"
        )
        set_tests_properties(metadata_lint PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

if(TINY_GCAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
