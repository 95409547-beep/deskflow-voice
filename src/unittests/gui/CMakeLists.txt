# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)

create_test(
  NAME VoiceSettingsTests
  DEPENDS gui
  SOURCE VoiceSettingsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

add_executable(VoiceSettingsPreview VoiceSettingsPreview.cpp)
target_link_libraries(VoiceSettingsPreview gui Qt::Widgets)

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
