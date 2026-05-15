# Codex Code Generation Execution Guide

本文档用于在新的 VS Code Codex 对话中指导固件代码生成。目标是让 Codex 基于当前 Keil5 STM32F103C8T6 工程、硬件资料和项目规格，按阶段生成可编译、可检查、可逐步验证的固件代码。

不要把本文档当成规格源头。权威需求仍以以下文件为准：

- `AGENTS.md`
- `skill.md`
- `requirements.md`
- `pinmap.md`
- `bringup-checklist.md`
- `Auto-lift-wiring-design.md`
- `Materials/UM244 使用手册V1.1.pdf`
- `Materials/42HSC1409-250NE2.pdf`
- `Materials/WF5805F 2Bar Datasheet V1.0.pdf`
- `Materials/STM32F103C8T6核心板原理图.pdf`
- `Materials/STM32F103x8B_DS_CH_V10.pdf`
- `Materials/STM32F10xxx参考手册（英文）.pdf`
- `Materials/STM32F103xx固件函数库用户手册.pdf`
- `Materials/ST-LINK+V2使用说明.pdf`
- `Materials/STM32F103C8T6引脚定义.xlsx`
- `Materials/最小系统板.png`
- `Materials/限位器接线.png`
- `Materials/npn型光耦隔离器-用于限位器信号输入.jpg`
- `Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg`
- `Materials/光耦隔离器原理图.jpg`
- `Materials/npn型光耦隔离器-用于给步进电机驱动器的拉低信号转换.jpg`
- `Materials/0.96寸4针B版本结构图.pdf`
- `Materials/0.96寸OLED规格书.pdf`
- `Materials/4-1 OLED显示屏.jpg`
- `Materials/中景园电子0.96OLED显示屏IIC接口原理图.pdf.pdf`
- `Materials/中景园电子0.96OLED显示屏_驱动芯片手册.pdf`
- `Materials/3-有源蜂鸣器/有源蜂鸣器模块原理图.png`
- `Materials/3-有源蜂鸣器/有源蜂鸣器模块实物图.png`
- `Reference/WF5805_2BAR官方驱动包`
- `Reference/步进电机驱动示例`

## 项目结构总览

本项目是 Keil5 STM32F103C8T6 标准外设库工程。代码生成时应把“已有工程结构”和“计划新增模块结构”分开理解：已有库、启动文件和系统延时文件保持稳定；新业务代码主要进入 `Hardware/`，主循环入口只收敛到 `user/main.c`。

当前项目结构：

| 路径                         | 作用                                                  | 代码生成策略                                 |
| ---------------------------- | ----------------------------------------------------- | -------------------------------------------- |
| `AGENTS.md`                  | Codex 工程级规则                                      | 必读，不作为业务代码修改对象                 |
| `skill.md`                   | 编码行为准则                                          | 写代码前必读，约束假设、简化、最小改动和验证 |
| `requirements.md`            | 固件需求摘要                                          | 作为快速需求入口                             |
| `pinmap.md`                  | 引脚、有效电平、I2C 地址摘要                          | 作为写 GPIO 和驱动前的硬件核对表             |
| `bringup-checklist.md`       | 硬件上电调试清单                                      | 作为硬件验证顺序                             |
| `codex-codegen-execution.md` | 分阶段代码生成执行手册                                | 作为新对话执行入口                           |
| `Auto-lift-wiring-design.md` | 主规格文档                                            | 权威系统行为和硬件方案                       |
| `Materials/`                 | 外设资料、PDF、接线图                                 | 写驱动前必须查阅                             |
| `Reference/`                 | 传感器、OLED、按键、蜂鸣器、步进/定时器参考代码包     | 只作参考，不直接整包复制                     |
| `Hardware/`                  | 现有 OLED、Key、LED、水相关代码；后续主要新增模块位置 | 允许新增 `.c/.h`，谨慎修改已有文件           |
| `user/main.c`                | 固件主入口                                            | 允许最小化修改，用于初始化和调用状态机       |
| `user/stm32f10x_it.c/.h`     | 中断入口                                              | 原则上不放复杂业务逻辑                       |
| `system/`                    | 延时等基础系统代码                                    | 不修改                                       |

参考代码使用规则：

- `Reference/WF5805_2BAR官方驱动包` 必须用于确认 WF5805F I2C 地址、命令、返回字节和压力/温度换算公式。
- `Reference/步进电机驱动示例` 可参考标准外设库 GPIO、TIM、PWM、OLED、Key、Buzzer 的初始化写法。
- 示例代码中使用 `Delay_ms`、`Delay_us`、忙等按键释放、循环翻转 GPIO 发脉冲的实现只能作为反例或临时理解材料，不能直接复制到本项目新模块。
- 示例工程缺少完整 Keil 环境文件时，不要求编译示例；只读取 `Hardware/` 和 `User/main.c` 中与当前阶段相关的实现。
| `Library/`                              | STM32 标准外设库                                      | 不修改                                          |
| `start/`                                | 启动文件和 CMSIS 基础文件                             | 不修改                                          |
| `Objects/`、`Listings/`、`DebugConfig/` | Keil 生成物和调试配置                                 | 不修改、不提交                                  |
| `project1.uvprojx`                      | Keil 工程文件                                         | 仅在加入新增 `.c` 文件或预留 Flash 参数区时修改 |
| `project1.uvoptx`                       | Keil 选项文件                                         | 谨慎修改，只在构建配置必须调整时处理            |
| `project1.uvguix.ukpkmkk`               | Keil 用户界面布局                                     | 不修改、不提交                                  |

计划固件模块结构：

| 模块         | 建议文件                                                                            | 职责                                             |
| ------------ | ----------------------------------------------------------------------------------- | ------------------------------------------------ |
| 板级配置     | `Hardware/board_config.h`                                                           | 集中定义引脚、有效电平、默认参数、方向反转宏     |
| 蜂鸣器       | `Hardware/buzzer.c/.h`                                                              | `PA0` 低电平触发报警输出                         |
| 限位输入     | `Hardware/limit.c/.h`                                                               | 四个 24V NPN 限位的低有效读取和一致性判断        |
| 按键事件     | `Hardware/key_scan.c/.h`                                                            | 25ms 消抖、短按、长按、3 秒维护入口事件          |
| 软件 I2C     | `Hardware/soft_i2c.c/.h`                                                            | OLED-I2C 加三组 WF5805F 软件 I2C                 |
| WF5805F 驱动 | `Hardware/wf5805f.c/.h`                                                             | 固定地址 `0x6D` 压力传感器读取和错误返回         |
| 水深计算     | `Hardware/water_depth.c/.h`                                                         | 压力差换算、滑动平均、水深单位统一               |
| 错误管理     | `Hardware/error_code.h`、`Hardware/error_manager.c/.h`                              | 固定错误码、严重故障、蜂鸣器静音不清故障         |
| 步进控制     | `Hardware/stepper_um244.c/.h`                                                       | STEP/DIR/MF、有限脉冲、限位急停                  |
| 位置与回零   | `Hardware/position_tracker.c/.h`、`Hardware/homing.c/.h`                            | `basket_position_mm = 0`、位置可信标志、维护回零 |
| 参数存储     | `Hardware/param_store.c/.h`、`Hardware/crc16.c/.h`                                  | Flash A/B 页、CRC、断电恢复状态                  |
| UI 页面      | `Hardware/ui_pages.c/.h`、`Hardware/menu.c/.h`                                      | OLED 页面、参数设置、报警显示、维护页面          |
| 调度与状态机 | `Hardware/nap_scheduler.c/.h`、`Hardware/self_test.c/.h`、`Hardware/app_state.c/.h` | 开机自检、断电恢复、自动打盹、故障/维护状态机    |

运行数据流：

```text
WF5805F/I2C -> water_depth -> error_manager -> app_state
limit/key   -> safety/menu -> app_state
app_state   -> stepper_um244/buzzer/OLED/param_store
```

实现顺序必须从底层硬件抽象到主状态机逐层推进。任何阶段发现引脚冲突、地址冲突、资料不明确或构建失败，都应停在当前阶段说明问题，不继续叠加后续模块。

## 1. 新对话启动提示词

在新的 Codex 对话中，第一条消息建议直接复制以下内容：

```text
请先不要写代码。

这是一个 Keil5 STM32F103C8T6 标准外设库工程。请先阅读项目根目录的 AGENTS.md，然后按 AGENTS.md 要求继续阅读：

- skill.md
- requirements.md
- pinmap.md
- bringup-checklist.md
- codex-codegen-execution.md
- Auto-lift-wiring-design.md
- Materials/UM244 使用手册V1.1.pdf
- Materials/42HSC1409-250NE2.pdf
- Materials/WF5805F 2Bar Datasheet V1.0.pdf
- Materials/STM32F103C8T6核心板原理图.pdf
- Materials/STM32F103x8B_DS_CH_V10.pdf
- Materials/STM32F10xxx参考手册（英文）.pdf
- Materials/STM32F103xx固件函数库用户手册.pdf
- Materials/ST-LINK+V2使用说明.pdf
- Materials/STM32F103C8T6引脚定义.xlsx
- Materials/最小系统板.png
- Materials/限位器接线.png
- Materials/npn型光耦隔离器-用于限位器信号输入.jpg
- Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg
- Materials/光耦隔离器原理图.jpg
- Materials/npn型光耦隔离器-用于给步进电机驱动器的拉低信号转换.jpg
- Materials/0.96寸4针B版本结构图.pdf
- Materials/0.96寸OLED规格书.pdf
- Materials/4-1 OLED显示屏.jpg
- Materials/中景园电子0.96OLED显示屏IIC接口原理图.pdf.pdf
- Materials/中景园电子0.96OLED显示屏_驱动芯片手册.pdf
- Materials/3-有源蜂鸣器/有源蜂鸣器模块原理图.png
- Materials/3-有源蜂鸣器/有源蜂鸣器模块实物图.png
- Reference/WF5805_2BAR官方驱动包
- Reference/步进电机驱动示例

阅读后请先总结：
1. 当前硬件连接假设，包括 24V、5V、3.3V 电源路径和共地关系
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

## 3. 三层分离与非阻塞架构硬性规则

固件必须按“三层分离”实现，不能把安全、驱动和业务状态混在一个长函数里：

| 层级         | 职责                                                               | 禁止事项                         |
| ------------ | ------------------------------------------------------------------ | -------------------------------- |
| 安全底层     | STEP 定时器急停、限位原始 GPIO 直读、严重故障置位                  | 禁止等待 OLED、I2C、菜单或 Flash |
| 驱动服务层   | GPIO、按键、限位滤波、软件 I2C、WF5805F、OLED、Flash、步进有限脉冲 | 禁止无超时等待，禁止吞掉错误     |
| 应用状态机层 | 自检、暂停、自动打盹、维护、故障恢复、参数保存策略                 | 禁止直接操作寄存器绕过驱动层     |

非阻塞规则：

- 新生成代码不得调用 `Delay_ms`、`Delay_us` 或任何 `Delay` 函数实现业务等待。
- 主循环、状态机、菜单、OLED 刷新、打盹间隔、传感器稳定等待、Flash 写入节流都必须使用 SysTick 或定时器产生的时间戳差值调度。
- 任何 `while` 等待外设状态的循环都必须有超时退出；没有超时的等待视为架构错误。
- 打盹间隔必须使用类似 `next_nap_ms = now_ms + nap_interval_ms` 的方式调度，在主循环中比较 `now_ms` 和 `next_nap_ms`，不得循环死等。
- 中断服务函数只允许做短路径动作：读取 GPIO、停止定时器、更新计数、置 `volatile` 标志；复杂错误处理和显示由主循环状态机完成。
- 允许保留已有 `system/Delay.c/.h` 文件，但新业务模块和新驱动模块不得依赖它。

安全底层直通规则：

- 限位输入既要在主循环中滤波轮询，也要在步进 STEP 定时器中断中进行原始 GPIO 急停检查。
- 每次 STEP 有效沿或翻转前，定时器中断必须按当前运动方向直接读取对应上/下限位 GPIO。
- 若当前方向对应限位原始有效，立即停止 STEP 定时器、禁止继续出脉冲，并置位严重故障标志。
- 中断内只置错误标志和停止定时器；错误码整理、OLED 显示、蜂鸣器策略在主循环处理。

I2C 容错规则：

- 软件 I2C 的每个等待 SDA/SCL 状态的步骤都必须有超时。
- 检测到 SDA 被拉低或总线死锁时，驱动必须能重新配置 GPIO，并发送 9 个 SCL 时钟脉冲尝试释放总线。
- 释放总线后发送 STOP，重新初始化该软件 I2C 总线，再按有限次数重试。
- 总线恢复失败达到规格次数后，上报对应 `E_I2C_A_FAIL`、`E_I2C_B_FAIL` 或 `E_I2C_C_FAIL`，系统进入故障或暂停，不能死机。

## 4. 阶段执行总原则

每个阶段都按这个顺序执行：

1. **阶段输入确认**：重读相关规格章节和已有代码。
2. **阶段设计摘要**：说明本阶段模块边界、函数接口和文件列表。
3. **实现**：只改本阶段需要的文件。
4. **工程集成**：如新增 `.c` 文件，加入 `project1.uvprojx`。
5. **构建验证**：优先使用可用 Keil 构建方式；无法构建时说明缺失工具或原因。
6. **阶段报告**：按固定格式汇报。
7. **等待确认**：用户确认后再进入下一阶段。

中文详细注释要求：

- 生成或修改固件代码时必须补充简体中文注释。
- 注释风格参考 `Reference/软件I2C读写MPU6050`：函数前用简短说明块描述用途、参数、返回值和注意事项；关键协议步骤用行内注释说明目的。
- 该参考代码只用于注释风格参考，不得复制其中阻塞 `Delay`、忙等按键释放或循环翻转 GPIO 发脉冲的实现方式。
- 新增 `.h` 公共接口必须注释模块用途、关键 API、参数、返回值和安全假设。
- 新增 `.c` 中非平凡的公开函数应使用中文函数说明块，至少包含功能、参数、返回值；涉及硬件或安全时必须补充注意事项。
- 简单 `static` 辅助函数可以只保留一句关键说明，避免把每一行都写成教程。
- `board_config.h` 中的重要宏必须注释硬件含义、单位和有效范围。
- 状态机必须注释每个状态和主要状态转换条件。
- 中断服务函数必须注释中断内允许执行的动作，以及必须延后到主循环处理的事项。
- 限位停机、I2C 超时、传感器失败、卡滞检测、报警锁存、电机保持/释放等安全逻辑必须有中文注释。
- I2C 起始/停止/ACK、9 脉冲总线恢复、STEP 有限脉冲、DIR 建立/保持、Flash A/B 页切换等协议或时序步骤必须注释关键动作和目的。
- 涉及单位、范围和有效电平时必须在注释中写清，例如 `ms`、`Hz`、`pulse`、`mm`、`mm_x10`、`hPa_x100`、低有效、高有效、阻塞/非阻塞。
- 已生成代码应按阶段先补齐中文注释，再继续叠加下一阶段的大功能。
- 禁止添加无意义注释，例如“变量加一”“调用函数”；优先解释原因、硬件假设、单位和安全约束。

分阶段注释重点：

| 阶段   | 注释重点                                        |
| ------ | ----------------------------------------------- |
| 阶段 1 | 板级引脚、有效电平、电气含义、单位              |
| 阶段 2 | 软件 I2C 时序、超时、总线恢复、WF5805F 数据格式 |
| 阶段 3 | 水深单位、滤波、传感器健康状态、报警阈值        |
| 阶段 4 | STEP/DIR/MF、脉冲频率、方向、限位急停           |
| 阶段 5 | 机械零点、位置可信标志、回零流程                |
| 阶段 6 | Flash 保存内容、写入节流、参数校验、断电恢复    |
| 阶段 7 | OLED 页面字段来源、单位、异常显示、菜单事件     |
| 阶段 8 | 主状态机、自检、暂停、自动打盹、故障处理        |
| 阶段 9 | 工程集成、构建条件、未验证硬件项                |

阶段内禁止事项：

- 禁止一次性生成所有模块。
- 禁止为后续阶段写大量未调用代码。
- 禁止在不理解 WF5805F 数据格式时猜测驱动实现。
- 禁止绕过限位保护。
- 禁止为了“让编译过”删除安全逻辑。
- 禁止改动与本阶段无关的已有文件。

## 5. 阶段 0：工程阅读与实施计划

目标：让 Codex 建立工程上下文，不写代码。

必须阅读：

- `AGENTS.md`
- `requirements.md`
- `pinmap.md`
- `codex-codegen-execution.md`
- 主规格文档
- `user/main.c`
- `Hardware/OLED.c/.h`
- `Reference/Key/Key.c/.h`
- `Hardware/LED.c/.h`
- `system/Delay.c/.h`
- `project1.uvprojx`
- `Reference/WF5805_2BAR官方驱动包`
- `Reference/步进电机驱动示例`

输出：

- 现有工程结构摘要
- 已有模块可复用点
- 待新增模块列表
- 预计需要修改的文件列表
- 构建方式判断
- 潜在冲突或缺失资料

阶段 0 不允许修改文件。

## 6. 阶段 1：板级配置、蜂鸣器、限位、按键事件

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

- `PA0` 蜂鸣器，低电平响，高电平关闭，初始化默认关闭。
- `PA6/PA7` 分别作为 LED1/LED2 状态指示输出；LED 正极/阳极经限流电阻接 3.3V，负极/阴极接 GPIO，低电平点亮，高电平熄灭。
- `PB12/PB13/PB14/PB15` 四个限位输入，低有效。
- 限位硬件必须按 24V NPN 输入型光耦隔离模块处理；`Materials/npn型光耦隔离器-用于限位器信号输入.jpg` 可作为限位输入模块，但必须使用 `Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg` 中的 24V 输入版本，输出侧 `VCC` 和板载/外接上拉使用 3.3V。
- 四个限位器必须对应四个独立光耦输入通道和四个独立 STM32 GPIO。
- 上限位、下限位、左右一致性查询接口，含限位输入滤波。
- `PB1/PB11/PB10/PB0` 按键扫描。
- 短按、长按、3 秒维护长按事件。
- 按键扫描周期 `10ms`，稳定确认 `25ms`。
- 短按 `25-1000ms`，普通长按 `>=1000ms`，维护入口长按 `PB10 >=3000ms`。
- 限位采样周期 `5-10ms`，触发确认 `20ms`，释放确认 `50ms`。
- 运动中当前方向对应限位原始有效时立即停止，再用滤波状态确认故障显示。
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

## 7. 阶段 2：OLED-I2C、软件 I2C 与 WF5805F 驱动

目标：保持 OLED 位于 `PB8/PB9`，并实现三组独立 WF5805F 软件 I2C 和原始压力读取。

建议新增文件：

- `Hardware/soft_i2c.c`
- `Hardware/soft_i2c.h`
- `Hardware/wf5805f.c`
- `Hardware/wf5805f.h`

必须先做：

- 阅读 `WF5805F 2Bar Datasheet V1.0.pdf` 和 `Reference/WF5805_2BAR官方驱动包`。
- 摘录 I2C 地址、读写命令、返回字节格式、状态位、压力换算公式。
- 明确官方驱动 `WFSensorIICDevice 0XDA` 是 WF5805F 的 8-bit 写地址，对应固件内部 7-bit 地址 `0x6D`。
- 参考驱动中 `WFSensor_indicateGroupConvert()` 写 `0x30 <- 0x0A`，`WFSensor_WaitFinish()` 读 `0x02`，`WFSensor_getTPData()` 从 `0x06` 连续读 5 字节；最终实现仍要按 PDF 核对并加超时。
- 如果 PDF 信息不完整，停止并向用户索要资料，不允许猜测。

必须实现：

- OLED-I2C：`PB8/PB9`，OLED 独占。
- I2C-A：`PA1/PA2`，`P_air`。
- I2C-B：`PB6/PB7`，`P_basket`。
- I2C-C：`PA8/PA9`，`P_tank`。
- 软件 I2C 起步速率约 100kHz。
- 7-bit 地址内部表示。
- 三颗 WF5805F 固定地址相同，不允许任意两颗挂在同一条 I2C 总线上。
- 每颗传感器独立读数接口。
- 读数失败计数接口。
- 总线恢复或重新初始化接口。
- 所有等待 SDA/SCL 的循环必须有超时退出。
- I2C 死锁恢复必须支持重新配置 GPIO、发送 9 个 SCL 脉冲、发送 STOP、重新初始化总线。
- 总线恢复重试次数受规格限制，超过后上报对应 I2C 严重故障。
- WF5805F 读取失败不能卡死主循环；必须返回错误并交给状态机处理。

阶段检查：

- 不破坏现有 OLED 软件 I2C 使用方式；OLED 继续使用 `PB8/PB9`。
- OLED 不与任何 WF5805F 共用 I2C 总线。
- 每条 I2C 总线上最多一颗 WF5805F。
- 返回压力单位统一为 `pressure_hpa_x100`。
- 读数失败时返回错误码，不返回伪造有效值。
- SDA 被拉低时能执行 9 脉冲总线释放流程。

## 8. 阶段 3：水深计算、滤波、传感器健康监测

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
- 目标水深误差超过 `±1mm`、重启水深差异超过 `3mm` 或自动/恢复阶段水深等待超时，统一使用 `E_DEPTH_TRACKING` 严重故障。
- 固定错误码表。

阶段检查：

- 所有水深单位清楚，不混用 `mm`、`mm_x10`。
- 错误码名称与主规格文档一致。
- 蜂鸣器静音不清除错误码。
- 严重故障能被主循环查询。

## 9. 阶段 4：UM244 步进控制与限位急停

目标：实现可控、有限脉冲的运动底层。

建议新增文件：

- `Hardware/stepper_um244.c`
- `Hardware/stepper_um244.h`

必须实现：

- `PA3 / TIM2_CH4` 输出 STEP。
- `PA4` 输出 DIR。
- `PA5` 输出 MF/电机释放。
- UM244 `PU-/DR-/MF-` 由三块单路 NPN 光耦模块下拉，参考 `Materials/npn型光耦隔离器-用于给步进电机驱动器的拉低信号转换.jpg`。
- 模块会反相：STM32 GPIO 输出低电平时，UM244 对应负端被拉低；STEP 空闲电平为 STM32 高电平，输出一步时拉低再恢复高电平。
- `PA5` 对应 `MF-`，低电平会释放电机；固件初始化后必须默认输出高电平，自动模式禁止释放。
- 硬件调试必须实测 `PU-/DR-/MF-` 有效低电平为 `0-0.5V`；若达不到，不能继续按该模块直接驱动方案生成固件假设。
- 可参考 `Reference/步进电机驱动示例/STM32驱动步进电机--IO口翻转/Hardware/Motor.c` 的 `PA3/PA4/PA5` GPIO 分工，但不得复制其中 `Delay_us` 循环翻转 STEP 的阻塞实现。
- 可参考 `Reference/步进电机驱动示例/驱动编码器电机/Hardware/Timer.c` 的 TIM/NVIC 标准外设库初始化写法，但本项目 STEP 中断必须加入有限脉冲计数和限位急停直读。
- 800 pulse/mm 换算。
- 自动打盹脉冲频率默认 800Hz，可降级为 400Hz。
- 自动打盹 `1-16 pulse` 不做加减速。
- `DIR` 改变后至少等待 5ms 再输出 STEP，最后一个 STEP 后至少保持 5ms。
- 手动速度 1mm/s = 800 pulse/s。
- 回零速度默认 0.5mm/s = 400 pulse/s。
- 有限脉冲输出接口，例如发送 N 个 pulse 后自动停止。
- `APP_NAP_MOVE` 忙锁，防止一次打盹被重复触发。
- 每个运动命令前检查限位。
- 运动中触发方向对应限位，立即停止。
- STEP 必须由定时器中断或等效硬件定时机制输出，不允许用 `Delay` 循环打脉冲。
- STEP 定时器中断中，每次 STEP 有效沿或翻转前必须直读当前方向对应限位 GPIO。
- 定时器中断发现方向对应限位原始有效时，必须立即停止定时器并置严重故障标志。
- 默认不释放电机。

阶段检查：

- `DIR` 高低电平必须通过配置宏可反转。
- 不在自动模式释放 `MF`。
- STEP 输出不是无限循环。
- 限位保护在底层强制执行，不依赖上层自觉。
- STEP 中断急停路径不依赖主循环滤波结果。
- `stepper_um244.c` 不调用任何 `Delay` 函数。

## 10. 阶段 5：机械回零与位置跟踪

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

## 11. 阶段 6：Flash 参数与恢复状态保存

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

## 12. 阶段 7：OLED 页面与菜单交互

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
- 自检页面
- 传感器页面
- 限位页面
- 参数页面
- 手动页面
- 报警页面
- 维护页面

必须实现交互：

- 页面文字、字段来源、单位、无效占位和错误短别名必须按 `Auto-lift-wiring-design.md` 的“OLED 显示文字与页面规格”实现。
- OLED 使用 4 行 x 16 字符 ASCII 模板；不足 16 字符的行要清尾，禁止依赖中文字库。
- `PB10` 暂停/确认/报警静音。
- `PB10` 长按 3 秒进入维护模式确认页。
- `PB0` 页面切换/取消。
- `PB1/PB11` 参数减/加或手动下降/上升。
- 手动移动必须长按，松手停止。
- 参数越界拒绝保存。

阶段检查：

- OLED 显示错误码和错误原因。
- 蜂鸣器静音状态可显示。
- 维护模式不能绕过限位。
- 参数页面不会写入非法参数。

## 13. 阶段 8：主状态机、自检、断电恢复、自动打盹

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
- 水深差异超过 3mm、历史有效水深缺失或恢复阶段水深等待超时时，使用 `E_DEPTH_TRACKING`，不追赶断电期间错过的运动。
- 上次为暂停则恢复暂停。
- 上次为维护、电机释放、故障则不自动运行。

必须实现自动打盹：

- 默认 1mm/day。
- 默认 8 pulse/次。
- 默认约 14.4min 间隔。
- 默认 800Hz 快速微冲，可降级为 400Hz。
- 每次运动前后读传感器和限位。
- 每次发送有限脉冲后停止。
- 打盹调度使用时间戳差值，不允许阻塞等待。
- 下一次打盹时间使用类似 `next_nap_ms = now_ms + nap_interval_ms` 的方式计算。
- 传感器稳定等待 10 秒、自检页面显示、OLED 刷新和菜单输入都必须由状态机轮询推进。
- 水深误差超过 ±1mm 时暂停报警，错误码使用 `E_DEPTH_TRACKING`。
- 累计 1mm 后做卡滞趋势检查。
- 连续 8 次卡滞嫌疑后 `E_STALL`。

阶段检查：

- 安全故障优先级高于自动和手动。
- `APP_NAP_MOVE` 不会无限输出 STEP。
- 主循环没有任何阻塞式打盹等待或长时间 `while` 等待。
- 故障清除后进入暂停，不直接自动运行。
- 蜂鸣器静音不清除故障。
- 断电恢复逻辑符合 requirements.md。

## 14. 阶段 9：工程集成、构建、修正

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

## 15. 每阶段固定检查清单

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
- 是否违反非阻塞规则或调用 Delay：
- 是否存在无超时 while 等待：
- 是否按本阶段重点补充了简体中文注释：
- 公开函数和关键宏是否有用途、参数、返回值、单位或注意事项说明：
- 是否存在无意义注释或只复述代码的注释：
- 是否满足限位中断急停规则：
- 是否满足 I2C 超时和 9 脉冲恢复规则：
- 是否存在占位项：
- 是否尝试构建：
- 构建是否通过：
- 若未通过，下一步需要什么：
```

## 16. 失败处理规则

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

## 17. 用户确认关卡

以下节点必须等待用户确认：

- 阶段 0 总结之后。
- 阶段 2 WF5805F 数据格式摘要之后。
- 阶段 4 第一次定义电机方向宏之后。
- 阶段 6 修改 Keil IROM 或 Flash 参数区之前。
- 阶段 8 主状态机接入自动运动之前。
- 阶段 9 构建通过或无法构建之后。

## 18. 最终交付标准

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
