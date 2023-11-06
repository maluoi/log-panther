cmake_minimum_required(VERSION 3.17)
project(log-panther
        VERSION 1.0
        DESCRIPTION "A nice tool for looking at ADB logcats")

# For creating application installer
set(CPACK_PACKAGE_NAME                 ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR               ${PROJECT_NAME})
set(CPACK_PACKAGE_INSTALL_DIRECTORY    ${PROJECT_NAME})
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION              ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION          ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY  ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_HOMEPAGE_URL         "https://github.com/maluoi/log-panther")
set(CPACK_PACKAGE_EXECUTABLES          log-panther;log-panther)
SET(CPACK_NSIS_INSTALLED_ICON_NAME     "bin/log-panther.exe")
SET(CPACK_NSIS_IGNORE_LICENSE_PAGE     "ON")
include(CPack)

set(CMAKE_CXX_STANDARD 20)
set(LIBRARIES_DIR lib)
set(IMGUI_DIR "${LIBRARIES_DIR}/imgui")
set(GLAD_DIR "${LIBRARIES_DIR}/glad")
set(GLFW_DIR "${LIBRARIES_DIR}/glfw")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(log-panther
        vendor/imgui_impl_glfw.h
        vendor/imgui_impl_glfw.cpp
        vendor/imgui_impl_opengl3.h
        vendor/imgui_impl_opengl3.cpp
        "${IMGUI_DIR}/src/imgui.cpp"
        "${IMGUI_DIR}/src/imgui_draw.cpp"
        "${IMGUI_DIR}/src/imgui_demo.cpp"
        "${IMGUI_DIR}/src/imgui_tables.cpp"
        "${IMGUI_DIR}/src/imgui_widgets.cpp"
        src/main.cpp
        src/logdata.cpp
        src/device_finder.cpp
        src/resources.rc)

target_include_directories(${PROJECT_NAME} PRIVATE "${IMGUI_DIR}/include")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                   ${CMAKE_SOURCE_DIR}/src/CascadiaMono.ttf
                   $<TARGET_FILE_DIR:${PROJECT_NAME}>)

install(TARGETS ${PROJECT_NAME})

# glad
add_library("glad" "${GLAD_DIR}/src/glad.c")
target_include_directories("glad" PRIVATE "${GLAD_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${GLAD_DIR}/include")
target_link_libraries(${PROJECT_NAME} "glad" "${CMAKE_DL_LIBS}")

# glfw
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")
add_subdirectory("${GLFW_DIR}")
target_link_libraries(${PROJECT_NAME} "glfw" "${GLFW_LIBRARIES}")
target_include_directories(${PROJECT_NAME} PRIVATE "${GLFW_DIR}/include")
target_compile_definitions(${PROJECT_NAME} PRIVATE "GLFW_INCLUDE_NONE")