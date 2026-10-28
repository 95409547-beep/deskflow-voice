# SPDX-FileCopyrightText: (C) 2026 Deskflow Voice contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(DeskflowVoiceCore LANGUAGES CXX)
  include(CTest)
endif()

add_library(voice_core STATIC Session.cpp Session.h KeyGate.cpp KeyGate.h)
target_compile_features(voice_core PUBLIC cxx_std_20)
target_include_directories(voice_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_target_properties(voice_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(TARGET Qt6::Network)
  add_library(voice_control STATIC control/Protocol.cpp control/Protocol.h control/ControlServer.cpp control/ControlServer.h)
  target_link_libraries(voice_control PUBLIC voice_core Qt6::Core Qt6::Network)
  target_compile_features(voice_control PUBLIC cxx_std_20)
endif()

if(BUILD_TESTING OR BUILD_TESTS)
  enable_testing()
  add_executable(VoiceSessionTests tests/SessionTests.cpp)
  target_link_libraries(VoiceSessionTests PRIVATE voice_core)
  set_target_properties(VoiceSessionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  add_executable(VoiceKeyGateTests tests/KeyGateTests.cpp)
  target_link_libraries(VoiceKeyGateTests PRIVATE voice_core)
  set_target_properties(VoiceKeyGateTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_test(NAME VoiceSessionTests COMMAND VoiceSessionTests)
    add_test(NAME VoiceKeyGateTests COMMAND VoiceKeyGateTests)
  endif()
endif()
