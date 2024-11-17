cmake_minimum_required(VERSION 3.14)
project("DX12 Renderer" VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

include("utils.cmake")
include("dependencies.cmake")

file(GLOB_RECURSE DX12_RENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
add_executable(DX12Renderer ${DX12_RENDERER_SOURCES})
target_include_directories(DX12Renderer PRIVATE "src/")
target_link_libraries(DX12Renderer PRIVATE dxgi d3d12 d3dcompiler DirectX-Guids DirectX-Headers glm::glm SDL2::SDL2 tinyobjloader vendored::imgui vendored::stb)
target_enable_warnings_as_errors(DX12Renderer)
target_copy_data_folder(DX12Renderer)
