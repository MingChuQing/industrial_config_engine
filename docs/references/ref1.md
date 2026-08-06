================================================================================
LLM4PLC: Harnessing Large Language Models for Verifiable Programming of PLCs
in Industrial Control Systems
================================================================================

【引用信息】
M. Fakih, R. Dharmaji, Y. Moghaddas, G. Quiros, O. Ogundare, and M. Al Faruque,
"LLM4PLC: Harnessing large language models for verifiable programming of PLCs
in industrial control systems,"
in Proceedings of the 46th International Conference on Software Engineering:
Software Engineering in Practice (ICSE-SEIP '24), pp. 192–203, 2024.

【DOI】
10.1145/3639477.3639743

【下载地址】
官方页面：https://its.uci.edu/research_products/conference-paper-llm4plc-harnessing-large-language-models-for-verifiable-programming-of-plcs-in-industrial-control-systems/
ACM页面：https://dl.acm.org/doi/10.1145/3639477.3639743
ACM PDF：https://dl.acm.org/doi/epdf/10.1145/3639477.3639743

【摘要】
Although Large Language Models (LLMs) have established predominance in automated
code generation, they are not devoid of shortcomings. The pertinent issues
primarily relate to the absence of execution guarantees for generated code,
a lack of explainability, and suboptimal support for essential but niche
programming languages. State-of-the-art LLMs such as GPT-4 and LLaMa2 fail to
produce valid programs for Industrial Control Systems (ICS) operated by
Programmable Logic Controllers (PLCs). We propose LLM4PLC, a user-guided
iterative pipeline leveraging user feedback and external verification tools —
including grammar checkers, compilers and SMV verifiers — to guide the LLM's
generation. We further enhance the generation potential of LLM by employing
Prompt Engineering and model fine-tuning through the creation and usage of LoRAs.
We validate this system using a FischerTechnik Manufacturing TestBed (MFTB),
illustrating how LLMs can evolve from generating structurally-flawed code to
producing verifiably correct programs for industrial applications. We run a
complete test suite on GPT-3.5, GPT-4, Code Llama-7B, a fine-tuned Code Llama-7B
model, Code Llama-34B, and a fine-tuned Code Llama-34B model.

【核心数据】
✅ 生成成功率：47% → 72%（提升25个百分点）
✅ 代码质量（专家评分）：2.25/10 → 7.75/10
✅ 方法：用户引导迭代管道 + 外部验证工具（语法检查器、编译器、SMV验证器）
✅ 验证平台：FischerTechnik Manufacturing TestBed (MFTB)

【与我论文的关联】
LLM4PLC 通过验证闭环提升LLM生成PLC代码的可靠性，但验证发生在代码生成之后。
本工作的核心差异在于将验证前移到配置生成阶段，通过分层隔离使LLM的输出空间
从完整PLC程序压缩为结构化流程编排配置，以"设计时正确性"替代"运行时修正"。