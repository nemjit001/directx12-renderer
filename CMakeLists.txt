cmake_minimum_required(VERSION 3.14)
project("Big Renderer" VERSION 0.1.0)

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

file(GLOB_RECURSE BIG_RENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
add_executable(BigRenderer ${BIG_RENDERER_SOURCES})
target_link_libraries(BigRenderer PRIVATE dxgi d3d12 d3dcompiler DirectX-Headers glm::glm SDL2::SDL2)

add_custom_target(BigRendererCopyData ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/" "${CMAKE_BINARY_DIR}/data/"
	COMMENT "BigRenderer: Copying data files..."
)
