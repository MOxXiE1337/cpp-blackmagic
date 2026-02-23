include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(CPPBM_ENABLE_PREPROCESS)
	set(options)
	set(oneValueArgs TARGET)
	cmake_parse_arguments(PATCH "${options}" "${oneValueArgs}" "" ${ARGN})

	if(NOT PATCH_TARGET)
		message(FATAL_ERROR "CPPBM_ENABLE_PREPROCESS: TARGET is required")
	endif()

	# python script path
	get_filename_component(PREPROCESS_SCRIPT
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../preprocess.py"
		ABSOLUTE
	)

	if(NOT EXISTS "${PREPROCESS_SCRIPT}")
        message(FATAL_ERROR "CPPBM_ENABLE_PREPROCESS: preprocess.py not found: ${PREPROCESS_SCRIPT}")
    endif()

	# get sources
	get_target_property(RAW_SOURCES ${PATCH_TARGET} SOURCES)

	if(NOT RAW_SOURCES)
          message(FATAL_ERROR "Target '${PATCH_TARGET}' has no SOURCES")
    endif()

	# start preprocess
	set(OUT_SOURCES "")
	foreach(SRC IN LISTS RAW_SOURCES)
		# convert to absolute path
		if(IS_ABSOLUTE ${SRC})
			set(ABS ${SRC})
		else()
			get_filename_component(ABS ${SRC} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
		endif()

		# only process C++ files
		get_filename_component(EXT ${ABS} EXT)
		if(EXT MATCHES "^\\.(cc|cpp|cxx|h|hpp|hxx)$")
			file(RELATIVE_PATH REL "${CMAKE_SOURCE_DIR}" "${ABS}")
			# output path
			set(OUT "${CMAKE_BINARY_DIR}/gen/${PATCH_TARGET}/${REL}")

			# execute python preprocess script
			add_custom_command(
				OUTPUT "${OUT}"
                COMMAND ${Python3_EXECUTABLE} "${PREPROCESS_SCRIPT}"
					--in "${ABS}"
                    --out "${OUT}"
                DEPENDS "${ABS}" "${PREPROCESS_SCRIPT}"
                COMMENT "Preprocess ${REL}"
                VERBATIM
             )

			list(APPEND OUT_SOURCES ${OUT})
		endif()

	endforeach()

	# replace sources
	set_property(TARGET ${PATCH_TARGET} PROPERTY SOURCES ${OUT_SOURCES})
endfunction()
