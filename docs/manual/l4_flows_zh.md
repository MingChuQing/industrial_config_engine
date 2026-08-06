# 流程层（Flow / L4）完整定义 v1.1

================================================================================
目录
================================================================================

一、概述
   1.1 什么是流程层（Flow）？
   1.2 L4 在四层架构中的位置
   1.3 L4 的核心特性
   1.4 L4 的职责边界

二、Flow 完整定义
   2.1 Flow 结构
   2.2 字段说明
   2.3 Profiles（运行模式）
   2.4 Flow 文件命名规范

三、Flow 引用 L3 Group
   3.1 引用方式
   3.2 完整 Flow 示例

四、完整示例合集
   4.1 示例1：简单顺序流程
   4.2 示例2：带模式切换的流程
   4.3 示例3：带条件分支的流程
   4.4 示例4：带循环的流程
   4.5 示例5：带全局急停清理的完整流程

五、L4 引用规则
   5.1 允许的内容
   5.2 禁止的内容
   5.3 引用深度限制

六、Flow 中可用的变量
   6.1 系统变量
   6.2 Profile 变量
   6.3 用户变量

七、错误处理
   7.1 错误传播
   7.2 Flow 级别的错误处理

八、Fast Reference
   8.1 Flow 字段速查
   8.2 模式参数引用速查
   8.3 L3 引用速查
   8.4 层级关系速查

九、文件模板
   9.1 最小 Flow 模板
   9.2 完整 Flow 模板
   9.3 多版本管理


================================================================================
一、概述
================================================================================

1.1 什么是流程层（Flow）？

流程层（L4）是四层架构中的顶层，是完整产线流程的最终可执行文件。
它通过编排 L3 执行组（Group）来定义完整的生产流程。

1.2 L4 在四层架构中的位置

+-------------------------------------------------------------+
|  L4 流程层（Flow）                                           |
|  完整的产线流程配置，顶层可执行文件                          |
|  示例：main_flow.json                                        |
|  ---------------------------------------------------------- |
|  只做三件事：编排顺序、传入参数、选择运行模式                |
+-------------------------------------------------------------+
|  L3 执行组（Group）                                          |
|  一组相关的 Node 完成一个完整操作                            |
|  示例："抽真空及保压" 工序                                   |
+-------------------------------------------------------------+
|  L2 执行节点（Node）                                         |
|  单个操作 + 重试 + 分支                                      |
|  示例："读取真空度" 节点                                     |
+-------------------------------------------------------------+
|  L1 原子操作（Action）                                       |
|  最小执行单元                                                |
|  示例："modbus_read_register"                                |
+-------------------------------------------------------------+

1.3 L4 的核心特性

+------------------+----------------------------------------------------------+
| 特性              | 说明                                                     |
+------------------+----------------------------------------------------------+
| 顶层编排          | 是系统最终执行的入口文件，组织所有 L3 Group               |
| 参数传递          | 向 L3 Group 传递运行时参数                                |
| 模式选择          | 支持不同运行模式（test / production / debug）             |
| 全局配置          | 定义整个流程的全局超时、重试策略等                        |
| 全局清理          | 定义整个流程的急停清理（兜底）                            |
| 版本管理          | 支持多版本并行，可快速切换                                |
| 超时处理          | 支持 Flow 级别的超时控制，超时后执行 on_timeout 分支     |
+------------------+----------------------------------------------------------+

1.4 L4 的职责边界

+------------------+----------------------------------------------------------+
| 职责              | 说明                                                     |
+------------------+----------------------------------------------------------+
| 编排顺序          | 决定 L3 Group 的执行顺序                                  |
| 传入参数          | 向 L3 Group 传递具体参数值                                |
| 选择模式          | 决定使用哪套参数配置                                      |
| 全局控制          | 定义超时、重试、急停等                                    |
| 不定义新功能      | 不包含 L3 的具体实现，只引用，不定义                      |
+------------------+----------------------------------------------------------+

关键约束：L4 只做编排，不定义新功能。


================================================================================
二、Flow 完整定义
================================================================================

2.1 Flow 结构

{
    "type": "flow",
    "name": "主流程名称",
    "description": "流程描述（可选）",
    "version": "1.0.0",
    "timeout_ms": 3600000,
    "profiles": {
        "test": {
            "description": "测试模式参数"
        },
        "production": {
            "description": "生产模式参数"
        }
    },
    "active_profile": "production",
    "body": [
        // L3 Group 引用或内联定义
    ],
    "on_timeout": [
        // 超时时执行的内容（可选）
    ],
    "on_emergency_cleanup": {
        // 全局急停清理（兜底）
    }
}

2.2 字段说明

+------------------------+--------+------------------+---------------------------+------------------------------------------+
| 字段                   | 必填   | 类型             | 默认值                    | 说明                                     |
+------------------------+--------+------------------+---------------------------+------------------------------------------+
| type                   | 是     | string           | —                         | 固定 "flow"                              |
| name                   | 是     | string           | —                         | 流程名称                                 |
| description            | 否     | string           | —                         | 流程描述                                 |
| version                | 否     | string           | "1.0.0"                   | 语义化版本号                             |
| timeout_ms             | 否     | uint32           | 3600000                   | 整个流程的总超时时间（毫秒），0=无限制   |
| profiles               | 否     | object           | —                         | 运行模式参数配置                         |
| active_profile         | 否     | string           | —                         | 当前激活的模式（必须在 profiles 中定义） |
| body                   | 是     | array            | —                         | L3 Group 执行内容                        |
| on_timeout             | 否     | array            | —                         | 超时时执行的内容（仅 timeout_ms>0 时生效）|
| on_emergency_cleanup   | 否     | object           | —                         | 全局急停清理（兜底）                     |
+------------------------+--------+------------------+---------------------------+------------------------------------------+

【重要说明：timeout_ms 的设计】

    timeout_ms 是可选字段，用于设置整个 Flow 的最长执行时间限制。

    +------------+----------------------------------------------------------+
    | 值         | 行为                                                     |
    +------------+----------------------------------------------------------+
    | 0          | 无超时限制。Flow 会一直等待直到所有 body 成员执行完成    |
    | > 0        | 设置超时时间（毫秒）。从 Flow 开始执行计时，              |
    |            | 超时后立即停止所有正在执行的 Group，执行 on_timeout 分支 |
    +------------+----------------------------------------------------------+

    设计原则：
    - timeout_ms = 0 表示"不限制执行时间"
    - timeout_ms 默认值为 3600000（1小时），兼顾大部分生产场景
    - 如需无限制执行，显式设置为 0
    - on_timeout 仅在 timeout_ms > 0 且超时发生时执行

2.3 Profiles（运行模式）

profiles 允许为不同运行环境配置不同参数：

"profiles": {
    "test": {
        "description": "测试模式 - 使用较短超时和模拟数据",
        "params": {
            "vacuum_target": 10.0,
            "hold_time": 30,
            "timeout": 60000
        }
    },
    "production": {
        "description": "生产模式 - 使用标准工艺参数",
        "params": {
            "vacuum_target": 5.0,
            "hold_time": 60,
            "timeout": 120000
        }
    },
    "debug": {
        "description": "调试模式 - 每步等待用户确认",
        "params": {
            "vacuum_target": 5.0,
            "hold_time": 60,
            "timeout": 120000
        }
    }
},
"active_profile": "production"

在 body 中通过 ${profile.参数名} 引用模式参数：

{
    "type": "group",
    "template": "L3_group/vacuum/pump_down.group.f_u16",
    "params": ["${profile.vacuum_target}", "${profile.timeout}"]
}

2.4 Flow 文件命名规范

+------------------+---------------------------+------------------------------------------+
| 类型              | 格式                      | 示例                                     |
+------------------+---------------------------+------------------------------------------+
| 主流程            | 自定义名称_flow.json      | main_flow.json                           |
| 带版本            | 自定义名称_v版本_flow.json| main_v1.0.0_flow.json                    |
| 按用途            | 用途_flow.json            | test_flow.json / debug_flow.json         |
+------------------+---------------------------+------------------------------------------+

命名规则：
    - 自定义名称：只能包含字母、数字、下划线
    - 必须包含 _flow 标识（与 L1/L2/L3 区分）
    - 建议按用途/环境分目录管理


================================================================================
三、Flow 引用 L3 Group
================================================================================

3.1 引用方式

【方式一：通过 template 引用（推荐）】
{
    "type": "group",
    "template": "L3_group/vacuum/pump_down.group.f_u16"
}

【方式二：带参数引用】
{
    "type": "group",
    "template": "L3_group/vacuum/pump_down.group.f_u16",
    "params": [5.0, 120000]
}

【方式三：引用时覆盖 Group 属性】
{
    "type": "group",
    "template": "L3_group/vacuum/pump_down.group.f_u16",
    "params": [5.0, 120000],
    "confirm_between": true,
    "delay_between": 1000
}

【方式四：内联定义 L3 Group（不推荐，仅用于临时调试）】
{
    "type": "group",
    "name": "临时工序",
    "mode": "sequence",
    "body": [
        { "type": "node", ... }
    ]
}

3.2 完整 Flow 示例

文件：L4_flow/production/main_flow.json

{
    "type": "flow",
    "name": "主生产流程",
    "description": "真空腔体生产线的完整流程，包含取料、抽真空、保压、泄漏检测",
    "version": "2.1.0",
    "timeout_ms": 3600000,
    "profiles": {
        "test": {
            "description": "测试模式 - 使用较短超时和较低要求",
            "params": {
                "vacuum_target": 10.0,
                "hold_time": 30,
                "leak_threshold": 1.0,
                "timeout": 60000,
                "confirm_between": false
            }
        },
        "production": {
            "description": "生产模式 - 标准工艺参数",
            "params": {
                "vacuum_target": 5.0,
                "hold_time": 60,
                "leak_threshold": 0.5,
                "timeout": 120000,
                "confirm_between": false
            }
        },
        "debug": {
            "description": "调试模式 - 每步需要用户确认",
            "params": {
                "vacuum_target": 5.0,
                "hold_time": 60,
                "leak_threshold": 0.5,
                "timeout": 180000,
                "confirm_between": true
            }
        }
    },
    "active_profile": "production",
    "body": [
        {
            "type": "group",
            "name": "阶段1：取空P10",
            "template": "L3_group/assembly/pick_place.group",
            "confirm_between": "${profile.confirm_between}"
        },
        {
            "type": "group",
            "name": "阶段2：抽真空",
            "template": "L3_group/vacuum/pump_down.group.f_u16",
            "params": ["${profile.vacuum_target}", "${profile.timeout}"],
            "confirm_between": "${profile.confirm_between}"
        },
        {
            "type": "group",
            "name": "阶段3：保压",
            "template": "L3_group/vacuum/hold_pressure.group.f_u16",
            "params": ["${profile.hold_time}", "${profile.timeout}"],
            "confirm_between": "${profile.confirm_between}"
        },
        {
            "type": "group",
            "name": "阶段4：泄漏检测",
            "template": "L3_group/vacuum/leak_test.group.f_f",
            "params": ["${profile.leak_threshold}", "${profile.timeout}"],
            "confirm_between": "${profile.confirm_between}"
        },
        {
            "type": "group",
            "name": "阶段5：封装A1",
            "template": "L3_group/assembly/fasten.group",
            "confirm_between": "${profile.confirm_between}"
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "[TIMEOUT] 主流程整体超时（1小时）" },
        {
            "type": "group",
            "template": "L3_group/common/emergency_stop.group"
        },
        {
            "type": "node",
            "name": "超时告警",
            "result_key": "popup_result",
            "params": ["主流程执行超时，已自动停止", "error"],
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ],
    "on_emergency_cleanup": {
        "type": "sequence",
        "body": [
            {
                "type": "group",
                "name": "全局紧急关机",
                "mode": "parallel",
                "body": [
                    {
                        "type": "node",
                        "name": "关闭所有阀门",
                        "result_key": "all_valves_closed",
                        "params": [],
                        "action": { "template": "L1_action/modbus/write_coil_off" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 所有阀门已关闭" }
                        ],
                        "on_failure": []
                    },
                    {
                        "type": "node",
                        "name": "停止所有泵",
                        "result_key": "all_pumps_stopped",
                        "params": [0x0051, 0x0000],
                        "action": { "template": "L1_action/modbus/write_register.u16_u16" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 所有泵已停止" }
                        ],
                        "on_failure": []
                    },
                    {
                        "type": "node",
                        "name": "停止所有电机",
                        "result_key": "all_motors_stopped",
                        "params": [0x0061, 0x0000],
                        "action": { "template": "L1_action/modbus/write_register.u16_u16" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 所有电机已停止" }
                        ],
                        "on_failure": []
                    }
                ]
            },
            {
                "type": "node",
                "name": "急停日志",
                "result_key": "log_result",
                "params": ["[EMERGENCY] 主流程全局急停清理完成"],
                "action": { "template": "L1_action/system/log.s" },
                "on_success": [],
                "on_failure": []
            },
            {
                "type": "node",
                "name": "急停弹框",
                "result_key": "popup_result",
                "params": ["系统已紧急停止！请检查后按重置恢复", "error"],
                "action": { "template": "L1_action/system/popup.s_s" },
                "on_success": [],
                "on_failure": []
            }
        ],
        "inherit_parent": "skip"
    }
}


================================================================================
四、完整示例合集
================================================================================

4.1 示例1：简单顺序流程（无超时限制）

文件：L4_flow/test/simple_flow.json

{
    "type": "flow",
    "name": "简单测试流程",
    "description": "仅用于功能验证的简单流程",
    "version": "1.0.0",
    "timeout_ms": 0,  // 无超时限制
    "body": [
        {
            "type": "group",
            "name": "读取温度",
            "template": "L3_group/common/read_sensor.group"
        },
        {
            "type": "group",
            "name": "等待5秒",
            "template": "L3_group/common/wait.group"
        },
        {
            "type": "group",
            "name": "写入结果",
            "template": "L3_group/common/write_result.group"
        }
    ]
    // 不需要 on_timeout，因为 timeout_ms=0
}

4.2 示例2：带模式切换的流程

文件：L4_flow/production/vacuum_flow.json

{
    "type": "flow",
    "name": "真空工艺流程",
    "description": "标准真空工艺，支持不同模式切换",
    "version": "2.0.0",
    "timeout_ms": 1800000,
    "profiles": {
        "test": {
            "params": {
                "target_pressure": 50.0,
                "pump_time": 30,
                "hold_time": 15,
                "leak_threshold": 2.0
            }
        },
        "production": {
            "params": {
                "target_pressure": 5.0,
                "pump_time": 60,
                "hold_time": 60,
                "leak_threshold": 0.5
            }
        },
        "high_precision": {
            "params": {
                "target_pressure": 1.0,
                "pump_time": 120,
                "hold_time": 120,
                "leak_threshold": 0.1
            }
        }
    },
    "active_profile": "production",
    "body": [
        {
            "type": "group",
            "name": "初始检测",
            "template": "L3_group/vacuum/initial_check.group",
            "params": ["${profile.target_pressure}"]
        },
        {
            "type": "group",
            "name": "抽真空",
            "template": "L3_group/vacuum/pump_down.group.f_u16",
            "params": ["${profile.target_pressure}", "${profile.pump_time}"]
        },
        {
            "type": "group",
            "name": "保压",
            "template": "L3_group/vacuum/hold_pressure.group.f_u16",
            "params": ["${profile.hold_time}", "${profile.pump_time}"]
        },
        {
            "type": "group",
            "name": "泄漏检测",
            "template": "L3_group/vacuum/leak_test.group.f_f",
            "params": ["${profile.leak_threshold}", "${profile.pump_time}"]
        },
        {
            "type": "group",
            "name": "记录结果",
            "template": "L3_group/common/log_result.group",
            "params": ["${profile.target_pressure}"]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "[TIMEOUT] 真空工艺超时" },
        {
            "type": "group",
            "template": "L3_group/common/emergency_stop.group"
        }
    ]
}

4.3 示例3：带条件分支的流程

文件：L4_flow/production/smart_flow.json

{
    "type": "flow",
    "name": "智能生产流程",
    "description": "根据检测结果自动选择不同路径",
    "version": "1.5.0",
    "timeout_ms": 7200000,
    "profiles": {
        "production": {
            "params": {
                "quality_threshold": 95.0
            }
        }
    },
    "active_profile": "production",
    "body": [
        {
            "type": "group",
            "name": "物料准备",
            "template": "L3_group/assembly/prepare_material.group"
        },
        {
            "type": "group",
            "name": "关键检测",
            "template": "L3_group/assembly/quality_check.group"
        },
        {
            "type": "group",
            "name": "判断质量结果",
            "mode": "if",
            "condition": {
                "type": "expression",
                "expression": "${quality_score} >= ${profile.quality_threshold}"
            },
            "then": [
                {
                    "type": "group",
                    "name": "合格产品：继续组装",
                    "template": "L3_group/assembly/assemble.group"
                },
                {
                    "type": "group",
                    "name": "包装出货",
                    "template": "L3_group/assembly/packaging.group"
                }
            ],
            "else": [
                {
                    "type": "group",
                    "name": "不合格产品：进入返工",
                    "template": "L3_group/assembly/rework.group"
                },
                {
                    "type": "group",
                    "name": "记录缺陷",
                    "template": "L3_group/common/log_defect.group",
                    "params": ["${quality_score}"]
                }
            ]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "[TIMEOUT] 智能生产流程超时" }
    ]
}

4.4 示例4：带循环的流程

文件：L4_flow/production/cyclic_flow.json

{
    "type": "flow",
    "name": "循环生产流程",
    "description": "循环执行生产批次",
    "version": "1.0.0",
    "timeout_ms": 86400000,  // 24小时
    "profiles": {
        "production": {
            "params": {
                "batch_count": 50,
                "batch_interval": 30
            }
        }
    },
    "active_profile": "production",
    "body": [
        {
            "type": "group",
            "name": "批次循环生产",
            "mode": "loop",
            "loop": {
                "type": "count",
                "count": "${profile.batch_count}"
            },
            "body": [
                {
                    "type": "group",
                    "name": "单批次生产",
                    "template": "L3_group/production/single_batch.group"
                },
                {
                    "type": "group",
                    "name": "批次间隔等待",
                    "template": "L3_group/common/wait.group",
                    "params": ["${profile.batch_interval}", "seconds"]
                }
            ]
        },
        {
            "type": "group",
            "name": "生产完成统计",
            "template": "L3_group/common/statistics.group"
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "[TIMEOUT] 循环生产流程超时（24小时）" },
        {
            "type": "group",
            "template": "L3_group/common/emergency_stop.group"
        }
    ]
}

4.5 示例5：带全局急停清理的完整流程

文件：L4_flow/production/main_with_emergency.json

{
    "type": "flow",
    "name": "带急停保护的完整流程",
    "description": "包含完整的急停清理保护",
    "version": "2.0.0",
    "timeout_ms": 7200000,
    "profiles": {
        "production": {
            "params": {
                "vacuum_target": 5.0,
                "hold_time": 60
            }
        }
    },
    "active_profile": "production",
    "body": [
        {
            "type": "group",
            "name": "取料",
            "template": "L3_group/assembly/pick_place.group"
        },
        {
            "type": "group",
            "name": "抽真空",
            "template": "L3_group/vacuum/pump_down.group.f_u16",
            "params": ["${profile.vacuum_target}", 120000]
        },
        {
            "type": "group",
            "name": "保压",
            "template": "L3_group/vacuum/hold_pressure.group.f_u16",
            "params": ["${profile.hold_time}", 120000]
        },
        {
            "type": "group",
            "name": "泄漏检测",
            "template": "L3_group/vacuum/leak_test.group.f_f"
        },
        {
            "type": "group",
            "name": "封装",
            "template": "L3_group/assembly/fasten.group"
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "[TIMEOUT] 完整流程超时" },
        {
            "type": "group",
            "template": "L3_group/common/emergency_stop.group"
        }
    ],
    "on_emergency_cleanup": {
        "type": "sequence",
        "body": [
            {
                "type": "group",
                "name": "全局紧急关机",
                "mode": "parallel",
                "body": [
                    {
                        "type": "node",
                        "name": "关闭所有阀门",
                        "result_key": "valves_closed",
                        "params": [],
                        "action": { "template": "L1_action/modbus/write_coil_off" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 所有阀门已关闭" }
                        ],
                        "on_failure": []
                    },
                    {
                        "type": "node",
                        "name": "停止所有泵",
                        "result_key": "pumps_stopped",
                        "params": [0x0051, 0x0000],
                        "action": { "template": "L1_action/modbus/write_register.u16_u16" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 所有泵已停止" }
                        ],
                        "on_failure": []
                    },
                    {
                        "type": "node",
                        "name": "停止所有电机",
                        "result_key": "motors_stopped",
                        "params": [0x0061, 0x0000],
                        "action": { "template": "L1_action/modbus/write_register.u16_u16" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 所有电机已停止" }
                        ],
                        "on_failure": []
                    },
                    {
                        "type": "node",
                        "name": "打开排气阀",
                        "result_key": "vent_opened",
                        "params": [],
                        "action": { "template": "L1_action/modbus/write_coil_on" },
                        "on_success": [
                            { "type": "log", "message": "[EMERGENCY] 排气阀已打开" }
                        ],
                        "on_failure": []
                    }
                ]
            },
            {
                "type": "node",
                "name": "急停日志",
                "result_key": "log_result",
                "params": ["[EMERGENCY] 全局急停清理完成 - ${timestamp}"],
                "action": { "template": "L1_action/system/log.s" },
                "on_success": [],
                "on_failure": []
            },
            {
                "type": "node",
                "name": "急停弹框",
                "result_key": "popup_result",
                "params": ["系统已紧急停止！所有设备已关闭，请检查安全后重置", "error"],
                "action": { "template": "L1_action/system/popup.s_s" },
                "on_success": [],
                "on_failure": []
            }
        ],
        "inherit_parent": "skip"
    }
}


================================================================================
五、L4 引用规则
================================================================================

5.1 允许的内容

+---------------------------+----------------------------------------------------------+
| 允许引用的内容            | 示例                                                     |
+---------------------------+----------------------------------------------------------+
| L3 Group（通过 template） | {"type":"group","template":"L3_group/xxx.group"}          |
| L3 Group（内联定义）      | {"type":"group","name":"...","mode":"sequence",...}       |
| 控制流 Group              | {"type":"group","mode":"if",...}                          |
| (sequence/parallel/loop/if/switch)                        |
+---------------------------+----------------------------------------------------------+

5.2 禁止的内容

+---------------------------+----------------------------------------------------------+
| 禁止引用的内容            | 原因                                                     |
+---------------------------+----------------------------------------------------------+
| L1 Action 直接引用        | L4 应该通过 L3 间接引用 L1                               |
| L2 Node 直接引用          | L4 应该通过 L3 间接引用 L2                               |
| 直接编写 L1/L2 逻辑       | L4 只做编排，不实现功能                                   |
+---------------------------+----------------------------------------------------------+

5.3 引用深度限制

L4 Flow
  +-- L3 Group (层级1)  ← 允许
        +-- L3 Group (层级2)  ← 允许（L3 内部可嵌套）
              +-- L2 Node (层级3)  ← 允许（L3 内部包含 L2）
                    +-- L1 Action (层级4)  ← 允许（L2 内部包含 L1）

L4 直接引用 L1 或 L2 → 禁止


================================================================================
六、Flow 中可用的变量
================================================================================

6.1 系统变量

+------------------+----------+------------------------------------------+
| 变量名           | 类型     | 说明                                     |
+------------------+----------+------------------------------------------+
| ${timestamp}     | uint64   | 当前时间戳                               |
| ${uuid}          | string   | 流程唯一标识                             |
| ${flow_name}     | string   | 当前流程名称                             |
| ${flow_version}  | string   | 当前流程版本                             |
| ${flow_start_time}| uint64  | 流程开始时间                             |
+------------------+----------+------------------------------------------+

6.2 Profile 变量

+---------------------------+----------------------------------------------------------+
| 变量名                    | 说明                                                     |
+---------------------------+----------------------------------------------------------+
| ${profile.参数名}         | 当前激活模式的参数值                                      |
| 示例                      | ${profile.vacuum_target}                                 |
+---------------------------+----------------------------------------------------------+

6.3 用户变量

通过 L2 Node 的 result_key 写入的变量，在整个 Flow 中可用：

{
    "type": "group",
    "template": "L3_group/common/read_sensor.group",
    "params": [0x01, 2000]
    // 该 Group 内部的 Node 写入 ${temperature} 后，后续 Group 可引用
},
{
    "type": "group",
    "template": "L3_group/common/check_temperature.group",
    "params": ["${temperature}", 100]  // 引用上一个 Group 的结果
}


================================================================================
七、错误处理
================================================================================

7.1 错误传播

L4 Flow 执行
    |
    v
L3 Group A 失败
    |
    v
L4 Flow 检测到 Group A 失败
    +-- 如果是 sequence 模式 → 停止执行，返回错误
    +-- 如果是 parallel 模式 → 等待所有 Group 完成，汇总错误
    |
    v
Flow 执行失败，返回错误状态

7.2 Flow 级别的错误处理

L4 Flow 本身不包含 on_failure 分支，而是由调用方处理。
Flow 执行完成后返回执行状态：

+------------------+----------------------------------------------------------+
| 返回状态          | 说明                                                     |
+------------------+----------------------------------------------------------+
| SUCCESS          | 所有 Group 执行成功                                       |
| FAILED           | 某个 Group 执行失败                                       |
| TIMEOUT          | Flow 整体超时                                             |
| ABORTED          | 被用户中止                                               |
| EMERGENCY        | 被急停中断                                               |
+------------------+----------------------------------------------------------+

7.3 on_timeout 与 timeout_ms 的关系

+------------------+------------------+------------------+------------------------------------------+
| timeout_ms       | on_timeout       | 行为              | 说明                                     |
+------------------+------------------+------------------+------------------------------------------+
| 0                | 定义或不定义     | 不触发            | on_timeout 永远不会被执行                 |
| > 0              | 已定义           | 超时时执行        | 执行 on_timeout 分支                     |
| > 0              | 未定义           | 超时向上传播      | 将超时状态返回给调用方                   |
+------------------+------------------+------------------+------------------------------------------+

7.4 超时传播规则

+---------------------------+----------------------------------------------------------+
| 场景                      | 行为                                                     |
+---------------------------+----------------------------------------------------------+
| timeout_ms = 0            | 不启动计时器，永远不会触发超时                           |
| timeout_ms > 0 且定义 on_timeout | 超时时执行 on_timeout 分支，不向上传播           |
| timeout_ms > 0 且未定义 on_timeout | 超时时将 TIMEOUT 状态返回给调用方             |
| Flow 未定义 on_timeout   | 超时状态返回给系统调用者                                  |
+---------------------------+----------------------------------------------------------+


================================================================================
八、Fast Reference
================================================================================

8.1 Flow 字段速查

+------------------------+--------+------------------+---------------------------+------------------------------------------+
| 字段                   | 必填   | 类型             | 默认值                    | 说明                                     |
+------------------------+--------+------------------+---------------------------+------------------------------------------+
| type                   | 是     | string           | —                         | 固定 "flow"                              |
| name                   | 是     | string           | —                         | 流程名称                                 |
| description            | 否     | string           | —                         | 流程描述                                 |
| version                | 否     | string           | "1.0.0"                   | 版本号                                   |
| timeout_ms             | 否     | uint32           | 3600000                   | 总超时时间(ms)，0=无限制                 |
| profiles               | 否     | object           | —                         | 运行模式配置                             |
| active_profile         | 否     | string           | —                         | 当前激活的模式                           |
| body                   | 是     | array            | —                         | L3 Group 列表                            |
| on_timeout             | 否     | array            | —                         | 超时时执行（仅 timeout_ms>0 时生效）    |
| on_emergency_cleanup   | 否     | object           | —                         | 全局急停清理                             |
+------------------------+--------+------------------+---------------------------+------------------------------------------+

8.2 模式参数引用速查

+---------------------------+----------------------------------------------------------+
| 场景                      | 写法                                                     |
+---------------------------+----------------------------------------------------------+
| 引用模式参数              | ${profile.参数名}                                         |
| 定义模式参数              | "参数名": 值                                              |
| 切换模式                  | 修改 active_profile 字段                                  |
+---------------------------+----------------------------------------------------------+

8.3 L3 引用速查

+---------------------------+----------------------------------------------------------+
| 场景                      | 写法                                                     |
+---------------------------+----------------------------------------------------------+
| 引用无参数 Group          | {"type":"group","template":"L3_group/path/name.group"}    |
| 引用带参数 Group          | {"type":"group","template":"L3_group/path/name.group.f_u16","params":[5.0,120000]} |
| 引用时覆盖属性            | {"type":"group","template":"L3_group/path/name.group","confirm_between":true} |
| 内联定义 Group            | {"type":"group","name":"...","mode":"sequence","body":[...]} |

8.4 层级关系速查

+------------------+------------------+------------------+
| 层级              | 包含             | 被包含           |
+------------------+------------------+------------------+
| L1 Action         | —                | L2 Node          |
| L2 Node           | L1 Action        | L3 Group         |
| L3 Group          | L2 Node, L3 Group| L4 Flow, L3 Group|
| L4 Flow           | L3 Group         | 系统调用者       |
+------------------+------------------+------------------+

8.5 on_timeout / timeout_ms 速查

+---------------------------+----------------------------------------------------------+
| 场景 / 配置               | 说明                                                     |
+---------------------------+----------------------------------------------------------+
| timeout_ms = 0            | 无超时限制，on_timeout 永远不会被触发                     |
| timeout_ms > 0            | 设置超时时间，超时时触发 on_timeout                       |
| timeout_ms > 0 + on_timeout 已定义 | 超时时执行 on_timeout 分支                      |
| timeout_ms > 0 + on_timeout 未定义 | 超时时将 TIMEOUT 状态返回给调用方              |
| 何时设置 timeout_ms > 0   | 有明确时间要求的生产流程                                 |
| 何时保持 timeout_ms = 0   | 无时间要求的测试流程、手动控制流程                        |
+---------------------------+----------------------------------------------------------+


================================================================================
九、文件模板
================================================================================

9.1 最小 Flow 模板（无超时限制）

{
    "type": "flow",
    "name": "最小流程",
    "timeout_ms": 0,  // 无超时限制
    "body": [
        {
            "type": "group",
            "template": "L3_group/your_first_step.group"
        }
    ]
    // 不需要 on_timeout
}

9.2 完整 Flow 模板（有超时限制）

{
    "type": "flow",
    "name": "完整流程模板",
    "description": "流程描述",
    "version": "1.0.0",
    "timeout_ms": 3600000,
    "profiles": {
        "production": {
            "params": {
                "param1": 100,
                "param2": "value"
            }
        },
        "test": {
            "params": {
                "param1": 50,
                "param2": "test_value"
            }
        }
    },
    "active_profile": "production",
    "body": [
        {
            "type": "group",
            "name": "步骤1",
            "template": "L3_group/path/step1.group",
            "params": ["${profile.param1}"]
        },
        {
            "type": "group",
            "name": "步骤2",
            "template": "L3_group/path/step2.group",
            "params": ["${profile.param2}"]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "[TIMEOUT] 完整流程模板超时" },
        {
            "type": "group",
            "template": "L3_group/common/emergency_stop.group"
        },
        {
            "type": "node",
            "name": "超时告警",
            "result_key": "popup_result",
            "params": ["流程执行超时，已自动停止", "error"],
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ],
    "on_emergency_cleanup": {
        "type": "sequence",
        "body": [
            {
                "type": "node",
                "name": "紧急停止所有设备",
                "result_key": "emergency_stop",
                "params": [],
                "action": { "template": "L1_action/modbus/write_coil_off" },
                "on_success": [
                    { "type": "log", "message": "[EMERGENCY] 所有设备已停止" }
                ],
                "on_failure": []
            }
        ],
        "inherit_parent": "skip"
    }
}

9.3 多版本管理

L4_flow/
+-- production/
|   +-- main_flow.json              # 当前版本（软链接到 v2.1.0）
|   +-- main_v1.0.0_flow.json       # 历史版本
|   +-- main_v1.5.0_flow.json       # 历史版本
|   +-- main_v2.1.0_flow.json       # 最新版本
+-- test/
    +-- test_flow.json


================================================================================
文档信息
================================================================================

文档版本：v1.1
最后更新：2026-08-06
维护者：明楚晴（东北育才学校）

主要变更（v1.1）：
- 新增 on_timeout 超时处理分支（Flow 级别）
- 更新字段说明，增加 timeout_ms 和 on_timeout
- 新增 timeout_ms 设计详解
- 新增 on_timeout 示例
- 新增 on_timeout 速查表
- 新增超时传播规则说明
- 所有示例增加 on_timeout 字段
- 新增完整 Flow 模板（含 on_timeout）

主要变更（v1.0）：
- 初始版本
- Flow 完整定义
- Profiles 运行模式
- L3 Group 引用方式
- 完整示例合集


================================================================================
END OF DOCUMENT
================================================================================