================================================================================
An experiment of using a large language model to control a water tank system
================================================================================

【引用信息】
H. Wen, J. Roberts, A. Zaidi, and A. McLeod,
"An experiment of using a large language model to control a water tank system,"
Computers & Chemical Engineering, vol. 211, article no. 109656, 2026.

【DOI】
10.1016/j.compchemeng.2026.109656

【下载地址】
ScienceDirect页面：https://www.sciencedirect.com/science/article/pii/S009813542630122X
（需要机构订阅或购买）

【摘要】
Large language models (LLMs) have been proposed as adaptive agents in control
systems, yet their performance in closed-loop process control remains insufficiently
quantified. This study experimentally evaluates an LLM-assisted supervisory
proportional–integral–derivative (PID) control framework using a nonlinear water-tank
benchmark implemented in a MATLAB/Simulink–Python co-simulation environment. The LLM
operates as a supervisory tuner, updating PID gains every 0.5 s to account for
inference latency (~0.3 s), rather than replacing the feedback controller. Four
configurations are compared: conventional PID, autotuned PID, direct LLM control,
and a hybrid LLM–PID controller. Under nominal setpoint tracking to 10 m, the hybrid
controller reduces rise time from 6.5 s (conventional PID) and 4.5 s (autotuned PID)
to approximately 3.5 s, with overshoot limited to about 2% and negligible steady-state
error. The direct LLM controller fails to achieve regulation, yielding a steady-state
error of approximately 8.5 m. Under measurement bias, the hybrid controller responds
fastest but with slightly higher control variability, whereas sustained measurement
drift leads to increased variability due to irregular gain updates.

【核心数据】
✅ 混合LLM-PID控制器：上升时间 6.5s → 3.5s，超调约2%
❌ 直接LLM控制器：稳态误差约8.5m，无法实现稳定调节
✅ 方法：LLM作为监督调谐器（每0.5秒更新PID增益），而非替代反馈控制器
✅ 代码公开：https://github.com/JTRIII/water_tank_system_experiment

【与我论文的关联】
本文直接引用了该实验作为"LLM非确定性本质在安全关键环境中不可接受"[3]的经验证据。
本文的核心差异在于：Wen等将LLM用于数值控制信号的生成，而本文让LLM生成结构化配置数据——
输出空间从连续数值压缩为离散结构，使验证以静态分析为主，而非依赖运行时迭代修正。