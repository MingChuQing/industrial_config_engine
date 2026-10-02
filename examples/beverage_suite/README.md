# 九配方饮料生产线（构造的模拟案例）

本例按需求新建，不是工厂原始数据，也不是从真实控制系统采集的实验。九份展开式 JSON 和四层配置分别表达相同的生产计划；模拟器分别解释两种配置，再比较其可观察执行轨迹。

## 生产规格

- 三个独立灌装口：橙汁、苹果汁、葡萄汁。
- 三种包装：家庭装 2000 mL、罐装 330 mL、小瓶装 250 mL。
- 七台步进电机：X、Y1、Y2、Z、进料 Infeed、封口 Closure、出料 Outfeed。
- 每份配方执行两批，每批六件；九份共 108 件、92.88 L。
- 每件流程：进料 → 取空容器 → 移至对应灌装口 → 灌装并轮询完成 → 移至封口点 → 送盖及封口 → 移至成品点 → 松夹及出料。罐装使用模拟卷封，其余使用模拟旋盖。
- 取空容器、灌装、封口、成品使用不同生产点位；工位进入点和工作点分开。共 37 个生产点位，每个映射为 X/Y/Z 各自的 PR0–PR15。

## 配置目录

`legacy/` 中九个文件对应 orange/apple/grape × family/can/small。每份 2692 行，总计 24228 行。外层批次和内层单件重复使用真实循环，未通过展开批次、空行或无用动作凑行数。

`layered/` 包含共享的 L1/L2、八个 L3 和九个 L4。九个 L4 分别选择配方索引；全部配方复用同一组子过程。

| 内容 | 行数 |
| --- | ---: |
| 九份原始 JSON | 24228 |
| L1：13 个原子动作 | 187 |
| L2：13 个节点 | 155 |
| L3：8 个共享子过程 | 799 |
| L4：9 个配方入口 | 126 |
| 设备端口与地址表 | 57 |
| 生产点位、配方和初始变量表 | 439 |

L1–L4 合计 1267 行，连同全部辅助表为 1763 行。公平比较时原始方案也计入相同的 57 行设备表，因此完整规模为 **24285 → 1763 行，减少 92.74%**。全部 JSON 使用相同的四空格缩进。L4 的 126 行不是整个四层方案的规模。

设备表统一给所有 Modbus 设备定义端口和地址。L1 请求只包含 PDU，不包含站地址与 CRC。`SIM_MOTION`、`SIM_PROCESS` 是虚拟端点，没有连接串口硬件。

PR 点位视为已在设备调试环节配置完成。生产流程只触发点号，不写入 PR 位置、速度等设置。`move_production_point` 接收生产点位索引，通过 `production_points` 数组取得各轴点号；只触发点号变化的定位轴。Y 变化时依次触发 Y1、Y2。Infeed、Closure、Outfeed 是重复工艺动作，每件均触发，不能因为点号未变化而跳过。

八个 L3 为 `trigger_motor`、`move_axis_if_changed`、`refresh_position`、`move_production_point`、`poll_sensor_until_ready`、`produce_one_container`、`produce_batch`、`run_recipe`。轮询、等待和超时处理同样由共享子过程完成。正常执行覆盖所有 L1、L2、L3，没有未使用模板。

## 验证及结论范围

Python 的两个解释器独立读取保存的两种配置，共用一个确定性的虚拟设备模型。模型包含电机和灌装延迟，轮询会实际重复执行；虚拟时间不需要等量的现实等待。

九种配方的带时间戳轨迹逐条一致：共 68697 个事件，其中 35478 次 Modbus 请求/响应交换；包括端口、站地址、PDU、响应、等待、位置缓存、页面坐标更新及成品记录。内部临时变量赋值不作为外部轨迹事件。全部流程结束时完成 108 件，设备任务已结束，夹爪和输出线圈关闭。

额外故障场景：进料卡住、灌装设备离线、封口卡住。两种配置分别得到相同的 EMPTY_FEED_TIMEOUT、COMM_OFFLINE、CLOSURE_TIMEOUT 及相同轨迹。八项测试还验证删减产量、错误配方、遗漏 Y2、修改轮询间隔、错误寄存器及缓存字节序会被发现。

`reports/cpp_smoke.json` 是现有 C++ 引擎执行九个 L4 的兼容性检查，每份完成 12 件。该检查使用立即就绪的设备响应，不能与 Python 延迟模型的请求次数混用。

这些结果支持本构造案例在指定输入和模型中的轨迹一致性，以及配置复用效果；不等同于真实硬件验证、全部异常路径的行为证明或自动迁移算法的实验成绩。机械坐标、协议寄存器和设备参数均为本模拟案例设定。

## 复现

在仓库根目录（或解压包根目录）运行，Python 只使用标准库：

```text
python scripts/simulate_beverage_suite.py --report examples/beverage_suite/reports/comparison.json --traces traces
python scripts/test_beverage_suite.py
python scripts/render_beverage_suite_report.py --traces traces --output report.html
```

重新生成配置必须指定一个空目录，以免覆盖正在编辑的文件：

```text
python scripts/build_beverage_suite.py --output regenerated
```

C++ 兼容性测试需要 C++17 编译器和 CMake：

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -R beverage_suite
```

交付包附带 Windows 测试程序，可在包根目录运行 `bin/test_beverage_suite.exe examples/beverage_suite`。该程序只模拟响应，不连接物理设备。交付包中的 `traces/` 保存九个配方各自的 original/layered 完整 JSONL 轨迹，共 18 份。
