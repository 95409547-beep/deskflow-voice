# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)

create_test(
  NAME VoiceBackendTests
  DEPENDS gui
  SOURCE VoiceBackendTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME VoiceSettingsTests
  DEPENDS gui
  SOURCE VoiceSettingsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

add_executable(VoiceSettingsPreview VoiceSettingsPreview.cpp)
target_link_libraries(VoiceSettingsPreview gui Qt::Widgets)

if(APPLE)
  add_executable(VoiceRuntimePreview MACOSX_BUNDLE VoiceRuntimePreview.cpp)
  target_link_libraries(VoiceRuntimePreview gui Qt6::Widgets)
  set_target_properties(VoiceRuntimePreview PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/VoiceRuntimePreview.plist.in")
endif()

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
