# Doctest (MIT) — single-header test framework.
# Prefers a vendored copy at app/native/third_party/doctest/doctest.h;
# falls back to FetchContent so CI never needs a package manager.
cmake_minimum_required(VERSION 3.24)

set(DOCTEST_VERSION 2.4.12)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../third_party/doctest/doctest.h")
    add_library(doctest_doctest INTERFACE)
    target_include_directories(doctest_doctest INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../third_party/doctest")
    message(STATUS "Trinity: using vendored doctest")
    return()
endif()

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v${DOCTEST_VERSION}
)
FetchContent_MakeAvailable(doctest)
add_library(doctest_doctest ALIAS doctest)
message(STATUS "Trinity: fetched doctest v${DOCTEST_VERSION}")
