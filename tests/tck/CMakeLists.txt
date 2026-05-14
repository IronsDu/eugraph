# ==================== TCK Tests (Cucumber BDD for openCypher TCK) ====================
#
# The amp-cucumber-cpp-runner is built as an ExternalProject in a vcpkg-free
# environment to avoid ALIAS target conflicts (GTest, fmt, nlohmann_json are
# all provided by both vcpkg and the runner's submodules).
#
# We build the runner, copy its artifacts to a staging dir, and import them.

option(TCK_RUN_TESTS "Register TCK tests with CTest (many expected failures)" ON)

include(ExternalProject)

set(CUKE_STAGE "${CMAKE_CURRENT_BINARY_DIR}/ccr-stage")
file(MAKE_DIRECTORY "${CUKE_STAGE}/include/cucumber-cpp")
file(MAKE_DIRECTORY "${CUKE_STAGE}/include/cucumber_cpp")
file(MAKE_DIRECTORY "${CUKE_STAGE}/include/gherkin")
file(MAKE_DIRECTORY "${CUKE_STAGE}/include/messages")
file(MAKE_DIRECTORY "${CUKE_STAGE}/lib")

# PATCH_COMMAND removes sanitizer flags that cause ABI incompatibility with vcpkg
ExternalProject_Add(ccr_ext
    GIT_REPOSITORY https://github.com/philips-software/amp-cucumber-cpp-runner.git
    GIT_TAG v4.0.1
    GIT_SUBMODULES_RECURSE ON
    SOURCE_SUBDIR ""  # build from project root
    PATCH_COMMAND sed -i
        "s|add_compile_options(-fsanitize=address -fsanitize=undefined)|# PATCHED: sanitizers removed|"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "s|add_link_options(-fsanitize=address -fsanitize=undefined)|# PATCHED: sanitizers removed|"
        <SOURCE_DIR>/CMakeLists.txt
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCCR_STANDALONE=ON
        -DCCR_FETCH_DEPS=ON
        -DCCR_BUILD_TESTS=OFF
        -DCMAKE_CXX_STANDARD=20
        "-DCMAKE_CXX_FLAGS=-Wno-error=deprecated-declarations"
    INSTALL_COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/copy_ccr_artifacts.sh"
        <SOURCE_DIR> <BINARY_DIR> "${CUKE_STAGE}"
    BUILD_BYPRODUCTS
        "${CUKE_STAGE}/lib/libcucumber_cpp.library.a"
        "${CUKE_STAGE}/lib/libcucumber_cpp.runner.a"
)

# ---- Import cucumber-cpp-runner libraries ----

# Helper macro to import a static library from staging
macro(ccr_import target lib_name)
    add_library(${target} STATIC IMPORTED)
    set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION "${CUKE_STAGE}/lib/${lib_name}"
        INTERFACE_INCLUDE_DIRECTORIES "${CUKE_STAGE}/include;${CUKE_STAGE}/include/cucumber-cpp;${CUKE_STAGE}/include/gherkin;${CUKE_STAGE}/include/messages"
    )
endmacro()

ccr_import(ccr_library libcucumber_cpp.library.a)
ccr_import(ccr_runner libcucumber_cpp.runner.a)
ccr_import(ccr_api libcucumber_cpp.library.api.a)
ccr_import(ccr_assemble libcucumber_cpp.library.assemble.a)
ccr_import(ccr_cuke_expr libcucumber_cpp.library.cucumber_expression.a)
ccr_import(ccr_engine libcucumber_cpp.library.engine.a)
ccr_import(ccr_formatter libcucumber_cpp.library.formatter.a)
ccr_import(ccr_formatter_helper libcucumber_cpp.library.formatter.helper.a)
ccr_import(ccr_query libcucumber_cpp.library.query.a)
ccr_import(ccr_runtime libcucumber_cpp.library.runtime.a)
ccr_import(ccr_support libcucumber_cpp.library.support.a)
ccr_import(ccr_tag_expr libcucumber_cpp.library.tag_expression.a)
ccr_import(ccr_util libcucumber_cpp.library.util.a)
ccr_import(ccr_gherkin libcucumber_gherkin.a)
ccr_import(ccr_messages libcucumber_messages.a)
ccr_import(ccr_pugixml libpugixml.a)

# Set up dependency chain so linker order is correct.
# cucumber_cpp.runner -> cucumber_cpp.library
target_link_libraries(ccr_runner INTERFACE ccr_library)

# cucumber_cpp.library depends on all sub-libraries + external deps
set(CCR_SUB_LIBS
    ccr_api ccr_engine ccr_formatter ccr_tag_expr ccr_util ccr_support
    ccr_assemble ccr_cuke_expr ccr_query ccr_runtime ccr_formatter_helper
)
target_link_libraries(ccr_library INTERFACE
    ${CCR_SUB_LIBS}
    ccr_gherkin ccr_messages ccr_pugixml
    GTest::gtest GTest::gmock
)

# Sub-library dependency declarations (matching cucumber-cpp-runner's own CMake)
target_link_libraries(ccr_api INTERFACE ccr_cuke_expr ccr_formatter ccr_runtime ccr_support ccr_util)
target_link_libraries(ccr_assemble INTERFACE ccr_cuke_expr ccr_support ccr_util)
target_link_libraries(ccr_cuke_expr INTERFACE ccr_pugixml)  # uses fmt from vcpkg
target_link_libraries(ccr_engine INTERFACE
    ccr_util ccr_cuke_expr ccr_support ccr_assemble ccr_runtime
)
target_link_libraries(ccr_formatter INTERFACE
    ccr_cuke_expr ccr_formatter_helper ccr_query ccr_support ccr_util
)
target_link_libraries(ccr_query INTERFACE ccr_gherkin ccr_util)
target_link_libraries(ccr_runtime INTERFACE ccr_assemble ccr_cuke_expr ccr_support ccr_util)
target_link_libraries(ccr_support INTERFACE ccr_cuke_expr ccr_tag_expr ccr_util)
target_link_libraries(ccr_tag_expr INTERFACE ccr_gherkin)
target_link_libraries(ccr_util INTERFACE ccr_gherkin)
target_link_libraries(ccr_formatter_helper INTERFACE ccr_cuke_expr ccr_support ccr_query)

# ccr_runner provides main()
target_link_libraries(ccr_runner INTERFACE GTest::gtest)

# ---- TCK step definitions library ----

add_library(tck_steps_lib STATIC
    tck_context.cpp
)

target_include_directories(tck_steps_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/tests/tck"
    "${CUKE_STAGE}/include"
    "${CUKE_STAGE}/include/cucumber-cpp"
    "${CUKE_STAGE}/include/gherkin"
    "${CUKE_STAGE}/include/messages"
)

target_link_libraries(tck_steps_lib PUBLIC
    eugraph_shell_lib
    eugraph_thrift
    Folly::folly
    spdlog::spdlog
)

# ---- TCK test executable ----

add_executable(tck_tests
    tck_steps.cpp
)

target_include_directories(tck_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/tests/tck"
    "${CUKE_STAGE}/include"
    "${CUKE_STAGE}/include/cucumber-cpp"
    "${CUKE_STAGE}/include/gherkin"
    "${CUKE_STAGE}/include/messages"
)

target_link_libraries(tck_tests PRIVATE
    tck_steps_lib
    ccr_runner
)

# Use --start-group/--end-group to handle circular dependencies between static libs
target_link_options(tck_tests PRIVATE
    -Wl,--start-group
    $<TARGET_FILE:ccr_runner>
    $<TARGET_FILE:ccr_library>
    $<TARGET_FILE:ccr_api>
    $<TARGET_FILE:ccr_engine>
    $<TARGET_FILE:ccr_formatter>
    $<TARGET_FILE:ccr_tag_expr>
    $<TARGET_FILE:ccr_util>
    $<TARGET_FILE:ccr_support>
    $<TARGET_FILE:ccr_assemble>
    $<TARGET_FILE:ccr_cuke_expr>
    $<TARGET_FILE:ccr_query>
    $<TARGET_FILE:ccr_runtime>
    $<TARGET_FILE:ccr_formatter_helper>
    $<TARGET_FILE:ccr_gherkin>
    $<TARGET_FILE:ccr_messages>
    $<TARGET_FILE:ccr_pugixml>
    -Wl,--end-group
)

add_dependencies(tck_tests eugraph-server ccr_ext)

# Symlink features/ -> openCypher TCK features for cucumber discovery
add_custom_command(TARGET tck_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/features"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${PROJECT_SOURCE_DIR}/tests/tck/features/opencypher_tck"
        "${CMAKE_CURRENT_BINARY_DIR}/features"
    COMMENT "Linking TCK features for cucumber discovery"
)

# Register with CTest so CI can optionally run the TCK tests.
# Uses Python script to manage server lifecycle (start/stop), so server
# crashes are not silently hidden by auto-restart logic.
if(TCK_RUN_TESTS)
    add_test(NAME tck_tests
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_tck.py"
            --server-bin "$<TARGET_FILE:eugraph-server>"
            --tck-bin "$<TARGET_FILE:tck_tests>"
            --features "${CMAKE_CURRENT_BINARY_DIR}/features"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()
