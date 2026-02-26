include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
	set(multiValueArgs MODULES)
	cmake_parse_arguments(DECOR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

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

	set(DECORATOR_MODULE_DEPENDS "")
	set(DECORATOR_MODULES_ARG "")
	if(DECOR_MODULES)
		foreach(MODULE IN LISTS DECOR_MODULES)
			if(NOT MODULE MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
				message(FATAL_ERROR "CPPBM_ENABLE_DECORATOR: invalid module name '${MODULE}'")
			endif()

			set(MODULE_SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../${MODULE}.py")
			get_filename_component(MODULE_SCRIPT "${MODULE_SCRIPT}" ABSOLUTE)
			if(NOT EXISTS "${MODULE_SCRIPT}")
				message(FATAL_ERROR "CPPBM_ENABLE_DECORATOR: module script not found for '${MODULE}': ${MODULE_SCRIPT}")
			endif()
			list(APPEND DECORATOR_MODULE_DEPENDS "${MODULE_SCRIPT}")
		endforeach()
		string(REPLACE ";" "," DECORATOR_MODULES_ARG "${DECOR_MODULES}")
	endif()

	get_target_property(RAW_SOURCES ${DECOR_TARGET} SOURCES)
	if(NOT RAW_SOURCES)
		message(FATAL_ERROR "Target '${DECOR_TARGET}' has no SOURCES")
	endif()

	set(OUT_SOURCES "")
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
					--modules "${DECORATOR_MODULES_ARG}"
				DEPENDS "${ABS}" "${DECORATOR_SCRIPT}" ${DECORATOR_MODULE_DEPENDS}
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

