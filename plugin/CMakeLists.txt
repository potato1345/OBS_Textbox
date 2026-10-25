cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(nlohmann_json REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs nlohmann_json::nlohmann_json)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Gui Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_QT)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/liedplan-dock.hpp
      src/liedplan-dock.cpp
      src/liedplan-render.hpp
      src/liedplan-render.cpp
      src/liedplan-card-source.hpp
      src/liedplan-card-source.cpp
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/liedplan-profiles.hpp
    src/liedplan-state.hpp
    src/liedplan-state.cpp
    src/liedplan-textfmt.hpp
    src/liedplan-html.hpp
    src/liedplan-html.cpp
    src/liedplan-scene.hpp
    src/liedplan-scene.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

enable_testing()

add_executable(
  liedplan-tests
  src/tests/test-liedplan-state.cpp
  src/liedplan-state.cpp
  src/liedplan-state.hpp
  src/liedplan-profiles.hpp
)
target_link_libraries(liedplan-tests PRIVATE nlohmann_json::nlohmann_json)

add_test(NAME liedplan-state COMMAND liedplan-tests)

add_executable(liedplan-textfmt-tests src/tests/test-liedplan-textfmt.cpp)
add_test(NAME liedplan-textfmt COMMAND liedplan-textfmt-tests)

if(ENABLE_QT)
  add_executable(
    liedplan-render-tests
    tests/render/test-liedplan-render.cpp
    src/liedplan-render.cpp
    src/liedplan-render.hpp
    src/liedplan-html.cpp
    src/liedplan-html.hpp
    src/liedplan-state.hpp
    src/liedplan-textfmt.hpp
  )
  target_compile_definitions(liedplan-render-tests PRIVATE LIEDPLAN_TEST_NO_OBS)
  target_include_directories(
    liedplan-render-tests
    PRIVATE $<TARGET_PROPERTY:OBS::libobs,INTERFACE_INCLUDE_DIRECTORIES>
  )
  target_link_libraries(liedplan-render-tests PRIVATE Qt6::Core Qt6::Gui)
  add_test(NAME liedplan-render COMMAND liedplan-render-tests)
  set_tests_properties(liedplan-render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  if(WIN32)
    add_custom_command(
      TARGET liedplan-render-tests
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:liedplan-render-tests>
        "$<TARGET_FILE_DIR:liedplan-render-tests>"
      COMMAND
        "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:liedplan-render-tests>/platforms"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
        "$<TARGET_FILE:Qt6::QMinimalIntegrationPlugin>"
        "$<TARGET_FILE_DIR:liedplan-render-tests>/platforms"
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()
