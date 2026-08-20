# Device Configuration Example - Dual-Axis Stepper Motor System

## Hardware Configuration
- **Serial port**: COM14
- **Baud rate**: 9600
- **Data bits**: 8
- **Stop bits**: 1
- **Parity**: None
- **Device 1**: X-axis, Modbus address 1
- **Device 2**: Y-axis, Modbus address 2

## Directory Structure

| File | Layer | Description |
|------|------|------|
| `links.json` | Layer 1 (physical) | Serial communication parameters |
| `protocols/modbus_rtu.json` | Layer 2 (protocol) | Modbus RTU device mapping |
| `types/modbus/iDM42_RS06.json` | Layer 3 (application) | Stepper motor type definition |

## Three-Layer Data Flow

```
User action: enable "X_Axis"
↓
instances: X_Axis → type_id: iDM42_RS06, link_id: 1, modbus_address: 1
↓
types: iDM42_RS06 → register_map: { control_word: 0x1801, ... }
↓
links: link_id: 1 → COM14, 9600, 8N1
↓
Execution: COM14 → Modbus RTU (address 1) → register 0x1801
```

## Usage

Copy the configuration files in this directory to the `device/` folder under the project root.
