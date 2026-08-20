Complete Directory Structure Overview

industrial_config_engine/
└── examples/
    └── config_examples/
        ├── L1_action/
        │   └── leak_test/
        │       └── all_actions.json              # 19 Actions aggregated
        ├── L2_node/
        │   └── leak_test/
        │       └── all_nodes.json                # 24 Nodes aggregated
        ├── L3_group/
        │   └── leak_test/
        │       ├── prepare_stage.group.json      # Preparation stage
        │       ├── fill_stage.group.json         # Filling stage
        │       ├── dwell_stage.group.json        # Dwell (pressure-hold) stage
        │       ├── test_stage.group.json         # Test stage
        │       ├── judgment_stage.group.json     # Judgment stage
        │       ├── end_stage.group.json          # End stage
        │       └── complete_leak_test.group.json # Complete flow
        └── L4_flow/
            └── leak_test/
                └── production_flow.json          # Production flow



Run Verification
1. Load L1 Actions
bash
./demo_l1_action_loader examples/example3/L1_action/leak_test/all_actions.json
2. Load L2 Nodes
bash
./demo_l2_node_loader examples/example3/L2_node/leak_test/all_nodes.json
3. Load L3 Groups
bash
./demo_l3_group_loader examples/example3/L3_group/leak_test/complete_leak_test.group.json --level=action --fill=true
4. Run the L4 Flow
bash
./demo_l4_flow_loader examples/example3/L4_flow/leak_test/production_flow.json
