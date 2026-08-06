# 执行组（Group / L3）完整定义 v2.1

（timeout_ms 可选项说明已修正）


================================================================================
一、概述
================================================================================

1.1 什么是执行组（Group）？

执行组（L3）是四层架构中的第三层，是一组相关的执行节点（L2 Node）和/或子执行组
（L3 Group）的组合，共同完成一个完整的操作或工序。

1.2 L3 在四层架构中的位置

+-------------------------------------------------------------+
|  L4 流程层（Flow）                                           |
|  完整的产线流程配置，由多个 L3 组成                          |
|  示例：main_flow.json                                        |
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

1.3 L3 的核心特性

+------------------+----------------------------------------------------------+
| 特性              | 说明                                                     |
+------------------+----------------------------------------------------------+
| 组合性            | 将多个 L2/L3 组合成一个完整的操作单元                     |
| 嵌套性            | 支持任意深度的嵌套（但禁止循环递归）                     |
| 执行模式          | 支持串行（sequence）、并行（parallel）、循环（loop）、    |
|                   | 条件分支（if）、多分支（switch）                          |
| 复用性            | 一个 L3 可被多个 L4 流程引用                              |
| 参数化            | 支持传入参数（可选）                                     |
| 结果输出          | 执行完成后可输出结果到变量（可选）                       |
| 合法性检查        | 支持指定最大 Node 数量，防止无限递归                     |
| 调试支持          | 支持在顺序执行的成员之间插入固定等待或手动确认           |
| 超时处理          | 支持 Group 级别的超时控制（可选），超时后执行 on_timeout 分支 |
+------------------+----------------------------------------------------------+


================================================================================
二、Group 完整定义
================================================================================

2.1 Group 结构

{
    "type": "group",
    "name": "执行组名称",
    "description": "执行组描述（可选）",
    "mode": "sequence",
    "max_nodes": 100,
    "delay_between": 0,
    "confirm_between": false,
    "timeout_ms": 0,
    "body": [
        // L2 Node 或 L3 Group
    ],
    "on_timeout": [
        // L2 Node 或 L3 Group（可选）
    ],
    "args": [
        // 参数定义（可选）
    ]
}

2.2 字段说明

+------------------+--------+------------------+---------------------------+------------------------------------------+
| 字段              | 必填   | 类型             | 默认值                    | 说明                                     |
+------------------+--------+------------------+---------------------------+------------------------------------------+
| type              | 是     | string           | —                         | 固定 "group"                             |
| name              | 是     | string           | —                         | 组名称，用于日志和调试                   |
| description       | 否     | string           | —                         | 组描述                                   |
| mode              | 是     | string           | —                         | sequence / parallel / loop / if / switch |
| max_nodes         | 否     | uint16           | 100                       | 本 group 底下所有 Node 的最大数量        |
| delay_between     | 否     | uint16           | 0                         | 顺序执行时成员间等待(ms)                 |
| confirm_between   | 否     | bool             | false                     | 顺序执行时成员间用户确认                 |
| timeout_ms        | 否     | uint32           | 0                         | Group 整体超时时间(ms)，0=无超时限制     |
| body              | 是     | array            | —                         | 执行内容（L2 Node 或 L3 Group）          |
| on_timeout        | 否     | array            | —                         | 超时时执行的内容（L2 Node 或 L3 Group）  |
| args              | 否     | array            | —                         | 参数定义                                 |
+------------------+--------+------------------+---------------------------+------------------------------------------+

【重要说明：timeout_ms 的设计】

    timeout_ms 是可选字段，用于设置整个 Group 的最长执行时间限制。

    +------------+----------------------------------------------------------+
    | 值         | 行为                                                     |
    +------------+----------------------------------------------------------+
    | 0（默认）  | 无超时限制。Group 会一直等待直到所有成员执行完成         |
    | > 0        | 设置超时时间（毫秒）。从 Group 开始执行计时，             |
    |            | 超时后立即停止所有正在执行的成员，执行 on_timeout 分支   |
    +------------+----------------------------------------------------------+

    设计原则：
    - timeout_ms = 0 表示"不限制执行时间"，是默认行为
    - timeout_ms > 0 表示"必须在指定时间内完成"，用于有严格时间要求的工序
    - 是否设置超时由业务场景决定：
        * 保压等待 60 秒：可能不需要 Group 超时（由内部 wait Node 控制）
        * 抽真空工序：可能需要 180 秒超时（防止真空泵故障导致无限等待）
    - on_timeout 仅在 timeout_ms > 0 且超时发生时执行
    - 如果 timeout_ms = 0，on_timeout 永远不会被触发

2.3 max_nodes 合法性检查详解

【作用】
max_nodes 用于防止意外无限递归和控制执行复杂度。

【检查规则】
    - 递归计算本 group 及其所有子 group 中包含的 Node 总数
    - 如果总数 > max_nodes，加载时报错并拒绝执行
    - 如果未指定 max_nodes，使用默认值 100

【示例】
{
    "type": "group",
    "name": "复杂工艺",
    "mode": "sequence",
    "max_nodes": 50,
    "body": [
        { "type": "node", "name": "N1", ... },        // 1
        { "type": "node", "name": "N2", ... },        // 2
        {
            "type": "group",
            "name": "子组",
            "max_nodes": 20,
            "body": [
                { "type": "node", "name": "N3", ... }, // 3
                { "type": "node", "name": "N4", ... }  // 4
            ]
        }
    ]
}

检查结果：总 Node 数 = 4 <= 50 ✓ 通过

【防止递归】
Group A (max_nodes: 10)
  body: [ Group B ]

Group B (max_nodes: 10)
  body: [ Group A ]  ← 循环引用！

检查时发现递归，报错：Circular reference detected

2.4 delay_between 详解

【作用】
在 mode: sequence 时，在每个成员执行之间插入固定等待时间。
仅在 mode: sequence 时生效。

【约束】
+---------+----------------------------------------------------------+
| 值      | 行为                                                     |
+---------+----------------------------------------------------------+
| 0       | 不等待                                                   |
| 10~60000| 等待指定毫秒数                                            |
+---------+----------------------------------------------------------+

【示例】
{
    "type": "group",
    "name": "带延迟的顺序执行",
    "mode": "sequence",
    "delay_between": 1000,
    "timeout_ms": 0,  // 无超时限制
    "body": [
        { "type": "node", "name": "第一步", ... },
        // 等待 1000ms
        { "type": "node", "name": "第二步", ... },
        // 等待 1000ms
        { "type": "node", "name": "第三步", ... }
    ]
}

2.5 confirm_between 详解

【作用】
在 mode: sequence 时，在每个成员执行之前弹出用户确认对话框。
仅在 mode: sequence 时生效。

【行为】
    - false：自动执行，无需确认
    - true：每个成员执行前，弹出 user_decision 对话框
    - 用户点击"确定" → 继续执行下一步
    - 用户点击"取消/暂停" → 暂停执行，等待进一步指令

【示例】
{
    "type": "group",
    "name": "手动调试流程",
    "mode": "sequence",
    "confirm_between": true,
    "timeout_ms": 0,  // 手动调试模式下不设超时
    "body": [
        { "type": "node", "name": "步骤1：开阀门", ... },
        // 弹出确认："即将执行步骤2：启动泵，是否继续？"
        { "type": "node", "name": "步骤2：启动泵", ... },
        // 弹出确认："即将执行步骤3：等待达标，是否继续？"
        { "type": "node", "name": "步骤3：等待达标", ... }
    ]
}

2.6 Group 文件命名规范

【格式】
    自定义名称.group.参数类型列表.json

【说明】
    - 自定义名称：只能包含字母、数字、下划线
    - 必须包含 .group. 标识（与 L1/L2/L4 区分）
    - 带参数时，在 .group. 后添加类型列表
    - 无参数时，文件名为 自定义名称.group.json

【示例】
+----------------------------------+------------------+----------------------------------+
| 文件名                           | 参数类型         | 说明                             |
+----------------------------------+------------------+----------------------------------+
| pump_down.group.json             | 无参数           | 抽真空工序                       |
| hold_pressure.group.u16_f.json   | u16, f           | 保压工序（带参数）               |
| leak_test.group.f.json           | f                | 泄漏检测（带参数）               |
| wait.group.json                  | 无参数           | 等待组                           |
+----------------------------------+------------------+----------------------------------+


================================================================================
三、执行模式详解
================================================================================

3.1 sequence（顺序执行）

- 按 body 数组的顺序依次执行
- 前一个执行完成后，才开始下一个
- 任意一个成员失败，立即停止，将错误状态返回给上层
- delay_between 和 confirm_between 在此模式下生效
- timeout_ms 控制整体执行时间（0=无限制）

{
    "type": "group",
    "name": "顺序操作示例",
    "mode": "sequence",
    "delay_between": 500,
    "confirm_between": false,
    "timeout_ms": 60000,  // 整体必须在60秒内完成
    "body": [
        { "type": "node", "name": "第一步", ... },
        { "type": "node", "name": "第二步", ... },
        { "type": "node", "name": "第三步", ... }
    ],
    "on_timeout": [
        { "type": "log", "message": "顺序操作超时" }
    ]
}

3.2 parallel（并行执行）

- body 中的所有项同时开始执行
- 等待所有项执行完成
- 收集所有执行结果
- delay_between 和 confirm_between 在 parallel 模式下无效
- timeout_ms 控制整体执行时间（0=无限制）

{
    "type": "group",
    "name": "并行操作示例",
    "mode": "parallel",
    "timeout_ms": 30000,  // 所有并行任务必须在30秒内完成
    "body": [
        { "type": "node", "name": "任务A", ... },
        { "type": "node", "name": "任务B", ... },
        { "type": "node", "name": "任务C", ... }
    ],
    "on_timeout": [
        { "type": "log", "message": "并行操作超时" },
        { "type": "popup", "message": "并行任务超时，请检查", "level": "error" }
    ]
}


================================================================================
四、循环控制流（loop）
================================================================================

4.1 设计原则

1. 循环本身（loop）是一种控制流模式，和 sequence、parallel、if、switch 同级
2. 循环体 body 只能是 L2 Node 或 L3 Group 的引用/内联定义
3. 循环体内部不支持直接写 sequence/parallel 等控制流模式（避免二义性）
4. 如果需要在循环内部实现复杂逻辑，封装成一个独立的 L3 Group 再引用
5. timeout_ms 控制整个循环的最大执行时间（0=无限制）

4.2 loop 结构

{
    "type": "group",
    "name": "循环组名称",
    "description": "循环执行 body 中的内容",
    "mode": "loop",
    "loop": {
        "type": "count",
        "count": 10
    },
    "timeout_ms": 0,  // 0=无超时限制
    "body": [
        // 只能是以下两种：
        // 1. L2 Node（内联定义）
        // 2. L3 Group（引用或内联定义）
    ],
    "on_timeout": [
        { "type": "log", "message": "循环执行超时" }
    ]
}

4.3 循环类型详解

【count - 固定次数循环】
{
    "loop": {
        "type": "count",
        "count": 10
    }
}

【while - 条件前置循环（条件为真时继续执行）】
{
    "loop": {
        "type": "while",
        "condition": "${pressure} > 5"
    }
}

【do-while - 条件后置循环（至少执行一次，条件为真时继续执行）】
{
    "loop": {
        "type": "do-while",
        "condition": "${pressure} > 5"
    }
}

【until - 条件前置循环（条件为真时停止执行）】
{
    "loop": {
        "type": "until",
        "condition": "${pressure} <= 5"
    }
}

【foreach - 遍历列表】
{
    "loop": {
        "type": "foreach",
        "items": "${valve_list}",
        "item_name": "valve_id"
    }
}

4.4 循环变量说明

+------------------+----------------------------------------------------------+
| 变量名           | 说明                                                     |
+------------------+----------------------------------------------------------+
| ${loop_index}    | 当前循环索引（从 0 开始，count/while/do-while/until 可用）|
| ${loop_count}    | 当前循环次数（从 1 开始，count 可用）                     |
| ${item_name}     | 当前遍历项的值（foreach 可用，名称由 item_name 指定）    |
+------------------+----------------------------------------------------------+

4.5 完整示例

【示例1：循环体内引用 L2 Node（无超时限制）】
{
    "type": "group",
    "name": "重复读取10次温度",
    "mode": "loop",
    "loop": {
        "type": "count",
        "count": 10
    },
    "timeout_ms": 0,  // 不限制总时间
    "body": [
        {
            "type": "node",
            "name": "读取温度",
            "result_key": "temp_${loop_index}",
            "params": [0x01, 2000],
            "timeout_ms": 5000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/modbus/read_register.u8_u16.r_u16_temperature" },
            "on_success": [
                { "type": "log", "message": "第${loop_index}次读取：${temperature}℃" }
            ],
            "on_failure": []
        }
    ]
}

【示例2：循环带超时限制（防止死循环）】
{
    "type": "group",
    "name": "等待压力达标",
    "mode": "loop",
    "loop": {
        "type": "while",
        "condition": "${pressure} > 5"
    },
    "timeout_ms": 120000,  // 最多等待2分钟
    "body": [
        {
            "type": "group",
            "name": "单次检测循环体",
            "mode": "sequence",
            "timeout_ms": 10000,
            "body": [
                {
                    "type": "node",
                    "name": "读取压力",
                    "result_key": "pressure",
                    "params": [0x01, 2000],
                    "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_pressure" },
                    "on_success": [],
                    "on_failure": []
                },
                {
                    "type": "node",
                    "name": "等待200ms",
                    "result_key": "wait_done",
                    "params": [200, "milliseconds"],
                    "action": { "template": "L1_action/control/wait.u16_s.r_b_wait_done" },
                    "on_success": [],
                    "on_failure": []
                }
            ],
            "on_timeout": [
                { "type": "log", "message": "单次检测超时" }
            ]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "等待压力达标超时（2分钟）" },
        { "type": "popup", "message": "压力未达标，请检查系统", "level": "error" }
    ]
}

【示例3：遍历阀门列表（无超时）】
{
    "type": "group",
    "name": "依次操作所有阀门",
    "mode": "loop",
    "loop": {
        "type": "foreach",
        "items": "${valve_list}",
        "item_name": "valve_id"
    },
    "timeout_ms": 0,  // 不限制总时间，由单个操作控制
    "body": [
        {
            "type": "group",
            "name": "单个阀门操作工序",
            "mode": "sequence",
            "timeout_ms": 10000,
            "body": [
                {
                    "type": "node",
                    "name": "打开阀门",
                    "result_key": "valve_${valve_id}_status",
                    "params": [],
                    "action": { "template": "L1_action/modbus/write_coil_on.r_b_coil_result" },
                    "on_success": [
                        { "type": "log", "message": "阀门 ${valve_id} 已打开" }
                    ],
                    "on_failure": [
                        { "type": "log", "message": "阀门 ${valve_id} 打开失败" }
                    ]
                }
            ],
            "on_timeout": [
                { "type": "log", "message": "操作阀门 ${valve_id} 超时" }
            ]
        }
    ]
}

4.6 约束总结

+------------------+----------------------------------------------------------+
| 约束              | 说明                                                     |
+------------------+----------------------------------------------------------+
| body 中每项       | 必须是一个完整的 L2 Node 或 L3 Group                     |
| 多个 Node 平铺    | 不允许，必须用 Group 包装                                 |
| 直接写控制流模式  | 不允许，必须封装为 L3 Group                              |
| 循环变量          | 仅在循环体内可用                                         |
| timeout_ms = 0    | 不限制循环总执行时间                                      |
+------------------+----------------------------------------------------------+


================================================================================
五、条件分支控制流（if）
================================================================================

5.1 设计原则

1. if 用来做条件分支，只做单一判断
2. then 和 else 分支内部遵循与 loop、sequence 相同的规则
3. 执行体内部只能包含 L2 Node 或 L3 Group
4. timeout_ms 控制整个 if 分支的执行时间（0=无限制）

5.2 if 结构

{
    "type": "group",
    "name": "条件分支组名称",
    "description": "根据条件选择执行哪个分支",
    "mode": "if",
    "condition": {
        "type": "compare",
        "condition": "le",
        "value": 5.0
    },
    "then": [
        // 条件成立时执行：L2 Node 或 L3 Group
    ],
    "else": [
        // 条件不成立时执行：L2 Node 或 L3 Group（可选）
    ],
    "timeout_ms": 0,  // 0=无超时限制
    "on_timeout": [
        // 超时时执行（可选）
    ]
}

5.3 condition 类型详解

【compare - 值比较】
{
    "condition": {
        "type": "compare",
        "condition": "le",
        "value": 5.0,
        "variable": "${pressure}"  // 可选，默认使用 result_key 或当前变量
    }
}

【expression - 表达式判断】
{
    "condition": {
        "type": "expression",
        "expression": "${pressure} <= 5"
    }
}

【exists - 存在性判断】
{
    "condition": {
        "type": "exists",
        "variable": "pressure"
    }
}

【choice - 用户选择判断】
{
    "condition": {
        "type": "choice",
        "value": "${user_choice}",
        "match": "继续"
    }
}

5.4 完整示例

【示例1：值比较分支（带超时限制）】
{
    "type": "group",
    "name": "判断真空度",
    "mode": "if",
    "condition": {
        "type": "compare",
        "condition": "le",
        "value": 5.0,
        "variable": "${pressure}"
    },
    "timeout_ms": 10000,  // 整个判断过程最多10秒
    "then": [
        {
            "type": "node",
            "name": "记录合格状态",
            "result_key": "vacuum_status",
            "params": ["合格"],
            "action": { "template": "L1_action/variable/set.s_s" },
            "on_success": [
                { "type": "log", "message": "真空度达标" }
            ],
            "on_failure": []
        }
    ],
    "else": [
        {
            "type": "node",
            "name": "记录不合格状态",
            "result_key": "vacuum_status",
            "params": ["不合格"],
            "action": { "template": "L1_action/variable/set.s_s" },
            "on_success": [
                { "type": "log", "message": "真空度不达标" }
            ],
            "on_failure": []
        },
        {
            "type": "node",
            "name": "提示用户",
            "result_key": "popup_result",
            "params": ["真空度未达标，当前值${pressure}Pa", "warning"],
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "判断真空度超时" },
        { "type": "popup", "message": "判断真空度超时，请检查传感器", "level": "error" }
    ]
}

【示例2：无超时限制的条件分支】
{
    "type": "group",
    "name": "判断泄漏率",
    "mode": "if",
    "condition": {
        "type": "expression",
        "expression": "(${initial_pressure} - ${final_pressure}) / ${initial_pressure} * 100 <= 0.5"
    },
    "timeout_ms": 0,  // 不限制时间
    "then": [
        { "type": "log", "message": "泄漏率合格" }
    ],
    "else": [
        { "type": "log", "message": "泄漏率超标" },
        {
            "type": "node",
            "name": "弹框警告",
            "result_key": "popup_result",
            "params": ["泄漏率超标，请检查密封", "error"],
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ]
}

5.5 约束总结

+------------------+----------------------------------------------------------+
| 约束              | 说明                                                     |
+------------------+----------------------------------------------------------+
| then 和 else      | 数组元素必须是 L2 Node 或 L3 Group                       |
| else 字段         | 可以省略，条件不成立时什么都不做                          |
| expression 类型   | 表达式必须返回布尔值                                     |
| choice 类型       | value 必须引用 user_decision 的结果变量                   |
| timeout_ms = 0    | 不限制条件分支的执行时间                                  |
+------------------+----------------------------------------------------------+


================================================================================
六、多分支控制流（switch）
================================================================================

6.1 设计原则

1. switch 是多分支结构，适合处理用户选择、状态路由等场景
2. 支持多个 case 分支，一个 default 兜底
3. 分支体内部只能包含 L2 Node 或 L3 Group
4. timeout_ms 控制整个 switch 的执行时间（0=无限制）

6.2 switch 结构

{
    "type": "group",
    "name": "多分支组名称",
    "description": "根据变量的值选择执行哪个分支",
    "mode": "switch",
    "condition": {
        "type": "value",
        "source": "${user_choice}"
    },
    "cases": [
        {
            "case": "继续",
            "body": [
                // L2 Node 或 L3 Group
            ]
        },
        {
            "case": "跳过",
            "body": [
                // L2 Node 或 L3 Group
            ]
        }
    ],
    "default": [
        // 所有 case 都不匹配时执行（可选）
    ],
    "timeout_ms": 0,  // 0=无超时限制
    "on_timeout": [
        // 超时时执行（可选）
    ]
}

6.3 字段说明

+------------------+--------+------------------+---------------------------+------------------------------------------+
| 字段              | 必填   | 类型             | 约束                      | 说明                                     |
+------------------+--------+------------------+---------------------------+------------------------------------------+
| mode              | 是     | string           | 固定 "switch"             | 表示多条件分支                           |
| condition.type    | 是     | string           | 固定 "value"              | 取变量的值做匹配                         |
| condition.source  | 是     | string           | ${变量名}                 | 要匹配的变量                             |
| cases             | 是     | array            | 至少 1 个 case            | case 分支数组                            |
| cases[].case      | 是     | any              | —                         | 匹配值（字符串/数字/布尔）               |
| cases[].body      | 是     | array            | L2 Node 或 L3 Group       | 该分支执行内容                           |
| default           | 否     | array            | L2 Node 或 L3 Group       | 所有 case 不匹配时执行                   |
| timeout_ms        | 否     | uint32           | 0                         | 整体超时时间(ms)，0=无限制               |
| on_timeout        | 否     | array            | L2 Node 或 L3 Group       | 超时时执行                               |
+------------------+--------+------------------+---------------------------+------------------------------------------+

6.4 完整示例

【示例1：用户选择分支（带超时限制）】
{
    "type": "group",
    "name": "根据用户选择执行",
    "mode": "sequence",
    "timeout_ms": 0,  // 外层不限制
    "body": [
        {
            "type": "node",
            "name": "询问用户",
            "result_key": "user_choice",
            "params": ["真空度已达标，请选择下一步：", "继续保压", "跳过保压", "终止流程"],
            "timeout_ms": 30000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/ui/confirm.s_s_s.r_s_user_choice" },
            "on_success": [],
            "on_failure": []
        },
        {
            "type": "group",
            "name": "处理用户选择",
            "mode": "switch",
            "timeout_ms": 30000,  // 分支处理最多30秒
            "condition": {
                "type": "value",
                "source": "${user_choice}"
            },
            "cases": [
                {
                    "case": "继续保压",
                    "body": [
                        { "type": "log", "message": "用户选择继续保压" },
                        {
                            "type": "group",
                            "template": "L3_group/vacuum/hold_pressure.group"
                        }
                    ]
                },
                {
                    "case": "跳过保压",
                    "body": [
                        { "type": "log", "message": "用户选择跳过保压" },
                        {
                            "type": "node",
                            "name": "提示跳过",
                            "result_key": "popup_result",
                            "params": ["已跳过保压步骤", "info"],
                            "action": { "template": "L1_action/system/popup.s_s" },
                            "on_success": [],
                            "on_failure": []
                        }
                    ]
                },
                {
                    "case": "终止流程",
                    "body": [
                        { "type": "log", "message": "用户终止流程" },
                        {
                            "type": "node",
                            "name": "提示终止",
                            "result_key": "popup_result",
                            "params": ["流程已终止", "error"],
                            "action": { "template": "L1_action/system/popup.s_s" },
                            "on_success": [],
                            "on_failure": []
                        }
                    ]
                }
            ],
            "default": [
                { "type": "log", "message": "未识别的选项：${user_choice}" },
                {
                    "type": "node",
                    "name": "提示无效选项",
                    "result_key": "popup_result",
                    "params": ["未识别的选项，请重新选择", "warning"],
                    "action": { "template": "L1_action/system/popup.s_s" },
                    "on_success": [],
                    "on_failure": []
                }
            ],
            "on_timeout": [
                { "type": "log", "message": "处理用户选择超时" },
                { "type": "popup", "message": "用户选择超时，默认终止", "level": "warning" }
            ]
        }
    ]
}

【示例2：设备状态路由（无超时）】
{
    "type": "group",
    "name": "根据设备状态执行不同操作",
    "mode": "switch",
    "timeout_ms": 0,  // 不限制时间
    "condition": {
        "type": "value",
        "source": "${device_status}"
    },
    "cases": [
        {
            "case": "idle",
            "body": [
                { "type": "log", "message": "设备空闲，开始执行任务" },
                {
                    "type": "group",
                    "template": "L3_group/assembly/pick_place.group"
                }
            ]
        },
        {
            "case": "running",
            "body": [
                { "type": "log", "message": "设备运行中，等待完成" },
                {
                    "type": "node",
                    "name": "等待设备完成",
                    "result_key": "wait_done",
                    "params": [10, "seconds"],
                    "timeout_ms": 12000,
                    "max_retries": 0,
                    "retry_interval": 0,
                    "action": { "template": "L1_action/control/wait.u16_s.r_b_wait_done" },
                    "on_success": [],
                    "on_failure": []
                }
            ]
        },
        {
            "case": "error",
            "body": [
                { "type": "log", "message": "设备故障，触发报警" },
                {
                    "type": "node",
                    "name": "报警提示",
                    "result_key": "popup_result",
                    "params": ["设备故障，请检查", "error"],
                    "action": { "template": "L1_action/system/popup.s_s" },
                    "on_success": [],
                    "on_failure": []
                }
            ]
        }
    ],
    "default": [
        { "type": "log", "message": "未知状态：${device_status}" }
    ]
}

6.5 约束总结

+------------------+----------------------------------------------------------+
| 约束              | 说明                                                     |
+------------------+----------------------------------------------------------+
| cases 至少 1 个   | 空数组无意义                                             |
| case 值唯一       | 不允许重复值                                             |
| default 可选      | 不填则所有 case 不匹配时什么都不做                        |
| condition.source  | 必须是 ${变量} 格式，不能直接写字面量                     |
| case 匹配         | 使用 === 严格相等，值和类型都必须匹配                     |
| timeout_ms = 0    | 不限制 switch 的执行时间                                  |
+------------------+----------------------------------------------------------+


================================================================================
七、超时处理（on_timeout）
================================================================================

7.1 设计原则

+---------------------------+----------------------------------------------------------+
| 原则                      | 说明                                                     |
+---------------------------+----------------------------------------------------------+
| on_timeout 是可选的       | 如果未定义，超时时将错误状态返回给上层                   |
| timeout_ms 是可选的       | 默认值为 0，表示无超时限制                               |
| Group 级别超时控制        | timeout_ms 控制整个 Group 的执行时间                      |
| 超时立即停止              | timeout_ms 到期后，立即停止所有正在执行的成员             |
| 超时分支支持复杂逻辑      | on_timeout 中可以包含 L2 Node 或 L3 Group                |
| 超时传播                  | 如果 Group 自身未处理超时，向上层传播                     |
+---------------------------+----------------------------------------------------------+

7.2 timeout_ms 设计详解

【为什么 timeout_ms 默认值为 0？】

    在实际工业控制场景中，并非所有工序都需要设置整体超时。例如：
    - 保压等待 60 秒：由内部 wait Node 控制，不需要 Group 额外超时
    - 手动调试模式：用户控制执行节奏，不应有 Group 超时
    - 数据采集循环：理论上无限执行，直到用户停止

    因此，timeout_ms = 0 表示"无超时限制"，是推荐的默认行为。
    只有在明确需要"必须在指定时间内完成"的工序（如抽真空、加热等）
    才设置具体的超时时间。

【超时设置建议】

+---------------------------+------------------+------------------------------------------+
| 场景                      | timeout_ms       | 说明                                     |
+---------------------------+------------------+------------------------------------------+
| 保压等待 60 秒            | 0                | 由内部 wait Node 控制                     |
| 手动调试流程              | 0                | 用户控制节奏                             |
| 数据采集循环              | 0                | 持续运行直到停止                         |
| 抽真空工序                | 180000           | 3 分钟必须完成                           |
| 加热升温                  | 300000           | 5 分钟必须完成                           |
| 并行任务集合              | 60000            | 所有并行任务 1 分钟内完成                |
| 条件等待循环              | 120000           | 防止死循环，2 分钟超时                   |
+---------------------------+------------------+------------------------------------------+

7.3 on_timeout 在 L3 中的位置

{
    "type": "group",
    "name": "执行组名称",
    "mode": "sequence",
    "timeout_ms": 60000,  // 设置超时，必须有 on_timeout 配套
    "body": [
        // 正常执行内容
    ],
    "on_timeout": [
        // 超时时执行的内容（可选）
        // 可以包含 L2 Node 或 L3 Group
    ]
}

{
    "type": "group",
    "name": "执行组名称",
    "mode": "sequence",
    "timeout_ms": 0,  // 无超时限制
    "body": [
        // 正常执行内容
    ]
    // on_timeout 永远不会被触发，可以不定义
}

7.4 执行流程（含 on_timeout）

+-------------------------------------------------------------+
|                    Group 执行流程（含 on_timeout）            |
+-------------------------------------------------------------+
|                                                             |
|  timeout_ms = 0?                                            |
|         |                                                    |
|     是 / 否                                                  |
|         |                                                    |
|         v                                                    |
|  +-------------------+                                       |
|  | timeout_ms > 0    |                                       |
|  | 启动计时器        |                                       |
|  +-------------------+                                       |
|         |                                                    |
|         v                                                    |
|  +-------------------+                                       |
|  | 执行 body 中的    |<----------------------------------+    |
|  | 成员（按 mode）   |                                   |    |
|  +-------------------+                                   |    |
|         |                                                |    |
|         v                                                |    |
|  +-------------------+    超时到期                        |    |
|  | 检查是否超时？   |--------> [on_timeout] 分支          |    |
|  +-------------------+         |                          |    |
|         | 未超时               v                          |    |
|         v                +-----------+                    |    |
|  +-------------------+   | 停止所有  |                    |    |
|  | 成员执行完成     |   | 正在执行  |                    |    |
|  +-------------------+   | 的成员    |                    |    |
|         |                +-----------+                    |    |
|         |                     |                           |    |
|         v                     v                           |    |
|  +-------------------+   +-----------+                    |    |
|  | 返回执行结果     |   | 执行      |                    |    |
|  | 给上层           |   | on_timeout|                    |    |
|  +-------------------+   +-----------+                    |    |
|                                                             |
+-------------------------------------------------------------+

7.5 on_timeout 示例

【示例1：抽真空工序（需要超时控制）】
{
    "type": "group",
    "name": "抽真空工序",
    "description": "完整的抽真空流程，必须在3分钟内完成",
    "mode": "sequence",
    "timeout_ms": 180000,  // 3分钟超时
    "max_nodes": 30,
    "body": [
        {
            "type": "node",
            "name": "开启阀门",
            "result_key": "valve_status",
            "params": [],
            "timeout_ms": 5000,
            "max_retries": 3,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/write_coil_on.r_b_coil_result" },
            "on_success": [ { "type": "log", "message": "阀门已开启" } ],
            "on_failure": [ { "type": "log", "message": "阀门开启失败" } ]
        },
        {
            "type": "node",
            "name": "启动真空泵",
            "result_key": "pump_started",
            "params": [0x0051, 0x0006],
            "timeout_ms": 10000,
            "max_retries": 3,
            "retry_interval": 1000,
            "action": { "template": "L1_action/modbus/write_register.u16_u16.r_b_write_result" },
            "on_success": [ { "type": "log", "message": "真空泵已启动" } ],
            "on_failure": [ { "type": "log", "message": "真空泵启动失败" } ]
        },
        {
            "type": "node",
            "name": "等待真空度达标",
            "result_key": "pressure",
            "params": [0x01, 2000],
            "timeout_ms": 120000,
            "max_retries": 10,
            "retry_interval": 2000,
            "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_pressure" },
            "judge": {
                "type": "compare",
                "condition": "le",
                "value": 5.0
            },
            "on_success": [
                { "type": "log", "message": "真空度达标：${pressure}Pa" }
            ],
            "on_failure": [
                { "type": "log", "message": "真空度未达标：${pressure}Pa" }
            ]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "抽真空工序整体超时（3分钟）" },
        {
            "type": "node",
            "name": "紧急停止真空泵",
            "result_key": "emergency_stop",
            "params": [0x0051, 0x0000],
            "timeout_ms": 3000,
            "max_retries": 3,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/write_register.u16_u16.r_b_write_result" },
            "on_success": [
                { "type": "log", "message": "紧急停止真空泵成功" }
            ],
            "on_failure": [
                { "type": "log", "message": "紧急停止真空泵失败" }
            ]
        },
        {
            "type": "node",
            "name": "关闭阀门",
            "result_key": "valve_closed",
            "params": [],
            "timeout_ms": 3000,
            "max_retries": 3,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/write_coil_off.r_b_coil_result" },
            "on_success": [
                { "type": "log", "message": "阀门已关闭" }
            ],
            "on_failure": [
                { "type": "log", "message": "阀门关闭失败" }
            ]
        },
        {
            "type": "node",
            "name": "弹出超时告警",
            "result_key": "popup_result",
            "params": ["抽真空超时，已紧急停止", "error"],
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ]
}

【示例2：保压工序（不需要超时控制）】
{
    "type": "group",
    "name": "保压工序",
    "description": "关闭阀门后保压60秒",
    "mode": "sequence",
    "timeout_ms": 0,  // 无超时限制
    "body": [
        {
            "type": "node",
            "name": "关闭所有阀门",
            "result_key": "valves_closed",
            "params": [],
            "action": { "template": "L2_node/close_all_valves.r_b_result" },
            "on_success": [ { "type": "log", "message": "阀门已关闭" } ],
            "on_failure": [ { "type": "log", "message": "阀门关闭失败" } ]
        },
        {
            "type": "node",
            "name": "等待60秒",
            "result_key": "wait_done",
            "params": [60, "seconds"],
            "timeout_ms": 65000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/control/wait.u16_s.r_b_wait_done" },
            "on_success": [ { "type": "log", "message": "保压完成" } ],
            "on_failure": []
        }
    ]
    // 没有 on_timeout，因为 timeout_ms=0 不会触发
}

【示例3：循环体 + on_timeout（防止死循环）】
{
    "type": "group",
    "name": "循环检测压力",
    "mode": "loop",
    "timeout_ms": 60000,  // 最多等待1分钟
    "loop": {
        "type": "while",
        "condition": "${pressure} > 5"
    },
    "body": [
        {
            "type": "group",
            "name": "单次检测",
            "mode": "sequence",
            "timeout_ms": 5000,
            "body": [
                {
                    "type": "node",
                    "name": "读取压力",
                    "result_key": "pressure",
                    "params": [0x01, 2000],
                    "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_pressure" },
                    "on_success": [],
                    "on_failure": []
                },
                {
                    "type": "node",
                    "name": "等待1秒",
                    "result_key": "wait_done",
                    "params": [1, "seconds"],
                    "action": { "template": "L1_action/control/wait.u16_s.r_b_wait_done" },
                    "on_success": [],
                    "on_failure": []
                }
            ],
            "on_timeout": [
                { "type": "log", "message": "单次检测超时" }
            ]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "循环检测整体超时（60秒）" },
        {
            "type": "node",
            "name": "超时告警",
            "result_key": "popup_result",
            "params": ["压力检测超时，当前压力${pressure}Pa", "error"],
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ]
}

7.6 超时传播规则

+---------------------------+----------------------------------------------------------+
| 场景                      | 行为                                                     |
+---------------------------+----------------------------------------------------------+
| timeout_ms = 0            | 不启动计时器，永远不会触发超时                           |
| timeout_ms > 0 且定义 on_timeout | 超时时执行 on_timeout 分支，不向上传播           |
| timeout_ms > 0 且未定义 on_timeout | 超时时将超时状态返回给上层                       |
| 上层 L3 定义 on_timeout   | 上层 L3 捕获超时并处理                                    |
| 上层 L3 未定义 on_timeout | 继续向上传播，直到 L4 Flow 或最顶层                       |
| L4 Flow 定义 on_timeout   | L4 Flow 捕获超时并处理                                    |
| 最顶层未定义 on_timeout   | 引擎记录错误，流程终止                                    |
+---------------------------+----------------------------------------------------------+

7.7 on_timeout 与 timeout_ms 的关系

+------------------+------------------+------------------+------------------------------------------+
| timeout_ms       | on_timeout       | 行为              | 说明                                     |
+------------------+------------------+------------------+------------------------------------------+
| 0（默认）        | 定义或不定义     | 不触发            | on_timeout 永远不会被执行                 |
| > 0              | 已定义           | 超时时执行        | 执行 on_timeout 分支                     |
| > 0              | 未定义           | 超时向上传播      | 将超时状态返回给上层                     |
+------------------+------------------+------------------+------------------------------------------+


================================================================================
八、控制流模式汇总与选择
================================================================================

8.1 模式汇总

+------------------+-------------------+------------------+------------------------------------------+
| 模式              | 适用场景          | 分支数量         | 说明                                     |
+------------------+-------------------+------------------+------------------------------------------+
| sequence          | 顺序执行          | 顺序             | 按 body 数组顺序执行                      |
| parallel          | 并行执行          | 并行             | 所有成员同时执行                          |
| loop              | 重复执行          | 1（body）        | 支持 count/while/do-while/until/foreach   |
| if                | 二选一            | 2（then/else）   | 条件分支                                  |
| switch            | 多选一            | N + default      | 多条件分支                                |
+------------------+-------------------+------------------+------------------------------------------+

8.2 选择建议

+---------------------------+----------------------------------------------------------+
| 场景                      | 推荐模式                                                 |
+---------------------------+----------------------------------------------------------+
| 按固定顺序执行多个操作    | sequence                                                 |
| 同时执行多个独立操作      | parallel                                                 |
| 重复执行固定次数          | loop (count)                                             |
| 重复执行直到条件满足      | loop (while/until)                                       |
| 遍历列表中的每个元素      | loop (foreach)                                           |
| 判断条件是否成立（是/否） | if                                                       |
| 用户选择（多个选项）      | switch                                                   |
| 状态路由（多种状态）      | switch                                                   |
| 按数字/字符串匹配         | switch                                                   |


================================================================================
九、嵌套规则
================================================================================

9.1 允许的嵌套

+------------------+---------------------------+------------------------------------------+
| 嵌套方式          | 是否允许                  | 示例                                     |
+------------------+---------------------------+------------------------------------------+
| L3 包含 L2 Node   | 是                        | body 中放 {"type":"node",...}            |
| L3 包含 L3 Group  | 是                        | body 中放 {"type":"group",...}           |
| L3 包含 L2 + L3   | 是                        | body 中可混合排列                        |
| 控制流嵌套        | 是                        | loop 内部包含 sequence/if/switch         |
+------------------+---------------------------+------------------------------------------+

9.2 禁止的嵌套

+------------------+---------------------------+------------------------------------------+
| 嵌套方式          | 是否允许                  | 原因                                     |
+------------------+---------------------------+------------------------------------------+
| L3 直接包含自身   | 否                        | 会导致无限递归                           |
| 循环嵌套          | 否                        | A包含B，B包含A → 循环递归                |
| loop body 多个 Node | 否（必须用 Group 包装）  | 保持 mode 语义一致性                     |
+------------------+---------------------------+------------------------------------------+

9.3 嵌套深度限制

+------------------+---------------------------+------------------------------------------+
| 限制项            | 值                        | 说明                                     |
+------------------+---------------------------+------------------------------------------+
| 最大嵌套深度      | 10 层                     | 防止过度嵌套导致栈溢出                   |
| 最大 body 长度    | 100 项                    | 防止单个 group 过大                      |
+------------------+---------------------------+------------------------------------------+

9.4 嵌套深度示例

L4 Flow
  +-- L3 Group (深度1)
        +-- L3 Group (深度2)
              +-- L3 Group (深度3)
                    +-- L2 Node (深度4)
                          +-- L1 Action (深度5)


================================================================================
十、L3 引用方式
================================================================================

10.1 在 L4 Flow 中引用 L3

文件：L4_flow/main_flow.json

{
    "type": "flow",
    "name": "主流程",
    "timeout_ms": 600000,
    "body": [
        {
            "type": "group",
            "template": "L3_group/vacuum/pump_down.group"
        },
        {
            "type": "group",
            "template": "L3_group/vacuum/hold_pressure.group"
        },
        {
            "type": "group",
            "template": "L3_group/vacuum/leak_test.group"
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "主流程超时" }
    ]
}

10.2 在 L3 中引用 L3

{
    "type": "group",
    "name": "完整真空工艺",
    "mode": "sequence",
    "timeout_ms": 300000,
    "max_nodes": 50,
    "body": [
        {
            "type": "group",
            "template": "L3_group/vacuum/pump_down.group"
        },
        {
            "type": "group",
            "template": "L3_group/vacuum/hold_pressure.group"
        },
        {
            "type": "group",
            "template": "L3_group/vacuum/leak_test.group"
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "完整真空工艺超时" }
    ]
}

10.3 引用带参数的 L3

{
    "type": "group",
    "template": "L3_group/vacuum/pump_down.group.u16_f",
    "params": [5, 120000]
}

10.4 引用时覆盖设置

可以在引用时覆盖被引用 group 的某些属性（如 confirm_between、delay_between）：

{
    "type": "group",
    "template": "L3_group/vacuum/pump_down.group",
    "confirm_between": true,
    "delay_between": 1000
}

注意：max_nodes 不可在引用时覆盖（应该由被引用 group 自己决定）。


================================================================================
十一、执行结果与变量
================================================================================

11.1 L3 执行结果

L3 Group 执行完成后，可以：

    1. 不返回结果：仅作为流程组织单元
    2. 返回最后一个 Node 的结果：mode: sequence 时，返回最后一个执行项的结果
    3. 返回所有并行结果：mode: parallel 时，返回所有项的 result_key 映射
    4. 超时返回：超时时，返回 on_timeout 分支的执行结果（如果定义了）

11.2 变量在 L3 中的传递

L3 内部的 L2 Node 通过 result_key 写入变量，同一 L3 中的后续 Node 可以通过
${result_key} 读取：

{
    "type": "group",
    "name": "读取并计算",
    "mode": "sequence",
    "timeout_ms": 30000,
    "body": [
        {
            "type": "node",
            "name": "读取温度",
            "result_key": "temperature",
            "params": [0x01, 2000],
            "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_temperature" },
            "on_success": [],
            "on_failure": []
        },
        {
            "type": "node",
            "name": "计算偏差",
            "result_key": "deviation",
            "params": ["(${temperature} - ${target}) / ${target} * 100"],
            "action": { "template": "L1_action/data/calculate.s.r_f_calc_result" },
            "on_success": [],
            "on_failure": []
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "读取并计算超时" }
    ]
}


================================================================================
十二、错误处理
================================================================================

12.1 错误传播规则

+------------------+----------------------------------------------------------+
| 执行模式          | 错误处理                                                 |
+------------------+----------------------------------------------------------+
| sequence          | 默认停止执行，将错误状态返回给上层                        |
| parallel          | 等待所有任务完成，汇总错误状态                            |
| loop              | 循环体中某次执行失败，根据是否定义 on_failure 决定        |
| if                | 条件判断失败或分支执行失败，返回错误状态                  |
| switch            | case 匹配失败或分支执行失败，返回错误状态                 |
+------------------+----------------------------------------------------------+

12.2 错误向上传播

L2 Node 失败
    |
    v
L3 Group 检测到失败（sequence 停止）
    |
    v
L3 Group 检查是否有 on_failure（L3 本身不定义 on_failure，只有 on_timeout）
    |
    v
错误状态返回给上层 L3 或 L4 Flow
    |
    v
L4 Flow 检测到 L3 失败
    |
    v
L4 Flow 进入自己的 on_failure 处理


================================================================================
十三、完整文件模板
================================================================================

13.1 无参数 Group 模板（无超时限制）

{
    "type": "group",
    "name": "执行组名称",
    "description": "执行组描述",
    "mode": "sequence",
    "max_nodes": 100,
    "delay_between": 0,
    "confirm_between": false,
    "timeout_ms": 0,  // 无超时限制
    "body": [
        // L2 Node 或 L3 Group
        {
            "type": "node",
            "name": "操作名称",
            "result_key": "result_var",
            "params": { "instance_id": "${instance_id}" },
            "timeout_ms": 5000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L2_node/node_name.r_b_result" },
            "on_success": [],
            "on_failure": []
        }
    ]
    // 不需要 on_timeout，因为 timeout_ms=0
}

13.2 带参数 Group 模板（有超时限制）

{
    "type": "group",
    "name": "执行组名称",
    "description": "执行组描述",
    "mode": "sequence",
    "max_nodes": 100,
    "delay_between": 0,
    "confirm_between": false,
    "timeout_ms": 120000,  // 2分钟超时
    "body": [
        // 可使用 ${参数名} 引用参数
        {
            "type": "node",
            "name": "操作名称",
            "result_key": "result_var",
            "params": ["${param1}", "${param2}"],
            "timeout_ms": 5000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L2_node/node_name.r_b_result" },
            "on_success": [],
            "on_failure": []
        }
    ],
    "args": [
        { "index": 0, "name": "param1", "type": "u16", "desc": "参数1描述" },
        { "index": 1, "name": "param2", "type": "f", "default": 0, "desc": "参数2描述" }
    ],
    "on_timeout": [
        { "type": "log", "message": "执行组超时" },
        {
            "type": "node",
            "name": "超时处理",
            "result_key": "timeout_handled",
            "params": ["执行超时，已处理", "error"],
            "timeout_ms": 3000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": []
        }
    ]
}

13.3 手动调试 Group 模板（无超时，有确认）

{
    "type": "group",
    "name": "手动调试组",
    "description": "每一步都需要用户确认",
    "mode": "sequence",
    "delay_between": 0,
    "confirm_between": true,
    "timeout_ms": 0,  // 手动调试不设超时
    "body": [
        // 每一步执行前都会弹出确认对话框
        {
            "type": "node",
            "name": "步骤1",
            ...
        },
        {
            "type": "node",
            "name": "步骤2",
            ...
        }
    ]
    // 不需要 on_timeout
}


================================================================================
十四、速查参考
================================================================================

14.1 Group 字段速查

+------------------+--------+------------------+---------------------------+------------------------------------------+
| 字段              | 必填   | 类型             | 默认值                    | 说明                                     |
+------------------+--------+------------------+---------------------------+------------------------------------------+
| type              | 是     | string           | —                         | 固定 "group"                             |
| name              | 是     | string           | —                         | 组名称                                   |
| description       | 否     | string           | —                         | 组描述                                   |
| mode              | 是     | string           | —                         | sequence/parallel/loop/if/switch          |
| max_nodes         | 否     | uint16           | 100                       | 最大 Node 数量                           |
| delay_between     | 否     | uint16           | 0                         | 成员间等待(ms)                           |
| confirm_between   | 否     | bool             | false                     | 成员间确认                               |
| timeout_ms        | 否     | uint32           | 0                         | 整体超时(ms)，0=无限制                   |
| body              | 是     | array            | —                         | 执行内容                                 |
| on_timeout        | 否     | array            | —                         | 超时时执行（仅当 timeout_ms>0 时生效）   |
| args              | 否     | array            | —                         | 参数定义                                 |
+------------------+--------+------------------+---------------------------+------------------------------------------+

14.2 控制流模式速查

+------------------+---------------------------+------------------------------------------+
| 模式              | 结构示例                 | 说明                                     |
+------------------+---------------------------+------------------------------------------+
| sequence          | {"mode":"sequence",...}  | 顺序执行                                 |
| parallel          | {"mode":"parallel",...}  | 并行执行                                 |
| loop              | {"mode":"loop","loop":{"type":"count","count":10},...} | 循环执行     |
| if                | {"mode":"if","condition":...,"then":[...],"else":[...]} | 条件分支     |
| switch            | {"mode":"switch","condition":...,"cases":[...],"default":[...]} | 多分支       |
+------------------+---------------------------+------------------------------------------+

14.3 引用方式速查

+---------------------------+----------------------------------------------------------+
| 场景                      | 写法                                                     |
+---------------------------+----------------------------------------------------------+
| 引用无参数 Group          | {"type":"group","template":"L3_group/path/name.group"}    |
| 引用带参数 Group          | {"type":"group","template":"L3_group/path/name.group.u16_f","params":[100,5.0]} |
| 引用时覆盖设置            | {"type":"group","template":"L3_group/path/name.group","confirm_between":true} |
| 内联定义 Group            | {"type":"group","name":"...","mode":"sequence","body":[...]} |

14.4 delay_between / confirm_between 生效条件

+---------------------------+------------------+------------------+
| 条件                      | delay_between    | confirm_between  |
+---------------------------+------------------+------------------+
| mode: sequence            | 生效             | 生效             |
| mode: parallel            | 无效             | 无效             |
| mode: loop                | 无效             | 无效             |
| mode: if                  | 无效             | 无效             |
| mode: switch              | 无效             | 无效             |
| body 中只有一个成员       | 不产生实际效果   | 执行前仍会确认   |
+---------------------------+------------------+------------------+

14.5 层级关系速查

+------------------+------------------+------------------+
| 层级              | 包含             | 被包含           |
+------------------+------------------+------------------+
| L1 Action         | —                | L2 Node          |
| L2 Node           | L1 Action        | L3 Group         |
| L3 Group          | L2 Node, L3 Group| L4 Flow, L3 Group|
| L4 Flow           | L3 Group         | —                |
+------------------+------------------+------------------+

14.6 on_timeout / timeout_ms 速查

+---------------------------+----------------------------------------------------------+
| 场景 / 配置               | 说明                                                     |
+---------------------------+----------------------------------------------------------+
| timeout_ms = 0（默认）    | 无超时限制，on_timeout 永远不会被触发                     |
| timeout_ms > 0            | 设置超时时间，超时时触发 on_timeout                       |
| timeout_ms > 0 + on_timeout 已定义 | 超时时执行 on_timeout 分支                      |
| timeout_ms > 0 + on_timeout 未定义 | 超时时将超时状态返回给上层                     |
| 何时设置 timeout_ms > 0   | 有明确时间要求的工序（抽真空、加热、并行任务等）          |
| 何时保持 timeout_ms = 0   | 无时间要求的工序（保压、手动调试、数据采集等）            |
+---------------------------+----------------------------------------------------------+


================================================================================
文档信息
================================================================================

文档版本：v2.1
最后更新：2026-08-06
维护者：明楚晴（东北育才学校）

主要变更（v2.1）：
- 新增 on_timeout 超时处理分支（Group 级别）
- 更新所有控制流模式（sequence/parallel/loop/if/switch）的 on_timeout 支持
- 新增第7章"超时处理（on_timeout）"完整说明
- 新增超时传播规则说明
- 所有示例增加 on_timeout 字段
- 新增带 on_timeout 的 Group 模板
- 新增 on_timeout 速查表
- 明确 timeout_ms 默认值为 0（无超时限制）
- 新增 timeout_ms 设计详解，说明何时设置超时、何时保持 0

主要变更（v2.0）：
- 新增 loop 循环控制流（count/while/do-while/until/foreach）
- 新增 if 条件分支控制流（compare/expression/exists/choice）
- 新增 switch 多分支控制流（value 匹配）
- 新增控制流模式汇总与选择建议
- 更新嵌套规则和示例


================================================================================
END OF DOCUMENT
================================================================================