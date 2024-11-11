cmake_minimum_required(VERSION 3.14)
project("DX12 Renderer" VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("vendored/DirectX-Headers/")
add_subdirectory("vendored/glm/")
add_subdirectory("vendored/SDL/")
add_subdirectory("vendored/tinyobjloader/")

# STB project
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "vendored/stb")

# ImGUI project
add_library(imgui STATIC "vendored/imgui/imgui.cpp" "vendored/imgui/imgui_demo.cpp" "vendored/imgui/imgui_draw.cpp"
	"vendored/imgui/imgui_tables.cpp" "vendored/imgui/imgui_widgets.cpp"
	"vendored/imgui/backends/imgui_impl_sdl2.cpp" # SDL2 hook
	"vendored/imgui/backends/imgui_impl_dx12.cpp" # DirectX 12 hook
)
target_include_directories(imgui PUBLIC "vendored/imgui/" "vendored/imgui/backends/")
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# Big Renderer project
file(GLOB_RECURSE DX12_RENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
add_executable(DX12Renderer ${DX12_RENDERER_SOURCES})
target_link_libraries(DX12Renderer PRIVATE dxgi d3d12 d3dcompiler DirectX-Guids DirectX-Headers glm::glm imgui SDL2::SDL2 stb tinyobjloader)
# set_target_properties(DX12Renderer PROPERTIES COMPILE_WARNING_AS_ERROR ON)

if (MSVC)
	target_compile_options(DX12Renderer PRIVATE /W4)
else()
	target_compile_options(DX12Renderer PRIVATE -Wall -Wpedantic -Wextra)
endif()

add_custom_target(RendererCopyData ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/" "${CMAKE_BINARY_DIR}/data/"
	COMMENT "Renderer: Copying data files..."
)
