================================================================================
Safe integration of Large Language Models into industrial process control:
a multi-agent architecture with P&ID-grounded validation
================================================================================

【引用信息】
D. Schall,
"Safe integration of Large Language Models into industrial process control:
a multi-agent architecture with P&ID-grounded validation,"
Autonomous Intelligent Systems, vol. 6, article no. 14, 2026.

【DOI】
10.1007/s43684-026-00136-1

【下载地址】
Springer页面：https://link.springer.com/article/10.1007/s43684-026-00136-1
（Open Access，可直接下载PDF）

【摘要】
Large Language Models (LLMs) offer powerful reasoning capabilities for industrial
process control, yet their non-deterministic nature, susceptibility to hallucination,
and lack of intrinsic physical understanding make direct deployment in safety-critical
environments unacceptable. This paper addresses five research questions on safely
integrating LLM-based reasoning into industrial process automation through the
Autonomous Action Execution (AAE) framework. For safe architectural integration (RQ1),
we present a four-layer multi-agent architecture that confines LLM inference to an
observation-only Monitor layer while safety-critical decisions are made by deterministic
Verification and Execution agents. For structuring heterogeneous plant data (RQ2),
we introduce a text-level aggregation framework with pluggable analyzers that transforms
SCADA states, time-series measurements, Piping and Instrumentation Diagrams (P&IDs),
and Standard Operating Procedures (SOPs) into contextually rich documents for LLM
consumption. For automated validation (RQ3), a P&ID-grounded method uses graph traversal
over the P&ID topology to verify physical consistency of LLM-generated proposals,
checking tag existence, actuatability, fail-state consistency, and downstream impact.
For quantifiable context enrichment (RQ4), a graduated baseline comparison (B0–B3)
demonstrates the incremental value of each pipeline component. For cross-domain
generalisability (RQ5), evaluation across five industrial scenarios—three derived from
the Tennessee Eastman Process (TEP) benchmark plus two retained scenarios—demonstrates
portability of the framework across continuous and batch processes.

【核心数据】
✅ 四层多智能体架构：Monitor（观察）→ Orchestrator（汇总）→ Verification（验证）→ Execution（执行）
✅ 验证层：6类P&ID结构检查（标签存在性、可执行性、SCADA可控性、回路关联性、故障状态一致性、下游影响）
✅ 错误注入测试（N=43）：零漏报（TPR=1.000），保守型设计（FPR≈0.75）
✅ 统计鲁棒性测试（N=50次LLM运行）：不安全动作发生率10%~70%，验证层100%拦截覆盖类别内的违规

【与我论文的关联】
AAE框架通过P&ID图遍历验证LLM生成的动作提案的物理一致性，与本文的三维语义验证
（结构校验+语义映射+仿真运行）在理念上高度一致——都是用"确定性验证"来约束
"非确定性LLM输出"。本文的语义映射方法可视为对AAE验证层的补充：不仅做结构检查，
还通过"配置→自然语言反向翻译"实现人与需求的双向对照。