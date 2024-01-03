
set(USER_NAME           )      
set(TOKEN               )      
set(GIT_REPOSITORY      "https://github.com/osu-crypto/libOTe.git")
set(GIT_TAG             "cf537295c47a3924c13030a9b796cee9d6ebeace")

set(DEP_NAME            libOTe)          
set(CLONE_DIR "${SECUREJOIN_THIRDPARTY_CLONE_DIR}/${DEP_NAME}")
set(BUILD_DIR "${CLONE_DIR}/out/build/${SECUREJOIN_CONFIG}")
set(LOG_FILE  "${CMAKE_CURRENT_LIST_DIR}/log-${DEP_NAME}.txt")


include("${CMAKE_CURRENT_LIST_DIR}/fetch.cmake")

option(LIBOTE_DEV "always build libOTe" OFF)

if(NOT ${DEP_NAME}_FOUND OR LIBOTE_DEV)
    if(APPLE)
        set(LIBOTE_OS_ARGS -DENABLE_RELIC=ON )
    else()
        set(LIBOTE_OS_ARGS -DENABLE_SODIUM=ON -DENABLE_MRR_TWIST=ON)
    endif()
    string (REPLACE ";" "%" CMAKE_PREFIX_PATH_STR "${CMAKE_PREFIX_PATH}")

    find_program(GIT git REQUIRED)
    set(DOWNLOAD_CMD  ${GIT} clone --recursive ${GIT_REPOSITORY})
    set(CHECKOUT_CMD  ${GIT} checkout ${GIT_TAG})
    set(SUBMODULE_CMD   ${GIT} submodule update --recursive)
    set(CONFIGURE_CMD ${CMAKE_COMMAND} -S ${CLONE_DIR} -B ${BUILD_DIR} -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                       -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH_STR}
                       -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE} 
                       -DNO_CMAKE_SYSTEM_PATH=${NO_CMAKE_SYSTEM_PATH}
                       -DFETCH_AUTO=${FETCH_AUTO}
                       -DVERBOSE_FETCH=${VERBOSE_FETCH}
                       -DENABLE_CIRCUITS=ON
                       -DENABLE_MRR=ON
                       -DENABLE_IKNP=ON
                       -DENABLE_SOFTSPOKEN_OT=ON
                       -DENABLE_SILENTOT=ON
                       -DENABLE_SILENT_VOLE=ON
                       ${LIBOTE_OS_ARGS}
                       -DENABLE_SSE=${SECUREJOIN_ENABLE_SSE}
                       -DOC_THIRDPARTY_CLONE_DIR=${SECUREJOIN_THIRDPARTY_CLONE_DIR}
                       -DENABLE_ASAN=${SECUREJOIN_ENABLE_ASAN}
                       -DENABLE_BOOST=${SECUREJOIN_ENABLE_BOOST}
                       -DOC_THIRDPARTY_INSTALL_PREFIX=${SECUREJOIN_THIRDPARTY_DIR}
                       -DENABLE_PIC=${SECUREJOIN_ENABLE_PIC}
                       )
    set(BUILD_CMD     ${CMAKE_COMMAND} --build ${BUILD_DIR} --config ${CMAKE_BUILD_TYPE})
    set(INSTALL_CMD   ${CMAKE_COMMAND} --install ${BUILD_DIR} --config ${CMAKE_BUILD_TYPE} --prefix ${SECUREJOIN_THIRDPARTY_DIR})
    

    message("============= Building ${DEP_NAME} =============")
    if(NOT EXISTS ${CLONE_DIR})
        run(NAME "Cloning ${GIT_REPOSITORY}" CMD ${DOWNLOAD_CMD} WD ${SECUREJOIN_THIRDPARTY_CLONE_DIR})
    endif()


    run(NAME "libOTe Checkout ${GIT_TAG} " CMD ${CHECKOUT_CMD}  WD ${CLONE_DIR})
    run(NAME "libOTe submodule"       CMD ${SUBMODULE_CMD} WD ${CLONE_DIR})
    run(NAME "libOTe Configure"       CMD ${CONFIGURE_CMD} WD ${CLONE_DIR})
    run(NAME "libOTe Build"           CMD ${BUILD_CMD}     WD ${CLONE_DIR})
    run(NAME "libOTe Install"         CMD ${INSTALL_CMD}   WD ${CLONE_DIR})

    message("log ${LOG_FILE}\n==========================================")
else()
    message("${DEP_NAME} already fetched.")
endif()

install(CODE "
    if(NOT CMAKE_INSTALL_PREFIX STREQUAL \"${SECUREJOIN_THIRDPARTY_CLONE_DIR}\")
        execute_process(
            COMMAND ${SUDO} \${CMAKE_COMMAND} --install ${BUILD_DIR}  --config ${CMAKE_BUILD_TYPE} --prefix \${CMAKE_INSTALL_PREFIX}
            WORKING_DIRECTORY ${CLONE_DIR}
            RESULT_VARIABLE RESULT
            COMMAND_ECHO STDOUT
        )
    endif()
")
