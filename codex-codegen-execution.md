# Codex Code Generation Execution Guide

本文档用于在新的 VS Code Codex 对话中指导固件代码生成。目标是让 Codex 基于当前 Keil5 STM32F103C8T6 工程、硬件资料和项目规格，按阶段生成可编译、可检查、可逐步验证的固件代码。

不要把本文档当成规格源头。权威需求仍以以下文件为准：

- `AGENTS.md`
- `requirements.md`
- `pinmap.md`
- `bringup-checklist.md`
- `docs/superpowers/specs/2026-05-04-aquarium-lift-wiring-design.md`
- `Materials/UM244 使用手册V1.1.pdf`
- `Materials/42HSC1409-250NE2.pdf`
- `Materials/WF5805F 2Bar Datasheet V1.0.pdf`
- `Materials/限位器接线.png`
- `Materials/4-1 OLED显示屏.jpg`
- `Materials/3-有源蜂鸣器/有源蜂鸣器模块原理图.png`
- `Reference/WF5805_2BAR官方驱动包`

## 1. 新对话启动提示词

在新的 Codex 对话中，第一条消息建议直接复制以下内容：

```text
请先不要写代码。

这是一个 Keil5 STM32F103C8T6 标准外设库工程。请先阅读项目根目录的 AGENTS.md，然后按 AGENTS.md 要求继续阅读：

- requirements.md
- pinmap.md
- bringup-checklist.md
- codex-codegen-execution.md
- docs/superpowers/specs/2026-05-04-aquarium-lift-wiring-design.md
- Materials/UM244 使用手册V1.1.pdf
- Materials/42HSC1409-250NE2.pdf
- Materials/WF5805F 2Bar Datasheet V1.0.pdf
- Materials/限位器接线.png
- Materials/4-1 OLED显示屏.jpg
- Materials/3-有源蜂鸣器/有源蜂鸣器模块原理图.png
- Reference/WF5805_2BAR官方驱动包

阅读后请先总结：
1. 当前硬件连接假设
2. 推荐的软件模块划分
3. 需要新增/修改哪些 .c/.h 文件
4. 哪些目录和文件不能修改
5. 是否发现规格文档、引脚分配或硬件资料之间有冲突
6. 计划如何按阶段实现和验证

总结完成后先等待我确认，不要马上开始写代码。
```

用户确认总结无误后，再发送：

```text
确认。请按 codex-codegen-execution.md 的阶段流程开始实现。

每个阶段必须：
1. 先说明本阶段目标、输入文档和要新增/修改的文件。
2. 只实现本阶段功能，不提前写后续阶段的大量逻辑。
3. 遵守 AGENTS.md 的禁止修改规则。
4. 完成后尝试构建 Keil 工程；如果无法构建，说明具体原因。
5. 汇报新增/修改文件、主要接口、与 pinmap.md 的对应关系、验证结果和下一阶段风险。
6. 阶段结束后停下来等待我确认，再进入下一阶段。
```

## 2. 全局工程规则

Codex 必须遵守：

- 使用 STM32 标准外设库，不使用 HAL。
- 保持 Keil C 兼容；Keil 工程已启用 C99，但代码采用保守 C 风格。
- 不使用动态内存、变长数组、复合字面量。
- 新代码优先放在 `Hardware/`。
- 主流程只允许修改 `user/main.c`。
- 不修改 `Objects/`、`Listings/`、`DebugConfig/`、`Library/`、`start/`、`system/`、`*.uvguix`。
- 只有为了加入新增源文件时，才允许修改 `project1.uvprojx`。
- 不提交或修改 `project1.uvguix.ukpkmkk`。
- 每个模块一个 `.c/.h` 文件。
- 硬件引脚、有效电平、默认参数集中放在板级配置头文件。
- 中断服务函数只做计数、置标志或紧急停机，不放复杂业务逻辑。
- 任何安全冲突优先停机，不尝试继续自动运行。

## 3. 阶段执行总原则

每个阶段都按这个顺序执行：

1. **阶段输入确认**：重读相关规格章节和已有代码。
2. **阶段设计摘要**：说明本阶段模块边界、函数接口和文件列表。
3. **实现**：只改本阶段需要的文件。
4. **工程集成**：如新增 `.c` 文件，加入 `project1.uvprojx`。
5. **构建验证**：优先使用可用 Keil 构建方式；无法构建时说明缺失工具或原因。
6. **阶段报告**：按固定格式汇报。
7. **等待确认**：用户确认后再进入下一阶段。

阶段内禁止事项：

- 禁止一次性生成所有模块。
- 禁止为后续阶段写大量未调用代码。
- 禁止在不理解 WF5805F 数据格式时猜测驱动实现。
- 禁止绕过限位保护。
- 禁止为了“让编译过”删除安全逻辑。
- 禁止改动与本阶段无关的已有文件。

## 4. 阶段 0：工程阅读与实施计划

目标：让 Codex 建立工程上下文，不写代码。

必须阅读：

- `AGENTS.md`
- `requirements.md`
- `pinmap.md`
- `codex-codegen-execution.md`
- 主规格文档
- `user/main.c`
- `Hardware/OLED.c/.h`
- `Hardware/Key.c/.h`
- `Hardware/LED.c/.h`
- `system/Delay.c/.h`
- `project1.uvprojx`
- `Reference/WF5805_2BAR官方驱动包`

输出：

- 现有工程结构摘要
- 已有模块可复用点
- 待新增模块列表
- 预计需要修改的文件列表
- 构建方式判断
- 潜在冲突或缺失资料

阶段 0 不允许修改文件。

## 5. 阶段 1：板级配置、蜂鸣器、限位、按键事件

目标：建立基础 GPIO 层和安全输入，不接入复杂主状态机。

建议新增文件：

- `Hardware/board_config.h`
- `Hardware/buzzer.c`
- `Hardware/buzzer.h`
- `Hardware/limit.c`
- `Hardware/limit.h`
- `Hardware/key_scan.c`
- `Hardware/key_scan.h`

允许修改：

- `user/main.c`：用于最小初始化和测试显示。
- `project1.uvprojx`：仅加入新增 `.c` 文件。

必须实现：

- `PA5` 蜂鸣器，低电平响，高电平关闭，初始化默认关闭。
- `PB12/PB13/PB14/PB15` 四个限位输入，低有效。
- 上限位、下限位、左右一致性查询接口。
- `PB1/PB11/PB10/PA7` 按键扫描。
- 短按、长按、3 秒维护长按事件。
- 所有引脚和有效电平宏集中放在 `board_config.h`。

阶段检查：

- `pinmap.md` 中所有阶段 1 引脚都有对应宏。
- 蜂鸣器初始化不会上电误鸣。
- 限位触发逻辑为低有效。
- 按键接口不阻塞主循环。
- 新增 `.c` 文件已加入 Keil 工程。

阶段报告格式：

```text
阶段 1 完成报告：
- 新增/修改文件：
- 主要接口：
- 引脚对应关系：
- 构建结果：
- 未验证硬件项：
- 下一阶段风险：
```

## 6. 阶段 2：软件 I2C 与 WF5805F 驱动

目标：实现三组软件 I2C 和 WF5805F 原始压力读取。

建议新增文件：

- `Hardware/soft_i2c.c`
- `Hardware/soft_i2c.h`
- `Hardware/wf5805f.c`
- `Hardware/wf5805f.h`

必须先做：

- 阅读 `WF5805F 2Bar Datasheet V1.0.pdf` 和 `Reference/WF5805_2BAR官方驱动包`。
- 摘录 I2C 地址、读写命令、返回字节格式、状态位、压力换算公式。
- 明确官方驱动 `WFSensorIICDevice 0XDA` 是 WF5805F 的 8-bit 写地址，对应固件内部 7-bit 地址 `0x6D`。
- 如果 PDF 信息不完整，停止并向用户索要资料，不允许猜测。

必须实现：

- I2C-A：`PB8/PB9`，OLED、`P_air`。
- I2C-B：`PB6/PB7`，`P_basket`。
- I2C-C：`PB0/PB5`，`P_tank`。
- 软件 I2C 起步速率约 100kHz。
- 7-bit 地址内部表示。
- 三颗 WF5805F 固定地址相同，不允许任意两颗挂在同一条 I2C 总线上。
- 每颗传感器独立读数接口。
- 读数失败计数接口。
- 总线恢复或重新初始化接口。

阶段检查：

- 不破坏现有 OLED 软件 I2C 使用方式，或明确迁移方案。
- WF5805F 固定地址 `0x6D` 和 OLED `0x3C` 不冲突。
- 每条 I2C 总线上最多一颗 WF5805F。
- 返回压力单位统一为 `pressure_hpa_x100`。
- 读数失败时返回错误码，不返回伪造有效值。

## 7. 阶段 3：水深计算、滤波、传感器健康监测

目标：把压力读数转换为水深和水位/传感器错误。

建议新增文件：

- `Hardware/water_depth.c`
- `Hardware/water_depth.h`
- `Hardware/error_code.h`
- `Hardware/error_manager.c`
- `Hardware/error_manager.h`

必须实现：

- `basket_depth_mm_x10 = (P_basket - P_air)` 换算。
- `tank_depth_mm_x10 = (P_tank - P_air)` 换算。
- 最近 5 次有效读数滑动平均。
- `tank_min_depth_mm = 250`
- `tank_max_depth_mm = 450`
- `basket_min_safe_depth_mm = 5`
- `basket_max_safe_depth_mm = 120`
- 水位突变 `10mm/min`。
- 单传感器连续 5 次失败报警。
- 单 I2C 总线恢复 5 次失败报警。
- 压力差对应水深小于 `-2mm` 判为物理异常。
- 固定错误码表。

阶段检查：

- 所有水深单位清楚，不混用 `mm`、`mm_x10`。
- 错误码名称与主规格文档一致。
- 蜂鸣器静音不清除错误码。
- 严重故障能被主循环查询。

## 8. 阶段 4：UM244 步进控制与限位急停

目标：实现可控、有限脉冲的运动底层。

建议新增文件：

- `Hardware/stepper_um244.c`
- `Hardware/stepper_um244.h`

必须实现：

- `PA0` 输出 STEP。
- `PA3` 输出 DIR。
- `PA4` 输出 MF/电机释放。
- 800 pulse/mm 换算。
- 自动脉冲频率默认 20Hz，可配置 20-50Hz。
- 手动速度 1mm/s = 800 pulse/s。
- 回零速度默认 0.5mm/s = 400 pulse/s。
- 有限脉冲输出接口，例如发送 N 个 pulse 后自动停止。
- 每个运动命令前检查限位。
- 运动中触发方向对应限位，立即停止。
- 默认不释放电机。

阶段检查：

- `DIR` 高低电平必须通过配置宏可反转。
- 不在自动模式释放 `MF`。
- STEP 输出不是无限循环。
- 限位保护在底层强制执行，不依赖上层自觉。

## 9. 阶段 5：机械回零与位置跟踪

目标：建立机械绝对零点和位置可信度。

建议新增文件：

- `Hardware/homing.c`
- `Hardware/homing.h`
- `Hardware/position_tracker.c`
- `Hardware/position_tracker.h`

必须实现：

- `basket_position_mm = 0` 表示框篮最低位。
- 向上为正，最大行程 100mm。
- 位置内部可用 pulse 保存。
- 维护模式下回零流程：
  1. 低速向下。
  2. 下限位触发停止。
  3. 上升 1mm 释放限位。
  4. 再低速下降到下限位。
  5. 记为 `0mm`。
- 位置可信标志。
- 电机释放、严重卡滞、限位矛盾后位置变为不可信。

阶段检查：

- 未回零或位置不可信时不允许自动运行。
- 回零不在开机时自动执行。
- 回零中仍不能忽略限位异常。

## 10. 阶段 6：Flash 参数与恢复状态保存

目标：实现断电恢复所需的参数持久化。

建议新增文件：

- `Hardware/param_store.c`
- `Hardware/param_store.h`
- `Hardware/crc16.c`
- `Hardware/crc16.h`

必须实现：

- 使用最后两个 1KB Flash 页：
  - A：`0x0800F800`
  - B：`0x0800FC00`
- Keil IROM 需要预留最后 2KB：代码区大小建议 `0x0000F800`。
- A/B 双页备份。
- `magic/version/seq/crc16`。
- 默认参数恢复。
- 参数修改后保存。
- 运行状态最多每 10 分钟保存一次。
- 暂停、故障、维护、电机释放等关键状态切换前保存。
- 不允许每次 8 pulse 都写 Flash。

阶段检查：

- Flash 地址没有和代码区重叠。
- 断电写入中断时至少保留一个有效记录。
- 两页都无效时使用默认值并置 `W_PARAM_DEFAULT`。
- 写入频率符合规格。

## 11. 阶段 7：OLED 页面与菜单交互

目标：让用户能查看状态、设置参数、进入维护模式和处理报警。

建议新增文件：

- `Hardware/ui_pages.c`
- `Hardware/ui_pages.h`
- `Hardware/menu.c`
- `Hardware/menu.h`

允许复用：

- `Hardware/OLED.c`
- `Hardware/OLED.h`

必须实现页面：

- 主页面
- 传感器页面
- 限位页面
- 参数页面
- 手动页面
- 报警页面
- 维护页面

必须实现交互：

- `PB10` 暂停/确认/报警静音。
- `PB10` 长按 3 秒进入维护模式确认页。
- `PA7` 页面切换/取消。
- `PB1/PB11` 参数减/加或手动下降/上升。
- 手动移动必须长按，松手停止。
- 参数越界拒绝保存。

阶段检查：

- OLED 显示错误码和错误原因。
- 蜂鸣器静音状态可显示。
- 维护模式不能绕过限位。
- 参数页面不会写入非法参数。

## 12. 阶段 8：主状态机、自检、断电恢复、自动打盹

目标：把各底层模块接入完整应用逻辑。

建议新增文件：

- `Hardware/app_state.c`
- `Hardware/app_state.h`
- `Hardware/self_test.c`
- `Hardware/self_test.h`
- `Hardware/nap_scheduler.c`
- `Hardware/nap_scheduler.h`

必须实现状态：

- `APP_SELF_TEST`
- `APP_PAUSED`
- `APP_AUTO_RUN`
- `APP_NAP_WAIT`
- `APP_NAP_MOVE`
- `APP_MANUAL`
- `APP_MAINTENANCE`
- `APP_MOTOR_RELEASE`
- `APP_FAULT`

必须实现自检：

- GPIO、软件 I2C、OLED、蜂鸣器、限位、步进、Flash 初始化。
- 蜂鸣器短鸣一次。
- 传感器稳定等待 10 秒。
- 三颗 WF5805F 读取正常。
- 四个限位状态没有物理矛盾。
- 水深处于合理范围。
- 参数处于允许范围。

必须实现断电恢复：

- 无 RTC。
- 只按上电运行时间累计运行天数和每日进度。
- 断电期间不计时、不补偿、不追赶。
- 上次为自动相关状态、自检通过、水深差异不超过 3mm、位置可信时自动恢复。
- 上次为暂停则恢复暂停。
- 上次为维护、电机释放、故障则不自动运行。

必须实现自动打盹：

- 默认 1mm/day。
- 默认 8 pulse/次。
- 默认约 14.4min 间隔。
- 每次运动前后读传感器和限位。
- 每次发送有限脉冲后停止。
- 水深误差超过 ±1mm 时暂停报警。
- 累计 1mm 后做卡滞趋势检查。
- 连续 8 次卡滞嫌疑后 `E_STALL`。

阶段检查：

- 安全故障优先级高于自动和手动。
- `APP_NAP_MOVE` 不会无限输出 STEP。
- 故障清除后进入暂停，不直接自动运行。
- 蜂鸣器静音不清除故障。
- 断电恢复逻辑符合 requirements.md。

## 13. 阶段 9：工程集成、构建、修正

目标：把所有新增源文件纳入 Keil 工程并编译通过。

必须检查：

- 所有新增 `.c` 文件已加入 `project1.uvprojx`。
- `project1.uvguix.ukpkmkk` 未修改。
- `Objects/`、`Listings/`、`DebugConfig/` 未修改或未提交。
- include path 不破坏原工程。
- C99 设置保持不变。
- IROM 范围已按 Flash 参数区要求预留最后 2KB。

构建要求：

- 优先使用可用的 Keil 命令行构建方式。
- 若本机无法命令行构建，说明缺少 `UV4.exe`、环境变量或授权。
- 不能在未构建成功时声称固件已验证。

阶段报告：

```text
阶段 9 完成报告：
- 构建命令：
- 构建结果：
- 错误/警告数量：
- 已修正问题：
- 未解决问题：
- 是否需要用户打开 Keil 手动确认：
```

## 14. 每阶段固定检查清单

每个阶段结束时，Codex 必须逐项报告：

```text
阶段 N 检查清单：
- 是否只修改了本阶段允许的文件：
- 是否新增了 .c/.h 文件：
- 是否更新 project1.uvprojx：
- 是否误改 project1.uvguix.ukpkmkk：
- 是否误改 Objects/Listings/DebugConfig/Library/start/system：
- 是否与 pinmap.md 一致：
- 是否与 requirements.md 一致：
- 是否存在占位项：
- 是否尝试构建：
- 构建是否通过：
- 若未通过，下一步需要什么：
```

## 15. 失败处理规则

如果出现编译错误：

1. 先读取完整错误信息。
2. 按文件和行号定位。
3. 优先修复当前阶段新增代码。
4. 不要通过删除功能绕过错误。
5. 修复后重新构建。
6. 同一问题连续 3 次修复失败时，停止并向用户说明根因假设。

如果出现资料不明确：

- WF5805F 命令、寄存器、数据格式不明确：停止，向用户索要更清晰资料。
- UM244 STEP/DIR/MF 有效逻辑不明确：保留配置宏，要求硬件实测。
- Keil 工程结构不明确：先总结工程文件，不直接大改。
- 引脚冲突：停止，询问用户，不擅自换引脚。

## 16. 用户确认关卡

以下节点必须等待用户确认：

- 阶段 0 总结之后。
- 阶段 2 WF5805F 数据格式摘要之后。
- 阶段 4 第一次定义电机方向宏之后。
- 阶段 6 修改 Keil IROM 或 Flash 参数区之前。
- 阶段 8 主状态机接入自动运动之前。
- 阶段 9 构建通过或无法构建之后。

## 17. 最终交付标准

最终交付不能只说“代码写好了”。必须包含：

- 新增/修改文件完整列表。
- 每个模块职责摘要。
- 引脚和有效电平摘要。
- 状态机摘要。
- 错误码摘要。
- Flash 地址和保存策略摘要。
- 构建命令和构建结果。
- 未在真实硬件上验证的项目。
- 建议按 `bringup-checklist.md` 执行的硬件调试顺序。

如果没有完成构建，最终结论必须写成“代码已生成但未完成构建验证”，不能写“固件已验证”。
