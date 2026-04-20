# tash plugin registry.
#
# Provides two functions:
#   tash_register_plugin(...)       — appends sources (and records a test spec)
#   tash_finalize_plugin_tests()    — creates the test executables once shell_lib exists
#
# The companion file cmake/plugin_list.cmake is an append-only list of
# tash_register_plugin() calls. Adding a new plugin means adding one call at
# the bottom of that file, which avoids the per-PR merge conflicts that an
# in-place SHELL_SOURCES list produced.

# Tracks every plugin that registered a test, in declaration order.
set(TASH_PLUGIN_TEST_NAMES "")

# Register one plugin.
#
# Arguments:
#   NAME <name>                     unique short identifier (required)
#   SOURCES <files>                 .cpp files added to SHELL_SOURCES
#   REQUIRES <var>                  variable name; skip this plugin if its value is falsy
#                                   (e.g. TASH_SQLITE_ENABLED)
#   TEST_SOURCES <files>            test .cpp files (creates test_<NAME> executable)
#   TEST_PREFIX <string>            gtest_discover_tests TEST_PREFIX (default: unit/<NAME>/)
#   TEST_INCLUDES <dirs>            extra target_include_directories for the test
#   TEST_LIBS <libs>                extra target_link_libraries for the test
#   TEST_DEFS <defs>                extra target_compile_definitions for the test
#   TEST_STANDALONE                 do NOT link shell_lib (use when the test compiles
#                                   its own sources and needs isolation; such tests
#                                   still get libcurl + nlohmann_json since AI is
#                                   always compiled in.)
function(tash_register_plugin)
    set(options TEST_STANDALONE)
    set(oneValueArgs NAME REQUIRES TEST_PREFIX)
    set(multiValueArgs SOURCES TEST_SOURCES TEST_INCLUDES TEST_LIBS TEST_DEFS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "tash_register_plugin: NAME is required")
    endif()

    if(ARG_REQUIRES)
        if(NOT ${${ARG_REQUIRES}})
            return()
        endif()
    endif()

    if(ARG_SOURCES)
        set(SHELL_SOURCES "${SHELL_SOURCES};${ARG_SOURCES}" PARENT_SCOPE)
    endif()

    if(ARG_TEST_SOURCES)
        set(_pref "TASH_TEST_${ARG_NAME}")
        set("${_pref}_SOURCES"    "${ARG_TEST_SOURCES}"    PARENT_SCOPE)
        set("${_pref}_INCLUDES"   "${ARG_TEST_INCLUDES}"   PARENT_SCOPE)
        set("${_pref}_LIBS"       "${ARG_TEST_LIBS}"       PARENT_SCOPE)
        set("${_pref}_DEFS"       "${ARG_TEST_DEFS}"       PARENT_SCOPE)
        if(ARG_TEST_PREFIX)
            set("${_pref}_PREFIX" "${ARG_TEST_PREFIX}" PARENT_SCOPE)
        else()
            set("${_pref}_PREFIX" "unit/${ARG_NAME}/"  PARENT_SCOPE)
        endif()
        if(ARG_TEST_STANDALONE)
            set("${_pref}_STANDALONE" TRUE PARENT_SCOPE)
        else()
            set("${_pref}_STANDALONE" FALSE PARENT_SCOPE)
        endif()
        set(TASH_PLUGIN_TEST_NAMES "${TASH_PLUGIN_TEST_NAMES};${ARG_NAME}" PARENT_SCOPE)
    endif()
endfunction()

# Materialize test executables. Call after shell_lib is defined.
function(tash_finalize_plugin_tests)
    if(NOT BUILD_TESTS)
        return()
    endif()
    foreach(name IN LISTS TASH_PLUGIN_TEST_NAMES)
        if(NOT name)
            continue()
        endif()
        set(_pref "TASH_TEST_${name}")

        add_executable(test_${name} ${${_pref}_SOURCES})
        target_include_directories(test_${name} PRIVATE
            ${TASH_INCLUDE_DIRS} ${${_pref}_INCLUDES})

        if(${${_pref}_STANDALONE})
            target_link_libraries(test_${name} PRIVATE GTest::gtest_main ${${_pref}_LIBS})
        else()
            target_link_libraries(test_${name} PRIVATE GTest::gtest_main shell_lib ${${_pref}_LIBS})
        endif()

        if(${_pref}_DEFS)
            target_compile_definitions(test_${name} PRIVATE ${${_pref}_DEFS})
        endif()

        # AI is always compiled in (libcurl is a hard build dependency), so
        # every test that uses shell_lib or pulls in tash headers needs the
        # JSON headers + libcurl on the link line. Standalone tests that
        # only exercise pure utilities happen to pick these up cheaply.
        target_include_directories(test_${name} PRIVATE
            ${nlohmann_json_SOURCE_DIR}/include)
        target_link_libraries(test_${name} PRIVATE CURL::libcurl)

        gtest_discover_tests(test_${name} TEST_PREFIX "${${_pref}_PREFIX}")
    endforeach()
endfunction()
