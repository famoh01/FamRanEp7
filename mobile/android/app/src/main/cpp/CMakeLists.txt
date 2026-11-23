# AI EP7 native library (original code). Built by Gradle through externalNativeBuild.
cmake_minimum_required(VERSION 3.22.1)
project(aiep7 C CXX)

# mobile/android/app/src/main/cpp -> mobile/
get_filename_component(MOBILE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
if(NOT EXISTS "${MOBILE_ROOT}/touch_hud/touch_hud.h")
    message(FATAL_ERROR "touch_hud not found at ${MOBILE_ROOT}/touch_hud")
endif()
if(NOT EXISTS "${MOBILE_ROOT}/net_client/api_client.h")
    message(FATAL_ERROR "net_client not found at ${MOBILE_ROOT}/net_client")
endif()
if(NOT EXISTS "${MOBILE_ROOT}/assets/asset_manifest.h")
    message(FATAL_ERROR "asset loader not found at ${MOBILE_ROOT}/assets")
endif()

set(NATIVE_APP_GLUE "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")

add_library(aiep7 SHARED
    main.cpp
    net_bridge.cpp
    "${MOBILE_ROOT}/net_client/api_client.cpp"
    "${MOBILE_ROOT}/net_client/http_client.cpp"
    "${MOBILE_ROOT}/net_client/world_state.cpp"
    "${MOBILE_ROOT}/net_client/world_flow.cpp"
    "${MOBILE_ROOT}/net_client/world_map.cpp"
    "${MOBILE_ROOT}/net_client/world_realtime.cpp"
    "${MOBILE_ROOT}/net_client/combat_session.cpp"
    "${MOBILE_ROOT}/net_client/social_view.cpp"
    "${MOBILE_ROOT}/net_client/ui_strings.cpp"
    "${MOBILE_ROOT}/assets/asset_manifest.cpp"
    "${NATIVE_APP_GLUE}/android_native_app_glue.c")

target_include_directories(aiep7 PRIVATE
    "${MOBILE_ROOT}/touch_hud"
    "${MOBILE_ROOT}/net_client"
    "${MOBILE_ROOT}/assets"
    "${NATIVE_APP_GLUE}")

target_compile_features(aiep7 PRIVATE cxx_std_17)
target_compile_options(aiep7 PRIVATE -Wall -Wextra)

target_link_libraries(aiep7
    android
    log
    EGL
    GLESv3
    m)
