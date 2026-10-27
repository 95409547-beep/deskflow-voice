# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

if (WIN32)
  create_test(
    NAME MSWindowsClipboardTests
    DEPENDS platform
    LIBS base arch
    SOURCE MSWindowsClipboardTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
elseif(APPLE)
  add_executable(TISQtRuntimeProbe TISQtRuntimeProbe.cpp)
  target_link_libraries(TISQtRuntimeProbe platform base arch app Qt::Widgets)
  add_test(NAME TISQtRuntimeProbe COMMAND $<TARGET_FILE:TISQtRuntimeProbe>)
  set_tests_properties(TISQtRuntimeProbe PROPERTIES TIMEOUT 40)
  create_test(
    NAME TISMainThreadProbe
    DEPENDS platform
    LIBS base arch app
    SOURCE TISMainThreadProbe.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  create_test(
    NAME TISReadPathsTests
    DEPENDS platform
    LIBS base arch app Qt::Widgets
    SOURCE TISReadPathsTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  set_tests_properties(TISMainThreadProbe TISReadPathsTests PROPERTIES TIMEOUT 90)
  create_test(
    NAME OSXClipboardTests
    DEPENDS platform
    LIBS base arch
    SOURCE OSXClipboardTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  create_test(
    NAME OSXKeyStateTests
    DEPENDS platform
    LIBS base arch
    SOURCE OSXKeyStateTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
elseif(UNIX)
  if(LIBEI_FOUND)
    create_test(
      NAME EiKeyStateTests
      DEPENDS platform
      LIBS base arch
      SOURCE EiKeyStateTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
  endif()

  if (BUILD_X11_SUPPORT)
    create_test(
      NAME XWindowsClipboardTests
      DEPENDS platform
      LIBS base arch
      SOURCE XWindowsClipboardTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
  endif()
endif()
