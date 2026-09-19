# doctest is vendored under third_party/doctest/doctest.h, so there is
# nothing to fetch. This file exists to document the choice and to fail
# fast with a clear message if the header ever goes missing.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest/doctest.h")
    message(FATAL_ERROR "Missing vendored doctest at third_party/doctest/doctest.h")
endif()
