include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
option(CPPBM_PREPROCESS_STRICT_PARSER "Fail preprocess when tree_sitter parser is unavailable or parse fails." OFF)

# Keep a stable Python executable across subdirectories/targets.
if(NOT DEFINED CPPBM_PYTHON_EXECUTABLE OR CPPBM_PYTHON_EXECUTABLE STREQUAL "")
	set(CPPBM_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Python interpreter for cpp-blackmagic preprocess scripts")
endif()

if(NOT CPPBM_PYTHON_EXECUTABLE)
	message(FATAL_ERROR "CPPBM preprocess requires Python interpreter, but CPPBM_PYTHON_EXECUTABLE is empty.")
endif()

function(_CPPBM_GET_RELATIVE_SOURCE_PATH ABS TARGET OUT_REL)
	file(TO_CMAKE_PATH "${ABS}" ABS_NORM)
	file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" BIN_NORM)
	set(GEN_PREFIX "${BIN_NORM}/gen/${TARGET}/")

	set(REL "")

	string(LENGTH "${GEN_PREFIX}" GEN_PREFIX_LEN)
	string(LENGTH "${ABS_NORM}" ABS_LEN)
	if(ABS_LEN GREATER_EQUAL GEN_PREFIX_LEN)
		string(SUBSTRING "${ABS_NORM}" 0 ${GEN_PREFIX_LEN} HEAD)
		if(HEAD STREQUAL "${GEN_PREFIX}")
			string(SUBSTRING "${ABS_NORM}" ${GEN_PREFIX_LEN} -1 REL)
		endif()
	endif()

	if(REL STREQUAL "")
		file(RELATIVE_PATH REL "${CMAKE_SOURCE_DIR}" "${ABS}")
		if(REL MATCHES "^\\.\\.")
			get_filename_component(FILE_NAME "${ABS}" NAME)
			set(REL "${FILE_NAME}")
		endif()
	endif()

	set(${OUT_REL} "${REL}" PARENT_SCOPE)
endfunction()

function(CPPBM_ENABLE_DECORATOR)
	set(options)
	set(oneValueArgs TARGET)
	cmake_parse_arguments(DECOR "${options}" "${oneValueArgs}" "" ${ARGN})

	if(NOT DECOR_TARGET)
		message(FATAL_ERROR "CPPBM_ENABLE_DECORATOR: TARGET is required")
	endif()

	get_filename_component(DECORATOR_SCRIPT
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../decorator.py"
		ABSOLUTE
	)
	if(NOT EXISTS "${DECORATOR_SCRIPT}")
		message(FATAL_ERROR "CPPBM_ENABLE_DECORATOR: decorator.py not found: ${DECORATOR_SCRIPT}")
	endif()

	get_target_property(RAW_SOURCES ${DECOR_TARGET} SOURCES)
	if(NOT RAW_SOURCES)
		message(FATAL_ERROR "Target '${DECOR_TARGET}' has no SOURCES")
	endif()

	set(OUT_SOURCES "")
	set(DECORATOR_EXTRA_ARGS "")
	if(CPPBM_PREPROCESS_STRICT_PARSER)
		list(APPEND DECORATOR_EXTRA_ARGS --strict-parser)
	endif()
	foreach(SRC IN LISTS RAW_SOURCES)
		if(IS_ABSOLUTE "${SRC}")
			set(ABS "${SRC}")
		else()
			get_filename_component(ABS "${SRC}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
		endif()

		get_filename_component(EXT "${ABS}" EXT)
		if(EXT MATCHES "^\\.(cc|cpp|cxx|h|hpp|hxx)$")
			_CPPBM_GET_RELATIVE_SOURCE_PATH("${ABS}" "${DECOR_TARGET}" REL)
			set(OUT "${CMAKE_BINARY_DIR}/cppbm-gen/${DECOR_TARGET}/${REL}")

			add_custom_command(
				OUTPUT "${OUT}"
				COMMAND "${CPPBM_PYTHON_EXECUTABLE}" "${DECORATOR_SCRIPT}"
					--in "${ABS}"
					--out "${OUT}"
					${DECORATOR_EXTRA_ARGS}
				DEPENDS "${ABS}" "${DECORATOR_SCRIPT}"
				COMMENT "Decorator preprocess ${REL}"
				VERBATIM
			)

			list(APPEND OUT_SOURCES "${OUT}")
		else()
			list(APPEND OUT_SOURCES "${ABS}")
		endif()
	endforeach()

	set_property(TARGET ${DECOR_TARGET} PROPERTY SOURCES ${OUT_SOURCES})
endfunction()

function(CPPBM_ENABLE_DEPENDENCY_INJECT)
	set(options)
	set(oneValueArgs TARGET)
	cmake_parse_arguments(INJECT "${options}" "${oneValueArgs}" "" ${ARGN})

	if(NOT INJECT_TARGET)
		message(FATAL_ERROR "CPPBM_ENABLE_DEPENDENCY_INJECT: TARGET is required")
	endif()

	get_filename_component(INJECT_SCRIPT
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../inject.py"
		ABSOLUTE
	)
	if(NOT EXISTS "${INJECT_SCRIPT}")
		message(FATAL_ERROR "CPPBM_ENABLE_DEPENDENCY_INJECT: inject.py not found: ${INJECT_SCRIPT}")
	endif()

	get_target_property(RAW_SOURCES ${INJECT_TARGET} SOURCES)
	if(NOT RAW_SOURCES)
		message(FATAL_ERROR "Target '${INJECT_TARGET}' has no SOURCES")
	endif()

	set(INJECT_INPUTS "")
	set(INJECT_COMMANDS "")
	set(INJECT_EXTRA_ARGS "")
	if(CPPBM_PREPROCESS_STRICT_PARSER)
		list(APPEND INJECT_EXTRA_ARGS --strict-parser)
	endif()
	foreach(SRC IN LISTS RAW_SOURCES)
		if(IS_ABSOLUTE "${SRC}")
			set(ABS "${SRC}")
		else()
			get_filename_component(ABS "${SRC}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
		endif()

		get_filename_component(EXT "${ABS}" EXT)
		if(EXT MATCHES "^\\.(cc|cpp|cxx|h|hpp|hxx)$")
			list(APPEND INJECT_INPUTS "${ABS}")
			list(APPEND INJECT_COMMANDS
				COMMAND "${CPPBM_PYTHON_EXECUTABLE}" "${INJECT_SCRIPT}" --in "${ABS}" --out "${ABS}" ${INJECT_EXTRA_ARGS})
		endif()
	endforeach()

	set(INJECT_TARGET_NAME "${INJECT_TARGET}__cppbm_inject_pass")
	if(TARGET ${INJECT_TARGET_NAME})
		message(FATAL_ERROR "CPPBM_ENABLE_DEPENDENCY_INJECT was called multiple times for target '${INJECT_TARGET}'.")
	else()
		# Remove legacy stamp directory from older implementation.
		file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/cppbm-gen/${INJECT_TARGET}/.inject_stamps")

		add_custom_target(${INJECT_TARGET_NAME}
			DEPENDS ${INJECT_INPUTS}
			${INJECT_COMMANDS}
			COMMENT "Inject preprocess for target ${INJECT_TARGET}"
			VERBATIM
		)
	endif()
	add_dependencies(${INJECT_TARGET} ${INJECT_TARGET_NAME})
endfunction()
