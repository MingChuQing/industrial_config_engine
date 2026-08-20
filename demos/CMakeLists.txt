# ============================================================
# Demo 1: demo_types - type system demo
# ============================================================
add_executable(demo_types
    demo_types.cpp
)

target_link_libraries(demo_types
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# Demo 2: demo_l1_action_loader - L1 Action loader demo
# ============================================================
add_executable(demo_l1_action_loader
    demo_l1_action_loader.cpp
)

target_link_libraries(demo_l1_action_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# Demo 3: demo_l2_node_loader - L2 Node loader demo
# ============================================================
add_executable(demo_l2_node_loader
    demo_l2_node_loader.cpp
)

target_link_libraries(demo_l2_node_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# Demo 4: demo_l3_group_loader - L3 Group loader demo
# ============================================================
add_executable(demo_l3_group_loader
    demo_l3_group_loader.cpp
)

target_link_libraries(demo_l3_group_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# Demo 5: demo_l3_nested_group - L3 nested Group demo
# ============================================================
add_executable(demo_l3_nested_group
    demo_l3_nested_group.cpp
)

target_link_libraries(demo_l3_nested_group
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# Demo 6: demo_l4_flow_loader - L4 Flow loader demo
# ============================================================
add_executable(demo_l4_flow_loader
    demo_l4_flow_loader.cpp
)

target_link_libraries(demo_l4_flow_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# Set output directory
# ============================================================
set_target_properties(
    demo_types 
    demo_l1_action_loader 
    demo_l2_node_loader
    demo_l3_group_loader
    demo_l3_nested_group
    demo_l4_flow_loader
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================
# Print info
# ============================================================
message(STATUS "========================================")
message(STATUS "Demo programs:")
message(STATUS "  - demo_types: type system demo")
message(STATUS "  - demo_l1_action_loader: L1 Action loader demo")
message(STATUS "  - demo_l2_node_loader: L2 Node loader demo")
message(STATUS "  - demo_l3_group_loader: L3 Group loader demo")
message(STATUS "  - demo_l3_nested_group: L3 nested Group demo")
message(STATUS "  - demo_l4_flow_loader: L4 Flow loader demo")
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "========================================")
