
```markdown
# Industrial Config Engine - 工业配置引擎

## 📖 简介

Industrial Config Engine 是一个基于四层架构（L1-L4）的工业自动化配置引擎，用于定义、加载和执行工业设备的配置和控制逻辑。所有配置均为 JSON 格式，支持版本控制和热加载。

### 四层架构

| 层级 | 目录 | 职责 | AI生成 |
|------|------|------|:---:|
| **L1 Action** | `L1_action/` | 最小执行单元，无状态（如单次Modbus读） | 否 |
| **L2 Node** | `L2_node/` | Action的状态封装（含超时、重试、成功/失败/超时三分支） | 否 |
| **L3 Group** | `L3_group/` | 流程编排（顺序/循环/条件/并行，可嵌套） | 是 |
| **L4 Flow** | `L4_flow/` | 顶层可执行配置，支持多运行模式 | 是 |

### 设计原则

- **L1/L2固化**：所有直接与硬件交互的层由工程师预定义，AI不参与生成。
- **L3/L4声明式**：AI生成的配置仅描述"做什么"（流程编排），不描述"怎么做"（设备驱动细节）。
- **接口契约**：L2 Node名称构成工艺师与技术人员之间的基础接口。技术人员负责L1/L2层实现，并可预先将常用流程组合封装为L3 Group（Group支持嵌套调用Node和其他Group），供工艺师在更高层流程编排中直接引用。各层之间通过统一的命名契约实现解耦。

### 核心特性

- ✅ **L1 原子操作**：Modbus读写、计算、日志、弹窗、等待等
- ✅ **L2 执行节点**：重试机制、超时控制、成功/失败/超时分支
- ✅ **L3 执行组**：支持 sequence、parallel、loop、if、switch 控制流
- ✅ **L4 流程层**：编排 L3 Group，构建完整产线流程
- ✅ **参数化**：支持 `${变量}` 占位符和参数传递
- ✅ **聚合文件**：L1/L2 使用 bundle 聚合文件，减少文件数量
- ✅ **JSON 配置**：所有配置为 JSON 格式，易于编辑和版本控制
- ✅ **三维语义验证**：结构校验 + 语义映射 + 仿真运行

---

## 📦 目录结构

```
industrial_config_engine/
├── include/
│   └── industrial_config_engine/        # 头文件
│       ├── action_loader.hpp           # L1 Action加载器
│       ├── config.hpp                  # 配置管理
│       ├── l1_action.hpp               # L1 原子操作类
│       ├── l2_node.hpp                 # L2 执行节点类
│       ├── l3_group.hpp                # L3 执行组类
│       ├── l4_flow.hpp                 # L4 流程类
│       ├── node_loader.hpp             # L2 Node 加载器
│       └── types.hpp                   # 数据类型定义
├── src/                                 # 源文件
│   ├── action_loader.cpp
│   ├── config.cpp
│   ├── l1_action.cpp
│   ├── l2_node.cpp
│   ├── l3_group.cpp
│   ├── l4_flow.cpp
│   └── node_loader.cpp
├── demos/                               # 演示程序
│   ├── demo_l1_action_loader.cpp       # L1 Action 加载器演示
│   ├── demo_l2_node_loader.cpp         # L2 Node 加载器演示
│   ├── demo_l3_group_loader.cpp        # L3 Group 加载器演示
│   ├── demo_l3_nested_group.cpp        # L3 嵌套 Group 演示
│   ├── demo_l4_flow_loader.cpp         # L4 Flow 加载器演示
│   └── demo_types.cpp                  # 类型系统演示
├── examples/                            # 示例配置文件
│   ├── config_examples/                # 原始配置示例（参考）
│   │   ├── L1_action/modbus/iDM_actions.json
│   │   ├── L2_node/iDM_nodes.json
│   │   ├── L3_group/*.group.json
│   │   └── L4_flow/main_flow.json
│   ├── example1/                       # 示例1：真空控制流程
│   │   ├── L1_action/                  # L1 原子操作（按目录分类）
│   │   │   ├── control/all_actions.json
│   │   │   ├── modbus/all_actions.json
│   │   │   └── system/all_actions.json
│   │   ├── L2_node/                    # L2 执行节点（按目录分类）
│   │   │   ├── control/all_nodes.json
│   │   │   ├── data/all_nodes.json
│   │   │   ├── system/all_nodes.json
│   │   │   └── vacuum/all_nodes.json
│   │   ├── L3_group/                   # L3 执行组
│   │   │   ├── common/
│   │   │   │   ├── emergency_stop.json
│   │   │   │   ├── log_result.json
│   │   │   │   └── system_check.json
│   │   │   └── vacuum/
│   │   │       ├── hold_pressure.json
│   │   │       ├── leak_test.json
│   │   │       ├── open_valve.json
│   │   │       └── pump_down.json
│   │   └── L4_flow/production/
│   │       └── vacuum_flow.json
│   ├── example2/                       # 示例2：电机上料流程
│   │   ├── L1_action/                  # L1 聚合文件（扁平结构）
│   │   │   ├── control_actions.json
│   │   │   ├── data_actions.json
│   │   │   ├── leadshine_actions.json
│   │   │   ├── modbus_actions.json
│   │   │   └── system_actions.json
│   │   ├── L2_node/                    # L2 聚合文件（扁平结构）
│   │   │   ├── data_nodes.json
│   │   │   ├── leadshine_nodes.json
│   │   │   ├── log_nodes.json
│   │   │   ├── sensor_nodes.json
│   │   │   ├── system_nodes.json
│   │   │   ├── ui_nodes.json
│   │   │   ├── vacuum_nodes.json
│   │   │   ├── variable_nodes.json
│   │   │   └── wait_nodes.json
│   │   ├── L3_group/                   # L3 执行组（扁平结构）
│   │   │   ├── complete_feeding.group.json
│   │   │   ├── emergency_stop.group.json
│   │   │   ├── jog_find_sensor.group.json
│   │   │   ├── precise_positioning.group.json
│   │   │   ├── run_pr_with_wait.group.json
│   │   │   ├── system_check.group.json
│   │   │   └── write_pr_position.group.json
│   │   └── L4_flow/
│   │       └── main_flow.json
│   └── example3/                       # 示例3：气密性检测流程
│       ├── L1_action/leak_test/all_actions.json
│       ├── L2_node/leak_test/all_nodes.json
│       ├── L3_group/leak_test/
│       │   ├── leak_test.group.json
│       │   └── pressure_hold.group.json
│       └── L4_flow/leak_test/
│           └── production_flow.json
├── CMakeLists.txt                       # 主构建文件
└── demos/CMakeLists.txt                 # 演示程序构建文件
```

---

## 🚀 编译

### 前置条件

- **CMake** 3.14 或更高版本
- **C++17** 编译器 (MSVC 2019+ / GCC 7+ / Clang 6+)
- **nlohmann/json** 库 (自动下载)

### Windows (Visual Studio)

```bash
# 配置
cmake -B out\build\x64-Debug -S .

# 编译所有
cmake --build out\build\x64-Debug

# 编译特定 demo
cmake --build out\build\x64-Debug --target demo_l3_group_loader
```

### Linux / macOS

```bash
# 配置
cmake -B out/build -S .

# 编译所有
cmake --build out/build

# 编译特定 demo
cmake --build out/build --target demo_l3_group_loader
```

### 输出目录

编译后的可执行文件位于：
- Windows: `out\build\x64-Debug\bin\`
- Linux/macOS: `out/build/bin/`

---

## 📋 Demo 程序列表

### 1. demo_types - 类型系统演示

演示数据类型系统的基本功能。

```bash
./demo_types
```

**功能**：
- 显示所有支持的数据类型 (u8, i8, u16, i16, u32, i32, f, bool, string, hex 等)
- 类型转换和验证
- 类型名称和大小信息

---

### 2. demo_l1_action_loader - L1 Action 加载器演示

演示 L1 原子操作的加载和显示。

```bash
# 默认目录（自动查找 examples/example1）
./demo_l1_action_loader

# 指定配置目录
./demo_l1_action_loader D:/path/to/examples/example2
```

**功能**：
- 加载 `L1_action` 目录下的所有 Action 配置文件
- 支持聚合文件 (`*_actions.json`) 和目录分类 (`category/all_actions.json`)
- 显示 Action 的详细信息 (type, request, response, args 等)

---

### 3. demo_l2_node_loader - L2 Node 加载器演示

演示 L2 执行节点的加载和显示，包括对 L1 Action 的引用解析。

```bash
# 默认目录
./demo_l2_node_loader

# 指定配置目录
./demo_l2_node_loader D:/path/to/examples/example2
```

**功能**：
- 加载 `L2_node` 目录下的所有 Node 配置文件
- 解析 Node 中引用的 L1 Action (template 引用)
- 显示 Node 的完整信息 (params, judge, on_success, on_failure, on_timeout)

---

### 4. demo_l3_group_loader - L3 Group 加载器演示

演示 L3 执行组的加载和显示，支持显示级别控制和参数填充。

```bash
# 基本用法
./demo_l3_group_loader

# 指定目录
./demo_l3_group_loader D:/path/to/examples/example2

# 显示级别控制
./demo_l3_group_loader --level=action      # 显示所有（包括 Action）
./demo_l3_group_loader --level=node        # 显示 Group + Node（默认）
./demo_l3_group_loader --level=group       # 只显示 Group

# 参数填充
./demo_l3_group_loader --fill=true         # 用参数值替换变量
./demo_l3_group_loader --fill=false        # 显示原始内容（默认）

# 自定义参数值
./demo_l3_group_loader --fill=true --param=instance_id="motor1"

# 组合使用
./demo_l3_group_loader --level=action --fill=true --param=pr_number=5
```

**功能**：
- 加载 `L3_group` 目录下的所有 Group 配置文件
- 支持 sequence、parallel、loop、if、switch 控制流
- **显示级别控制**：
  - `group`：只显示 Group 结构
  - `node`：显示 Group + Node 详情
  - `action`：显示所有（包括 on_success/on_failure/on_timeout 中的 Action）
- **参数填充**：
  - `--fill=true`：用实际值替换 `${变量}` 占位符
  - `--fill=false`：显示原始内容（默认）
- 自定义参数值：`--param=key=value`

---

### 5. demo_l3_nested_group - L3 嵌套 Group 演示

演示 L3 Group 的嵌套功能，展示如何构建复杂的控制流层次结构。

```bash
./demo_l3_nested_group
```

**功能**：
- 创建和显示嵌套 Group 结构
- 演示 sequence + parallel 混合模式
- 显示 Group 结构树
- 验证嵌套深度限制

---

### 6. demo_l4_flow_loader - L4 Flow 加载器演示

演示 L4 流程层的加载和执行。

```bash
# 默认目录
./demo_l4_flow_loader

# 指定目录
./demo_l4_flow_loader D:/path/to/examples/example2
```

**功能**：
- 加载 `L4_flow` 目录下的 Flow 配置文件
- 解析 Flow 中引用的 L3 Group
- 显示完整的流程结构树
- 支持流程参数传递

---

## 🎯 配置文件格式

### 目录组织方式

项目支持两种 L1/L2 配置组织方式：

| 方式 | 目录结构 | 适用场景 |
|------|---------|----------|
| **分类目录** | `L1_action/category/all_actions.json` | 按功能模块分类，适合大型项目 |
| **聚合文件** | `L1_action/category_actions.json` | 扁平结构，适合中小型项目 |

两种方式可以混用，加载器会自动识别。

### L1 Action 命名规范

```
格式: 名称.参数类型列表.r_返回值类型_返回值变量名
```

| 示例 | 说明 |
|------|------|
| `log.s` | 参数: s, 无返回值 |
| `popup.s_s` | 参数: s, s, 无返回值 |
| `write_register.u16_u16.r_b_write_result` | 参数: u16, u16, 返回: b |
| `read_pressure.u8_u16.r_u16_pressure` | 参数: u8, u16, 返回: u16 |

### L2 Node 命名规范

与 L1 Action 相同格式。Node 通过 `action.template` 引用 L1 Action：

```json
"action": {
    "template": "L1_action/modbus_actions.json/read_pressure.u8_u16.r_u16_pressure"
}
```

### L3 Group 命名规范

| 类型 | 格式 | 示例 |
|------|------|------|
| 带参数 | `名称.group.类型1_类型2.json` | `pump_down.group.u16_f.json` |
| 无参数 | `名称.group.json` | `motor_enable.group.json` |

### 控制流模式

| 模式 | 说明 | 示例 |
|------|------|------|
| `sequence` | 顺序执行 | `"mode": "sequence"` |
| `parallel` | 并行执行 | `"mode": "parallel"` |
| `loop` | 循环执行 | `"mode": "loop"` |
| `if` | 条件分支 | `"mode": "if"` |
| `switch` | 多条件分支 | `"mode": "switch"` |

---

## 🔧 引用路径规则

| 引用类型 | 路径格式 | 示例 |
|---------|---------|------|
| L4 → L3 Group | `L3_group/xxx.group.json` | `L3_group/pump_down.group.json` |
| L3 → L2 Node | `L2_node/xxx_nodes.json/filename` | `L2_node/vacuum_nodes.json/read_pressure.r_u16_pressure` |
| L2 → L1 Action | `L1_action/xxx_actions.json/filename` | `L1_action/modbus_actions.json/read_pressure.u8_u16.r_u16_pressure` |

**注意**：
- L3 Group 的 `body` 中只能包含 L2 Node 或 L3 Group
- L2 Node 的 `action` 只能引用 L1 Action
- 不支持 L3 直接引用 L1 Action

---

## 🔧 常见问题

### Q: 编译时提示找不到 nlohmann/json

**A**: CMake 会自动下载 nlohmann/json。如果网络问题导致下载失败，可以手动安装：

```bash
# Windows (vcpkg)
vcpkg install nlohmann-json

# Linux (apt)
sudo apt install nlohmann-json3-dev

# macOS (brew)
brew install nlohmann-json
```

### Q: 运行时提示找不到配置文件

**A**: 程序默认在 `bin/examples/example1/` 目录下查找配置。可以通过命令行参数指定目录：

```bash
./demo_l3_group_loader D:/path/to/your/config
```

### Q: 如何选择使用哪个示例？

**A**: 程序会自动查找 `examples/` 下的子目录。也可以手动指定：

```bash
# 使用 example1（真空控制）
./demo_l3_group_loader D:/project/industrial_config_engine/examples/example1

# 使用 example2（电机上料）
./demo_l3_group_loader D:/project/industrial_config_engine/examples/example2
```

### Q: 如何查看更详细的输出？

**A**: 使用 `--level=action` 参数：

```bash
./demo_l3_group_loader --level=action
```

### Q: 如何查看填充参数后的内容？

**A**: 使用 `--fill=true` 参数：

```bash
./demo_l3_group_loader --fill=true
```

---

## 📝 许可证

本项目遵循 **GNU General Public License v3.0** 开源协议。

Copyright (c) 2024-2026 明楚晴

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

---

**特别声明**：本代码为论文《数据驱动的工业控制配置语言：面向LLM安全集成的语义验证框架》的配套实验代码，仅供学术研究使用。如需商用，请联系作者获取授权。

---

## 👥 贡献者

### 作者

**明楚晴**

- **单位**：东北育才学校（Northeast Yucai School）
- **地区**：中国 辽宁（Liaoning, China）
- **角色**：项目负责人 & 核心开发者
- **贡献**：
  - 四层架构（L1-L4）设计与实现
  - L1 原子操作、L2 执行节点、L3 执行组、L4 流程层核心模块开发
  - 配置引擎与加载器实现
  - 论文撰写与实验验证

---

### 所属机构

**东北育才学校**
Northeast Yucai School
中国 辽宁
Liaoning, China

---

## 📖 如何引用

如果您在学术研究或论文中使用了本项目，请按以下格式引用：

```bibtex
@article{明楚晴2026工业配置引擎,
  title   = {数据驱动的工业控制配置语言：面向LLM安全集成的语义验证框架},
  author  = {明楚晴},
  school  = {东北育才学校},
  year    = {2026},
  address = {中国 辽宁}
}
```

---

## 📚 相关文档

- [四层架构设计文档](examples/docs/)
```

---

**主要修改**：

| 修改点 | 说明 |
|--------|------|
| 四层架构表格 | 增加"AI生成"列，与论文一致 |
| 设计原则 | 新增完整的接口契约说明，体现技术人员可封装L3 Group |
| 核心特性 | 新增"三维语义验证" |
| 示例名称 | 与论文表格一致（真空控制流程/电机上料流程/气密性检测流程） |
| example2 L3 | 精简为与论文T2对应的7个Group |
| example3 L3 | 与论文T3对应（气密性检测） |
| 特别声明 | 添加论文标题 |
| 引用格式 | 添加论文标题 |
| L3嵌套演示 | 移除"max_nodes"（论文未提及） |