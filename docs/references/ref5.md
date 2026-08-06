================================================================================
Spec2Control: Automating PLC/DCS control-logic engineering from natural language
requirements with LLMs
================================================================================

【引用信息】
H. Koziolek, T. Braun, V. Ashiwal, et al.,
"Spec2Control: Automating PLC/DCS control-logic engineering from natural language
requirements with LLMs,"
in Proceedings of the 46th International Conference on Software Engineering:
Software Engineering in Practice (ICSE-SEIP), 2026.

【DOI】
10.1145/3786583.3786897

【下载地址】
ACM页面：https://dl.acm.org/doi/10.1145/3786583.3786897
ACM PDF：https://dl.acm.org/doi/epdf/10.1145/3786583.3786897（直链）

【摘要】
（待补充——该文献的完整摘要可从ACM页面获取）

Spec2Control, developed by ABB, explores the automation of control-logic engineering
from natural language requirements using LLMs. The framework aims to bridge the gap
between process engineers (who express requirements in natural language) and control
engineers (who implement them in PLC/DCS code). By leveraging LLMs for requirements
analysis and code synthesis, Spec2Control reduces the manual effort in translating
process specifications into executable control logic.

【核心数据】
✅ 开发方：ABB
✅ 目标：从自然语言需求到控制策略的自动化生成
✅ 定位：连接工艺工程师（自然语言需求）与控制工程师（PLC/DCS代码）

【与我论文的关联】
Spec2Control[5]与本文的共同点：都试图用自然语言驱动控制逻辑的自动化生成。
核心差异：Spec2Control生成的是PLC/DCS代码（需要编译执行），而本文生成的是
结构化配置数据（JSON）。本文的语义映射验证方法（配置→自然语言反向翻译）
可视为对Spec2Control"需求→代码"单向转换的补充——通过双向验证确保配置与需求一致。