
# Industrial Config Engine

论文与复现实验快照：标签 **icdm2026-review-revision-20261003**。对应修订稿源码和配图见 [docs/paper-revision-20261003/](docs/paper-revision-20261003/)。这是当前评审修订稿（7 页），尚待压缩至 Teen Track 要求的 5 页；不是已完成提交的 camera-ready 版本。

## 一条命令重现配置缺陷、检查结果和修正

Windows PowerShell 在仓库根目录运行：

~~~powershell
.\verify-defects.ps1
~~~

其它环境使用 Python 3.10+：

~~~bash
python scripts/verify_defects.py
~~~

这会实际调用 C++ 引擎，运行 **18 个配置缺陷 × 正常、缺陷、修正三个版本，共 54 次**。覆盖 example1 真空、example2 送料、example3 检漏，以及完整饮料案例 example4/example5。部分测试运行一个 L3 子过程；正常对照共 8 种，重复运行 18 次。案例包含不支持的结构、变量拼写、循环退出条件、引用错误、参数缺失/类型/范围、判定错误、设备绑定、时长不足、工序顺序和遗漏检查。

入口使用现有 Python 和 C++17 工具链，不下载软件、不连接设备。本工作区可直接使用已有的便携 Zig；其它机器可以使用 PATH 中的 Zig、clang++、g++，或已配置编译器的 CMake。首次运行或 C++ 源码变化时自动构建探针；源码及二进制哈希一致时复用编译结果，每次仍重新执行全部案例。工具缺失、运行异常、正常/修正版本失败或结果偏离已登记基准都会返回非零退出码，不会跳过后报成功。

可指定已有编译器及输出目录：

~~~powershell
.\verify-defects.ps1 -Compiler C:\tools\zig\zig.exe -OutputDirectory build\defect-verification
.\verify-defects.ps1 -Help
~~~

对应 Python 参数为 --compiler、--output、--help；也可通过 ICE_CXX 指定编译器。

结果保存在 **build/defect-verification/**：

- **report.md**：中文逐例对照表，链接到每例的错误说明、实际诊断和修正方案。
- **report.json**：机器可读的实际结果、统计、编译来源和实验边界。
- **cases/案例编号/**：control、faulty、fixed 三个完整请求及真实执行日志；请求内的 configuration 是配置，params 是调用参数。repair.diff 展示修正的准确改动。
- **measured_signatures.json**：本次实测摘要；与 scripts/defect_expectations.json 中登记的回归基准核对，包括正常运行的结果哈希。

| 本组 18 个缺陷的实测结果 | 数量 |
|---|---:|
| 顶层结构解析/校验拒绝 | 2 |
| Executor 执行路径拒绝（内部仍含结构校验） | 10 |
| 在顶层结构检查之外，Executor 新增拒绝 | 8 |
| 引擎仍成功执行，但测试程序发现结果差异 | 8 |
| 修正后通过，并与正常运行结果一致 | 18 |
| 正常对照被拒绝 | 0 / 18 次（8 种对照） |

**PASS 表示实验被重现，包括已知漏检；不表示所有缺陷均被引擎发现。** 结果对照检查 ROUTE/CHANGE 事件及其虚拟时间、最终变量和虚拟设备状态。它是依赖已知正确基准的回归检查，不能算作引擎验证门或人工审核。修正方案明确恢复原配置，并非程序自动推导修复。Executor 仍执行内部结构检查，因此上述结果是覆盖范围及增量的测量，不是关闭结构检查后的完整消融。这里只测有限的人工配置变异，不能外推 LLM 错误检出率或现场安全性。

实现入口见 [verify_defects.py](scripts/verify_defects.py)、[案例定义](scripts/defect_cases.py) 和 [C++ 探针](tests/defect_probe.cpp)。本命令与下方饮料协议/设备故障验证互补：**配置缺陷**与**设备故障**分别统计。

## 独立人工语义审核材料（R3-4）

在仓库根目录运行：

~~~bash
python scripts/review_semantics.py prepare
~~~

材料生成到 build/semantic-review/。审核集含 26 个匿名编号候选：8 个独立正常配置和 18 个变异配置。每例附过程要求、调用参数、变量及可信支持定义/接口契约。仅把 reviewer_packet 文件夹和分配给该审核者的 reviewer_a.json 或 reviewer_b.json 交给审核者；coordinator_only 中的参考标签与来源由协调者保管，审核完成前不要共享。两份判定表由真人分别填写，记录接受/拒绝、依据及耗时；第二名审核者须未参与这些配置的生成或修复。

收到两份完整记录后运行：

~~~bash
python scripts/review_semantics.py score --reviewer-a build/semantic-review/reviewer_a.json --reviewer-b build/semantic-review/reviewer_b.json --key build/semantic-review/coordinator_only/reference_key.json --output build/semantic-review/results.json
~~~

计分工具检查记录完整性并输出一致率、Cohen's κ、分歧项和相对参考标签的误判。参考标签是构造实验的预期，不代表已经取得人工共识。空表和不符合独立性声明的表不会产生有效的一致率报告；重复准备材料会保留已填写的判定表。

**当前只提供审核协议和工具，尚无独立真人审核结果。** 自动模拟批准、恢复基准后的测试通过、脚本单元测试都不能作为人工审核可靠性的证据。

## 一条命令验证完整饮料生产线数据

Windows PowerShell 在仓库根目录运行：

```powershell
.\verify.ps1
```

入口会寻找现有的 Python 3.10+，也支持本机已有的 Codex 桌面 Python 运行时，不下载或安装软件。已配置 Python 的其它环境可直接运行（仅使用标准库，不需要连接设备或安装第三方 Python 包）：

```bash
python scripts/verify_beverage_full.py
```

该命令会重新生成并核对配置，执行 example4 的 6 个配方和 example5 的 9 个配方、每组 11 类故障、6 项回归/反例测试，再核对完整轨迹、模板覆盖率、配置行数及新增规格的文件差异。它比较的是本次实际计算结果与仓库保存的数据，不是读取历史 `PASS` 字样。任何检查失败都会返回非零退出码；版本化配置和报告不会被覆盖。

验证结果写入 `build/beverage-verification/`。配置核对只忽略 Windows/Linux 换行符差异。完整案例位于 [`examples/beverage_full/`](examples/beverage_full/)，详细工艺见 [说明](examples/beverage_full/README.txt)。

| 完整十二阶段案例 | 单体方案（含设备表） | 四层方案（含全部辅助表） |
|---|---:|---:|
| example4：3 种口味 × 2 L、330 mL | 30,893 行 | 4,500 行 |
| example5：增加 3 种口味的 250 mL | 46,295 行 | 4,728 行 |
| 新增规格的净增量 | 15,402 行 | 228 行 |

两种表示均保留真实循环。L4 传入口味和规格，复用初始化、上料、瓶位检测、冲洗、灌装定位、灌装、液位复检、封口、喷码贴标、出料、CIP、回零待机十二个工序。全部共享 L1–L3、设备表、点位表及配方表均计入上表。该案例是构造的确定性模拟，行数减少不代表人工工时减少或真实设备验证。

默认命令验证 Python 协议模拟与配置数据，明确报告 C++ 检查未运行。若已安装 CMake 3.14+、CTest 和 C++17 编译器，可用同一入口额外编译并验证实际 C++ 引擎：

```bash
python scripts/verify_beverage_full.py --cpp
```

PowerShell 对应命令为 `.\verify.ps1 -Cpp`。

`examples/example4/` 是较早的单条饮料线迁移案例；`examples/beverage_suite/` 是较早的简化九配方案例。它们与上述完整十二阶段案例的工艺范围不同，不能混用统计结果。

## 📖 Introduction

Industrial Config Engine is an industrial automation configuration engine based on a four-layer architecture (L1-L4) for defining, loading, and executing configuration and control logic for industrial equipment. All configuration is in JSON format and supports version control and hot reloading.

### Four-Layer Architecture

| Layer | Directory | Responsibility | AI-Generated |
|------|------|------|:---:|
| **L1 Action** | `L1_action/` | Smallest execution unit, stateless (e.g., a single Modbus read) | No |
| **L2 Node** | `L2_node/` | Stateful wrapper around Actions (with timeout, retry, and success/failure/timeout three-way branching) | No |
| **L3 Group** | `L3_group/` | Process orchestration (sequence/loop/condition/parallel, nestable) | Yes |
| **L4 Flow** | `L4_flow/` | Top-level executable configuration, supports multiple run modes | Yes |

### Design Principles

- **L1/L2 are fixed**: All layers that interact directly with hardware are predefined by engineers; AI does not participate in their generation.
- **L3/L4 are declarative**: AI-generated configuration describes only "what to do" (process orchestration), not "how to do it" (device driver details).
- **Interface contract**: L2 Node names form the base interface between process engineers and technicians. Technicians are responsible for the L1/L2 implementation and can pre-package commonly used process combinations as L3 Groups (a Group supports nested calls to Nodes and other Groups), which process engineers reference directly in higher-level process orchestration. The layers are decoupled through a unified naming contract.

### Core Features

- ✅ **L1 Action (atomic operations)**: Modbus read/write, computation, logging, popup, wait, etc.
- ✅ **L2 Node (execution node)**: retry mechanism, timeout control, success/failure/timeout branches
- ✅ **L3 Group (execution group)**: supports sequence, parallel, loop, if, switch control flow
- ✅ **L4 Flow (flow layer)**: orchestrates L3 Groups to build complete production-line flows
- ✅ **Parameterization**: supports `${variable}` placeholders and parameter passing
- ✅ **Bundle files**: L1/L2 use bundle aggregation files to reduce the number of files
- ✅ **JSON configuration**: all configuration is in JSON format, easy to edit and version-control
- ✅ **Three-gate semantic verification**: structural validation + simulation execution + semantic mapping
- ✅ **Simulation executor**: virtual device registry + runtime variable store + virtual clock + step budget (dead-loop prevention) + audit log + per-step operator confirmation

---

## 📦 Directory Structure

```
industrial_config_engine/
├── include/
│   └── industrial_config_engine/        # Header files
│       ├── action_loader.hpp           # L1 Action loader
│       ├── config.hpp                  # Configuration management
│       ├── l1_action.hpp               # L1 Action (atomic operation) class
│       ├── l2_node.hpp                 # L2 Node (execution node) class
│       ├── l3_group.hpp                # L3 Group (execution group) class
│       ├── l4_flow.hpp                 # L4 Flow class
│       ├── node_loader.hpp             # L2 Node loader
│       └── types.hpp                   # Data type definitions
├── src/                                 # Source files
│   ├── action_loader.cpp
│   ├── config.cpp
│   ├── l1_action.cpp
│   ├── l2_node.cpp
│   ├── l3_group.cpp
│   ├── l4_flow.cpp
│   └── node_loader.cpp
├── demos/                               # Demo programs
│   ├── demo_l1_action_loader.cpp       # L1 Action loader demo
│   ├── demo_l2_node_loader.cpp         # L2 Node loader demo
│   ├── demo_l3_group_loader.cpp        # L3 Group loader demo
│   ├── demo_l3_nested_group.cpp        # L3 nested Group demo
│   ├── demo_l4_flow_loader.cpp         # L4 Flow loader demo
│   └── demo_types.cpp                  # Type system demo
├── examples/                            # Example configuration files
│   ├── config_examples/                # Original configuration examples (reference)
│   │   ├── L1_action/modbus/iDM_actions.json
│   │   ├── L2_node/iDM_nodes.json
│   │   ├── L3_group/*.group.json
│   │   └── L4_flow/main_flow.json
│   ├── example1/                       # Example 1: vacuum control flow
│   │   ├── L1_action/                  # L1 Action (atomic operations, organized by directory)
│   │   │   ├── control/all_actions.json
│   │   │   ├── modbus/all_actions.json
│   │   │   └── system/all_actions.json
│   │   ├── L2_node/                    # L2 Node (execution nodes, organized by directory)
│   │   │   ├── control/all_nodes.json
│   │   │   ├── data/all_nodes.json
│   │   │   ├── system/all_nodes.json
│   │   │   └── vacuum/all_nodes.json
│   │   ├── L3_group/                   # L3 Group (execution groups)
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
│   ├── example2/                       # Example 2: motor feeding flow
│   │   ├── L1_action/                  # L1 bundle files (flat structure)
│   │   │   ├── control_actions.json
│   │   │   ├── data_actions.json
│   │   │   ├── leadshine_actions.json
│   │   │   ├── modbus_actions.json
│   │   │   └── system_actions.json
│   │   ├── L2_node/                    # L2 bundle files (flat structure)
│   │   │   ├── data_nodes.json
│   │   │   ├── leadshine_nodes.json
│   │   │   ├── log_nodes.json
│   │   │   ├── sensor_nodes.json
│   │   │   ├── system_nodes.json
│   │   │   ├── ui_nodes.json
│   │   │   ├── vacuum_nodes.json
│   │   │   ├── variable_nodes.json
│   │   │   └── wait_nodes.json
│   │   ├── L3_group/                   # L3 Group (execution groups, flat structure)
│   │   │   ├── complete_feeding.group.json
│   │   │   ├── emergency_stop.group.json
│   │   │   ├── jog_find_sensor.group.json
│   │   │   ├── precise_positioning.group.json
│   │   │   ├── run_pr_with_wait.group.json
│   │   │   ├── system_check.group.json
│   │   │   └── write_pr_position.group.json
│   │   └── L4_flow/
│   │       └── main_flow.json
│   └── example3/                       # Example 3: leak/gas-tightness testing flow
│       ├── L1_action/leak_test/all_actions.json
│       ├── L2_node/leak_test/all_nodes.json
│       ├── L3_group/leak_test/
│       │   ├── leak_test.group.json
│       │   └── pressure_hold.group.json
│       └── L4_flow/leak_test/
│           └── production_flow.json
├── CMakeLists.txt                       # Main build file
└── demos/CMakeLists.txt                 # Demo program build file
```

---

## 🚀 Building

### Prerequisites

- **CMake** 3.14 or later
- **C++17** compiler (MSVC 2019+ / GCC 7+ / Clang 6+)
- **nlohmann/json** library (downloaded automatically)

### Windows (Visual Studio)

```bash
# Configure
cmake -B out\build\x64-Debug -S .

# Build everything
cmake --build out\build\x64-Debug

# Build a specific demo
cmake --build out\build\x64-Debug --target demo_l3_group_loader
```

### Linux / macOS

```bash
# Configure
cmake -B out/build -S .

# Build everything
cmake --build out/build

# Build a specific demo
cmake --build out/build --target demo_l3_group_loader
```

### Output Directory

The compiled executables are located in:
- Windows: `out\build\x64-Debug\bin\`
- Linux/macOS: `out/build/bin/`

---

## 📋 Demo Programs

### 1. demo_types - Type System Demo

Demonstrates the basic functionality of the data type system.

```bash
./demo_types
```

**Features**:
- Displays all supported data types (u8, i8, u16, i16, u32, i32, f, bool, string, hex, etc.)
- Type conversion and validation
- Type names and size information

---

### 2. demo_l1_action_loader - L1 Action Loader Demo

Demonstrates loading and display of L1 Actions (atomic operations).

```bash
# Default directory (auto-discovers examples/example1)
./demo_l1_action_loader

# Specify a configuration directory
./demo_l1_action_loader D:/path/to/examples/example2
```

**Features**:
- Loads all Action configuration files under the `L1_action` directory
- Supports bundle files (`*_actions.json`) and directory classification (`category/all_actions.json`)
- Displays detailed Action information (type, request, response, args, etc.)

---

### 3. demo_l2_node_loader - L2 Node Loader Demo

Demonstrates loading and display of L2 Nodes (execution nodes), including resolution of references to L1 Actions.

```bash
# Default directory
./demo_l2_node_loader

# Specify a configuration directory
./demo_l2_node_loader D:/path/to/examples/example2
```

**Features**:
- Loads all Node configuration files under the `L2_node` directory
- Resolves L1 Actions referenced by Nodes (template references)
- Displays complete Node information (params, judge, on_success, on_failure, on_timeout)

---

### 4. demo_l3_group_loader - L3 Group Loader Demo

Demonstrates loading and display of L3 Groups (execution groups), with display-level control and parameter filling.

```bash
# Basic usage
./demo_l3_group_loader

# Specify a directory
./demo_l3_group_loader D:/path/to/examples/example2

# Display-level control
./demo_l3_group_loader --level=action      # Show everything (including Actions)
./demo_l3_group_loader --level=node        # Show Group + Node (default)
./demo_l3_group_loader --level=group       # Show Groups only

# Parameter filling
./demo_l3_group_loader --fill=true         # Replace variables with parameter values
./demo_l3_group_loader --fill=false        # Show raw content (default)

# Custom parameter values
./demo_l3_group_loader --fill=true --param=instance_id="motor1"

# Combined usage
./demo_l3_group_loader --level=action --fill=true --param=pr_number=5
```

**Features**:
- Loads all Group configuration files under the `L3_group` directory
- Supports sequence, parallel, loop, if, switch control flow
- **Display-level control**:
  - `group`: shows only the Group structure
  - `node`: shows Group + Node details
  - `action`: shows everything (including Actions in on_success/on_failure/on_timeout)
- **Parameter filling**:
  - `--fill=true`: replaces `${variable}` placeholders with actual values
  - `--fill=false`: shows raw content (default)
- Custom parameter values: `--param=key=value`

---

### 5. demo_l3_nested_group - L3 Nested Group Demo

Demonstrates nesting of L3 Groups, showing how to build complex control-flow hierarchies.

```bash
./demo_l3_nested_group
```

**Features**:
- Creates and displays nested Group structures
- Demonstrates mixed sequence + parallel modes
- Displays the Group structure tree
- Validates nesting depth limits

---

### 6. demo_l4_flow_loader - L4 Flow Loader Demo

Demonstrates loading and execution of the L4 Flow layer.

```bash
# Default directory
./demo_l4_flow_loader

# Specify a directory
./demo_l4_flow_loader D:/path/to/examples/example2
```

**Features**:
- Loads Flow configuration files under the `L4_flow` directory
- Resolves L3 Groups referenced by the Flow
- Displays the complete flow structure tree
- Supports flow parameter passing

---

### 7. demo_simulator - Simulation Runner Demo

Demonstrates simulation execution of configurations: virtual device registry + runtime variable store + virtual clock + step budget (dead-loop detection) + audit log + per-step operator confirmation.

```bash
./demo_simulator [config root 1] [config root 2] ...
```

**Features**:
- Executes L3/L4 configurations on virtual devices (registers/coils) and outputs a timing trace
- Loop condition evaluation: referencing an undefined variable → CONDITION_ERROR; an always-true condition → step budget exceeded (suspected infinite loop)
- Missing reference (undefined node/template) → REFERENCE_ERROR
- All execution decisions are written to the audit log; when confirm_between=true, operator confirmation is recorded at every step
- Built-in fault-injection demos (misspelled variable, always-true loop, missing node)

---

## 🎯 Configuration File Format

### Directory Organization

The project supports two ways of organizing L1/L2 configuration:

| Style | Directory structure | Suitable scenario |
|------|---------|----------|
| **Directory classification** | `L1_action/category/all_actions.json` | Organized by functional module; suitable for large projects |
| **Bundle files** | `L1_action/category_actions.json` | Flat structure; suitable for small-to-medium projects |

The two styles can be mixed, and the loader detects them automatically.

### L1 Action Naming Convention

```
Format: name.parameter_type_list.r_return_value_type_return_value_variable_name
```

| Example | Description |
|------|------|
| `log.s` | Parameters: s, no return value |
| `popup.s_s` | Parameters: s, s, no return value |
| `write_register.u16_u16.r_b_write_result` | Parameters: u16, u16, returns: b |
| `read_pressure.u8_u16.r_u16_pressure` | Parameters: u8, u16, returns: u16 |

### L2 Node Naming Convention

Same format as L1 Actions. Nodes reference L1 Actions via `action.template`:

```json
"action": {
    "template": "L1_action/modbus_actions.json/read_pressure.u8_u16.r_u16_pressure"
}
```

### L3 Group Naming Convention

| Type | Format | Example |
|------|------|------|
| With parameters | `name.group.type1_type2.json` | `pump_down.group.u16_f.json` |
| Without parameters | `name.group.json` | `motor_enable.group.json` |

### Control Flow Modes

| Mode | Description | Example |
|------|------|------|
| `sequence` | Sequential execution | `"mode": "sequence"` |
| `parallel` | Parallel execution | `"mode": "parallel"` |
| `loop` | Loop execution | `"mode": "loop"` |
| `if` | Conditional branch | `"mode": "if"` |
| `switch` | Multi-condition branch | `"mode": "switch"` |

---

## 🔧 Reference Path Rules

| Reference type | Path format | Example |
|---------|---------|------|
| L4 → L3 Group | `L3_group/xxx.group.json` | `L3_group/pump_down.group.json` |
| L3 → L2 Node | `L2_node/xxx_nodes.json/filename` | `L2_node/vacuum_nodes.json/read_pressure.r_u16_pressure` |
| L2 → L1 Action | `L1_action/xxx_actions.json/filename` | `L1_action/modbus_actions.json/read_pressure.u8_u16.r_u16_pressure` |

**Note**:
- The `body` of an L3 Group may contain only L2 Nodes or L3 Groups
- The `action` of an L2 Node may reference only L1 Actions
- L3 directly referencing L1 Actions is not supported

---

## 🔧 FAQ

### Q: The build reports that nlohmann/json cannot be found

**A**: CMake downloads nlohmann/json automatically. If a network problem causes the download to fail, you can install it manually:

```bash
# Windows (vcpkg)
vcpkg install nlohmann-json

# Linux (apt)
sudo apt install nlohmann-json3-dev

# macOS (brew)
brew install nlohmann-json
```

### Q: At runtime it reports that the configuration file cannot be found

**A**: By default the program looks for configuration under the `bin/examples/example1/` directory. You can specify a directory via a command-line argument:

```bash
./demo_l3_group_loader D:/path/to/your/config
```

### Q: How do I choose which example to use?

**A**: The program auto-discovers subdirectories under `examples/`. You can also specify one manually:

```bash
# Use example1 (vacuum control)
./demo_l3_group_loader D:/project/industrial_config_engine/examples/example1

# Use example2 (motor feeding)
./demo_l3_group_loader D:/project/industrial_config_engine/examples/example2
```

### Q: How do I view more detailed output?

**A**: Use the `--level=action` option:

```bash
./demo_l3_group_loader --level=action
```

### Q: How do I view content after parameter filling?

**A**: Use the `--fill=true` option:

```bash
./demo_l3_group_loader --fill=true
```

---

## 📝 License

This project is released under the **GNU General Public License v3.0** open-source license.

Copyright (c) 2024-2026 Chuqing Ming

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

**Special note**: This code is the accompanying experimental code for the paper "A Data-Driven Industrial Control Configuration Language: A Semantic Verification Framework for Safe LLM Integration", provided for academic research only. For commercial use, please contact the author for authorization.

---

## 👥 Contributors

### Author

**Chuqing Ming**

- **Affiliation**: Northeast Yucai School
- **Region**: Liaoning, China
- **Role**: Project lead & core developer
- **Contributions**:
  - Design and implementation of the four-layer architecture (L1-L4)
  - Core module development for the L1 Action (atomic operations), L2 Node (execution nodes), L3 Group (execution groups), and L4 Flow (flow layer)
  - Implementation of the configuration engine and loaders
  - Paper writing and experimental validation

---

### Affiliation

**Northeast Yucai School**
Liaoning, China

---

## 📖 How to Cite

If you use this project in academic research or a paper, please cite it in the following format:

```bibtex
@article{ming2026industrial,
  title   = {A Data-Driven Industrial Control Configuration Language: A Semantic Verification Framework for Safe LLM Integration},
  author  = {Chuqing Ming},
  school  = {Northeast Yucai School},
  year    = {2026},
  address = {Liaoning, China}
}
```

---

## 📚 Related Documentation

- [Four-Layer Architecture Design Documentation](examples/docs/)

---

**Main changes**:

| Change | Description |
|--------|------|
| Four-layer architecture table | Added an "AI-Generated" column, consistent with the paper |
| Design principles | Added a complete interface-contract description, reflecting that technicians can package L3 Groups |
| Core features | Added "three-gate semantic verification" |
| Example names | Consistent with the paper's table (vacuum control flow / motor feeding flow / leak/gas-tightness testing flow) |
| example2 L3 | Corresponds to T2 in the paper (12 Groups) |
| example3 L3 | Corresponds to T3 in the paper (leak/gas-tightness testing, 7 Groups) |
| Special note | Added the paper title |
| Citation format | Added the paper title |
| L3 nesting demo | Removed "max_nodes" (not mentioned in the paper) |
