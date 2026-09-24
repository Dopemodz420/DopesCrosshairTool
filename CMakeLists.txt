cmake_minimum_required(VERSION 3.24)
project(DopesCrosshairTool VERSION 1.0.2 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(_WIN32_WINNT=0x0601)
# Use Unicode - note: WIN32_LEAN_AND_MEAN breaks Gdiplus, so not defined
add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

set(SOURCES
    src/main.cpp
    src/Crosshair.cpp
    src/CrosshairRenderer.cpp
    src/OverlayWindow.cpp
    src/ProcessTracker.cpp
    src/ConfigManager.cpp
    src/ConfigWindow.cpp
    src/Updater.cpp
)

set(HEADERS
    src/Crosshair.h
    src/CrosshairRenderer.h
    src/OverlayWindow.h
    src/ProcessTracker.h
    src/ConfigManager.h
    src/ConfigWindow.h
    src/Utils.h
)

add_executable(DopesCrosshairTool WIN32 ${SOURCES} ${HEADERS} resources/app.rc)

target_include_directories(DopesCrosshairTool PRIVATE src)

target_link_libraries(DopesCrosshairTool PRIVATE
    imgui
    d3d11
    dxgi
    gdiplus
    gdi32
    user32
    dwmapi
    shlwapi
    comdlg32
    winmm
    wininet
)

if(MSVC)
    target_compile_options(DopesCrosshairTool PRIVATE /W4 /permissive- /utf-8)
    set_target_properties(DopesCrosshairTool PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

set_target_properties(DopesCrosshairTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS DopesCrosshairTool DESTINATION .)
# Bundle assets — including 530 crossover pack (READ ONLY source: D:\DopesAIDevelopment\Examples\crossover-main\src\static\crosshairs)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION . OPTIONAL)
# Also ensure crossover pack is copied to build bin for dev runs
add_custom_command(TARGET DopesCrosshairTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:DopesCrosshairTool>/assets"
)
