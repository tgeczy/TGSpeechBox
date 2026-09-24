# SAPI engine tests: drive the built TGSpeechSapi.dll through ISpTTSEngine with
# a fake host site (abort, rate, audio capture).  Built when both
# TGSB_BUILD_SAPI and TGSB_BUILD_TESTS are ON.
#
#   cmake --build build-x64 --config MinSizeRel --target tgsb_sapi_tests
#   build-x64/tests/sapi/MinSizeRel/tgsb_sapi_tests.exe
#
# The DLL is staged next to copies of packs/ and espeak-ng-data/, laid out as
# the installer lays it out, so the engine finds its data the way it does
# after an install and reads no user settings.ini.

set(TGSB_SAPI_STAGE "${CMAKE_CURRENT_BINARY_DIR}/stage")

add_executable(tgsb_sapi_tests test_sapi_engine.cpp)
add_dependencies(tgsb_sapi_tests TGSpeechSapi)
target_include_directories(tgsb_sapi_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../cpp")
target_compile_features(tgsb_sapi_tests PRIVATE cxx_std_17)
target_compile_definitions(tgsb_sapi_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  TGSB_SAPI_STAGE_DLL=L"${TGSB_SAPI_STAGE}/x64/TGSpeechSapi.dll"
)
target_link_libraries(tgsb_sapi_tests PRIVATE ole32 oleaut32 advapi32 sapi)

add_custom_command(TARGET tgsb_sapi_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TGSB_SAPI_STAGE}/x64"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TGSpeechSapi>" "${TGSB_SAPI_STAGE}/x64/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${TGSB_DIR}/packs" "${TGSB_SAPI_STAGE}/packs"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${TGSB_DIR}/resources/espeak-ng-data" "${TGSB_SAPI_STAGE}/espeak-ng-data"
  VERBATIM
)

add_test(NAME tgsb_sapi_tests COMMAND tgsb_sapi_tests)
