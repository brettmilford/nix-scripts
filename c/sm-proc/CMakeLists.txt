cmake_minimum_required(VERSION 3.20)
project(sm-proc VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(LIBCONFIG REQUIRED libconfig)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
pkg_check_modules(CJSON REQUIRED libcjson)
pkg_check_modules(POPPLER REQUIRED poppler-glib)

# Find xlsxwriter manually since it doesn't provide pkgconfig
find_path(XLSXWRITER_INCLUDE_DIR xlsxwriter.h)
find_library(XLSXWRITER_LIBRARY NAMES xlsxwriter)

if(NOT XLSXWRITER_INCLUDE_DIR OR NOT XLSXWRITER_LIBRARY)
    message(FATAL_ERROR "libxlsxwriter not found")
endif()

# Source files
set(SOURCES
    src/main.c
    src/utils.c
    src/config.c
    src/transaction.c
    src/categoriser.c
    src/parsers/parser_registry.c
    src/parsers/cba_parser.c
    src/parsers/anz_parser.c
    src/xlsx_writer.c
    src/paperless_api.c
    src/ai/ai_service.c
)

# Main executable
add_executable(sm-proc ${SOURCES})

target_include_directories(sm-proc PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${LIBCONFIG_INCLUDE_DIRS}
    ${PCRE2_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
    ${POPPLER_INCLUDE_DIRS}
    ${XLSXWRITER_INCLUDE_DIR}
)

target_link_libraries(sm-proc
    ${CURL_LIBRARIES}
    ${LIBCONFIG_LIBRARIES}
    ${PCRE2_LIBRARIES}
    ${CJSON_LIBRARIES}
    ${POPPLER_LIBRARIES}
    ${XLSXWRITER_LIBRARY}
)

# Compiler warnings
target_compile_options(sm-proc PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

# Installation
install(TARGETS sm-proc DESTINATION bin)
install(FILES examples/sm-proc.cfg 
        DESTINATION share/sm-proc/examples)

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Test source files (all sources except main.c for linking with tests)
    set(TEST_SOURCES
        src/utils.c
        src/config.c
        src/transaction.c
        src/categoriser.c
        src/parsers/parser_registry.c
        src/parsers/cba_parser.c
        src/parsers/anz_parser.c
        src/xlsx_writer.c
        src/paperless_api.c
        src/ai/ai_service.c
    )
    
    # Create a library for testing (excludes main.c)
    add_library(sm-proc-lib STATIC ${TEST_SOURCES})
    
    target_include_directories(sm-proc-lib PRIVATE
        ${CURL_INCLUDE_DIRS}
        ${LIBCONFIG_INCLUDE_DIRS}
        ${PCRE2_INCLUDE_DIRS}
        ${CJSON_INCLUDE_DIRS}
        ${POPPLER_INCLUDE_DIRS}
        ${XLSXWRITER_INCLUDE_DIR}
    )
    
    target_link_libraries(sm-proc-lib
        ${CURL_LIBRARIES}
        ${LIBCONFIG_LIBRARIES}
        ${PCRE2_LIBRARIES}
        ${CJSON_LIBRARIES}
        ${POPPLER_LIBRARIES}
        ${XLSXWRITER_LIBRARY}
    )
    
    target_compile_options(sm-proc-lib PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    
    # Check if test files exist before creating test targets
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ai_service.c" AND 
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/mocks/ai_service_mock.c" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_parsers.c" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cba_ai_config.c")
    
        message(STATUS "Test files found - creating test targets")
        
        # AI Service tests  
        add_executable(test_ai_service tests/test_ai_service.c tests/mocks/ai_service_mock.c)
        target_link_libraries(test_ai_service sm-proc-lib)
        target_include_directories(test_ai_service PRIVATE src)
        add_test(NAME test_ai_service COMMAND test_ai_service)
        
        set_tests_properties(test_ai_service PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            ENVIRONMENT "TEST_MODE=1"
        )
        
        # Parser tests
        add_executable(test_parsers tests/test_parsers.c)
        target_link_libraries(test_parsers sm-proc-lib)
        target_include_directories(test_parsers PRIVATE src)
        add_test(NAME test_parsers COMMAND test_parsers)
        
        set_tests_properties(test_parsers PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        
        # CBA AI Configuration tests
        add_executable(test_cba_ai_config tests/test_cba_ai_config.c)
        target_link_libraries(test_cba_ai_config sm-proc-lib)
        target_include_directories(test_cba_ai_config PRIVATE src)
        add_test(NAME test_cba_ai_config COMMAND test_cba_ai_config)
        
        set_tests_properties(test_cba_ai_config PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            ENVIRONMENT "TEST_MODE=1"
        )
        
        # Add a custom target to run tests after build
        add_custom_target(run_tests ALL
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS test_ai_service test_parsers test_cba_ai_config
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests automatically after build"
        )
        
    else()
        message(WARNING "Test files not found - skipping test creation")
        message(STATUS "Tests will not be built but testing framework is ready")
    endif()
    
endif()
