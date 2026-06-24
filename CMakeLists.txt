##
# @file CMakeLists.txt
# @brief 
#/

# APP_PATH
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})

# APP_NAME
get_filename_component(APP_NAME ${APP_PATH} NAME)

# APP_SRCS
aux_source_directory(${APP_PATH}/src APP_SRCS)
aux_source_directory(${APP_PATH}/tools APP_SRCS)
aux_source_directory(${APP_PATH}/cron_service APP_SRCS)
aux_source_directory(${APP_PATH}/heartbeat APP_SRCS)
aux_source_directory(${APP_PATH}/memory APP_SRCS)
aux_source_directory(${APP_PATH}/gateway APP_SRCS)
aux_source_directory(${APP_PATH}/skills APP_SRCS)
aux_source_directory(${APP_PATH}/agent APP_SRCS)

# VRM 3D renderer sources
aux_source_directory(${APP_PATH}/src/vrm APP_VRM_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/loader APP_VRM_LOADER_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/renderer APP_VRM_RENDERER_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/overlay APP_VRM_OVERLAY_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/emotion APP_VRM_EMOTION_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/spring_bone APP_VRM_SPRING_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/scene APP_VRM_SCENE_SRCS)
list(APPEND APP_SRCS
    ${APP_VRM_SRCS}
    ${APP_VRM_LOADER_SRCS}
    ${APP_VRM_RENDERER_SRCS}
    ${APP_VRM_OVERLAY_SRCS}
    ${APP_VRM_EMOTION_SRCS}
    ${APP_VRM_SPRING_SRCS}
    ${APP_VRM_SCENE_SRCS}
)

# APP_INC
set(APP_INC
    ${APP_PATH}
    ${APP_PATH}/include
    ${APP_PATH}/tools
    ${APP_PATH}/cron_service
    ${APP_PATH}/heartbeat
    ${APP_PATH}/memory
    ${APP_PATH}/gateway
    ${APP_PATH}/skills
    ${APP_PATH}/agent
    ${APP_PATH}/ai_components/assets/include
    ${APP_PATH}/third_party/stb
    ${APP_PATH}/src/vrm
    ${APP_PATH}/TuyaOpen/src/liblvgl/v9/lvgl/src/libs/tiny_ttf
)

include(${APP_PATH}/cmake/vrm_linux_deps.cmake)

########################################
# Target Configure
########################################
add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
    )

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
    )

if(VRM_RENDER_COMPILE_FLAGS)
    target_compile_options(${EXAMPLE_LIB} PRIVATE ${VRM_RENDER_COMPILE_FLAGS})
endif()

add_subdirectory(${APP_PATH}/ai_components ${CMAKE_BINARY_DIR}/ai_components)
add_subdirectory(${APP_PATH}/IM ${CMAKE_BINARY_DIR}/IM)