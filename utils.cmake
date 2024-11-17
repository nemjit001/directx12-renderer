
function(target_enable_warnings_as_errors TARGET_NAME)
	message(STATUS "${TARGET_NAME}: Compiling warnings as errors")
	# set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
	if (MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE /W4)
	else()
		target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
endfunction()

function(target_copy_data_folder TARGET_NAME)
	add_custom_target(${TARGET_NAME}_DataCopy ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/" "$<TARGET_FILE_DIR:${TARGET_NAME}>/data/"
		COMMENT "${TARGET_NAME}: Copying data folder ${CMAKE_SOURCE_DIR}/data/ -> $<TARGET_FILE_DIR:${TARGET_NAME}>/data/"
	)
endfunction()
