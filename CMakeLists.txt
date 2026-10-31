cmake_minimum_required(VERSION 3.25)

project(plusplusc
    VERSION 0.1.0
    DESCRIPTION "++C"
    LANGUAGES C ASM
)

if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(XXC_FREEBSD ON)
endif()

if (NOT (WIN32 OR LINUX OR XXC_FREEBSD))
    message(FATAL_ERROR "++C only supports Linux, FreeBSD and Windows targets (got: ${CMAKE_SYSTEM_NAME})")
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(XXC_BUILD_TEST "Build the ++C test executable" ${PROJECT_IS_TOP_LEVEL})
option(XXC_BUILD_DEMO "Build the ++C demo executable" ${PROJECT_IS_TOP_LEVEL})
option(XXC_STATIC_PIE "Link static executables as -static-pie (self-relocating) instead of -static -no-pie" ON)
option(XXC_TESTS_USES_SHARED "Link the tests against the shared minicrt/xxc libraries when available" OFF)
option(XXC_DEMO_USES_SHARED "Link the demo against the shared minicrt/xxc libraries when available" OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileOptions.cmake")

add_subdirectory(platform)
add_subdirectory(libminicrt)
add_subdirectory(libxxc)

if (TARGET sys_win32)
    set(XXC_EXEC_CRT0 $<TARGET_OBJECTS:sys_win32>)
elseif (TARGET minicrt_crt0)
    set(XXC_EXEC_CRT0 $<TARGET_OBJECTS:minicrt_crt0>)
else()
    set(XXC_EXEC_CRT0)
endif()

if (WIN32 AND (XXC_TESTS_USES_SHARED OR XXC_DEMO_USES_SHARED))
    set(XXC_EXEC_STARTUP "${CMAKE_CURRENT_SOURCE_DIR}/libminicrt/src/crt1.c" $<TARGET_OBJECTS:xxc_ssp>)
else()
    set(XXC_EXEC_STARTUP)
endif()

if (XXC_BUILD_TEST)
    add_custom_target(check)

    function(xxc_add_test name source)
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/${source}" PROPERTIES LANGUAGE C)
        add_executable(xxc_${name} "${CMAKE_CURRENT_SOURCE_DIR}/${source}" ${XXC_EXEC_STARTUP} ${XXC_EXEC_CRT0})
        set_target_properties(xxc_${name} PROPERTIES OUTPUT_NAME ${name})

        set(XXC_TEST_LIB xxc::xxc)
        if (XXC_TESTS_USES_SHARED)
            if (NOT TARGET xxc::xxc_shared)
                message(FATAL_ERROR "XXC_TESTS_USES_SHARED requires the shared xxc target to exist")
            endif()
            set(XXC_TEST_LIB xxc::xxc_shared)
        endif()

        if (XXC_TESTS_USES_SHARED)
            target_compile_definitions(xxc_${name} PRIVATE XXC_USING_SHARED_LIB)
        endif()

        if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(xxc_${name} PRIVATE
                -Wno-literal-range
                -Wno-format
            )
        endif()

        target_link_libraries(xxc_${name} PRIVATE ${XXC_TEST_LIB})
        if (XXC_TESTS_USES_SHARED)
            set_target_properties(xxc_${name} PROPERTIES
                BUILD_RPATH "${CMAKE_BINARY_DIR}"
                INSTALL_RPATH "$ORIGIN"
            )
        endif()
        xxc_freestanding_compile_options(xxc_${name} PRIVATE)
        xxc_freestanding_link_options(xxc_${name})
        add_custom_target(run_${name} COMMAND xxc_${name} DEPENDS xxc_${name} USES_TERMINAL)
        add_dependencies(check run_${name})
    endfunction()

    xxc_add_test(test         test/test.ppc)
    xxc_add_test(leak_test    test/leak.ppc)
    xxc_add_test(initfini_test test/initfini.ppc)
    xxc_add_test(stackframe_test test/stackframe.ppc)

    if (XXC_TESTS_USES_SHARED AND (LINUX OR XXC_FREEBSD))
        foreach(_n a b)
            set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/test/shfini_${_n}.c" PROPERTIES LANGUAGE C)
            add_library(shfini_${_n} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/test/shfini_${_n}.c")
            target_compile_definitions(shfini_${_n} PRIVATE XXC_USING_SHARED_LIB)
            target_link_libraries(shfini_${_n} PRIVATE xxc::xxc_shared)
            set_target_properties(shfini_${_n} PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}" INSTALL_RPATH "$ORIGIN")
            xxc_freestanding_compile_options(shfini_${_n} PRIVATE)
            xxc_freestanding_link_options(shfini_${_n})
        endforeach()
        target_link_libraries(shfini_b PUBLIC shfini_a)

        xxc_add_test(shfini_test test/shfini.ppc)
        target_link_libraries(xxc_shfini_test PRIVATE shfini_b)
        add_custom_target(check_shfini
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:xxc_shfini_test>
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckShfini.cmake"
            DEPENDS xxc_shfini_test USES_TERMINAL)
        add_dependencies(check check_shfini)
    endif()
endif()

if (XXC_BUILD_DEMO)
    set(XXC_DEMO_LIB xxc::xxc)
    if (XXC_DEMO_USES_SHARED)
        if (NOT TARGET xxc::xxc_shared)
            message(FATAL_ERROR "XXC_DEMO_USES_SHARED requires the shared xxc target to exist")
        endif()
        set(XXC_DEMO_LIB xxc::xxc_shared)
    endif()

    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/app-demo.ppc"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/socket-demo.ppc"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-demo.ppc"
        PROPERTIES LANGUAGE C
    )

    add_executable(xxc_demo_app "${CMAKE_CURRENT_SOURCE_DIR}/examples/app-demo.ppc" ${XXC_EXEC_STARTUP} ${XXC_EXEC_CRT0})
    set_target_properties(xxc_demo_app PROPERTIES OUTPUT_NAME app)
    if (XXC_DEMO_USES_SHARED)
        target_compile_definitions(xxc_demo_app PRIVATE XXC_USING_SHARED_LIB)
    endif()
    target_link_libraries(xxc_demo_app PRIVATE ${XXC_DEMO_LIB})
    if (XXC_DEMO_USES_SHARED)
        set_target_properties(xxc_demo_app PROPERTIES
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "$ORIGIN"
        )
    endif()

    xxc_freestanding_compile_options(xxc_demo_app PRIVATE)
    xxc_freestanding_link_options(xxc_demo_app)

    add_executable(xxc_demo_socket "${CMAKE_CURRENT_SOURCE_DIR}/examples/socket-demo.ppc" ${XXC_EXEC_STARTUP} ${XXC_EXEC_CRT0})
    set_target_properties(xxc_demo_socket PROPERTIES OUTPUT_NAME socket)
    if (XXC_DEMO_USES_SHARED)
        target_compile_definitions(xxc_demo_socket PRIVATE XXC_USING_SHARED_LIB)
    endif()
    target_link_libraries(xxc_demo_socket PRIVATE ${XXC_DEMO_LIB})
    if (XXC_DEMO_USES_SHARED)
        set_target_properties(xxc_demo_socket PROPERTIES
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "$ORIGIN"
        )
    endif()

    xxc_freestanding_compile_options(xxc_demo_socket PRIVATE)
    xxc_freestanding_link_options(xxc_demo_socket)

    add_executable(xxc_demo_http "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-demo.ppc" ${XXC_EXEC_STARTUP} ${XXC_EXEC_CRT0})
    set_target_properties(xxc_demo_http PROPERTIES OUTPUT_NAME http)
    if (XXC_DEMO_USES_SHARED)
        target_compile_definitions(xxc_demo_http PRIVATE XXC_USING_SHARED_LIB)
    endif()
    target_link_libraries(xxc_demo_http PRIVATE ${XXC_DEMO_LIB})
    if (XXC_DEMO_USES_SHARED)
        set_target_properties(xxc_demo_http PROPERTIES
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "$ORIGIN"
        )
    endif()

    xxc_freestanding_compile_options(xxc_demo_http PRIVATE)
    xxc_freestanding_link_options(xxc_demo_http)
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "plusplusc")
set(CPACK_PACKAGE_NAME "plusplusc")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_LIBXXC_PACKAGE_NAME "libxxc")
set(CPACK_DEBIAN_LIBXXC-HEADERS_PACKAGE_NAME "libxxc-headers")
set(CPACK_DEBIAN_LIBXXC-HEADERS_PACKAGE_DEPENDS "libxxc (= ${PROJECT_VERSION})")
include(CPack)