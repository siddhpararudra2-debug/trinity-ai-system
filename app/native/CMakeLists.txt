cmake_minimum_required(VERSION 3.24)
project(trinity-native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRINITY_WITH_QT "Build the Qt 6 / QML desktop shell" OFF)
option(TRINITY_BUILD_TESTS "Build native unit tests" ON)
option(TRINITY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    if(TRINITY_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(TRINITY_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

# ------------------------------------------------------------- dependencies
include(cmake/VendorDoctest.cmake)

# ------------------------------------------------------------------- sqlite
add_library(trinity_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(trinity_sqlite PUBLIC third_party/sqlite)
target_compile_definitions(trinity_sqlite
    PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_ENABLE_FTS5
        SQLITE_ENABLE_RTREE
        SQLITE_ENABLE_JSON1
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_OMIT_DEPRECATED
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
)

# -------------------------------------------------------------- trinity_core
# The core is a plain static library: no UI toolkit, no server, no Python.
# `cmake --build build --target trinity_core` must succeed on a machine with
# nothing but a C++20 compiler, which is what the native CI job asserts.
set(TRINITY_CORE_SOURCES
    core/Json.cpp
    core/Error.cpp
    core/Time.cpp
    core/Uuid.cpp
    core/Sha256.cpp
    core/Logging.cpp
    core/Paths.cpp
    core/FileSystem.cpp
    core/EventBus.cpp
    db/Database.cpp
    db/Schema.cpp
    platform/Platform.cpp
    storage/Storage.cpp
    projects/Project.cpp
    artifacts/Artifact.cpp
    jobs/JobSystem.cpp
    workflows/Workflow.cpp
    validation/ValidationEngine.cpp
    commands/Commands.cpp
    commands/CommandRegistry.cpp
    commands/BuiltinCommands.cpp
    engines/Engine.cpp
    engines/ScaffoldEngines.cpp
    engines/cad/Mesh.cpp
    engines/cad/QuadcopterFrame.cpp
    engines/cad/CadEngine.cpp
    engines/math/MathEngine.cpp
    model/ModelInterfaces.cpp
    plugins/PluginManager.cpp
    app/Application.cpp
    ipc/Ipc.cpp
    process/ProcessRunner.cpp
    monitoring/ResourceMonitor.cpp
    settings/Settings.cpp
)
set(TRINITY_CORE_HEADERS
    core/Json.hpp
    core/Error.hpp
    core/Result.hpp
    core/Time.hpp
    core/Uuid.hpp
    core/Sha256.hpp
    core/Logging.hpp
    core/Paths.hpp
    core/FileSystem.hpp
    core/EventBus.hpp
    db/Database.hpp
    db/Schema.hpp
    platform/Platform.hpp
    storage/Storage.hpp
    projects/Project.hpp
    artifacts/Artifact.hpp
    jobs/JobSystem.hpp
    workflows/Workflow.hpp
    validation/ValidationEngine.hpp
    commands/Commands.hpp
    commands/CommandRegistry.hpp
    commands/BuiltinCommands.hpp
    engines/Engine.hpp
    engines/ScaffoldEngines.hpp
    engines/cad/Mesh.hpp
    engines/cad/QuadcopterFrame.hpp
    engines/cad/CadEngine.hpp
    engines/math/MathEngine.hpp
    model/ModelInterfaces.hpp
    plugins/PluginManager.hpp
    app/Application.hpp
    interfaces/CoreInterfaces.hpp
    ipc/Ipc.hpp
    process/ProcessRunner.hpp
    monitoring/ResourceMonitor.hpp
    settings/Settings.hpp
)

find_package(Threads REQUIRED)
add_library(trinity_core STATIC ${TRINITY_CORE_SOURCES} ${TRINITY_CORE_HEADERS})
target_include_directories(trinity_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(trinity_core PUBLIC trinity_sqlite Threads::Threads)

# --------------------------------------------------------------- shell apps
add_executable(trinity-shell shell/trinity_shell.cpp)
target_link_libraries(trinity-shell PRIVATE trinity_core)

# ------------------------------------------------------------------- tests
if(TRINITY_BUILD_TESTS)
    enable_testing()
    add_executable(trinity_tests
        tests/test_main.cpp
        tests/test_core.cpp
        tests/test_db.cpp
        tests/test_cad_engine.cpp
        tests/test_math_jobs.cpp
        tests/test_platform_storage.cpp
        tests/test_events_plugins.cpp
        tests/test_application.cpp
    )
    target_link_libraries(trinity_tests PRIVATE trinity_core doctest_doctest)
    add_test(NAME trinity_native_tests COMMAND trinity_tests)
    set_tests_properties(trinity_native_tests PROPERTIES TIMEOUT 300)
endif()

# ------------------------------------------------------------- optional Qt
if(TRINITY_WITH_QT)
    message(STATUS "Trinity: building Qt 6 desktop shell")
    find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
    qt_standard_project_setup()
    qt_add_executable(trinity shell/trinity_qt_main.cpp shell/TrinityBridge.cpp)
    target_link_libraries(trinity PRIVATE trinity_core Qt6::Quick Qt6::QuickControls2)
    qt_add_qml_module(trinity
        URI Trinity.Shell
        VERSION 1.0
        QML_FILES
            qml/Main.qml
            qml/Viewport3D.qml
            qml/CommandConsole.qml
            qml/ProjectTree.qml
            qml/PropertiesPanel.qml
            qml/JobMonitor.qml
            qml/CommandPalette.qml
            qml/SettingsDialog.qml
    )
else()
    message(STATUS "Trinity: Qt shell disabled (TRINITY_WITH_QT=OFF) — console shell only")
endif()

# ------------------------------------------------------------- installation
# Layout produced by the installer (see installer/README.md):
#   bin/trinity-shell.exe
#   bin/python/services/engine_host.py   (bridge to the existing Python engines)
install(TARGETS trinity-shell RUNTIME DESTINATION bin)
install(DIRECTORY python/ DESTINATION bin/python)

set(CPACK_PACKAGE_NAME "Trinity")
set(CPACK_PACKAGE_VENDOR "Trinity Engineering")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Trinity — engineering operating system (native desktop core)")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Trinity")
set(CPACK_PACKAGE_FILE_NAME "Trinity-${PROJECT_VERSION}-win64")
if(WIN32)
    # ZIP for a portable copy; NSIS produces the Trinity Setup executable.
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Trinity")
    set(CPACK_NSIS_PACKAGE_NAME "Trinity")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_PACKAGE_EXECUTABLES "trinity-shell" "Trinity")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
    set(CPACK_GENERATOR "ZIP")
endif()
include(CPack)

# --------------------------------------------------------------- summary
message(STATUS "Trinity native: tests=${TRINITY_BUILD_TESTS} qt=${TRINITY_WITH_QT}")
