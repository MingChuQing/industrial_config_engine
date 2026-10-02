# D:\project\industrial_config_engine\CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(industrial_config_engine VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Compile options - add UTF-8 support
# ============================================================
if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/W4)
    add_compile_definitions(_WIN32)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================
# Options
# ============================================================
option(BUILD_DEMOS "Build demo programs" ON)
include(CTest)

# ============================================================
# Main library - library sources only, no demos
# ============================================================
set(LIB_SOURCES
    src/l1_action.cpp
    src/action_loader.cpp
    src/l2_node.cpp
    src/node_loader.cpp
    src/l3_group.cpp
    src/l4_flow.cpp
    src/executor.cpp
    src/config.cpp
)

add_library(industrial_config_engine STATIC ${LIB_SOURCES})

# Include directories
target_include_directories(industrial_config_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(BUILD_TESTING)
    add_executable(test_device_points tests/test_device_points.cpp)
    target_link_libraries(test_device_points industrial_config_engine)
    add_test(NAME device_points COMMAND test_device_points
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/example4")
    add_executable(test_beverage_suite tests/test_beverage_suite.cpp)
    target_link_libraries(test_beverage_suite industrial_config_engine)
    add_test(NAME beverage_suite COMMAND test_beverage_suite
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/beverage_suite")
    add_executable(test_beverage_full tests/test_beverage_full.cpp)
    target_link_libraries(test_beverage_full industrial_config_engine)
    add_test(NAME beverage_full_baseline COMMAND test_beverage_full
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/beverage_full/example4")
    add_test(NAME beverage_full_extended COMMAND test_beverage_full
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/beverage_full/example5")
endif()

# ============================================================
# Demo programs - each demo contains only its own source
# ============================================================
if(BUILD_DEMOS)
    # 1. demo_types - type system demo
    add_executable(demo_types
        demos/demo_types.cpp
    )
    target_link_libraries(demo_types
        industrial_config_engine
    )
    set_target_properties(demo_types PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 2. demo_l1_action_loader - L1 Action loader demo
    add_executable(demo_l1_action_loader
        demos/demo_l1_action_loader.cpp
    )
    target_link_libraries(demo_l1_action_loader
        industrial_config_engine
    )
    set_target_properties(demo_l1_action_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 3. demo_l2_node_loader - L2 Node loader demo
    add_executable(demo_l2_node_loader
        demos/demo_l2_node_loader.cpp
    )
    target_link_libraries(demo_l2_node_loader
        industrial_config_engine
    )
    set_target_properties(demo_l2_node_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 4. demo_l3_group_loader - L3 Group loader demo
    add_executable(demo_l3_group_loader
        demos/demo_l3_group_loader.cpp
    )
    target_link_libraries(demo_l3_group_loader
        industrial_config_engine
    )
    set_target_properties(demo_l3_group_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 5. demo_l3_nested_group - L3 nested Group demo
    add_executable(demo_l3_nested_group
        demos/demo_l3_nested_group.cpp
    )
    target_link_libraries(demo_l3_nested_group
        industrial_config_engine
    )
    set_target_properties(demo_l3_nested_group PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 6. demo_l4_flow_loader - L4 Flow loader demo
    add_executable(demo_l4_flow_loader
        demos/demo_l4_flow_loader.cpp
    )
    target_link_libraries(demo_l4_flow_loader
        industrial_config_engine
    )
    set_target_properties(demo_l4_flow_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 7. demo_simulator - simulator demo (virtual devices + variable store + audit log + fault injection)
    add_executable(demo_simulator
        demos/demo_simulator.cpp
    )
    target_link_libraries(demo_simulator
        industrial_config_engine
    )
    set_target_properties(demo_simulator PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 8. demo_summary - one-click run of all paper conclusions (English output)
    add_executable(demo_summary
        demos/demo_summary.cpp
    )
    target_link_libraries(demo_summary
        industrial_config_engine
    )
    set_target_properties(demo_summary PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ============================================================
# Print info
# ============================================================
message(STATUS "========================================")
message(STATUS "Industrial Config Engine v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Sources: ${LIB_SOURCES}")
message(STATUS "Include path: ${CMAKE_CURRENT_SOURCE_DIR}/third_party")
message(STATUS "========================================")

if(BUILD_DEMOS)
    message(STATUS "Demo programs:")
    message(STATUS "  - demo_types: type system demo")
    message(STATUS "  - demo_l1_action_loader: L1 Action loader demo")
    message(STATUS "  - demo_l2_node_loader: L2 Node loader demo")
    message(STATUS "  - demo_l3_group_loader: L3 Group loader demo")
    message(STATUS "  - demo_l3_nested_group: L3 nested Group demo")
    message(STATUS "  - demo_l4_flow_loader: L4 Flow loader demo")
    message(STATUS "  - demo_simulator: simulator demo")
    message(STATUS "  - demo_summary: one-click run of all paper conclusions")
    message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
    message(STATUS "========================================")
endif()
