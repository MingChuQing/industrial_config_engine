完整目录结构总览

industrial_config_engine/
└── examples/
    └── config_examples/
        ├── L1_action/
        │   └── leak_test/
        │       └── all_actions.json              # 19个Action聚合
        ├── L2_node/
        │   └── leak_test/
        │       └── all_nodes.json                # 24个Node聚合
        ├── L3_group/
        │   └── leak_test/
        │       ├── prepare_stage.group.json      # 准备阶段
        │       ├── fill_stage.group.json         # 充气阶段
        │       ├── dwell_stage.group.json        # 保压阶段
        │       ├── test_stage.group.json         # 检测阶段
        │       ├── judgment_stage.group.json     # 判断阶段
        │       ├── end_stage.group.json          # 结束阶段
        │       └── complete_leak_test.group.json # 完整流程
        └── L4_flow/
            └── leak_test/
                └── production_flow.json          # 生产流程
				
				
				
运行验证
1. 加载 L1 Action
bash
./demo_l1_action_loader examples/example3/L1_action/leak_test/all_actions.json
2. 加载 L2 Node
bash
./demo_l2_node_loader examples/example3/L2_node/leak_test/all_nodes.json
3. 加载 L3 Group
bash
./demo_l3_group_loader examples/example3/L3_group/leak_test/complete_leak_test.group.json --level=action --fill=true
4. 运行 L4 Flow
bash
./demo_l4_flow_loader examples/example3/L4_flow/leak_test/production_flow.json				