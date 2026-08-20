# ============================================================
# 演示程序1: demo_types - 类型系统演示
# ============================================================
add_executable(demo_types
    demo_types.cpp
)

target_link_libraries(demo_types
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# 演示程序2: demo_l1_action_loader - L1 Action加载器演示
# ============================================================
add_executable(demo_l1_action_loader
    demo_l1_action_loader.cpp
)

target_link_libraries(demo_l1_action_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# 演示程序3: demo_l2_node_loader - L2 Node加载器演示
# ============================================================
add_executable(demo_l2_node_loader
    demo_l2_node_loader.cpp
)

target_link_libraries(demo_l2_node_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# 演示程序4: demo_l3_group_loader - L3 Group加载器演示
# ============================================================
add_executable(demo_l3_group_loader
    demo_l3_group_loader.cpp
)

target_link_libraries(demo_l3_group_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# 演示程序5: demo_l3_nested_group - L3嵌套Group演示
# ============================================================
add_executable(demo_l3_nested_group
    demo_l3_nested_group.cpp
)

target_link_libraries(demo_l3_nested_group
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# 演示程序6: demo_l4_flow_loader - L4 Flow加载器演示
# ============================================================
add_executable(demo_l4_flow_loader
    demo_l4_flow_loader.cpp
)

target_link_libraries(demo_l4_flow_loader
    industrial_config_engine
    nlohmann_json::nlohmann_json
)

# ============================================================
# 设置输出目录
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
# 打印信息
# ============================================================
message(STATUS "========================================")
message(STATUS "演示程序:")
message(STATUS "  - demo_types: 类型系统演示")
message(STATUS "  - demo_l1_action_loader: L1 Action加载器演示")
message(STATUS "  - demo_l2_node_loader: L2 Node加载器演示")
message(STATUS "  - demo_l3_group_loader: L3 Group加载器演示")
message(STATUS "  - demo_l3_nested_group: L3嵌套Group演示")
message(STATUS "  - demo_l4_flow_loader: L4 Flow加载器演示")
message(STATUS "输出目录: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "========================================")