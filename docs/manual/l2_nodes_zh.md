# 执行节点（Node / L2）完整定义 v2.3

================================================================================
目录
================================================================================

一、概述
   1.1 什么是执行节点（Node）？
   1.2 执行节点的核心设计：默认成功 + 可选判断
   1.3 执行流程总览

二、Node 文件命名规范
   2.1 命名格式
   2.2 格式说明
   2.3 命名示例
   2.4 类型缩写表
   2.5 文件名解析规则
   2.6 优势总结

三、执行节点（Node）完整定义
   3.1 Node 结构
   3.2 字段说明

四、默认成功行为（无 judge）
   4.1 各原子操作的默认成功判定
   4.2 无 judge 示例

五、Judge 判断依据（可选）
   5.1 值比较（compare）
   5.2 表达式判断（expression）
   5.3 存在性判断（exists）

六、重试机制详解
   6.1 执行流程
   6.2 关键规则
   6.3 重试示例

七、超时处理（on_timeout）
   7.1 设计原则
   7.2 执行流程（含 on_timeout）
   7.3 on_timeout 示例
   7.4 on_timeout vs on_failure 的区别

八、完整示例合集
   8.1 示例1：无需 judge（默认成功）
   8.2 示例2：值比较判断
   8.3 示例3：表达式判断
   8.4 示例4：用户决策三选项
   8.5 示例5：带 on_timeout 的节点

九、L2 Node 聚合方案
   9.1 聚合文件格式
   9.2 目录结构（两种方式共存）
   9.3 各类聚合文件
   9.4 文件名->result_key推导对照表
   9.5 优势总结

十、快速参考
   10.1 字段总览
   10.2 Judge 类型速查
   10.3 Node 最小模板
   10.4 文件名->result_key 推导示例
   10.5 on_timeout 速查


================================================================================
一、概述
================================================================================

1.1 什么是执行节点（Node）？

执行节点是四层架构中的第二层（L2），是一个带状态机的执行单元。它封装了：
    - 一个原子操作（L1 Action）：要执行的具体操作
    - 判断依据（Judge）：可选，决定原子操作的执行结果算"成功"还是"失败"
    - 三个结果分支：成功、失败、超时
    - 输入参数：传给原子操作的参数
    - 输出变量：原子操作的结果存入的位置
    - 重试机制：失败或超时后自动重试

1.2 执行节点的核心设计：默认成功 + 可选判断

99% 的情况下，原子操作正常返回就意味着"成功"。因此，judge 字段是可选的，
只有需要特殊业务判断时才需要显式定义。

原子操作执行完成（未超时）
    |
    v
+-------------------------------------------------------------+
|  是否有 judge 定义？                                         |
|  +-- 否 -> 原子操作正常返回 = 成功                          |
|  |        -> 走 on_success                                  |
|  +-- 是 -> 执行 judge 判断                                  |
|            +-- judge 返回 true -> 走 on_success             |
|            +-- judge 返回 false -> 走 on_failure            |
+-------------------------------------------------------------+

1.3 执行流程总览

+-------------------------------------------------------------+
|                    Node 执行流程                              |
+-------------------------------------------------------------+
|                                                             |
|  ┌─────────────────────────────────────────────────────┐     |
|  │ 1. 启动计时器（timeout_ms）                        │     |
|  │ 2. 重试计数 = 0                                    │     |
|  └─────────────────────────────────────────────────────┘     |
|                         |                                    |
|                         v                                    |
|  ┌─────────────────────────────────────────────────────┐     |
|  │ 3. 执行 L1 Action                                  │     |
|  └─────────────────────────────────────────────────────┘     |
|                         |                                    |
|         ┌───────────────┼───────────────┐                   |
|         |               |               |                   |
|         v               v               v                   |
|  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐        |
|  │ 执行成功     │ │ 执行失败     │ │ 超时         │        |
|  │ (无异常返回) │ │ (返回失败)   │ │ (超时到期)   │        |
|  └──────────────┘ └──────────────┘ └──────────────┘        |
|         |               |               |                   |
|         v               |               |                   |
|  ┌──────────────┐       |               |                   |
|  │ 执行 judge?  │       |               |                   |
|  └──────────────┘       |               |                   |
|     |           |       |               |                   |
|     否          是      |               |                   |
|     |           |       |               |                   |
|     v           v       v               v                   |
|  ┌──────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────┐ |
|  │ 走       │ │ judge=true   │ │ 走           │ │ 走     │ |
|  │ on_success│ │ -> on_success│ │ on_failure   │ │ on_    │ |
|  │          │ │ judge=false  │ │              │ │ timeout│ |
|  │          │ │ -> on_failure│ │              │ │        │ |
|  └──────────┘ └──────────────┘ └──────────────┘ └────────┘ |
|                                                             |
+-------------------------------------------------------------+


================================================================================
二、Node 文件命名规范
================================================================================

2.1 命名格式

    自定义名称.参数类型列表.r_返回值类型_返回值变量名.json

2.2 格式说明

+------------------+--------------------------------------------------+---------------------------+
| 组成部分         | 说明                                             | 示例                      |
+------------------+--------------------------------------------------+---------------------------+
| 自定义名称       | 字母、数字、下划线，不含小数点                    | motor_enable、read_status |
| 参数类型列表     | 用 _ 分隔，按参数顺序排列；无参数时省略          | u16_u16、u8_u16           |
| r_               | 固定分隔符，标识后面是返回值信息                 | r_                        |
| 返回值类型       | 类型缩写；无返回值时可省略整个r_部分             | b、u16、i32               |
| 返回值变量名     | 结果存入的变量名，用 _ 连接在返回值类型后面      | enable_result、motor_status|
+------------------+--------------------------------------------------+---------------------------+

2.3 命名示例

+--------------------------------------------------+--------------+--------------+-----------------+----------------------------------+
| 文件名                                           | 参数类型     | 返回值类型   | 返回值变量名    | 说明                             |
+--------------------------------------------------+--------------+--------------+-----------------+----------------------------------+
| motor_enable.r_b_enable_result                    | 无           | b            | enable_result   | 使能电机                         |
| motor_disable.r_b_disable_result                  | 无           | b            | disable_result  | 关闭使能                         |
| read_status.r_u16_motor_status                    | 无           | u16          | motor_status    | 读取状态                         |
| read_position.r_i32_motor_position                | 无           | i32          | motor_position  | 读取位置                         |
| set_jog_speed.u16.r_b_speed_result                | 一个 u16     | b            | speed_result    | 设置速度                         |
| set_homing_mode.u16.r_b_mode_result               | 一个 u16     | b            | mode_result     | 设置回零模式                     |
| write_register.u16_u16.r_b_write_result           | 两个 u16     | b            | write_result    | 写寄存器                         |
| calculate.f_f.r_f_calc_result                     | 两个 f       | f            | calc_result     | 计算                             |
| wait.u16.r_b_wait_done                            | 一个 u16     | b            | wait_done       | 等待                             |
| log.s                                             | 一个 s       | 无返回值     | —               | 写日志（无返回值）               |
| popup.s_s                                         | 两个 s       | 无返回值     | —               | 弹框提示（无返回值）             |
+--------------------------------------------------+--------------+--------------+-----------------+----------------------------------+

2.4 类型缩写表

+------+----------+-------+----------------------+
| 缩写 | 完整名称 | 字节数 | 取值范围              |
+------+----------+-------+----------------------+
| u8   | uint8    | 1     | 0 ~ 255               |
| i8   | int8     | 1     | -128 ~ 127            |
| u16  | uint16   | 2     | 0 ~ 65535             |
| i16  | int16    | 2     | -32768 ~ 32767        |
| u32  | uint32   | 4     | 0 ~ 4294967295        |
| i32  | int32    | 4     | -2147483648 ~ 2147483647 |
| f    | float32  | 4     | +-3.4e38              |
| b    | bool     | —     | true / false          |
| s    | string   | —     | 任意字符串            |
| hex  | hex_string| —    | 十六进制字符串        |
+------+----------+-------+----------------------+

2.5 文件名解析规则

// 解析文件名: "set_homing_mode.u16.r_b_mode_result"
//                    ------------- --- - - ------------
//                    自定义名称    参数  r_返回值类型_变量名

// 解析逻辑：
// 1. 找到 ".r_" -> 后面是 "返回值类型_变量名"
// 2. 用 "_" 分割返回值部分 -> [0]=类型, [1]=变量名
// 3. 没有 ".r_" -> 返回值类型为 "void"，无变量名

// 解析示例：
// "motor_enable.r_b_enable_result" -> return_type="b", result_key="enable_result"
// "set_homing_mode.u16.r_b_mode_result" -> params=["u16"], return_type="b", result_key="mode_result"
// "log.s" -> params=["s"], return_type="void", result_key=""

// 代码实现
struct NodeSignature {
    std::string name;
    std::vector<std::string> params;
    std::string return_type;
    std::string result_key;
};

NodeSignature parseFilename(const std::string& filename) {
    NodeSignature sig;
    
    // 查找 ".r_"
    size_t r_pos = filename.find(".r_");
    if (r_pos != std::string::npos) {
        // 提取返回值部分: "b_enable_result"
        std::string return_part = filename.substr(r_pos + 3);
        size_t underscore_pos = return_part.find('_');
        if (underscore_pos != std::string::npos) {
            sig.return_type = return_part.substr(0, underscore_pos);
            sig.result_key = return_part.substr(underscore_pos + 1);
        } else {
            sig.return_type = return_part;
            sig.result_key = "";
        }
        
        // 提取参数部分
        std::string prefix = filename.substr(0, r_pos);
        size_t dot_pos = prefix.find('.');
        if (dot_pos != std::string::npos) {
            sig.name = prefix.substr(0, dot_pos);
            std::string params_str = prefix.substr(dot_pos + 1);
            if (!params_str.empty()) {
                size_t pos = 0;
                while ((pos = params_str.find('_')) != std::string::npos) {
                    sig.params.push_back(params_str.substr(0, pos));
                    params_str.erase(0, pos + 1);
                }
                sig.params.push_back(params_str);
            }
        } else {
            sig.name = prefix;
        }
    } else {
        // 无返回值
        size_t dot_pos = filename.find('.');
        if (dot_pos != std::string::npos) {
            sig.name = filename.substr(0, dot_pos);
            std::string params_str = filename.substr(dot_pos + 1);
            size_t pos = 0;
            while ((pos = params_str.find('_')) != std::string::npos) {
                sig.params.push_back(params_str.substr(0, pos));
                params_str.erase(0, pos + 1);
            }
            sig.params.push_back(params_str);
        } else {
            sig.name = filename;
        }
        sig.return_type = "void";
        sig.result_key = "";
    }
    
    return sig;
}

2.6 优势总结

+------------------------+----------------------------------------------------------+
| 优势                   | 说明                                                     |
+------------------------+----------------------------------------------------------+
| 自描述                 | 文件名即文档，一眼可知参数、返回值、变量名               |
| 省字段                 | 无需在 JSON 中单独定义 result_key                        |
| 类型安全               | 引擎可在加载时校验参数和返回值类型                       |
| IDE 友好               | 文件列表即可查看所有 Node 的签名                         |
| 与L1统一               | 相同的命名规范，降低学习成本                             |
+------------------------+----------------------------------------------------------+


================================================================================
三、执行节点（Node）完整定义
================================================================================

3.1 Node 结构（result_key 由文件名推导，无需在 JSON 中定义）

{
    "type": "node",
    "name": "节点名称",
    "description": "节点描述（可选）",
    "params": [参数1, 参数2, ...],
    "timeout_ms": 30000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": {
        "template": "L1_action/路径/文件名"
    },
    "judge": {
        // 可选。不定义时，默认原子操作正常返回即成功
    },
    "on_success": [
        // L1 原子操作数组
    ],
    "on_failure": [
        // L1 原子操作数组
    ],
    "on_timeout": [
        // L1 原子操作数组（可选）
    ]
}

3.2 字段说明

+-----------------+--------+------------------+-----------------------------+------------------------------------------+
| 字段            | 必填   | 类型             | 约束                        | 说明                                     |
+-----------------+--------+------------------+-----------------------------+------------------------------------------+
| type            | 是     | string           | 固定 "node"                 | 节点类型标识                             |
| name            | 是     | string           | —                           | 节点名称，用于日志和调试                 |
| description     | 否     | string           | —                           | 节点描述                                 |
| params          | 否     | array/object     | —                           | 传给原子操作的参数列表                   |
| timeout_ms      | 是     | uint32           | >=1                         | 整个节点的超时时间（毫秒）               |
| max_retries     | 是     | uint8            | 0 ~ 255，默认 0             | 重试次数，0=不重试                       |
| retry_interval  | 是     | uint16           | 0 或 10~10000，默认 0       | 重试间隔（毫秒），0=不等待               |
| action          | 是     | object           | —                           | 引用的 L1 原子操作                       |
| action.template | 是     | string           | —                           | L1 原子操作的文件路径                    |
| judge           | 否     | object           | —                           | 判断依据，不定义则默认返回即成功         |
| on_success      | 是     | array            | 只能放 L1 原子操作          | 成功时执行的操作数组                     |
| on_failure      | 是     | array            | 只能放 L1 原子操作          | 失败时执行的操作数组                     |
| on_timeout      | 否     | array            | 只能放 L1 原子操作          | 超时时执行的操作数组（可选）             |
+-----------------+--------+------------------+-----------------------------+------------------------------------------+

注意：
    1. result_key 字段不再需要在 JSON 中定义，它由文件名中的 "返回值类型_变量名" 部分推导。
       如果文件名为 "read_status.r_u16_motor_status"，则 result_key = "motor_status"，result_type = "u16"。
    2. on_timeout 为可选字段。如果未定义，超时时走 on_failure。


================================================================================
四、默认成功行为（无 judge）
================================================================================

4.1 各原子操作的默认成功判定

+---------------------------+------------------+---------------------------+---------------------------+
| 原子操作类型              | 返回结果         | 默认成功条件              | 默认失败条件              |
+---------------------------+------------------+---------------------------+---------------------------+
| modbus_write_verify       | true / false     | 响应匹配 (true)           | 响应不匹配 (false)        |
| modbus_read_cache         | 读取值 / null    | 读到值 (value != null)    | 读到 null                 |
| modbus_read_check         | true/false/null  | 条件满足 (true)           | 条件不满足(false)或null   |
| wait                      | true             | 等待完成                  | 等待被中断                |
| calculate                 | 计算值 / null    | 计算成功 (value != null)  | 计算失败 (null)           |
| calculate_check           | true/false/null  | 条件满足 (true)           | 条件不满足(false)或null   |
| set                       | 设置的值 / null  | 设置成功 (value != null)  | 设置失败 (null)           |
| read_variable             | 读取的值 / null  | 变量存在 (value != null)  | 变量不存在 (null)         |
| ui_action                 | true / false     | 更新成功 (true)           | 更新失败 (false)          |
| user_decision             | 选项字符串 / null| 用户选择了选项            | 对话框被关闭              |
| log                       | true / false     | 写入成功 (true)           | 写入失败 (false)          |
| popup                     | true / false     | 显示成功 (true)           | 显示失败 (false)          |
| script_exec               | 脚本返回值 / null| 执行成功 (value != null)  | 执行失败 (null)           |
+---------------------------+------------------+---------------------------+---------------------------+

4.2 无 judge 示例

{
    "type": "node",
    "name": "启动真空泵",
    "description": "发送启动指令，正常返回即成功",
    "params": [0x0051, 0x0006],
    "timeout_ms": 3000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": {
        "template": "L1_action/modbus/write_register.u16_u16.r_b_write_result"
    },
    // 没有 judge 字段！默认 write_verify 返回 true 即成功
    // result_key 由文件名推导（write_register.u16_u16.r_b_write_result -> result_key="write_result"）
    "on_success": [
        { "type": "log", "message": "真空泵启动成功" }
    ],
    "on_failure": [
        { "type": "log", "message": "真空泵启动失败" }
    ],
    "on_timeout": [
        { "type": "log", "message": "真空泵启动超时" },
        { "type": "popup", "message": "启动超时，请检查设备", "level": "error" }
    ]
}


================================================================================
五、Judge 判断依据（可选）
================================================================================

当业务需要特殊判断时，显式定义 judge 覆盖默认行为。

5.1 值比较（compare）

"judge": {
    "type": "compare",
    "condition": "le",
    "value": 5.0
}

判断逻辑：${result_key} <= 5.0 -> 成功，否则失败

+-------------+--------+-----------------------------------------------------------+
| 字段        | 必填   | 说明                                                      |
+-------------+--------+-----------------------------------------------------------+
| type        | 是     | 固定 "compare"                                            |
| condition   | 是     | eq / ne / lt / le / gt / ge / abs_ge / abs_gt / abs_le / abs_lt |
| value       | 是     | 比较值（数字）                                            |
+-------------+--------+-----------------------------------------------------------+

5.2 表达式判断（expression）

"judge": {
    "type": "expression",
    "expression": "${leak_rate} <= 0.5"
}

判断逻辑：表达式结果为 true -> 成功，false -> 失败

+-------------+--------+----------------------------------------------+
| 字段        | 必填   | 说明                                         |
+-------------+--------+----------------------------------------------+
| type        | 是     | 固定 "expression"                            |
| expression  | 是     | 返回 true 或 false 的表达式                  |
+-------------+--------+----------------------------------------------+

5.3 存在性判断（exists）

"judge": {
    "type": "exists"
}

判断逻辑：${result_key} != null -> 成功，否则失败

+-------------+--------+----------------------------------------------+
| 字段        | 必填   | 说明                                         |
+-------------+--------+----------------------------------------------+
| type        | 是     | 固定 "exists"                                |
+-------------+--------+----------------------------------------------+


================================================================================
六、重试机制详解
================================================================================

6.1 执行流程

Node 开始执行
    |
    v
+-------------------------------------------------------------+
|  计时开始（timeout_ms）                                      |
|  重试计数 = 0                                                |
+-------------------------------------------------------------+
    |
    v
+-------------------------------------------------------------+
|  执行 action（L1 原子操作）                                  |
+-------------------------------------------------------------+
    |
    v
+-------------------------------------------------------------+
|  判断结果                                                     |
|  +-- 成功（原子操作返回成功，且 judge 通过）                 |
|  |   -> 停止计时                                             |
|  |   -> 走 on_success                                        |
|  +-- 失败（原子操作返回失败，或 judge 不通过）               |
|  |   -> 重试计数 +1                                          |
|  |   -> 判断：重试计数 <= max_retries?                       |
|  |       +-- 是 -> 等待 retry_interval                       |
|  |       |      -> 检查是否超时                              |
|  |       |      +-- 超时 -> 走 on_timeout（若定义）/ on_failure |
|  |       |      +-- 未超时 -> 回到"执行 action"              |
|  |       +-- 否 -> 走 on_failure                             |
|  +-- 超时（timeout_ms 到期）                                 |
|      -> 走 on_timeout（若定义）/ on_failure                  |
+-------------------------------------------------------------+

6.2 关键规则

+---------------------------+----------------------------------------------------------+
| 规则                      | 说明                                                     |
+---------------------------+----------------------------------------------------------+
| 任意一次成功即停止        | 无论之前失败多少次，只要某次执行成功，立即走 on_success  |
| 全部重试用完仍失败        | 走 on_failure                                            |
| 超时立即停止              | timeout_ms 到期后，即使还有重试次数，也走 on_timeout/on_failure |
| 超时优先走 on_timeout     | 如果定义了 on_timeout，超时时走 on_timeout；否则走 on_failure |
| 重试间隔约束              | retry_interval 为 0 时不等待；>0 时必须 >=10 且 <=10000 ms |
+---------------------------+----------------------------------------------------------+

6.3 重试示例

【示例1：不重试（默认）】

{
    "type": "node",
    "name": "写入配置",
    "params": [0x0051, 300],
    "timeout_ms": 3000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": { "template": "L1_action/modbus/write_register.u16_u16.r_b_write_result" },
    "on_success": [...],
    "on_failure": [...],
    "on_timeout": [...]
}

【示例2：重试3次，间隔500ms】

{
    "type": "node",
    "name": "读取温度",
    "params": [0x01, 2000],
    "timeout_ms": 10000,
    "max_retries": 3,
    "retry_interval": 500,
    "action": { "template": "L1_action/modbus/read_register.u8_u16.r_u16_temperature" },
    // result_key = "temperature"（由文件名推导）
    "on_success": [
        { "type": "log", "message": "温度读取成功：${temperature}℃" }
    ],
    "on_failure": [
        { "type": "log", "message": "温度读取失败（已重试3次）" },
        { "type": "popup", "message": "设备无响应", "level": "error" }
    ],
    "on_timeout": [
        { "type": "log", "message": "温度读取超时" },
        { "type": "popup", "message": "读取超时，请检查通讯", "level": "error" }
    ]
}


================================================================================
七、超时处理（on_timeout）
================================================================================

7.1 设计原则

+---------------------------+----------------------------------------------------------+
| 原则                      | 说明                                                     |
+---------------------------+----------------------------------------------------------+
| on_timeout 是可选的       | 如果未定义，超时时统一走 on_failure                       |
| 超时和失败分开处理        | 超时通常意味着通讯问题或设备无响应，需要不同的处理逻辑    |
| on_timeout 支持重试       | 超时本身已经过重试，on_timeout 中不应再包含复杂重试逻辑   |
| on_timeout 应轻量化       | 建议只做日志记录、告警提示、状态恢复等轻量操作            |
+---------------------------+----------------------------------------------------------+

7.2 执行流程（含 on_timeout）

+-------------------------------------------------------------+
|                    Node 执行流程（含 on_timeout）             |
+-------------------------------------------------------------+
|                                                             |
|  启动计时器 (timeout_ms)                                    |
|         |                                                    |
|         v                                                    |
|  +-------------------+                                       |
|  | 执行 L1 Action    |<----------------------------------+    |
|  +-------------------+                                   |    |
|         |                                                |    |
|         v                                                |    |
|  +-------------------+    超时到期                        |    |
|  | 检查是否超时？   |--------> [on_timeout] 分支          |    |
|  +-------------------+         |                          |    |
|         | 未超时               v                          |    |
|         v                +-----------+                    |    |
|  +-------------------+   | 执行      |                    |    |
|  | 检查执行结果     |   | on_timeout|                    |    |
|  +-------------------+   +-----------+                    |    |
|         |                                                |    |
|    成功 / 失败                                           |    |
|         |                                                |    |
|         v                                                |    |
|  +-------------------+   +-----------+                    |    |
|  | 执行 judge?      |   | 重试逻辑  |                    |    |
|  +-------------------+   +-----------+                    |    |
|         |                    |                            |    |
|    有 / 无                失败 + 未超时                   |    |
|         |                    |                            |    |
|         v                    +----------------------------+    |
|  +-------------------+                                       |
|  | 判断成功/失败    |                                        |
|  +-------------------+                                       |
|         |                                                    |
|    成功 / 失败                                               |
|         |                                                    |
|         v                                                    |
|  +-------------------+                                       |
|  | on_success /      |                                       |
|  | on_failure        |                                       |
|  +-------------------+                                       |
|                                                             |
+-------------------------------------------------------------+

7.3 on_timeout 示例

{
    "type": "node",
    "name": "读取设备状态",
    "description": "从设备读取状态寄存器，超时则告警",
    "params": [0x01, 2000],
    "timeout_ms": 3000,
    "max_retries": 2,
    "retry_interval": 500,
    "action": {
        "template": "L1_action/modbus/read_register.u8_u16.r_u16_device_status"
    },
    // result_key = "device_status"（由文件名推导）
    "on_success": [
        { "type": "log", "message": "设备状态读取成功：${device_status}" },
        { "type": "set", "key": "last_status", "value": "${device_status}" }
    ],
    "on_failure": [
        { "type": "log", "message": "设备状态读取失败" },
        { "type": "popup", "message": "读取设备状态失败", "level": "warning" }
    ],
    "on_timeout": [
        { "type": "log", "message": "设备状态读取超时" },
        { "type": "popup", "message": "设备通讯超时，请检查连接", "level": "error" },
        { "type": "set", "key": "last_status", "value": "0" },
        { "type": "set", "key": "comm_error", "value": "true" }
    ]
}

7.4 on_timeout vs on_failure 的区别

+------------------+---------------------------+---------------------------+
| 对比项           | on_timeout                | on_failure                |
+------------------+---------------------------+---------------------------+
| 触发条件         | timeout_ms 到期           | 执行失败 / judge 不通过   |
| 代表含义         | 设备无响应 / 通讯中断     | 业务逻辑失败 / 数据异常   |
| 建议操作         | 告警、记录、复位通讯      | 重试、降级、补偿          |
| 是否可重试       | 超时前已执行重试           | 可配置重试次数             |
| 是否必需         | 可选                       | 必需                       |
+------------------+---------------------------+---------------------------+


================================================================================
八、完整示例合集
================================================================================

8.1 示例1：无需 judge（默认成功）

{
    "type": "node",
    "name": "等待60秒",
    "description": "保压等待60秒",
    "params": [60, "seconds"],
    "timeout_ms": 60000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": {
        "template": "L1_action/control/wait.u16_s.r_b_wait_done"
    },
    // 没有 judge，默认 wait 完成即成功
    // result_key 由文件名推导（wait.u16_s.r_b_wait_done -> result_key="wait_done"）
    "on_success": [
        { "type": "log", "message": "等待完成" }
    ],
    "on_failure": [],
    "on_timeout": []
}

8.2 示例2：值比较判断

{
    "type": "node",
    "name": "检查真空度",
    "description": "读取真空度，判断是否<=5Pa",
    "params": [0x01, 2000],
    "timeout_ms": 15000,
    "max_retries": 3,
    "retry_interval": 1000,
    "action": {
        "template": "L1_action/modbus/read_register.u8_u16.r_f_vacuum_pressure"
    },
    // result_key 由文件名推导（read_register.u8_u16.r_f_vacuum_pressure -> result_key="vacuum_pressure"）
    "judge": {
        "type": "compare",
        "condition": "le",
        "value": 5.0
    },
    "on_success": [
        { "type": "log", "message": "真空度达标：${vacuum_pressure}Pa" }
    ],
    "on_failure": [
        { "type": "log", "message": "真空度未达标：${vacuum_pressure}Pa" },
        { "type": "popup", "message": "真空度${vacuum_pressure}Pa，要求<=5Pa", "level": "warning" }
    ],
    "on_timeout": [
        { "type": "log", "message": "读取真空度超时" },
        { "type": "popup", "message": "真空计通讯超时", "level": "error" }
    ]
}

8.3 示例3：表达式判断

{
    "type": "node",
    "name": "计算泄漏率",
    "description": "计算泄漏率，判断是否<=0.5%",
    "params": ["(${initial_pressure} - ${final_pressure}) / ${initial_pressure} * 100"],
    "timeout_ms": 5000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": {
        "template": "L1_action/data/calculate.s.r_f_leak_rate"
    },
    // result_key 由文件名推导（calculate.s.r_f_leak_rate -> result_key="leak_rate"）
    "judge": {
        "type": "expression",
        "expression": "${leak_rate} <= 0.5"
    },
    "on_success": [
        { "type": "log", "message": "泄漏率合格：${leak_rate}%" }
    ],
    "on_failure": [
        { "type": "log", "message": "泄漏率超标：${leak_rate}%" },
        { "type": "popup", "message": "泄漏率${leak_rate}%，要求<=0.5%", "level": "error" }
    ],
    "on_timeout": [
        { "type": "log", "message": "计算泄漏率超时" }
    ]
}

8.4 示例4：用户决策三选项

{
    "type": "node",
    "name": "用户选择操作",
    "description": "让用户选择下一步操作",
    "params": ["检测到异常，请选择操作：", "重试", "跳过", "取消"],
    "timeout_ms": 30000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": {
        "template": "L1_action/ui/confirm.s_s_s.r_s_user_choice"
    },
    // result_key 由文件名推导（confirm.s_s_s.r_s_user_choice -> result_key="user_choice"）
    "judge": {
        "type": "expression",
        "expression": "${user_choice} == '重试'"
    },
    "on_success": [
        { "type": "log", "message": "用户选择重试" },
        { "type": "popup", "message": "即将重试当前操作", "level": "info" }
    ],
    "on_failure": [
        { "type": "log", "message": "用户选择：${user_choice}" },
        {
            "type": "node",
            "name": "处理跳过/取消",
            "params": ["${user_choice}"],
            "timeout_ms": 5000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/data/calculate.s.r_s_skip_result" },
            "judge": {
                "type": "expression",
                "expression": "${skip_result} == '跳过'"
            },
            "on_success": [
                { "type": "log", "message": "用户选择跳过" },
                { "type": "popup", "message": "已跳过当前操作", "level": "warning" }
            ],
            "on_failure": [
                { "type": "log", "message": "用户取消操作" },
                { "type": "popup", "message": "操作已取消", "level": "error" }
            ],
            "on_timeout": [
                { "type": "log", "message": "处理用户选择超时" }
            ]
        }
    ],
    "on_timeout": [
        { "type": "log", "message": "用户选择超时，默认取消" },
        { "type": "popup", "message": "操作超时已取消", "level": "error" }
    ]
}

8.5 示例5：带 on_timeout 的节点

{
    "type": "node",
    "name": "关键数据读取",
    "description": "读取关键工艺参数，超时则触发紧急处理",
    "params": [0x10, 5000],
    "timeout_ms": 3000,
    "max_retries": 2,
    "retry_interval": 200,
    "action": {
        "template": "L1_action/modbus/read_register.u8_u16.r_f_critical_value"
    },
    // result_key = "critical_value"
    "judge": {
        "type": "compare",
        "condition": "ge",
        "value": 0.5
    },
    "on_success": [
        { "type": "log", "message": "关键值读取成功：${critical_value}" },
        { "type": "set", "key": "last_critical_value", "value": "${critical_value}" }
    ],
    "on_failure": [
        { "type": "log", "message": "关键值异常：${critical_value}" },
        { "type": "popup", "message": "关键值${critical_value}，要求>=0.5", "level": "error" },
        { "type": "set", "key": "critical_error", "value": "true" }
    ],
    "on_timeout": [
        { "type": "log", "message": "关键数据读取超时" },
        { "type": "popup", "message": "关键数据读取超时，触发紧急停止", "level": "critical" },
        { "type": "set", "key": "critical_error", "value": "true" },
        { "type": "set", "key": "critical_value", "value": "0.0" }
    ]
}


================================================================================
九、L2 Node 聚合方案
================================================================================

设计思路：
    - 保持单个文件独立：原有的单个 Node 文件依然有效
    - 新增聚合文件：一个 JSON 文件可以包含多个 Node
    - 两种方式共存：加载器同时支持单文件和多文件两种格式

9.1 聚合文件格式

【设计思路】
聚合文件将多个相关的 L2 Node 打包到一个 JSON 文件中，便于管理和部署。
每个 Node 在聚合文件中通过 `filename` 字段获得一个虚拟路径标识符，
外部引用时使用 "聚合文件路径 + # + filename" 的格式。

【引用格式】
引用聚合文件中的 Node 时，使用以下格式：
    L2_node/目录名/聚合文件名#filename

示例：
    L2_node/motion/all_nodes.json#motor_enable.r_b_enable_result

【加载规则】
1. 加载器读取聚合文件时，以每个 Node 的 `filename` 字段为 Key 注册该 Node
2. `filename` 用于推导 `result_key`（规则与单文件版本完全相同）
3. `filename` 仅在聚合文件内部唯一，不同聚合文件之间可以重名
4. 加载器应支持单文件和聚合文件两种引用方式并存

【文件：L2_node/motion/all_nodes.json】

{
    "type": "node_bundle",
    "description": "运动控制类执行节点集合",
    "version": "1.0.0",
    "nodes": [
        {
            // filename: 虚拟路径标识符，用于外部引用和 result_key 推导
            // 引用方式：L2_node/motion/all_nodes.json#motor_enable.r_b_enable_result
            "filename": "motor_enable.r_b_enable_result",
            "type": "node",
            "name": "电机使能",
            "description": "使能电机驱动器",
            "params": [],
            "timeout_ms": 5000,
            "max_retries": 2,
            "retry_interval": 500,
            "action": {
                "template": "L1_action/modbus/write_coil_on.r_b_coil_on_result"
            },
            "on_success": [
                { "type": "log", "message": "电机使能成功" }
            ],
            "on_failure": [
                { "type": "log", "message": "电机使能失败" },
                { "type": "popup", "message": "电机使能失败，请检查", "level": "error" }
            ],
            "on_timeout": [
                { "type": "log", "message": "电机使能超时" },
                { "type": "popup", "message": "电机使能超时，请检查通讯", "level": "error" }
            ]
        },
        {
            "filename": "motor_disable.r_b_disable_result",
            "type": "node",
            "name": "电机去使能",
            "description": "关闭电机驱动器使能",
            "params": [],
            "timeout_ms": 5000,
            "max_retries": 2,
            "retry_interval": 500,
            "action": {
                "template": "L1_action/modbus/write_coil_off.r_b_coil_off_result"
            },
            "on_success": [
                { "type": "log", "message": "电机去使能成功" }
            ],
            "on_failure": [
                { "type": "log", "message": "电机去使能失败" }
            ],
            "on_timeout": [
                { "type": "log", "message": "电机去使能超时" }
            ]
        },
        {
            "filename": "read_status.r_u16_motor_status",
            "type": "node",
            "name": "读取电机状态",
            "description": "读取电机当前状态寄存器",
            "params": [0x01, 2000],
            "timeout_ms": 3000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": {
                "template": "L1_action/modbus/read_register.u8_u16.r_u16_register_value"
            },
            // result_key = "motor_status"（由 filename 推导）
            "on_success": [
                { "type": "log", "message": "电机状态：${motor_status}" }
            ],
            "on_failure": [
                { "type": "log", "message": "读取电机状态失败" }
            ],
            "on_timeout": [
                { "type": "log", "message": "读取电机状态超时" },
                { "type": "set", "key": "motor_status", "value": "0" }
            ]
        },
        {
            "filename": "set_jog_speed.u16.r_b_speed_result",
            "type": "node",
            "name": "设置点动速度",
            "description": "设置电机点动速度",
            "params": [500],
            "timeout_ms": 3000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": {
                "template": "L1_action/modbus/write_register.u16_u16.r_b_write_result"
            },
            "judge": {
                "type": "compare",
                "condition": "eq",
                "value": true
            },
            "on_success": [
                { "type": "log", "message": "点动速度设置成功" }
            ],
            "on_failure": [
                { "type": "log", "message": "点动速度设置失败" }
            ],
            "on_timeout": [
                { "type": "log", "message": "点动速度设置超时" }
            ]
        }
    ]
}

【filename 字段详解】

+------------------+----------------------------------------------------------+
| 特性              | 说明                                                     |
+------------------+----------------------------------------------------------+
| 作用              | 作为 Node 在聚合包内的虚拟路径标识符                      |
| 来源              | 与单文件版本的完整文件名（不含 .json）相同                |
| 唯一性            | 在同一聚合文件内必须唯一                                  |
| result_key 推导   | 基于 filename 的后半部分（.r_类型_变量名）推导            |
| 引用格式          | 聚合文件路径#filename                                     |
| 是否必须          | 是                                                       |
+------------------+----------------------------------------------------------+

【引用示例】

在 L3 Group 或 L4 Flow 中引用聚合文件中的 Node：

{
    "type": "node",
    "template": "L2_node/motion/all_nodes.json#motor_enable.r_b_enable_result",
    "params": [],
    "on_success": [...],
    "on_failure": [...]
}

{
    "type": "node",
    "template": "L2_node/motion/all_nodes.json#read_status.r_u16_motor_status",
    "params": [0x01, 2000],
    "on_success": [...],
    "on_failure": [...]
}

9.2 目录结构（两种方式共存）

L2_node/
+-- motion/
|   +-- all_nodes.json                        # 聚合文件（推荐）
|   +-- motor_enable.r_b_enable_result.json   # 单个文件（也支持）
|   +-- motor_disable.r_b_disable_result.json
|   +-- read_status.r_u16_motor_status.json
|   +-- set_jog_speed.u16.r_b_speed_result.json
+-- vacuum/
|   +-- all_vacuum_nodes.json                 # 真空控制类聚合
|   +-- vacuum_on.r_b_vacuum_on_result.json
|   +-- vacuum_off.r_b_vacuum_off_result.json
|   +-- check_vacuum.r_b_vacuum_check_result.json
+-- measurement/
|   +-- all_measurement_nodes.json            # 测量类聚合
|   +-- read_temperature.r_f_temp_value.json
|   +-- read_pressure.r_f_pressure_value.json
|   +-- read_position.r_i32_position_value.json
+-- system/
    +-- all_system_nodes.json                 # 系统类聚合
    +-- wait_5s.r_b_wait_done.json
    +-- log_info.s
    +-- popup_warning.s_s

9.3 各类聚合文件

【运动控制类聚合】L2_node/motion/all_nodes.json（见 9.1）

【真空控制类聚合】L2_node/vacuum/all_vacuum_nodes.json

{
    "type": "node_bundle",
    "description": "真空控制类执行节点集合",
    "version": "1.0.0",
    "nodes": [
        {
            "filename": "vacuum_on.r_b_vacuum_on_result",
            "type": "node",
            "name": "开启真空",
            "params": [],
            "timeout_ms": 5000,
            "max_retries": 2,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/write_coil_on.r_b_coil_on_result" },
            "on_success": [ { "type": "log", "message": "真空开启成功" } ],
            "on_failure": [ { "type": "log", "message": "真空开启失败" } ],
            "on_timeout": [ { "type": "log", "message": "真空开启超时" } ]
        },
        {
            "filename": "vacuum_off.r_b_vacuum_off_result",
            "type": "node",
            "name": "关闭真空",
            "params": [],
            "timeout_ms": 5000,
            "max_retries": 2,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/write_coil_off.r_b_coil_off_result" },
            "on_success": [ { "type": "log", "message": "真空关闭成功" } ],
            "on_failure": [ { "type": "log", "message": "真空关闭失败" } ],
            "on_timeout": [ { "type": "log", "message": "真空关闭超时" } ]
        },
        {
            "filename": "check_vacuum.r_b_vacuum_check_result",
            "type": "node",
            "name": "检查真空度",
            "params": [],
            "timeout_ms": 10000,
            "max_retries": 3,
            "retry_interval": 1000,
            "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_vacuum_pressure" },
            "judge": { "type": "compare", "condition": "le", "value": 5.0 },
            "on_success": [ { "type": "log", "message": "真空度达标：${vacuum_pressure}Pa" } ],
            "on_failure": [ { "type": "log", "message": "真空度未达标：${vacuum_pressure}Pa" } ],
            "on_timeout": [ { "type": "log", "message": "检查真空度超时" } ]
        }
    ]
}

【测量类聚合】L2_node/measurement/all_measurement_nodes.json

{
    "type": "node_bundle",
    "description": "测量类执行节点集合",
    "version": "1.0.0",
    "nodes": [
        {
            "filename": "read_temperature.r_f_temp_value",
            "type": "node",
            "name": "读取温度",
            "params": [0x01, 2000],
            "timeout_ms": 3000,
            "max_retries": 2,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_temp_register" },
            // result_key = "temp_register"
            "on_success": [ { "type": "log", "message": "温度：${temp_register}℃" } ],
            "on_failure": [ { "type": "log", "message": "读取温度失败" } ],
            "on_timeout": [ { "type": "log", "message": "读取温度超时" } ]
        },
        {
            "filename": "read_pressure.r_f_pressure_value",
            "type": "node",
            "name": "读取压力",
            "params": [0x02, 2000],
            "timeout_ms": 3000,
            "max_retries": 2,
            "retry_interval": 500,
            "action": { "template": "L1_action/modbus/read_register.u8_u16.r_f_pressure_register" },
            // result_key = "pressure_register"
            "on_success": [ { "type": "log", "message": "压力：${pressure_register}MPa" } ],
            "on_failure": [ { "type": "log", "message": "读取压力失败" } ],
            "on_timeout": [ { "type": "log", "message": "读取压力超时" } ]
        },
        {
            "filename": "read_position.r_i32_position_value",
            "type": "node",
            "name": "读取位置",
            "params": [0x03, 2000],
            "timeout_ms": 3000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/modbus/read_register_32.u8_u16.r_i32_position_register" },
            // result_key = "position_register"
            "on_success": [ { "type": "log", "message": "位置：${position_register}" } ],
            "on_failure": [ { "type": "log", "message": "读取位置失败" } ],
            "on_timeout": [ { "type": "log", "message": "读取位置超时" } ]
        }
    ]
}

【系统类聚合】L2_node/system/all_system_nodes.json

{
    "type": "node_bundle",
    "description": "系统类执行节点集合",
    "version": "1.0.0",
    "nodes": [
        {
            "filename": "wait_5s.r_b_wait_done",
            "type": "node",
            "name": "等待5秒",
            "params": [],
            "timeout_ms": 6000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/control/wait.u16_s.r_b_wait_done" },
            "on_success": [ { "type": "log", "message": "等待完成" } ],
            "on_failure": [],
            "on_timeout": []
        },
        {
            "filename": "log_info.s",
            "type": "node",
            "name": "记录信息日志",
            "params": ["系统启动完成"],
            "timeout_ms": 1000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/system/log.s" },
            "on_success": [],
            "on_failure": [],
            "on_timeout": []
        },
        {
            "filename": "popup_warning.s_s",
            "type": "node",
            "name": "显示警告弹窗",
            "params": ["系统异常", "warning"],
            "timeout_ms": 3000,
            "max_retries": 0,
            "retry_interval": 0,
            "action": { "template": "L1_action/system/popup.s_s" },
            "on_success": [],
            "on_failure": [],
            "on_timeout": []
        }
    ]
}

9.4 文件名->result_key推导对照表

+--------------------------------------------------+--------------+-------------------+----------------------------------+
| 文件名                                           | 返回值类型   | result_key        | 说明                             |
+--------------------------------------------------+--------------+-------------------+----------------------------------+
| motor_enable.r_b_enable_result                    | b            | enable_result     | 电机使能结果                     |
| motor_disable.r_b_disable_result                  | b            | disable_result    | 电机去使能结果                   |
| read_status.r_u16_motor_status                    | u16          | motor_status      | 电机状态值                       |
| read_position.r_i32_motor_position                | i32          | motor_position    | 电机位置值                       |
| set_jog_speed.u16.r_b_speed_result                | b            | speed_result      | 速度设置结果                     |
| set_homing_mode.u16.r_b_mode_result               | b            | mode_result       | 模式设置结果                     |
| write_register.u16_u16.r_b_write_result           | b            | write_result      | 写入结果                         |
| calculate.f_f.r_f_calc_result                     | f            | calc_result       | 计算结果                         |
| wait.u16.r_b_wait_done                            | b            | wait_done         | 等待完成标志                     |
| vacuum_on.r_b_vacuum_on_result                    | b            | vacuum_on_result  | 真空开启结果                     |
| vacuum_off.r_b_vacuum_off_result                  | b            | vacuum_off_result | 真空关闭结果                     |
| check_vacuum.r_b_vacuum_check_result              | b            | vacuum_check_result| 真空检查结果                     |
| read_temperature.r_f_temp_value                   | f            | temp_value        | 温度值                           |
| read_pressure.r_f_pressure_value                  | f            | pressure_value    | 压力值                           |
| read_position.r_i32_position_value                | i32          | position_value    | 位置值                           |
| log.s                                             | void         | —                 | 无返回值                         |
| popup.s_s                                         | void         | —                 | 无返回值                         |
+--------------------------------------------------+--------------+-------------------+----------------------------------+

9.5 优势总结

+------------------------+----------------------------------------------------------+
| 优势                   | 说明                                                     |
+------------------------+----------------------------------------------------------+
| 统一命名规范           | L1和L2遵循相同的文件名规则，易于理解和使用                |
| 完全自描述             | 文件名包含参数类型、返回值类型和变量名，无需额外文档      |
| 灵活部署               | 支持单文件和聚合文件两种方式，适应不同场景                |
| 性能优化               | 聚合文件减少文件IO，提升加载速度                          |
| 易于维护               | 同类型Node集中管理，修改更方便                            |
| 类型安全               | 统一的类型系统，加载时可校验                              |
| 清晰的分支处理         | on_success/on_failure/on_timeout 明确区分三种结果        |
+------------------------+----------------------------------------------------------+


================================================================================
十、快速参考
================================================================================

10.1 字段总览

+-----------------+--------+------------------+-----------------------------+------------------------------------------+
| 字段            | 必填   | 类型             | 约束                        | 说明                                     |
+-----------------+--------+------------------+-----------------------------+------------------------------------------+
| type            | 是     | string           | 固定 "node"                 | 节点类型标识                             |
| name            | 是     | string           | —                           | 节点名称，用于日志和调试                 |
| description     | 否     | string           | —                           | 节点描述                                 |
| params          | 否     | array/object     | —                           | 传给原子操作的参数列表                   |
| timeout_ms      | 是     | uint32           | >=1                         | 整个节点的超时时间（毫秒）               |
| max_retries     | 是     | uint8            | 0 ~ 255，默认 0             | 重试次数，0=不重试                       |
| retry_interval  | 是     | uint16           | 0 或 10~10000，默认 0       | 重试间隔（毫秒），0=不等待               |
| action          | 是     | object           | —                           | 引用的 L1 原子操作                       |
| action.template | 是     | string           | —                           | L1 原子操作的文件路径                    |
| judge           | 否     | object           | —                           | 判断依据，不定义则默认返回即成功         |
| on_success      | 是     | array            | 只能放 L1 原子操作          | 成功时执行的操作数组                     |
| on_failure      | 是     | array            | 只能放 L1 原子操作          | 失败时执行的操作数组                     |
| on_timeout      | 否     | array            | 只能放 L1 原子操作          | 超时时执行的操作数组（可选）             |
+-----------------+--------+------------------+-----------------------------+------------------------------------------+

10.2 Judge 类型速查

+------------------+---------------------------+------------------------------------------+
| Judge 类型       | 用途                      | 示例                                     |
+------------------+---------------------------+------------------------------------------+
| （无 judge）     | 默认成功                  | —                                        |
| compare          | 数值比较                  | {"type":"compare","condition":"le","value":5.0} |
| expression       | 表达式判断                | {"type":"expression","expression":"${rate}<=0.5"} |
| exists           | 存在性判断                | {"type":"exists"}                        |
+------------------+---------------------------+------------------------------------------+

10.3 Node 最小模板

{
    "type": "node",
    "name": "节点名称",
    "params": [],
    "timeout_ms": 3000,
    "max_retries": 0,
    "retry_interval": 0,
    "action": {
        "template": "L1_action/路径/文件名"
    },
    // 无 judge，默认返回即成功
    // result_key 由文件名推导
    "on_success": [],
    "on_failure": [],
    "on_timeout": []  // 可选
}

10.4 文件名->result_key 推导示例

+--------------------------------------------------+------------------+----------------------------------+
| 文件名                                           | result_key       | 说明                             |
+--------------------------------------------------+------------------+----------------------------------+
| motor_enable.r_b_enable_result                    | enable_result    | 使能结果                         |
| read_status.r_u16_motor_status                    | motor_status     | 状态值                           |
| read_position.r_i32_motor_position                | motor_position   | 位置值                           |
| set_jog_speed.u16.r_b_speed_result                | speed_result     | 设置结果                         |
| set_homing_mode.u16.r_b_mode_result               | mode_result      | 模式设置结果                     |
| write_register.u16_u16.r_b_write_result           | write_result     | 写入结果                         |
| wait.u16.r_b_wait_done                            | wait_done        | 等待完成标志                     |
| log.s                                             | （无）           | 无返回值                         |
+--------------------------------------------------+------------------+----------------------------------+

10.5 on_timeout 速查

+---------------------------+----------------------------------------------------------+
| 场景                      | 建议 on_timeout 操作                                      |
+---------------------------+----------------------------------------------------------+
| 通讯超时                  | 记录日志、告警、标记通讯错误状态                          |
| 设备无响应                | 记录日志、尝试复位通讯、通知操作员                        |
| 数据读取超时              | 记录日志、使用默认值或上次有效值继续                      |
| 关键操作超时              | 记录日志、触发紧急停止、告警                              |
| 等待超时                  | 记录日志、跳过等待、继续执行                              |
+---------------------------+----------------------------------------------------------+


================================================================================
文档信息
================================================================================

文档版本：v2.3
最后更新：2026-08-06
维护者：明楚晴（东北育才学校）

主要变更（v2.3）：
- 新增 on_timeout 超时处理分支
- 更新执行流程（含 on_timeout）
- 新增 on_timeout vs on_failure 区别说明
- 所有示例增加 on_timeout 字段
- 聚合文件增加 on_timeout 支持
- 新增 on_timeout 速查表

主要变更（v2.2）：
- 文件名增加 r_返回值类型_返回值变量名 支持，实现完全自描述
- 所有带返回值的 Node 文件名包含返回值类型和变量名
- 无返回值的 Node 文件名不包含 .r_ 部分
- 与 L1 Action 命名规范保持一致
- 新增 L2 Node 聚合方案章节


================================================================================
END OF DOCUMENT
================================================================================