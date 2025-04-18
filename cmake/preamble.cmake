

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")

	############################################
	#          If top level cmake              #
	############################################
	if(MSVC)
		# optionally add the following to CMAKE_PREFIX_PATH
		#if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED NO_OC_DEFAULT_PREFIX)
		#	set(CMAKE_PREFIX_PATH 
		#		"c:/libs"
		#		"${CMAKE_CURRENT_SOURCE_DIR}/..;"
		#		)
		#endif()
		
	else()
		set(COMMON_FLAGS "-Wall -Wfatal-errors")

		if(NOT DEFINED NO_ARCH_NATIVE)
			set(COMMON_FLAGS "${COMMON_FLAGS} -march=native")
		endif()

		SET(CMAKE_CXX_FLAGS_RELEASE "-O3  -DNDEBUG")
		SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O2 -g -ggdb")
		SET(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -ggdb")
		#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -std=c++17")
		
	endif()



	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")
	

	############################################
	#           Build mode checks              #
	############################################

	# Set a default build type for single-configuration
	# CMake generators if no build type is set.
	if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	   SET(CMAKE_BUILD_TYPE Release)
	endif()

	if(    NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release"
       AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Debug"
       AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo" )

        message(WARNING ": Unknown build type - \${CMAKE_BUILD_TYPE}=${CMAKE_BUILD_TYPE}.  Please use one of Debug, Release, or RelWithDebInfo. e.g. call\n\tcmake . -DCMAKE_BUILD_TYPE=Release\n" )
	endif()
endif()

if(MSVC)
    set(SECUREJOIN_CONFIG_NAME "${CMAKE_BUILD_TYPE}")
    if("${SECUREJOIN_CONFIG_NAME}" STREQUAL "RelWithDebInfo" OR "${SECUREJOIN_CONFIG_NAME}" STREQUAL "")
        set(SECUREJOIN_CONFIG_NAME "Release")
	endif()
    set(SECUREJOIN_CONFIG "x64-${SECUREJOIN_CONFIG_NAME}")
elseif(APPLE)
    set(SECUREJOIN_CONFIG "osx")
else()
    set(SECUREJOIN_CONFIG "linux")
endif()

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/install.cmake)
	set(SECUREJOIN_IN_BUILD_TREE ON)
else()
	set(SECUREJOIN_IN_BUILD_TREE OFF)
endif()

if(SECUREJOIN_IN_BUILD_TREE)

	if(SECUREJOIN_BUILD)
		set(SECUREJOIN_BUILD_DIR ${CMAKE_BINARY_DIR})
	endif()
    # we currenty are in the vole psi source tree, vole-psi/cmake
	if(NOT DEFINED SECUREJOIN_BUILD_DIR)
		set(SECUREJOIN_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/../out/build/${SECUREJOIN_CONFIG}")
		get_filename_component(SECUREJOIN_BUILD_DIR ${SECUREJOIN_BUILD_DIR} ABSOLUTE)
	endif()

	if(NOT (${CMAKE_BINARY_DIR} STREQUAL ${SECUREJOIN_BUILD_DIR}))
		message(WARNING "incorrect build directory. \n\tCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}\nbut expect\n\tSECUREJOIN_BUILD_DIR=${SECUREJOIN_BUILD_DIR}")
	endif()

	if(NOT DEFINED SECUREJOIN_THIRDPARTY_DIR)
		set(SECUREJOIN_THIRDPARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/../out/install/${SECUREJOIN_CONFIG}")
		get_filename_component(SECUREJOIN_THIRDPARTY_DIR ${SECUREJOIN_THIRDPARTY_DIR} ABSOLUTE)
	endif()
else()
    # we currenty are in install tree, <install-prefix>/lib/cmake/vole-psi
	if(NOT DEFINED SECUREJOIN_THIRDPARTY_DIR)
		set(SECUREJOIN_THIRDPARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
		get_filename_component(SECUREJOIN_THIRDPARTY_DIR ${SECUREJOIN_THIRDPARTY_DIR} ABSOLUTE)
	endif()
endif()


if(NOT SECUREJOIN_THIRDPARTY_CLONE_DIR)
	get_filename_component(SECUREJOIN_THIRDPARTY_CLONE_DIR "${CMAKE_CURRENT_LIST_DIR}/../out/" ABSOLUTE)
endif()
