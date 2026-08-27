cmake_minimum_required(VERSION 3.19)
project(calendae LANGUAGES CXX)

option(TINY_GCAL_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets LinguistTools)

qt_standard_project_setup()

include(FetchContent)

set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
# libsecret-1-dev isn't installed on this machine; qtkeychain still supports
# KWallet via D-Bus without it (keychain_unix.cpp picks a backend at
# runtime), so this only drops the Secret Service backend, not Linux
# support in general.
set(LIBSECRET_SUPPORT OFF CACHE BOOL "" FORCE)

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
    src/calendar/montheventitem.h
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
)

qt_add_translations(
    TARGETS calendae
    TS_FILES translations/calendae_en_001.ts
)

target_link_libraries(calendae
    PRIVATE
        tgc_auth
        tgc_calendar
)

install(TARGETS calendae
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET calendae
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

if(TINY_GCAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
