cmake_minimum_required(VERSION 3.21)

# This is a Win32 GUI utility for editing TGSB YAML packs.
# It is intentionally kept "plain Win32" (no WPF/MFC/Qt).

set(TGSB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(tgsbPhonemeEditor WIN32
  main.cpp
  AppController.cpp
  AppController.h
  chunking.cpp
  chunking.h
  phonemizer_cli.cpp
  phonemizer_cli.h
  Dialogs.cpp
  Dialogs.h
  VoiceProfileEditor.cpp
  VoiceProfileEditor.h
  RulesEditor.cpp
  RulesEditor.h
  WinUtils.cpp
  WinUtils.h
  AccessibilityUtils.cpp
  AccessibilityUtils.h
  yaml_edit.cpp
  yaml_edit.h
  tgsb_runtime.cpp
  tgsb_runtime.h
  process_util.cpp
  process_util.h
  wav_writer.cpp
  wav_writer.h
  resource.h
  resources.rc

  # Reuse TGSB's small YAML + UTF-8 helpers (no external deps).
  "${TGSB_ROOT}/src/frontend/yaml_min.cpp"
  "${TGSB_ROOT}/src/frontend/yaml_min.h"
  "${TGSB_ROOT}/src/frontend/utf8.cpp"
  "${TGSB_ROOT}/src/frontend/utf8.h"
)

target_include_directories(tgsbPhonemeEditor PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${TGSB_ROOT}/src"
  "${TGSB_ROOT}/src/frontend"
)

target_compile_features(tgsbPhonemeEditor PRIVATE cxx_std_17)

# For IPA symbols in string literals.
if(MSVC)
  target_compile_options(tgsbPhonemeEditor PRIVATE /utf-8)
endif()

if(WIN32)
  target_link_libraries(tgsbPhonemeEditor PRIVATE
    comctl32
    oleacc
    ole32
    shell32
    shlwapi
    winmm
  )
endif()
