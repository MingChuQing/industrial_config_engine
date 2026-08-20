# D:\project\industrial_config_engine\CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(industrial_config_engine VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 编译选项 - 添加 UTF-8 支持
# ============================================================
if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/W4)
    add_compile_definitions(_WIN32)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================
# 选项
# ============================================================
option(BUILD_DEMOS "构建演示程序" ON)

# ============================================================
# 主库 - 只包含库源文件，不包含 demo
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

# 包含目录
target_include_directories(industrial_config_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# ============================================================
# 演示程序 - 每个 demo 只包含自己的源文件
# ============================================================
if(BUILD_DEMOS)
    # 1. demo_types - 类型系统演示
    add_executable(demo_types
        demos/demo_types.cpp
    )
    target_link_libraries(demo_types
        industrial_config_engine
    )
    set_target_properties(demo_types PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 2. demo_l1_action_loader - L1 Action加载器演示
    add_executable(demo_l1_action_loader
        demos/demo_l1_action_loader.cpp
    )
    target_link_libraries(demo_l1_action_loader
        industrial_config_engine
    )
    set_target_properties(demo_l1_action_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 3. demo_l2_node_loader - L2 Node加载器演示
    add_executable(demo_l2_node_loader
        demos/demo_l2_node_loader.cpp
    )
    target_link_libraries(demo_l2_node_loader
        industrial_config_engine
    )
    set_target_properties(demo_l2_node_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 4. demo_l3_group_loader - L3 Group加载器演示
    add_executable(demo_l3_group_loader
        demos/demo_l3_group_loader.cpp
    )
    target_link_libraries(demo_l3_group_loader
        industrial_config_engine
    )
    set_target_properties(demo_l3_group_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 5. demo_l3_nested_group - L3嵌套Group演示
    add_executable(demo_l3_nested_group
        demos/demo_l3_nested_group.cpp
    )
    target_link_libraries(demo_l3_nested_group
        industrial_config_engine
    )
    set_target_properties(demo_l3_nested_group PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 6. demo_l4_flow_loader - L4 Flow加载器演示
    add_executable(demo_l4_flow_loader
        demos/demo_l4_flow_loader.cpp
    )
    target_link_libraries(demo_l4_flow_loader
        industrial_config_engine
    )
    set_target_properties(demo_l4_flow_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # 7. demo_simulator - 仿真运行器演示（虚拟设备 + 变量存储 + 审计日志 + 错误注入）
    add_executable(demo_simulator
        demos/demo_simulator.cpp
    )
    target_link_libraries(demo_simulator
        industrial_config_engine
    )
    set_target_properties(demo_simulator PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ============================================================
# 打印信息
# ============================================================
message(STATUS "========================================")
message(STATUS "工业配置引擎 v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "C++ 标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "源文件: ${LIB_SOURCES}")
message(STATUS "包含路径: ${CMAKE_CURRENT_SOURCE_DIR}/third_party")
message(STATUS "========================================")

if(BUILD_DEMOS)
    message(STATUS "演示程序:")
    message(STATUS "  - demo_types: 类型系统演示")
    message(STATUS "  - demo_l1_action_loader: L1 Action加载器演示")
    message(STATUS "  - demo_l2_node_loader: L2 Node加载器演示")
    message(STATUS "  - demo_l3_group_loader: L3 Group加载器演示")
    message(STATUS "  - demo_l3_nested_group: L3嵌套Group演示")
    message(STATUS "  - demo_l4_flow_loader: L4 Flow加载器演示")
    message(STATUS "  - demo_simulator: 仿真运行器演示")
    message(STATUS "输出目录: ${CMAKE_BINARY_DIR}/bin")
    message(STATUS "========================================")
endif()
