#ifndef __BOARD_CONFIG_H
#define __BOARD_CONFIG_H

#include "stm32f10x.h"

// 板级 GPIO 时钟：当前已生成模块只使用 GPIOA/GPIOB，PA13/PA14 保留给 SWD 调试。
#define BOARD_RCC_GPIOA                  RCC_APB2Periph_GPIOA
#define BOARD_RCC_GPIOB                  RCC_APB2Periph_GPIOB

// OLED 专用软件 I2C：PB8/PB9 只接 OLED，OLED 地址固定为 7-bit 0x3C。
// 注意事项：三颗 WF5805F 地址相同，OLED 总线不能与任意 WF5805F 共用。
#define BOARD_OLED_SCL_GPIO              GPIOB
#define BOARD_OLED_SCL_PIN               GPIO_Pin_8
#define BOARD_OLED_SDA_GPIO              GPIOB
#define BOARD_OLED_SDA_PIN               GPIO_Pin_9

// WF5805F 三条独立软件 I2C：A=空气参考，B=框篮水深，C=鱼缸水深。
// 硬件假设：每条总线上最多挂一颗 WF5805F，SCL/SDA 由模块或外部电阻上拉到 3.3V。
#define BOARD_I2C_A_SCL_GPIO             GPIOA
#define BOARD_I2C_A_SCL_PIN              GPIO_Pin_1
#define BOARD_I2C_A_SDA_GPIO             GPIOA
#define BOARD_I2C_A_SDA_PIN              GPIO_Pin_2
#define BOARD_I2C_B_SCL_GPIO             GPIOB
#define BOARD_I2C_B_SCL_PIN              GPIO_Pin_6
#define BOARD_I2C_B_SDA_GPIO             GPIOB
#define BOARD_I2C_B_SDA_PIN              GPIO_Pin_7
#define BOARD_I2C_C_SCL_GPIO             GPIOA
#define BOARD_I2C_C_SCL_PIN              GPIO_Pin_8
#define BOARD_I2C_C_SDA_GPIO             GPIOA
#define BOARD_I2C_C_SDA_PIN              GPIO_Pin_9

// I2C 地址统一使用 7-bit 形式。
// OLED 示例中的 0x78、WF5805F 官方驱动中的 0xDA 都是包含写方向位的 8-bit 写地址。
#define BOARD_OLED_ADDR_7BIT             0x3CU
#define BOARD_WF5805F_ADDR_7BIT          0x6DU

// UM244 控制引脚：PA3/PA4/PA5 经三路 NPN 光耦分别下拉 PU-/DR-/MF-。
// 注意事项：UM244 的 PU+/DR+/MF+ 接 5V 信号正端，STM32 不直接驱动这些 5V 端子。
#define BOARD_UM244_STEP_GPIO            GPIOA
#define BOARD_UM244_STEP_PIN             GPIO_Pin_3
#define BOARD_UM244_DIR_GPIO             GPIOA
#define BOARD_UM244_DIR_PIN              GPIO_Pin_4
#define BOARD_UM244_MF_GPIO              GPIOA
#define BOARD_UM244_MF_PIN               GPIO_Pin_5

// 光耦输出反相：STM32 输出低电平时，UM244 对应负端被拉低。
// 默认安全态：STEP 空闲为高电平，MF 保持为高电平，避免复位后误出脉冲或释放电机。
#define BOARD_UM244_STEP_IDLE_LEVEL      Bit_SET
#define BOARD_UM244_STEP_ACTIVE_LEVEL    Bit_RESET
#define BOARD_UM244_MF_HOLD_LEVEL        Bit_SET
#define BOARD_UM244_MF_RELEASE_LEVEL     Bit_RESET
// DIR 高低电平需要首轮带载前实测；若方向相反，应改这里的映射而不是改控制算法。
#define BOARD_UM244_DIR_UP_LEVEL         Bit_RESET
#define BOARD_UM244_DIR_DOWN_LEVEL       Bit_SET

// 四路 24V NPN 限位经光耦隔离后进入 STM32，MCU 侧使用 3.3V 上拉。
// 有效电平：GPIO 读到低电平表示对应物理限位已触发。
#define BOARD_LIMIT_LEFT_UPPER_GPIO      GPIOB
#define BOARD_LIMIT_LEFT_UPPER_PIN       GPIO_Pin_12
#define BOARD_LIMIT_LEFT_LOWER_GPIO      GPIOB
#define BOARD_LIMIT_LEFT_LOWER_PIN       GPIO_Pin_13
#define BOARD_LIMIT_RIGHT_UPPER_GPIO     GPIOB
#define BOARD_LIMIT_RIGHT_UPPER_PIN      GPIO_Pin_14
#define BOARD_LIMIT_RIGHT_LOWER_GPIO     GPIOB
#define BOARD_LIMIT_RIGHT_LOWER_PIN      GPIO_Pin_15
#define BOARD_LIMIT_ACTIVE_LEVEL         Bit_RESET

// 限位滤波时间单位为 ms。
// 触发确认 20ms 小于释放确认 50ms，目的是让运动方向限位尽快停机，同时避免松开抖动。
#define BOARD_LIMIT_SAMPLE_MS            5U
#define BOARD_LIMIT_TRIGGER_CONFIRM_MS   20U
#define BOARD_LIMIT_RELEASE_CONFIRM_MS   50U

// 四个按键均按低有效处理，输入侧依赖 MCU 上拉。
// PB10 同时承担暂停/确认/静音/维护入口，维护入口长按阈值为 3000ms。
#define BOARD_KEY1_GPIO                  GPIOB
#define BOARD_KEY1_PIN                   GPIO_Pin_1
#define BOARD_KEY2_GPIO                  GPIOB
#define BOARD_KEY2_PIN                   GPIO_Pin_11
#define BOARD_KEY_PAUSE_GPIO             GPIOB
#define BOARD_KEY_PAUSE_PIN              GPIO_Pin_10
#define BOARD_KEY_PAGE_GPIO              GPIOB
#define BOARD_KEY_PAGE_PIN               GPIO_Pin_0
#define BOARD_KEY_ACTIVE_LEVEL           Bit_RESET

// 按键时间单位为 ms：扫描周期 10ms，消抖 25ms。
// 事件定义：25-1000ms 为短按，>=1000ms 为普通长按，PB10 >=3000ms 额外上报维护入口。
#define BOARD_KEY_SCAN_PERIOD_MS         10U
#define BOARD_KEY_DEBOUNCE_MS            25U
#define BOARD_KEY_SHORT_MIN_MS           25U
#define BOARD_KEY_SHORT_MAX_MS           1000U
#define BOARD_KEY_LONG_MS                1000U
#define BOARD_KEY_MAINTENANCE_MS         3000U

// UI/菜单调度时间，单位 ms；OLED 刷新不放在中断中执行，避免软件 I2C 长时间占用安全路径。
#define BOARD_UI_REFRESH_MS              250U
// 参数页长按后的连续加减间隔，单位 ms；与按键消抖分离，避免单次按下修改过快。
#define BOARD_UI_PARAM_REPEAT_MS         200U
// 参数错误或维护风险提示的短鸣时长，单位 ms；静音故障不清除错误码。
#define BOARD_UI_BEEP_MS                 80U
// 阶段 8 开机自检时间，单位 ms；传感器稳定等待由主状态机轮询推进，不允许 Delay 阻塞。
#define BOARD_SELF_TEST_SENSOR_STABLE_MS 10000UL
// 开机自检蜂鸣器短鸣时间，单位 ms；只验证蜂鸣器可控，不清除故障锁存。
#define BOARD_SELF_TEST_BEEP_MS          80U

// 有源蜂鸣器模块接 PA0，模块 I/O 为低电平触发。
// 默认安全态：PA0 输出高电平关闭蜂鸣器；蜂鸣器静音不代表故障被清除。
#define BOARD_BUZZER_GPIO                GPIOA
#define BOARD_BUZZER_PIN                 GPIO_Pin_0
#define BOARD_BUZZER_ON_LEVEL            Bit_RESET
#define BOARD_BUZZER_OFF_LEVEL           Bit_SET

// 板载状态 LED：阳极经限流电阻接 3.3V，阴极接 GPIO。
// 有效电平：GPIO 拉低点亮，拉高熄灭。
#define BOARD_LED1_RCC                   BOARD_RCC_GPIOA
#define BOARD_LED1_GPIO                  GPIOA
#define BOARD_LED1_PIN                   GPIO_Pin_6
#define BOARD_LED2_RCC                   BOARD_RCC_GPIOA
#define BOARD_LED2_GPIO                  GPIOA
#define BOARD_LED2_PIN                   GPIO_Pin_7

// 运动换算：UM244 1600 pulse/rev，丝杆导程 2.0mm/rev。
// 换算结果：800 pulse/mm，内部位置跟踪和有限脉冲命令都以 pulse 为基本单位。
#define BOARD_STEPPER_PULSE_PER_REV      1600U
#define BOARD_LEADSCREW_MM_PER_REV_X10   20U
#define BOARD_STEPPER_PULSE_PER_MM       800U
// 自动打盹单次脉冲范围，单位 pulse；8 pulse 约等于 0.01mm，最大不超过 24 pulse。
#define BOARD_NAP_DEFAULT_PULSES         8U
#define BOARD_NAP_MAX_PULSES             24U
// 自动打盹最小间隔，单位 ms；防止菜单参数导致唤醒过于频繁。
#define BOARD_NAP_MIN_INTERVAL_MS        300000UL
// 自动目标水深允许误差，单位 mm_x10；超过 ±1.0mm 后续阶段应暂停报警。
#define BOARD_CONTROL_TOLERANCE_MM_X10   10
// 自动打盹前后等待新鲜水深读数的最长时间，单位 ms；超时说明传感器链路不能支撑安全运动确认。
#define BOARD_NAP_SENSOR_FRESH_TIMEOUT_MS 5000UL
// 自动打盹完成后，允许水深滤波更新的轮询保护时间，单位 ms；不得用 Delay 等待。
#define BOARD_NAP_POST_DEPTH_TIMEOUT_MS  5000UL
// 无 RTC，运行日固定按上电累计秒数折算。
#define BOARD_SECONDS_PER_DAY            86400UL
// 卡滞趋势检查阈值：累计 1mm 后期望水深至少同向变化约 1mm，连续 8 次为严重故障。
#define BOARD_STALL_CHECK_PULSES         BOARD_STEPPER_PULSE_PER_MM
#define BOARD_STALL_EXPECTED_DELTA_MM_X10 10
#define BOARD_STALL_FAILURE_LIMIT        8U
// DIR 建立/保持时间单位 ms，必须覆盖 UM244 对方向信号稳定时间的要求。
#define BOARD_DIR_SETUP_HOLD_MS          5U
// STEP 频率单位 Hz：自动默认 800Hz，回零默认 400Hz，手动 800Hz = 1mm/s。
#define BOARD_STEPPER_AUTO_FREQ_HZ       800U
#define BOARD_STEPPER_FALLBACK_FREQ_HZ   400U
#define BOARD_STEPPER_MANUAL_FREQ_HZ     800U
#define BOARD_STEPPER_HOMING_FREQ_HZ     400U
#define BOARD_STEPPER_MAX_FREQ_HZ        5000U
// 手动点动采用有限脉冲小段连续触发，80 pulse @800Hz 约 100ms；松手后可立即停止后续小段。
#define BOARD_STEPPER_MANUAL_CHUNK_PULSES 80U
// 位置范围：框篮机械最大行程 170mm，内部位置以 pulse 保存。
#define BOARD_BASKET_MAX_TRAVEL_MM       170U
#define BOARD_BASKET_MAX_POSITION_PULSES (BOARD_BASKET_MAX_TRAVEL_MM * BOARD_STEPPER_PULSE_PER_MM)
// 回零参数：先离开下限位 1mm，再二次低速靠近；搜索上限防止无止境运动。
#define BOARD_HOMING_BACKOFF_MM          1U
#define BOARD_HOMING_BACKOFF_PULSES      (BOARD_HOMING_BACKOFF_MM * BOARD_STEPPER_PULSE_PER_MM)
#define BOARD_HOMING_SEARCH_CHUNK_PULSES 4000U
#define BOARD_HOMING_MAX_SEARCH_PULSES   ((BOARD_BASKET_MAX_TRAVEL_MM + 5U) * BOARD_STEPPER_PULSE_PER_MM)
#define BOARD_HOMING_RELEASE_WAIT_MS     100U

// 水深和传感器健康参数。
// 水深阈值单位为 mm，滤波样本数为最近有效压力读数个数。
#define BOARD_WATER_FILTER_SAMPLES       5U
// 水深趋势缓存：每 1s 记录一次滤波水深，覆盖快速掉水 15s 判断窗口。
#define BOARD_WATER_TREND_SAMPLE_MS      1000UL
#define BOARD_WATER_TREND_SAMPLES        20U
#define BOARD_TANK_MIN_DEPTH_MM          250
#define BOARD_TANK_MAX_DEPTH_MM          450
#define BOARD_BASKET_MIN_SAFE_DEPTH_MM   5
#define BOARD_BASKET_MAX_SAFE_DEPTH_MM   120
// 旧水位突变阈值保留用于非可跟随异常；普通鱼缸掉水由快速跟随仲裁处理。
#define BOARD_WATER_JUMP_MM_PER_MIN      10
#define BOARD_SENSOR_FAILURE_LIMIT       5U
#define BOARD_I2C_RECOVERY_FAILURE_LIMIT 5U
// 压力差换算水深低于 -2.0mm 视为物理异常，单位 mm_x10。
#define BOARD_PRESSURE_PHYSICAL_MIN_MM_X10 (-20)

// 低频闭环水深修正参数。单位见宏名：ms、mm_x10、pulse、Hz。
// error = basket_depth - target_depth；正值表示框篮实际水深偏深，需要上升变浅。
#define BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS 180000UL
#define BOARD_DEPTH_TRACK_START_DEADBAND_MM_X10 20
#define BOARD_DEPTH_TRACK_STOP_DEADBAND_MM_X10 10
#define BOARD_DEPTH_TRACK_STABLE_DELTA_MM_X10 5
#define BOARD_DEPTH_TRACK_HARD_ERROR_MM_X10 50
#define BOARD_DEPTH_TRACK_MIN_STEP_PULSES 160U
#define BOARD_DEPTH_TRACK_DEFAULT_STEP_PULSES 200U
#define BOARD_DEPTH_TRACK_MAX_STEP_PULSES 400U
#define BOARD_DEPTH_TRACK_FREQ_HZ        800U
#define BOARD_DEPTH_TRACK_STABLE_WAIT_MS 15000UL
#define BOARD_DEPTH_TRACK_STABLE_SAMPLES 3U
#define BOARD_DEPTH_TRACK_MAX_FAILURES   5U
#define BOARD_DEPTH_TRACK_HOUR_LIMIT_PULSES 2400U
#define BOARD_DEPTH_TRACK_DAY_LIMIT_PULSES 8000U

// 快速掉水跟随参数。只允许框篮下降，普通 5..30mm/min 掉水可跟随，超过 30mm/min 报警。
#define BOARD_DROP_TREND_WINDOW_MS       15000UL
#define BOARD_DROP_TREND_MIN_SAMPLES     3U
#define BOARD_DROP_ENTRY_RATE_MM_PER_MIN 5
#define BOARD_DROP_DANGER_RATE_MM_PER_MIN 30
#define BOARD_DROP_STABLE_RATE_MM_PER_MIN 2
#define BOARD_DROP_START_ERROR_MM_X10    20
#define BOARD_DROP_STOP_ERROR_MM_X10     10
#define BOARD_DROP_MIN_STEP_PULSES       400U
#define BOARD_DROP_MAX_STEP_PULSES       800U
#define BOARD_DROP_FREQ_HZ               800U
#define BOARD_DROP_MAX_CONT_TIME_MS      30000UL
#define BOARD_DROP_HARD_MAX_TIME_MS      60000UL
#define BOARD_DROP_MAX_DISTANCE_PULSES   24000U
#define BOARD_DROP_STABLE_WAIT_MS        15000UL

// Flash 参数区：STM32F103C8T6 标称 64KB Flash，末尾两个 1KB 页用于 A/B 备份。
// 注意事项：Keil IROM 必须限制为 0x08000000 + 0x0000F800，避免代码覆盖参数页。
#define BOARD_FLASH_BASE_ADDR            0x08000000UL
#define BOARD_FLASH_TOTAL_SIZE_BYTES     0x00010000UL
#define BOARD_FLASH_PAGE_SIZE_BYTES      0x00000400UL
#define BOARD_PARAM_FLASH_PAGE_A_ADDR    0x0800F800UL
#define BOARD_PARAM_FLASH_PAGE_B_ADDR    0x0800FC00UL
#define BOARD_PARAM_FLASH_PAGE_SIZE      BOARD_FLASH_PAGE_SIZE_BYTES
#define BOARD_IROM_RESERVED_SIZE_BYTES   0x0000F800UL
#define BOARD_IROM_END_ADDR              (BOARD_FLASH_BASE_ADDR + BOARD_IROM_RESERVED_SIZE_BYTES)

// Flash 保存节流：运行状态最多每 10min 写入一次；关键状态切换使用强制保存接口。
// 单位为 ms，禁止把每次 1 pulse 或 8 pulse 打盹动作直接绑定到 Flash 擦写。
#define BOARD_PARAM_RUNTIME_SAVE_MS      600000UL

// 持久化默认参数，水深单位为 mm_x10，脉冲单位为 pulse。
// 目标范围 5..100mm；默认从 100mm 逐日变浅到 10mm，每日 1mm。
#define BOARD_PARAM_MAGIC                0x414C4654UL
#define BOARD_PARAM_VERSION              1U
#define BOARD_TARGET_MIN_DEPTH_MM_X10    50
#define BOARD_TARGET_MAX_DEPTH_MM_X10    1000
#define BOARD_TARGET_DEFAULT_INITIAL_MM_X10 1000
#define BOARD_TARGET_DEFAULT_FINAL_MM_X10   100
#define BOARD_DAILY_SHALLOW_DEFAULT_MM_X10  10
#define BOARD_DAILY_SHALLOW_MAX_MM_X10      30
#define BOARD_RESTART_DEPTH_DIFF_MM_X10     30

#endif
