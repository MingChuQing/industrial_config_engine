# 原子操作手册（完整版）v2.0

================================================================================
目录
================================================================================

一、概述
   1.1 什么是原子操作？
   1.2 原子操作的特点
   1.3 四层架构回顾
   1.4 目录结构
   1.5 文件命名规范（v2.1）

二、数据类型系统
   2.1 数据类型总览
   2.2 类型校验规则

三、原子操作完整清单
   3.1 通信类（Modbus）
   3.2 控制类
   3.3 数据类
   3.4 变量类
   3.5 界面类
   3.6 系统类
   3.7 扩展类

四、参数占位符规则

五、各原子操作详细定义
   5.1 modbus_write_verify（写寄存器）
   5.2 modbus_write_verify（写线圈）
   5.3 modbus_read_cache（读寄存器）
   5.4 modbus_read_check（读寄存器并检查条件）
   5.5 wait（等待）
   5.6 calculate（计算表达式）
   5.7 calculate_check（计算表达式并检查条件）
   5.8 set（设置变量）
   5.9 read_variable（读取变量）
   5.10 ui_action（更新界面）
   5.11 user_decision（用户确认）
   5.12 log（写日志）
   5.13 popup（弹框提示）
   5.14 script_exec（执行脚本）

六、无参数原子操作规范

七、Node 引用原子操作

八、L1 Action 聚合方案
   8.1 聚合文件格式
   8.2 目录结构（两种方式共存）
   8.3 各类聚合文件
   8.4 文件名→result_key推导对照表
   8.5 L2 Node 引用示例
   8.6 优势总结

九、快速参考
   9.1 文件名→参数/返回值速查
   9.2 返回结果速查
   9.3 符号说明

十、规则总结


================================================================================
一、概述
================================================================================

1.1 什么是原子操作？

原子操作（L1 Action）是四层架构中第一层（L1）的最小执行单元，不可拆分。
每个原子操作只做一件事：发请求、等响应、返回结果。

原子操作不包含任何异常处理分支（如 on_success / on_failure / on_timeout），
这些由上一层的执行节点（Node）负责。


1.2 原子操作的特点

+--------------+------------------------------------------------------------+
| 特点         | 说明                                                       |
+--------------+------------------------------------------------------------+
| 不可拆分     | 一个原子操作执行一个完整的、最小的设备操作或计算           |
| 无状态       | 不依赖之前的执行状态，每次执行都是独立的                   |
| 无分支       | 不包含 on_success / on_failure / on_timeout                |
| 返回结果     | 执行完成后返回一个值（数值/布尔/字符串/null）              |
| 参数化       | 通过 args 定义输入参数，使用参数名占位                     |
| 文件独立     | 每个原子操作是一个独立的 JSON 文件                         |
+--------------+------------------------------------------------------------+


1.3 四层架构回顾

+--------+-------------------+----------------------------------+------------------+
| 层级   | 目录              | 职责                             | 维护者           |
+--------+-------------------+----------------------------------+------------------+
| L1     | L1_action/        | 单一设备操作模板，最小执行单元   | 设备专家/工程师  |
| L2     | L2_node/          | 带状态机、重试和分支的执行单元   | 自动化工程师     |
| L3     | L3_group/         | 完整工序，支持控制流             | 工艺工程师       |
| L4     | L4_flow/          | 完整产线流程编排，顶层文件       | 操作员/AI        |
+--------+-------------------+----------------------------------+------------------+


1.4 目录结构

industrial_config_engine/
+-- L1_action/                           # 原子操作层（最小执行单元）
|   +-- modbus/                          # Modbus 通信类
|   |   +-- all_actions.json             # 聚合文件（推荐）
|   |   +-- write_register.u16_u16.r_b_write_result.json
|   |   +-- write_register_fixed.r_b_write_result.json
|   |   +-- write_coil.u16_b.r_b_coil_result.json
|   |   +-- write_coil_on.r_b_coil_on_result.json
|   |   +-- write_coil_off.r_b_coil_off_result.json
|   |   +-- read_register.u8_u16.r_u16_register_value.json
|   |   +-- read_register_fixed.r_u16_register_value.json
|   |   +-- check_register.u8_s_u16_u16.r_b_check_result.json
|   |   +-- check_register_fixed.r_b_check_result.json
|   +-- data/                            # 数据计算类
|   |   +-- all_data_actions.json
|   |   +-- calculate.s.r_f_calc_result.json
|   |   +-- calculate_check.s_f_s.r_b_check_result.json
|   +-- control/                         # 控制类
|   |   +-- all_control_actions.json
|   |   +-- wait.u16_s.r_b_wait_done.json
|   +-- ui/                              # 界面交互类
|   |   +-- all_ui_actions.json
|   |   +-- show_status.s_s.json
|   |   +-- confirm.s_s_s.r_s_user_choice.json
|   +-- system/                          # 系统类
|   |   +-- all_system_actions.json
|   |   +-- log.s.json
|   |   +-- popup.s_s.json
|   +-- variable/                        # 变量操作类
|   |   +-- all_variable_actions.json
|   |   +-- set.s_s.json
|   |   +-- read.s.r_s_read_value.json
|   +-- script/                          # 脚本执行类
|       +-- all_script_actions.json
|       +-- exec.s_s.r_s_script_result.json
|
+-- L2_node/                             # 执行节点层（L1 的包装）
+-- L3_group/                            # 执行组层
+-- L4_flow/                             # 流程层（顶层配置文件）
+-- include/                             # 头文件
+-- src/                                 # 源文件
+-- demos/                               # 演示程序
+-- tests/                               # 单元测试


1.5 文件命名规范（v2.1）

核心改进：文件名同时包含参数类型、返回值类型和返回值变量名，实现完全自描述，
无需在JSON中单独定义result_key。

1.5.1 命名格式

    自定义名称.参数类型列表.r_返回值类型_返回值变量名.json

1.5.2 格式说明

+------------------+--------------------------------------------------+---------------------------+
| 组成部分         | 说明                                             | 示例                      |
+------------------+--------------------------------------------------+---------------------------+
| 自定义名称       | 字母、数字、下划线，不含小数点                    | write_register、read_status|
| 参数类型列表     | 用 _ 分隔，按参数顺序排列；无参数时省略          | u16_u16、u8_u16           |
| r_               | 固定分隔符，标识后面是返回值信息                 | r_                        |
| 返回值类型       | 类型缩写；无返回值时可省略整个r_部分             | b、u16、i32               |
| 返回值变量名     | 结果存入的变量名，跟在返回值类型后面，用_连接    | write_result、register_value|
+------------------+--------------------------------------------------+---------------------------+

1.5.3 命名示例

+-----------------------------------------------------+--------------+---------+-----------------+----------------------------------+
| 文件名                                              | 参数类型     | 返回值类型 | 返回值变量名   | 说明                             |
+-----------------------------------------------------+--------------+---------+-----------------+----------------------------------+
| write_register.u16_u16.r_b_write_result              | 两个 u16     | b       | write_result    | 写寄存器，结果存入write_result   |
| read_register.u8_u16.r_u16_register_value            | u8, u16      | u16     | register_value  | 读寄存器，值存入register_value   |
| read_register_32.u8_u16.r_i32_position               | u8, u16      | i32     | position        | 读32位值，存入position           |
| check_register.u8_s_u16_u16.r_b_check_result         | u8, s, u16, u16| b     | check_result    | 检查条件，结果存入check_result   |
| write_coil.u16_b.r_b_coil_result                     | u16, b       | b       | coil_result     | 写线圈，结果存入coil_result      |
| write_coil_on.r_b_coil_on_result                     | 无           | b       | coil_on_result  | 开启线圈，结果存入coil_on_result |
| write_coil_off.r_b_coil_off_result                   | 无           | b       | coil_off_result | 关闭线圈，结果存入coil_off_result|
| wait.u16_s.r_b_wait_done                             | u16, s       | b       | wait_done       | 等待，完成标志存入wait_done      |
| calculate.s.r_f_calc_result                          | s            | f       | calc_result     | 计算表达式，结果存入calc_result  |
| calculate_check.s_f_s.r_b_check_result               | s, f, s      | b       | check_result    | 计算并检查，结果存入check_result |
| set.s_s                                             | s, s         | 无返回值 | —               | 设置变量（无返回值）             |
| read.s.r_s_read_value                                | s            | s       | read_value      | 读取变量，值存入read_value       |
| show_status.s_s                                      | s, s         | 无返回值 | —               | 更新界面（无返回值）             |
| confirm.s_s_s.r_s_user_choice                        | s, s, s      | s       | user_choice     | 用户确认，选项存入user_choice    |
| log.s                                               | s            | 无返回值 | —               | 写日志（无返回值）               |
| popup.s_s                                           | s, s         | 无返回值 | —               | 弹框提示（无返回值）             |
| exec.s_s.r_s_script_result                           | s, s         | s       | script_result   | 执行脚本，结果存入script_result  |

1.5.4 类型缩写表

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
| arr  | array    | —     | JSON数组              |

1.5.5 文件名解析规则

// 解析文件名: "write_register.u16_u16.r_b_write_result"
//                    ------------ ------- - - ------------
//                    自定义名称   参数列表  r_返回值类型_变量名

// 解析逻辑：
// 1. 找到 ".r_" -> 后面是 "返回值类型_变量名"
// 2. 用 "_" 分割返回值部分 -> [0]=类型, [1]=变量名
// 3. 没有 ".r_" -> 返回值类型为 "void"，无变量名

// 解析示例：
// "write_register.u16_u16.r_b_write_result" -> params=["u16","u16"], return_type="b", result_key="write_result"
// "read_register.u8_u16.r_u16_register_value" -> params=["u8","u16"], return_type="u16", result_key="register_value"
// "write_coil_on.r_b_coil_on_result" -> params=[], return_type="b", result_key="coil_on_result"
// "log.s" -> params=["s"], return_type="void", result_key=""

// 代码实现
struct ActionSignature {
    std::string name;
    std::vector<std::string> params;
    std::string return_type;
    std::string result_key;
};

ActionSignature parseFilename(const std::string& filename) {
    ActionSignature sig;
    
    // 查找 ".r_"
    size_t r_pos = filename.find(".r_");
    if (r_pos != std::string::npos) {
        // 提取返回值部分: "b_write_result"
        std::string return_part = filename.substr(r_pos + 3);
        size_t underscore_pos = return_part.find('_');
        if (underscore_pos != std::string::npos) {
            sig.return_type = return_part.substr(0, underscore_pos);
            sig.result_key = return_part.substr(underscore_pos + 1);
        } else {
            sig.return_type = return_part;
            sig.result_key = "";  // 只有类型没有变量名
        }
        
        // 提取参数部分: "u16_u16"
        std::string prefix = filename.substr(0, r_pos);
        size_t dot_pos = prefix.find('.');
        if (dot_pos != std::string::npos) {
            sig.name = prefix.substr(0, dot_pos);
            std::string params_str = prefix.substr(dot_pos + 1);
            if (!params_str.empty()) {
                // 分割参数
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
            // 分割参数
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

1.5.6 与L2的统一

+--------+----------------------------------------+--------------------------------------+---------------------+
| 层级   | 命名格式                               | 示例                                 | result_key来源      |
+--------+----------------------------------------+--------------------------------------+---------------------+
| L1     | 名称.参数.r_类型_变量名                | read_register.u8_u16.r_u16_register_value | 从文件名推导   |
| L2     | 名称.参数.r_类型_变量名                | read_status.r_u16_motor_status       | 从文件名推导         |
+--------+----------------------------------------+--------------------------------------+---------------------+

统一规则：L1和L2都遵循相同的命名规范，result_key统一从文件名推导，JSON中不再需要定义。

1.5.7 优势总结

+---------------------------+-----------------------------------------------------+
| 优势                      | 说明                                                |
+---------------------------+-----------------------------------------------------+
| 完全自描述                | 文件名包含参数类型、返回值类型和变量名，一目了然    |
| 省字段                    | JSON中无需定义result_key和return_type               |
| 类型安全                  | 引擎可在加载时校验参数和返回值类型                  |
| 与L2统一                  | 同一个命名规范适用于L1和L2，降低学习成本            |
| IDE友好                   | 文件列表即可查看所有Action的签名                    |
| 易于维护                  | 修改变量名只需重命名文件，无需修改JSON内容          |
+---------------------------+-----------------------------------------------------+


================================================================================
二、数据类型系统
================================================================================

2.1 数据类型总览

+------+----------+-------+----------------------+-------------+
| #    | 缩写     | 完整名称 | 字节数              | 取值范围    | JSON示例    |
+------+----------+-------+----------------------+-------------+
| 1    | u8       | uint8 | 1                   | 0 ~ 255     | 25          |
| 2    | i8       | int8  | 1                   | -128 ~ 127  | -30         |
| 3    | u16      | uint16| 2                   | 0 ~ 65535   | 300         |
| 4    | i16      | int16 | 2                   | -32768 ~ 32767 | -5000    |
| 5    | u32      | uint32| 4                   | 0 ~ 4294967295 | 120000   |
| 6    | i32      | int32 | 4                   | -2147483648 ~ 2147483647 | -100000 |
| 7    | f        | float32| 4                  | +-3.4e38    | 3.14        |
| 8    | b        | bool  | —                   | true / false| true        |
| 9    | s        | string| —                   | 任意字符串  | "hello"     |
| 10   | hex      | hex_string| —               | 十六进制字符串 | "01 02 FF" |
| 11   | arr      | array | —                   | JSON数组    | [1, 2, 3]   |
+------+----------+-------+----------------------+-------------+

2.2 类型校验规则

+------+------------------------------------------------------------+
| 类型 | 校验规则                                                   |
+------+------------------------------------------------------------+
| u8   | 整数 >= 0 且 <= 255                                         |
| i8   | 整数 >= -128 且 <= 127                                      |
| u16  | 整数 >= 0 且 <= 65535                                       |
| i16  | 整数 >= -32768 且 <= 32767                                  |
| u32  | 整数 >= 0 且 <= 4294967295                                  |
| i32  | 整数 >= -2147483648 且 <= 2147483647                        |
| f    | 浮点数                                                     |
| b    | true 或 false                                              |
| s    | 任意字符串                                                 |
| hex  | 匹配 ^([0-9A-Fa-f]{2}\s)*[0-9A-Fa-f]{2}$ 格式              |
| arr  | JSON数组                                                   |
+------+------------------------------------------------------------+


================================================================================
三、原子操作完整清单
================================================================================

3.1 通信类（Modbus）

+----+---------------------+--------------------------------+------------------------------------+---------------------------+----------+
| #  | 类型                | 带参文件名                     | 无参文件名                         | 说明                      | 返回值   |
+----+---------------------+--------------------------------+------------------------------------+---------------------------+----------+
| 1  | modbus_write_verify | write_register.u16_u16.r_b     | write_register_fixed.r_b           | 写入寄存器并验证响应      | b        |
| 2  | modbus_write_verify | write_coil.u16_b.r_b           | write_coil_on.r_b / write_coil_off.r_b | 写线圈并验证响应       | b        |
| 3  | modbus_read_cache   | read_register.u8_u16.r_u16     | read_register_fixed.r_u16          | 读取寄存器并返回结果      | u16      |
| 4  | modbus_read_check   | check_register.u8_s_u16_u16.r_b| check_register_fixed.r_b           | 读取寄存器并检查条件      | b        |
+----+---------------------+--------------------------------+------------------------------------+---------------------------+----------+

3.2 控制类

+----+--------+---------------------+---------------------+------------------+----------+
| #  | 类型   | 带参文件名          | 无参文件名          | 说明             | 返回值   |
+----+--------+---------------------+---------------------+------------------+----------+
| 5  | wait   | wait.u16_s.r_b      | —                   | 等待指定时间     | b        |
+----+--------+---------------------+---------------------+------------------+----------+

3.3 数据类

+----+----------------+-------------------------+---------------------+-----------------------------+----------+
| #  | 类型           | 带参文件名              | 无参文件名          | 说明                        | 返回值   |
+----+----------------+-------------------------+---------------------+-----------------------------+----------+
| 6  | calculate      | calculate.s.r_f         | —                   | 计算表达式并返回结果        | f        |
| 7  | calculate_check| calculate_check.s_f_s.r_b| —                   | 计算表达式并检查条件        | b        |
+----+----------------+-------------------------+---------------------+-----------------------------+----------+

3.4 变量类

+----+----------------+---------------------+---------------------+----------------------+----------+
| #  | 类型           | 带参文件名          | 无参文件名          | 说明                 | 返回值   |
+----+----------------+---------------------+---------------------+----------------------+----------+
| 8  | set            | set.s_s             | —                   | 直接设置变量值       | 无       |
| 9  | read_variable  | read.s.r_s          | —                   | 从缓存读取变量       | s        |
+----+----------------+---------------------+---------------------+----------------------+----------+

3.5 界面类

+----+----------------+---------------------+---------------------+----------------------+----------+
| #  | 类型           | 带参文件名          | 无参文件名          | 说明                 | 返回值   |
+----+----------------+---------------------+---------------------+----------------------+----------+
| 10 | ui_action      | show_status.s_s     | —                   | 更新界面             | 无       |
| 11 | user_decision  | confirm.s_s_s.r_s   | —                   | 阻塞式用户确认       | s        |
+----+----------------+---------------------+---------------------+----------------------+----------+

3.6 系统类

+----+----------------+---------------------+---------------------+----------------------+----------+
| #  | 类型           | 带参文件名          | 无参文件名          | 说明                 | 返回值   |
+----+----------------+---------------------+---------------------+----------------------+----------+
| 12 | log            | log.s               | —                   | 写日志               | 无       |
| 13 | popup          | popup.s_s           | —                   | 弹框提示             | 无       |
+----+----------------+---------------------+---------------------+----------------------+----------+

3.7 扩展类

+----+----------------+---------------------+---------------------+----------------------+----------+
| #  | 类型           | 带参文件名          | 无参文件名          | 说明                 | 返回值   |
+----+----------------+---------------------+---------------------+----------------------+----------+
| 14 | script_exec    | exec.s_s.r_s        | —                   | 执行外部脚本         | s        |
+----+----------------+---------------------+---------------------+----------------------+----------+


================================================================================
四、参数占位符规则
================================================================================

4.1 使用参数名占位

原子操作中所有需要引用输入参数的地方，使用 ${参数名} 格式：

正确示例：
    "request": "03 ${register} 00 01",
    "check": { "condition": "${condition}", "value": "${compare_value}" },
    "timeout_ms": "${timeout}"

错误示例（不要使用序号）：
    "request": "03 ${0} 00 01",
    "check": { "condition": "${1}", "value": "${2}" },
    "timeout_ms": "${3}"

4.2 参数名与 args 对应

"args": [
    { "index": 0, "name": "register", ... },
    { "index": 1, "name": "condition", ... },
    { "index": 2, "name": "compare_value", ... },
    { "index": 3, "name": "timeout", ... }
]

以上定义表示：
    ${register} 对应 params[0]
    ${condition} 对应 params[1]
    ${compare_value} 对应 params[2]
    ${timeout} 对应 params[3]

4.3 Node 调用传参

Node 通过 params 数组按顺序传入参数：

{
    "type": "node",
    "name": "检查温度",
    "template": "L1_action/modbus/check_register.u8_s_u16_u16.r_b",
    "params": [0x01, "le", 100, 2000],
    "on_success": [...],
    "on_failure": [...]
}

参数映射：
    params[0] = 0x01    -> ${register}
    params[1] = "le"    -> ${condition}
    params[2] = 100     -> ${compare_value}
    params[3] = 2000    -> ${timeout}


================================================================================
五、各原子操作详细定义
================================================================================

5.1 modbus_write_verify（写寄存器）

【带参数版本】L1_action/modbus/write_register.u16_u16.r_b_write_result.json

{
    "type": "modbus_write_verify",
    "description": "写入单个寄存器",
    "protocol": "modbus",
    "request": "06 ${register} ${value}",
    "response": "06 ${register} ${value}",
    "timeout_ms": 3000,
    "args": [
        { "index": 0, "name": "register", "type": "u16", "min": 0, "max": 65535, "desc": "寄存器地址" },
        { "index": 1, "name": "value", "type": "u16", "min": 0, "max": 65535, "desc": "写入值" }
    ]
}

【无参数版本】L1_action/modbus/write_register_fixed.r_b_write_result.json

{
    "type": "modbus_write_verify",
    "description": "写入固定寄存器（地址0x0051，值300）",
    "protocol": "modbus",
    "request": "06 00 51 01 2C",
    "response": "06 00 51 01 2C",
    "timeout_ms": 3000
}

返回结果：true（响应匹配）/ false（响应不匹配或超时）


5.2 modbus_write_verify（写线圈）

【带参数版本】L1_action/modbus/write_coil.u16_b.r_b_coil_result.json

{
    "type": "modbus_write_verify",
    "description": "写线圈",
    "protocol": "modbus",
    "request": "05 ${coil} ${value}",
    "response": "05 ${coil} ${value}",
    "timeout_ms": 3000,
    "args": [
        { "index": 0, "name": "coil", "type": "u16", "min": 0, "max": 65535, "desc": "线圈地址" },
        { "index": 1, "name": "value", "type": "b", "desc": "写入值：true=开启(FF00)，false=关闭(0000)" }
    ]
}

【无参数版本】L1_action/modbus/write_coil_on.r_b_coil_on_result.json

{
    "type": "modbus_write_verify",
    "description": "开启线圈（地址0x0001）",
    "protocol": "modbus",
    "request": "05 00 01 FF 00",
    "response": "05 00 01 FF 00",
    "timeout_ms": 3000
}

【无参数版本】L1_action/modbus/write_coil_off.r_b_coil_off_result.json

{
    "type": "modbus_write_verify",
    "description": "关闭线圈（地址0x0001）",
    "protocol": "modbus",
    "request": "05 00 01 00 00",
    "response": "05 00 01 00 00",
    "timeout_ms": 3000
}

返回结果：true（响应匹配）/ false（响应不匹配或超时）


5.3 modbus_read_cache（读寄存器）

【带参数版本】L1_action/modbus/read_register.u8_u16.r_u16_register_value.json

{
    "type": "modbus_read_cache",
    "description": "读取单个寄存器值",
    "protocol": "modbus",
    "request": "03 ${register} 00 01",
    "parse": {
        "start_byte": 2,
        "length": 2,
        "endian": "big",
        "type": "uint16"
    },
    "timeout_ms": "${timeout}",
    "args": [
        { "index": 0, "name": "register", "type": "u8", "min": 0, "max": 255, "desc": "寄存器地址" },
        { "index": 1, "name": "timeout", "type": "u16", "min": 500, "max": 10000, "default": 2000, "desc": "超时(毫秒)" }
    ]
}

【无参数版本】L1_action/modbus/read_register_fixed.r_u16_register_value.json

{
    "type": "modbus_read_cache",
    "description": "读取固定寄存器（地址0x01）",
    "protocol": "modbus",
    "request": "03 00 01 00 01",
    "parse": {
        "start_byte": 2,
        "length": 2,
        "endian": "big",
        "type": "uint16"
    },
    "timeout_ms": 2000
}

返回结果：成功返回读取值（数值）/ 失败返回 null


5.4 modbus_read_check（读寄存器并检查条件）

【带参数版本】L1_action/modbus/check_register.u8_s_u16_u16.r_b_check_result.json

{
    "type": "modbus_read_check",
    "description": "读取寄存器并检查条件",
    "protocol": "modbus",
    "request": "03 ${register} 00 01",
    "parse": {
        "start_byte": 2,
        "length": 2,
        "endian": "big",
        "type": "uint16"
    },
    "check": {
        "condition": "${condition}",
        "value": "${compare_value}"
    },
    "timeout_ms": "${timeout}",
    "args": [
        { "index": 0, "name": "register", "type": "u8", "min": 0, "max": 255, "desc": "寄存器地址" },
        { "index": 1, "name": "condition", "type": "s", "enum": ["eq","ne","lt","le","gt","ge"], "desc": "比较条件" },
        { "index": 2, "name": "compare_value", "type": "u16", "min": 0, "max": 65535, "desc": "比较值" },
        { "index": 3, "name": "timeout", "type": "u16", "min": 500, "max": 10000, "default": 2000, "desc": "超时(毫秒)" }
    ]
}

【无参数版本】L1_action/modbus/check_register_fixed.r_b_check_result.json

{
    "type": "modbus_read_check",
    "description": "检查固定寄存器（地址0x01，值<=100）",
    "protocol": "modbus",
    "request": "03 00 01 00 01",
    "parse": {
        "start_byte": 2,
        "length": 2,
        "endian": "big",
        "type": "uint16"
    },
    "check": {
        "condition": "le",
        "value": 100
    },
    "timeout_ms": 2000
}

返回结果：条件满足 true / 条件不满足 false / 失败 null


5.5 wait（等待）

文件名：L1_action/control/wait.u16_s.r_b_wait_done.json

{
    "type": "wait",
    "description": "等待指定时间",
    "duration": "${duration}",
    "unit": "${unit}",
    "args": [
        { "index": 0, "name": "duration", "type": "u16", "min": 1, "max": 3600, "desc": "等待时长" },
        { "index": 1, "name": "unit", "type": "s", "enum": ["seconds", "milliseconds"], "desc": "时间单位" }
    ]
}

返回结果：true（等待完成）


5.6 calculate（计算表达式）

文件名：L1_action/data/calculate.s.r_f_calc_result.json

{
    "type": "assign",
    "description": "计算表达式并返回结果",
    "expression": "${expression}",
    "args": [
        { "index": 0, "name": "expression", "type": "s", "desc": "算术表达式，如 (${p1}-${p2})/${p1}*100" }
    ]
}

返回结果：计算出的数值 / 失败返回 null


5.7 calculate_check（计算表达式并检查条件）

文件名：L1_action/data/calculate_check.s_f_s.r_b_check_result.json

{
    "type": "calculate_and_check",
    "description": "计算表达式并检查条件",
    "expression": "${expression}",
    "check": {
        "condition": "${condition}",
        "value": "${threshold}"
    },
    "args": [
        { "index": 0, "name": "expression", "type": "s", "desc": "算术表达式" },
        { "index": 1, "name": "threshold", "type": "f", "desc": "阈值" },
        { "index": 2, "name": "condition", "type": "s", "enum": ["eq","ne","lt","le","gt","ge"], "desc": "比较条件" }
    ]
}

返回结果：条件满足 true / 条件不满足 false / 失败 null


5.8 set（设置变量）

文件名：L1_action/variable/set.s_s.json

{
    "type": "set_variable",
    "description": "设置变量值",
    "cache_key": "${key}",
    "value": "${value}",
    "args": [
        { "index": 0, "name": "key", "type": "s", "desc": "变量名" },
        { "index": 1, "name": "value", "type": "s", "desc": "变量值" }
    ]
}

返回结果：成功返回设置的值 / 失败返回 null


5.9 read_variable（读取变量）

文件名：L1_action/variable/read.s.r_s_read_value.json

{
    "type": "read",
    "description": "从缓存读取变量",
    "source": "${source}",
    "args": [
        { "index": 0, "name": "source", "type": "s", "desc": "源变量名" }
    ]
}

返回结果：成功返回读取到的值 / 失败返回 null


5.10 ui_action（更新界面）

文件名：L1_action/ui/show_status.s_s.json

{
    "type": "ui_action",
    "description": "更新状态显示",
    "action": "update_status",
    "data": {
        "text": "${text}",
        "color": "${color}"
    },
    "args": [
        { "index": 0, "name": "text", "type": "s", "desc": "状态文字" },
        { "index": 1, "name": "color", "type": "s", "enum": ["red","green","blue","yellow"], "default": "blue", "desc": "颜色" }
    ]
}

返回结果：true / false


5.11 user_decision（用户确认）

文件名：L1_action/ui/confirm.s_s_s.r_s_user_choice.json

{
    "type": "user_decision",
    "description": "用户确认对话框",
    "message": "${message}",
    "options": ["${option1}", "${option2}"],
    "args": [
        { "index": 0, "name": "message", "type": "s", "desc": "提示信息" },
        { "index": 1, "name": "option1", "type": "s", "desc": "选项1" },
        { "index": 2, "name": "option2", "type": "s", "desc": "选项2" }
    ]
}

返回结果：用户选中的选项字符串 / 关闭返回 null


5.12 log（写日志）

文件名：L1_action/system/log.s.json

{
    "type": "log",
    "description": "写入日志",
    "message": "${message}",
    "args": [
        { "index": 0, "name": "message", "type": "s", "desc": "日志内容" }
    ]
}

返回结果：true / false


5.13 popup（弹框提示）

文件名：L1_action/system/popup.s_s.json

{
    "type": "popup",
    "description": "弹框提示用户",
    "message": "${message}",
    "level": "${level}",
    "args": [
        { "index": 0, "name": "message", "type": "s", "desc": "提示内容" },
        { "index": 1, "name": "level", "type": "s", "enum": ["info","warning","error"], "default": "info", "desc": "提示级别" }
    ]
}

返回结果：true / false


5.14 script_exec（执行脚本）

文件名：L1_action/script/exec.s_s.r_s_script_result.json

{
    "type": "script_exec",
    "description": "执行外部脚本",
    "script_type": "${script_type}",
    "script_file": "${script_file}",
    "params": {},
    "args": [
        { "index": 0, "name": "script_type", "type": "s", "enum": ["python","javascript","lua"], "desc": "脚本类型" },
        { "index": 1, "name": "script_file", "type": "s", "desc": "脚本文件名" }
    ]
}

返回结果：脚本返回值 / 失败返回 null


================================================================================
六、无参数原子操作规范
================================================================================

6.1 定义规则

1. 文件名格式：自定义名称.r_返回值类型_返回值变量名.json（有返回值时）
               或 自定义名称.json（无返回值时）
2. 不包含 args 字段：没有参数定义
3. 所有值固定：request、response、timeout_ms 等全部写死

6.2 常用无参数原子操作示例

+---------------------------+---------------------------------+---------------------------+----------+
| 文件名                    | 路径                            | 用途                      | 返回值   |
+---------------------------+---------------------------------+---------------------------+----------+
| write_coil_on.r_b_coil_on_result | L1_action/modbus/          | 开启线圈（固定地址）      | b        |
| write_coil_off.r_b_coil_off_result | L1_action/modbus/         | 关闭线圈（固定地址）      | b        |
| write_register_fixed.r_b_write_result | L1_action/modbus/        | 写入固定值到固定寄存器    | b        |
| read_register_fixed.r_u16_register_value | L1_action/modbus/        | 读取固定寄存器            | u16      |
| check_register_fixed.r_b_check_result | L1_action/modbus/        | 检查固定寄存器            | b        |
| trigger_run.r_b_run_result            | L1_action/modbus/        | 触发运行                  | b        |
| trigger_stop.r_b_stop_result          | L1_action/modbus/        | 触发停止                  | b        |
| reset_alarm.r_b_alarm_reset           | L1_action/modbus/        | 复位报警                  | b        |
+---------------------------+---------------------------------+---------------------------+----------+

6.3 Node 调用无参数原子操作

{
    "type": "node",
    "name": "触发运行",
    "template": "L1_action/modbus/trigger_run.r_b_run_result",
    "params": [],
    "on_success": [
        { "type": "log", "message": "运行触发成功" }
    ],
    "on_failure": [
        { "type": "log", "message": "运行触发失败" }
    ]
}


================================================================================
七、Node 引用原子操作
================================================================================

7.1 引用方式

Node 通过 template 字段引用原子操作文件：

{
    "type": "node",
    "name": "读取温度",
    "template": "L1_action/modbus/read_register.u8_u16.r_u16_register_value",
    "params": [0x01, 2000],
    "on_success": [...],
    "on_failure": [...]
}

7.2 路径解析规则

template: "L1_action/modbus/read_register.u8_u16.r_u16_register_value"
              |
              v
实际文件: L1_action/modbus/read_register.u8_u16.r_u16_register_value.json
              |
              v
解析: 参数签名 = [u8, u16], 返回值签名 = u16, result_key = "register_value"
              |
              v
校验: params 必须有 2 个参数
      params[0] 必须是 u8 类型
      params[1] 必须是 u16 类型
      返回值类型为 u16

7.3 完整示例

{
    "type": "node",
    "name": "检查温度是否<=100",
    "description": "读取寄存器0x01，检查是否<=100",
    "template": "L1_action/modbus/check_register.u8_s_u16_u16.r_b_check_result",
    "params": [0x01, "le", 100, 2000],
    "on_success": [
        { "type": "log", "message": "温度达标" },
        { "type": "ui_action", "action": "update_status", "data": { "text": "温度正常", "color": "green" } }
    ],
    "on_failure": [
        { "type": "log", "message": "温度超标或读取失败" },
        { "type": "popup", "message": "温度异常，请检查", "level": "error" }
    ]
}


================================================================================
八、L1 Action 聚合方案
================================================================================

设计思路：
    - 保持单个文件独立：原有的单个 Action 文件依然有效
    - 新增聚合文件：一个 JSON 文件可以包含多个 Action
    - 两种方式共存：加载器同时支持单文件和多文件两种格式

8.1 聚合文件格式

【设计思路】
聚合文件将多个相关的 L1 Action 打包到一个 JSON 文件中，便于管理和部署。
每个 Action 在聚合文件中通过 `filename` 字段获得一个虚拟路径标识符，
外部引用时使用 "聚合文件路径 + # + filename" 的格式。

【引用格式】
引用聚合文件中的 Action 时，使用以下格式：
    L1_action/目录名/聚合文件名#filename

示例：
    L1_action/modbus/all_actions.json#write_register.u16_u16.r_b_write_result

【加载规则】
1. 加载器读取聚合文件时，以每个 Action 的 `filename` 字段为 Key 注册该 Action
2. `filename` 用于推导 `result_key`（规则与单文件版本完全相同）
3. `filename` 仅在聚合文件内部唯一，不同聚合文件之间可以重名
4. 加载器应支持单文件和聚合文件两种引用方式并存

【文件：L1_action/modbus/all_actions.json】

{
    "type": "action_bundle",
    "description": "Modbus 通信类原子操作集合",
    "version": "1.0.0",
    "actions": [
        {
            // filename: 虚拟路径标识符，用于外部引用和 result_key 推导
            // 引用方式：L1_action/modbus/all_actions.json#write_register.u16_u16.r_b_write_result
            "filename": "write_register.u16_u16.r_b_write_result",
            "type": "modbus_write_verify",
            "description": "写入单个寄存器",
            "protocol": "modbus",
            "request": "06 ${register} ${value}",
            "response": "06 ${register} ${value}",
            "timeout_ms": 3000,
            "args": [
                { "index": 0, "name": "register", "type": "u16", "min": 0, "max": 65535, "desc": "寄存器地址" },
                { "index": 1, "name": "value", "type": "u16", "min": 0, "max": 65535, "desc": "写入值" }
            ]
        },
        {

            "filename": "read_register.u8_u16.r_u16_register_value",
            "type": "modbus_read_cache",
            "description": "读取单个寄存器值",
            "protocol": "modbus",
            "request": "03 ${register} 00 01",
            "parse": {
                "start_byte": 2,
                "length": 2,
                "endian": "big",
                "type": "uint16"
            },
            "timeout_ms": "${timeout}",
            "args": [
                { "index": 0, "name": "register", "type": "u8", "min": 0, "max": 255, "desc": "寄存器地址" },
                { "index": 1, "name": "timeout", "type": "u16", "min": 500, "max": 10000, "default": 2000, "desc": "超时(毫秒)" }
            ]
        },
        {
            "filename": "check_register.u8_s_u16_u16.r_b_check_result",
            "type": "modbus_read_check",
            "description": "读取寄存器并检查条件",
            "protocol": "modbus",
            "request": "03 ${register} 00 01",
            "parse": {
                "start_byte": 2,
                "length": 2,
                "endian": "big",
                "type": "uint16"
            },
            "check": {
                "condition": "${condition}",
                "value": "${compare_value}"
            },
            "timeout_ms": "${timeout}",
            "args": [
                { "index": 0, "name": "register", "type": "u8", "min": 0, "max": 255, "desc": "寄存器地址" },
                { "index": 1, "name": "condition", "type": "s", "enum": ["eq","ne","lt","le","gt","ge"], "desc": "比较条件" },
                { "index": 2, "name": "compare_value", "type": "u16", "min": 0, "max": 65535, "desc": "比较值" },
                { "index": 3, "name": "timeout", "type": "u16", "min": 500, "max": 10000, "default": 2000, "desc": "超时(毫秒)" }
            ]
        },
        {
            "filename": "write_coil.u16_b.r_b_coil_result",
            "type": "modbus_write_verify",
            "description": "写线圈",
            "protocol": "modbus",
            "request": "05 ${coil} ${value}",
            "response": "05 ${coil} ${value}",
            "timeout_ms": 3000,
            "args": [
                { "index": 0, "name": "coil", "type": "u16", "min": 0, "max": 65535, "desc": "线圈地址" },
                { "index": 1, "name": "value", "type": "b", "desc": "写入值：true=开启(FF00)，false=关闭(0000)" }
            ]
        },
        {
            "filename": "write_coil_on.r_b_coil_on_result",
            "type": "modbus_write_verify",
            "description": "开启线圈（固定地址0x0001）",
            "protocol": "modbus",
            "request": "05 00 01 FF 00",
            "response": "05 00 01 FF 00",
            "timeout_ms": 3000,
            "args": []
        },
        {
            "filename": "write_coil_off.r_b_coil_off_result",
            "type": "modbus_write_verify",
            "description": "关闭线圈（固定地址0x0001）",
            "protocol": "modbus",
            "request": "05 00 01 00 00",
            "response": "05 00 01 00 00",
            "timeout_ms": 3000,
            "args": []
        }
    ]
}

8.2 目录结构（两种方式共存）

L1_action/
+-- modbus/
|   +-- all_actions.json                        # 聚合文件（推荐）
|   +-- write_register.u16_u16.r_b_write_result.json   # 单个文件（也支持）
|   +-- read_register.u8_u16.r_u16_register_value.json # 单个文件（也支持）
+-- data/
|   +-- all_data_actions.json                   # 数据类聚合
+-- control/
|   +-- all_control_actions.json                # 控制类聚合
+-- ui/
|   +-- all_ui_actions.json                     # UI类聚合
+-- system/
|   +-- all_system_actions.json                 # 系统类聚合
+-- variable/
|   +-- all_variable_actions.json               # 变量类聚合
+-- script/
    +-- all_script_actions.json                 # 脚本类聚合

8.3 各类聚合文件

【数据类聚合】L1_action/data/all_data_actions.json

{
    "type": "action_bundle",
    "description": "数据计算类原子操作集合",
    "version": "1.0.0",
    "actions": [
        {
            "filename": "calculate.s.r_f_calc_result",
            "type": "calculate",
            "description": "计算表达式并返回结果",
            "expression": "${expression}",
            "args": [
                { "index": 0, "name": "expression", "type": "s", "desc": "算术表达式，如 (${p1}-${p2})/${p1}*100" }
            ]
        },
        {
            "filename": "calculate_check.s_f_s.r_b_check_result",
            "type": "calculate_check",
            "description": "计算表达式并检查条件",
            "expression": "${expression}",
            "check": {
                "condition": "${condition}",
                "value": "${threshold}"
            },
            "args": [
                { "index": 0, "name": "expression", "type": "s", "desc": "算术表达式" },
                { "index": 1, "name": "threshold", "type": "f", "desc": "阈值" },
                { "index": 2, "name": "condition", "type": "s", "enum": ["eq","ne","lt","le","gt","ge"], "desc": "比较条件" }
            ]
        }
    ]
}

【控制类聚合】L1_action/control/all_control_actions.json

{
    "type": "action_bundle",
    "description": "控制类原子操作集合",
    "version": "1.0.0",
    "actions": [
        {
            "filename": "wait.u16_s.r_b_wait_done",
            "type": "wait",
            "description": "等待指定时间",
            "duration": "${duration}",
            "unit": "${unit}",
            "args": [
                { "index": 0, "name": "duration", "type": "u16", "min": 1, "max": 65535, "desc": "等待时长" },
                { "index": 1, "name": "unit", "type": "s", "enum": ["ms", "seconds", "minutes"], "default": "seconds", "desc": "时间单位" }
            ]
        },
        {
            "filename": "wait_ms.u16.r_b_wait_done",
            "type": "wait",
            "description": "等待指定毫秒数",
            "duration": "${duration}",
            "unit": "ms",
            "args": [
                { "index": 0, "name": "duration", "type": "u16", "min": 1, "max": 65535, "desc": "等待毫秒数" }
            ]
        }
    ]
}

【UI类聚合】L1_action/ui/all_ui_actions.json

{
    "type": "action_bundle",
    "description": "用户界面类原子操作集合",
    "version": "1.0.0",
    "actions": [
        {
            "filename": "popup.s_s",
            "type": "popup",
            "description": "显示弹窗提示",
            "message": "${message}",
            "level": "${level}",
            "args": [
                { "index": 0, "name": "message", "type": "s", "desc": "弹窗消息内容" },
                { "index": 1, "name": "level", "type": "s", "enum": ["info", "warning", "error", "success"], "default": "info", "desc": "消息级别" }
            ]
        },
        {
            "filename": "confirm.s_s_s.r_s_user_choice",
            "type": "user_decision",
            "description": "用户选择对话框（三选项）",
            "message": "${message}",
            "option1": "${option1}",
            "option2": "${option2}",
            "option3": "${option3}",
            "args": [
                { "index": 0, "name": "message", "type": "s", "desc": "提示消息" },
                { "index": 1, "name": "option1", "type": "s", "default": "确定", "desc": "选项1" },
                { "index": 2, "name": "option2", "type": "s", "default": "取消", "desc": "选项2" },
                { "index": 3, "name": "option3", "type": "s", "default": "", "desc": "选项3（可选）" }
            ]
        },
        {
            "filename": "log.s",
            "type": "log",
            "description": "写入日志",
            "message": "${message}",
            "args": [
                { "index": 0, "name": "message", "type": "s", "desc": "日志消息" }
            ]
        }
    ]
}

【变量类聚合】L1_action/variable/all_variable_actions.json

{
    "type": "action_bundle",
    "description": "变量操作类原子操作集合",
    "version": "1.0.0",
    "actions": [
        {
            "filename": "set.s_s",
            "type": "set",
            "description": "设置变量值",
            "key": "${key}",
            "value": "${value}",
            "args": [
                { "index": 0, "name": "key", "type": "s", "desc": "变量名" },
                { "index": 1, "name": "value", "type": "s", "desc": "变量值" }
            ]
        },
        {
            "filename": "read.s.r_s_read_value",
            "type": "read_variable",
            "description": "读取变量值",
            "key": "${key}",
            "args": [
                { "index": 0, "name": "key", "type": "s", "desc": "变量名" }
            ]
        }
    ]
}

8.4 文件名->result_key推导对照表

+---------------------------------------------+--------------+-------------------+----------------------------------+
| 文件名                                       | 返回值类型   | result_key        | 说明                             |
+---------------------------------------------+--------------+-------------------+----------------------------------+
| write_register.u16_u16.r_b_write_result      | b            | write_result      | 写寄存器结果                     |
| read_register.u8_u16.r_u16_register_value    | u16          | register_value    | 读取的寄存器值                   |
| check_register.u8_s_u16_u16.r_b_check_result | b            | check_result      | 检查结果                         |
| write_coil.u16_b.r_b_coil_result             | b            | coil_result       | 写线圈结果                       |
| write_coil_on.r_b_coil_on_result             | b            | coil_on_result    | 开启线圈结果                     |
| write_coil_off.r_b_coil_off_result           | b            | coil_off_result   | 关闭线圈结果                     |
| wait.u16_s.r_b_wait_done                     | b            | wait_done         | 等待完成标志                     |
| wait_ms.u16.r_b_wait_done                    | b            | wait_done         | 等待完成标志                     |
| calculate.s.r_f_calc_result                  | f            | calc_result       | 计算结果                         |
| calculate_check.s_f_s.r_b_check_result       | b            | check_result      | 检查结果                         |
| popup.s_s                                    | void         | —                 | 无返回值                         |
| confirm.s_s_s.r_s_user_choice                | s            | user_choice       | 用户选择                         |
| log.s                                        | void         | —                 | 无返回值                         |
| set.s_s                                      | void         | —                 | 无返回值                         |
| read.s.r_s_read_value                        | s            | read_value        | 读取的值                         |
+---------------------------------------------+--------------+-------------------+----------------------------------+

8.5 L2 Node 引用示例

{
    "type": "node",
    "name": "读取温度并判断",
    "description": "从寄存器0x01读取温度，判断是否<=50℃",
    "params": [0x01, 2000],
    "timeout_ms": 15000,
    "max_retries": 3,
    "retry_interval": 1000,
    "action": {
        "template": "L1_action/modbus/read_register.u8_u16.r_u16_register_value"
    },
    "judge": {
        "type": "compare",
        "condition": "le",
        "value": 50.0
    },
    "on_success": [
        {
            "type": "log",
            "message": "温度达标：${register_value}℃"
        }
    ],
    "on_failure": [
        {
            "type": "log",
            "message": "温度超标：${register_value}℃"
        },
        {
            "type": "popup",
            "message": "温度${register_value}℃，要求<=50℃",
            "level": "warning"
        }
    ]
}

8.6 优势总结

+------------------------+----------------------------------------------------------+
| 优势                   | 说明                                                     |
+------------------------+----------------------------------------------------------+
| 统一命名规范           | L1和L2都遵循相同的文件名规则，易于理解和使用              |
| 完全自描述             | 文件名包含参数类型、返回值类型和变量名，无需额外文档      |
| 灵活部署               | 支持单文件和聚合文件两种方式，适应不同场景                |
| 性能优化               | 聚合文件减少文件IO，提升加载速度                          |
| 易于维护               | 同类型Action集中管理，修改更方便                          |
| 类型安全               | 统一的类型系统，加载时可校验                              |
+------------------------+----------------------------------------------------------+


================================================================================
九、快速参考
================================================================================

9.1 文件名->参数/返回值速查

+---------------------------------------------+----------+------------------------------------+----------+
| 文件名                                       | 路径     | 参数列表                           | 返回值   |
+---------------------------------------------+----------+------------------------------------+----------+
| write_register.u16_u16.r_b_write_result      | modbus/  | [register:u16, value:u16]          | b        |
| write_register_fixed.r_b_write_result        | modbus/  | []（无参数）                       | b        |
| write_coil.u16_b.r_b_coil_result             | modbus/  | [coil:u16, value:b]                | b        |
| write_coil_on.r_b_coil_on_result             | modbus/  | []（无参数）                       | b        |
| write_coil_off.r_b_coil_off_result           | modbus/  | []（无参数）                       | b        |
| read_register.u8_u16.r_u16_register_value    | modbus/  | [register:u8, timeout:u16]         | u16      |
| read_register_fixed.r_u16_register_value     | modbus/  | []（无参数）                       | u16      |
| check_register.u8_s_u16_u16.r_b_check_result | modbus/  | [register:u8, condition:s, compare_value:u16, timeout:u16] | b |
| check_register_fixed.r_b_check_result        | modbus/  | []（无参数）                       | b        |
| wait.u16_s.r_b_wait_done                     | control/ | [duration:u16, unit:s]             | b        |
| calculate.s.r_f_calc_result                  | data/    | [expression:s]                     | f        |
| calculate_check.s_f_s.r_b_check_result       | data/    | [expression:s, threshold:f, condition:s] | b    |
| set.s_s                                      | variable/| [key:s, value:s]                   | 无       |
| read.s.r_s_read_value                        | variable/| [source:s]                         | s        |
| show_status.s_s                              | ui/      | [text:s, color:s]                  | 无       |
| confirm.s_s_s.r_s_user_choice                | ui/      | [message:s, option1:s, option2:s]  | s        |
| log.s                                        | system/  | [message:s]                        | 无       |
| popup.s_s                                    | system/  | [message:s, level:s]               | 无       |
| exec.s_s.r_s_script_result                   | script/  | [script_type:s, script_file:s]     | s        |
+---------------------------------------------+----------+------------------------------------+----------+

9.2 返回结果速查

+---------------------------+------------------+------------------+
| 类型                      | 成功返回         | 失败返回         |
+---------------------------+------------------+------------------+
| modbus_write_verify       | true             | false            |
| modbus_read_cache         | 读取值（数值）   | null             |
| modbus_read_check         | true/false       | null             |
| wait                      | true             | false            |
| calculate                 | 计算值（数值）   | null             |
| calculate_check           | true/false       | null             |
| set                       | 设置的值         | null             |
| read_variable             | 读取的值         | null             |
| ui_action                 | true             | false            |
| user_decision             | 选项字符串       | null             |
| log                       | true             | false            |
| popup                     | true             | false            |
| script_exec               | 脚本返回值       | null             |
+---------------------------+------------------+------------------+

9.3 符号说明

+------+------------------------------------------------------------+
| 符号 | 含义                                                       |
+------+------------------------------------------------------------+
| []   | 数组，参数按顺序传入                                       |
| {}   | 对象，包含多个键值对                                       |
| |    | 或者，表示可选项                                           |
| $    | 变量引用前缀，如 ${variable}                               |
| .r_  | 固定分隔符，标识返回值类型                                 |
+------+------------------------------------------------------------+


================================================================================
十、规则总结
================================================================================

10.1 poll_until 不属于原子操作

poll_until 包含轮询逻辑（循环 + 条件判断 + 超时控制），是组合逻辑，
应放在执行节点层（Node）或 L3 执行组层，不作为原子操作。

10.2 无参数原子操作

- 文件名：有返回值时 "自定义名称.r_返回值类型_返回值变量名.json"；
          无返回值时 "自定义名称.json"
- 文件内容：不包含 args 字段
- Node 调用：params: []

10.3 参数占位符

- 使用 ${参数名} 格式引用参数
- 参数名与 args 中的 name 一致
- 不使用 ${0}、${1} 等序号方式

10.4 命名风格统一

+-------------------+-------------------+--------------------------------------+
| 操作类型          | 命名风格          | 示例                                 |
+-------------------+-------------------+--------------------------------------+
| Modbus 通信       | modbus_动词_对象  | modbus_write_verify                  |
| 数据计算          | calculate / calculate_check | calculate                    |
| 变量操作          | 简短动词          | set / read_variable                  |
| 界面交互          | 名词或动词短语    | show_status / confirm                |
| 系统操作          | 简短名词          | log / popup                          |
+-------------------+-------------------+--------------------------------------+

10.5 原子操作类型分组

+----+---------------------+--------------------------------+----------+
| #  | 类型                | 说明                           | 返回值   |
+----+---------------------+--------------------------------+----------+
| 1  | modbus_write_verify | 写入并验证响应                 | b        |
| 2  | modbus_read_cache   | 读取并返回结果                 | 数值     |
| 3  | modbus_read_check   | 读取并检查条件                 | b        |
| 4  | wait                | 等待指定时间                   | b        |
| 5  | calculate           | 计算表达式并返回结果           | f        |
| 6  | calculate_check     | 计算表达式并检查条件           | b        |
| 7  | set                 | 设置变量值                     | 无       |
| 8  | read_variable       | 读取变量值                     | 值       |
| 9  | ui_action           | 更新界面                       | 无       |
| 10 | user_decision       | 用户确认                       | s        |
| 11 | log                 | 写日志                         | 无       |
| 12 | popup               | 弹框提示                       | 无       |
| 13 | script_exec         | 执行脚本                       | 值       |
+----+---------------------+--------------------------------+----------+


================================================================================
文档信息
================================================================================

文档版本：v2.0
最后更新：2026-08-03
维护者：明楚晴（东北育才学校）

主要变更：
- 文件名增加 r_返回值类型_返回值变量名 支持，实现完全自描述
- 所有带返回值的 Action 文件名包含返回值类型和变量名
- 无返回值的 Action 文件名不包含 .r_ 部分
- 与 L2 Node 命名规范保持一致
- 新增 L1 Action 聚合方案章节
- 完善各原子操作详细定义

================================================================================
END OF DOCUMENT
================================================================================