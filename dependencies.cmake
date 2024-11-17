
set(VENDORED_BASE_DIR "${CMAKE_SOURCE_DIR}/vendored/")

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${VENDORED_BASE_DIR}/DirectX-Headers/")
add_subdirectory("${VENDORED_BASE_DIR}/glm/")
add_subdirectory("${VENDORED_BASE_DIR}/SDL/")
add_subdirectory("${VENDORED_BASE_DIR}/tinyobjloader/")

add_library(imgui STATIC "${VENDORED_BASE_DIR}/imgui/imgui.cpp" "${VENDORED_BASE_DIR}/imgui/imgui_demo.cpp" "${VENDORED_BASE_DIR}/imgui/imgui_draw.cpp"
	"${VENDORED_BASE_DIR}/imgui/imgui_tables.cpp" "${VENDORED_BASE_DIR}/imgui/imgui_widgets.cpp"
	"${VENDORED_BASE_DIR}/imgui/backends/imgui_impl_sdl2.cpp" # SDL2 hook
	"${VENDORED_BASE_DIR}/imgui/backends/imgui_impl_dx12.cpp" # DX12 hook
)
target_include_directories(imgui PUBLIC "vendored/imgui/" "vendored/imgui/backends/")
target_link_libraries(imgui PUBLIC SDL2::SDL2 DirectX-Guids DirectX-Headers)
add_library(vendored::imgui ALIAS imgui)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${VENDORED_BASE_DIR}/stb/")
add_library(vendored::stb ALIAS stb)
