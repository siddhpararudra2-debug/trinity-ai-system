cmake_minimum_required(VERSION 3.24)

project(trinity VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRINITY_WITH_QT "Build the Qt 6 desktop shell (Trinity.exe)" ON)
option(TRINITY_BUILD_TESTS "Build native unit tests" ON)
option(TRINITY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /FS)
    if(TRINITY_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(TRINITY_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

set(CMAKE_AUTOMOC ON)

include(cmake/VendorDoctest.cmake)

# ------------------------------------------------------------------ sqlite
# Vendored amalgamation: no network, no package manager required.
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

# -------------------------------------------------------------------- core
# Plain static library: no UI toolkit, no server, no Python, no model.
set(TRINITY_CORE_SOURCES
    src/core/Error.cpp
    src/core/Config.cpp
    src/core/Logger.cpp
    src/core/Paths.cpp
    src/core/Uuid.cpp
    src/core/Time.cpp
    src/core/ApplicationContext.cpp
    src/fs/Filesystem.cpp
    src/cad/FrameParams.cpp
    src/cad/Mesh.cpp
    src/cad/Builder.cpp
    src/cad/StlWriter.cpp
    src/cad/Validators.cpp
    src/intelligence/ToolCall.cpp
    src/intelligence/Intent.cpp
    src/intelligence/ModelRequest.cpp
    src/intelligence/ModelResponse.cpp
    src/intelligence/ModelProvider.cpp
    src/intelligence/ModelProviderFactory.cpp
    src/intelligence/ExampleProvider.cpp
    src/intelligence/Planner.cpp
    src/validation/ValidationResult.cpp
    src/engines/Engine.cpp
    src/engines/EngineRegistry.cpp
    src/engines/MathEngine.cpp
    src/engines/CadEngine.cpp
    src/engines/StubEngines.cpp
    src/workflows/Workflow.cpp
    src/storage/Database.cpp
    src/storage/Repositories.cpp
    src/artifacts/Artifact.cpp
    src/jobs/Job.cpp
)

add_library(trinity_core STATIC ${TRINITY_CORE_SOURCES})
target_include_directories(trinity_core
    PUBLIC
        include
        third_party/json
)
target_link_libraries(trinity_core PUBLIC trinity_sqlite)
if(WIN32)
    target_link_libraries(trinity_core PUBLIC bcrypt shell32 ole32)
endif()

# ------------------------------------------------------------------- app
if(TRINITY_WITH_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    qt_add_executable(Trinity
        src/main.cpp
        ui/MainWindow.cpp
        ui/MainWindow.hpp
    )
    target_compile_definitions(Trinity PRIVATE TRINITY_WITH_QT)
    target_link_libraries(Trinity PRIVATE trinity_core Qt6::Widgets)
    set_target_properties(Trinity PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    add_executable(Trinity src/main.cpp)
    target_link_libraries(Trinity PRIVATE trinity_core)
endif()

# ------------------------------------------------------------------- tests
if(TRINITY_BUILD_TESTS)
    include(CTest)
    add_executable(trinity_tests
        tests/test_main.cpp
        tests/test_error.cpp
        tests/test_registry.cpp
        tests/test_workflow.cpp
        tests/test_database.cpp
        tests/test_model_provider.cpp
        tests/test_paths_config.cpp
        tests/test_jobs.cpp
        tests/test_uuid.cpp
        tests/test_serialization.cpp
        tests/test_filesystem.cpp
        tests/test_repositories.cpp
        tests/test_logging_config.cpp
        tests/test_math_engine.cpp
        tests/test_cad_stubs.cpp
        tests/test_cad_generate.cpp
        tests/test_validation.cpp
        tests/test_provider_factory.cpp
        tests/test_planner.cpp
    )
    target_include_directories(trinity_tests PRIVATE third_party/doctest)
    target_link_libraries(trinity_tests PRIVATE trinity_core)
    add_test(NAME trinity_tests COMMAND trinity_tests)
endif()
