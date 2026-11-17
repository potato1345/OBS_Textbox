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
if(WIN32)
  # Windows: statische zlib aus obs-deps
  set(ZLIB_USE_STATIC_LIBS ON)
endif()
find_package(ZLIB REQUIRED)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs nlohmann_json::nlohmann_json ZLIB::ZLIB
)
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp)
endif()

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
endif()

if(APPLE)
  # libobs.framework-Header nutzen Double-Quote-Includes; Xcode-Default macht
  # das mit -Werror zum Fehler. OBS-Template unterdrückt diese Warnung.
  add_compile_options(-Wno-quoted-include-in-framework-header -Wno-comma)
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
      src/textbox-dock.hpp
      src/textbox-dock.cpp
      src/textbox-theme.hpp
      src/textbox-render.hpp
      src/textbox-render.cpp
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/textbox-profiles.hpp
    src/textbox-state.hpp
    src/textbox-state.cpp
    src/textbox-textfmt.hpp
    src/textbox-html.hpp
    src/textbox-html.cpp
    src/textbox-scene.hpp
    src/textbox-scene.cpp
    src/textbox-import.hpp
    src/textbox-import.cpp
    src/textbox-ai.hpp
    src/textbox-ai.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# IMP-D (#5): Overlay-Template in das OBS-Plugin-Data-Verzeichnis mitinstallieren
# (analog target_install_resources -> ${CMAKE_PROJECT_NAME}/data für obs_module_file()).
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/data/index.html"
  DESTINATION "${CMAKE_PROJECT_NAME}/data"
  COMPONENT Runtime
  OPTIONAL
)

# IMP-F (F2b): Vorlagen ins Data-Verzeichnis ("Vorlagen-Ordner") — nur die
# Standard-Vorlage wird geshiped (15.11: Beispiel-Vorlagen entfernt).
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates/"
  DESTINATION "${CMAKE_PROJECT_NAME}/data/templates"
  COMPONENT Runtime
  OPTIONAL
  FILES_MATCHING PATTERN "*.html"
)

# 15.11: Extra Ordner für die mit dem Plugin geshiped Animationen.
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/animations/"
  DESTINATION "${CMAKE_PROJECT_NAME}/data/animations"
  COMPONENT Runtime
  OPTIONAL
  FILES_MATCHING PATTERN "*.webp" PATTERN "*.gif" PATTERN "*.png" PATTERN "*.md"
)

enable_testing()

add_executable(
  textbox-tests
  src/tests/test-textbox-state.cpp
  src/textbox-state.cpp
  src/textbox-state.hpp
  src/textbox-profiles.hpp
)
target_link_libraries(textbox-tests PRIVATE nlohmann_json::nlohmann_json)

add_test(NAME textbox-state COMMAND textbox-tests)

add_executable(textbox-textfmt-tests src/tests/test-textbox-textfmt.cpp)
add_test(NAME textbox-textfmt COMMAND textbox-textfmt-tests)

add_executable(textbox-theme-icons-tests src/tests/test-textbox-theme-icons.cpp)
add_test(NAME textbox-theme-icons COMMAND textbox-theme-icons-tests)

add_executable(
  textbox-import-tests
  src/tests/test-textbox-import.cpp
  src/textbox-import.cpp
  src/textbox-import.hpp
)
target_link_libraries(textbox-import-tests PRIVATE nlohmann_json::nlohmann_json ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(textbox-import-tests PRIVATE winhttp)
endif()
target_compile_definitions(textbox-import-tests PRIVATE
  TEXTBOX_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME textbox-import COMMAND textbox-import-tests)

add_executable(
  textbox-ai-tests
  src/tests/test-textbox-ai.cpp
  src/textbox-ai.cpp
  src/textbox-ai.hpp
)
target_compile_definitions(
  textbox-ai-tests
  PRIVATE TEXTBOX_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(textbox-ai-tests PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(textbox-ai-tests PRIVATE winhttp)
endif()
add_test(NAME textbox-ai COMMAND textbox-ai-tests)

if(ENABLE_QT)
  add_executable(
    textbox-render-tests
    tests/render/test-textbox-render.cpp
    src/textbox-render.cpp
    src/textbox-render.hpp
    src/textbox-html.cpp
    src/textbox-html.hpp
    src/textbox-state.cpp
    src/textbox-state.hpp
    src/textbox-profiles.hpp
    src/textbox-textfmt.hpp
  )
  target_compile_definitions(
    textbox-render-tests
    PRIVATE
      TEXTBOX_TEST_NO_OBS
      TEXTBOX_DEV_REPO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/index.html"
  )
  target_include_directories(
    textbox-render-tests
    PRIVATE $<TARGET_PROPERTY:OBS::libobs,INTERFACE_INCLUDE_DIRECTORIES>
  )
  target_link_libraries(textbox-render-tests PRIVATE Qt6::Core Qt6::Gui nlohmann_json::nlohmann_json)
  add_test(NAME textbox-render COMMAND textbox-render-tests)
  set_tests_properties(textbox-render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(
    test-textbox-html
    src/tests/test-textbox-html.cpp
    src/textbox-html.cpp
    src/textbox-html.hpp
    src/textbox-state.cpp
    src/textbox-state.hpp
    src/textbox-profiles.hpp
    src/textbox-textfmt.hpp
  )
  target_compile_definitions(
    test-textbox-html
    PRIVATE
      TEXTBOX_TEST_NO_OBS
      TEXTBOX_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
      TEXTBOX_DEV_REPO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/index.html"
  )
  target_include_directories(
    test-textbox-html
    PRIVATE $<TARGET_PROPERTY:OBS::libobs,INTERFACE_INCLUDE_DIRECTORIES>
  )
  target_link_libraries(test-textbox-html PRIVATE Qt6::Core nlohmann_json::nlohmann_json)
  add_test(NAME test-textbox-html COMMAND test-textbox-html)

  add_executable(
    test-textbox-templates
    src/tests/test-textbox-templates.cpp
    src/textbox-html.cpp
    src/textbox-html.hpp
    src/textbox-state.cpp
    src/textbox-state.hpp
    src/textbox-profiles.hpp
    src/textbox-textfmt.hpp
  )
  target_compile_definitions(
    test-textbox-templates
    PRIVATE
      TEXTBOX_TEST_NO_OBS
      TEXTBOX_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
      TEXTBOX_DEV_REPO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/index.html"
  )
  target_include_directories(
    test-textbox-templates
    PRIVATE $<TARGET_PROPERTY:OBS::libobs,INTERFACE_INCLUDE_DIRECTORIES>
  )
  target_link_libraries(test-textbox-templates PRIVATE Qt6::Core nlohmann_json::nlohmann_json)
  add_test(NAME test-textbox-templates COMMAND test-textbox-templates)

  if(WIN32)
    add_custom_command(
      TARGET textbox-render-tests
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:textbox-render-tests>
        "$<TARGET_FILE_DIR:textbox-render-tests>"
      COMMAND
        "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:textbox-render-tests>/platforms"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
        "$<TARGET_FILE:Qt6::QMinimalIntegrationPlugin>"
        "$<TARGET_FILE_DIR:textbox-render-tests>/platforms"
      COMMAND_EXPAND_LISTS
    )
    add_custom_command(
      TARGET test-textbox-html
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:test-textbox-html>
        "$<TARGET_FILE_DIR:test-textbox-html>"
      COMMAND_EXPAND_LISTS
    )
    add_custom_command(
      TARGET test-textbox-templates
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:test-textbox-templates>
        "$<TARGET_FILE_DIR:test-textbox-templates>"
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()
