# 设备配置示例 -  双轴步进电机系统

## 硬件配置
- **串口**: COM14
- **波特率**: 9600
- **数据位**: 8
- **停止位**: 1
- **校验位**: None
- **设备1**: X轴，Modbus地址 1
- **设备2**: Y轴，Modbus地址 2

## 目录结构说明

| 文件 | 层级 | 说明 |
|------|------|------|
| `links.json` | 第1层 物理层 | 串口通信参数 |
| `protocols/modbus_rtu.json` | 第2层 协议层 | Modbus RTU 设备映射 |
| `types/modbus/iDM42_RS06.json` | 第3层 应用层 |  步进电机类型定义 |

## 三层数据流


用户操作: 使能 "X_Axis"
↓
instances: X_Axis → type_id: iDM42_RS06, link_id: 1, modbus_address: 1
↓
types: iDM42_RS06 → register_map: { control_word: 0x1801, ... }
↓
links: link_id: 1 → COM14, 9600, 8N1
↓
执行: COM14 → Modbus RTU (地址1) → 寄存器 0x1801




## 使用方式

将此目录下的配置文件复制到项目根目录的 `device/` 文件夹下使用。

