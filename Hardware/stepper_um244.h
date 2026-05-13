#ifndef __STEPPER_UM244_H
#define __STEPPER_UM244_H

#include "stm32f10x.h"

// 模    块：UM244 步进驱动底层
// 硬件假设：PA3/PA4/PA5 分别通过 NPN 光耦下拉 UM244 的 PU-/DR-/MF-。
// 有效电平：低电平为 STEP 有效脉冲，MF 低电平会释放电机。
// 安全约束：所有运动都是有限脉冲；限位保护在命令前和 TIM2 中断内同时执行。

typedef enum
{
	// 框篮上升，basket_depth_mm 变小。
	STEPPER_UM244_DIRECTION_UP = 0,
	// 框篮下降，basket_depth_mm 变大。
	STEPPER_UM244_DIRECTION_DOWN
} StepperUM244_Direction_t;

typedef enum
{
	// 命令接受。
	STEPPER_UM244_STATUS_OK = 0,
	// 正在等待方向建立、输出脉冲或保持时间，拒绝重入。
	STEPPER_UM244_STATUS_BUSY,
	// 参数非法，例如脉冲数为 0 或频率超出范围。
	STEPPER_UM244_STATUS_ERROR_PARAM,
	// 命令方向被当前限位禁止。
	STEPPER_UM244_STATUS_ERROR_LIMIT,
	// 模块已进入故障状态，需要受控清除。
	STEPPER_UM244_STATUS_ERROR_FAULT
} StepperUM244_Status_t;

typedef enum
{
	// 尚无停止原因。
	STEPPER_UM244_STOP_NONE = 0,
	// 有限脉冲数量输出完成。
	STEPPER_UM244_STOP_PULSE_DONE,
	// 回零等预期场景中触发目标限位而停止。
	STEPPER_UM244_STOP_EXPECTED_LIMIT,
	// 非预期方向限位触发导致急停。
	STEPPER_UM244_STOP_LIMIT_FAULT,
	// 左右同方向限位不一致导致急停。
	STEPPER_UM244_STOP_MISMATCH_FAULT,
	// 上层主动请求停止。
	STEPPER_UM244_STOP_REQUESTED
} StepperUM244_StopReason_t;

typedef enum
{
	// 空闲，允许接受新的有限脉冲命令。
	STEPPER_UM244_STATE_IDLE = 0,
	// DIR 已设置，等待 5ms 建立时间。
	STEPPER_UM244_STATE_DIR_WAIT,
	// TIM2 正在输出 STEP 有限脉冲。
	STEPPER_UM244_STATE_RUNNING,
	// 最后一个 STEP 后保持 5ms，再回到空闲。
	STEPPER_UM244_STATE_HOLD_WAIT,
	// 安全故障锁定，需上层确认后清除。
	STEPPER_UM244_STATE_FAULT
} StepperUM244_State_t;

// 函    数：StepperUM244_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 STEP/DIR/MF GPIO 和 TIM2，默认保持电机、不释放。
void StepperUM244_Init(void);

// 函    数：StepperUM244_Poll
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：非阻塞推进方向建立和末脉冲保持时间，不输出 STEP。
void StepperUM244_Poll(uint32_t now_ms);

// 函    数：StepperUM244_StartPulses
// 参    数：direction 运动方向；pulses 有限脉冲数；frequency_hz STEP 脉冲频率，单位 Hz；
//           now_ms 系统毫秒时间戳。
// 返 回 值：命令状态。
// 注意事项：启动前检查限位；DIR 建立 5ms 后才由 TIM2 输出 STEP。
StepperUM244_Status_t StepperUM244_StartPulses(StepperUM244_Direction_t direction,
                                               uint16_t pulses,
                                               uint16_t frequency_hz,
                                               uint32_t now_ms);

// 函    数：StepperUM244_StartNapMove
// 参    数：direction 运动方向；pulses 自动打盹脉冲数；now_ms 系统毫秒时间戳。
// 返 回 值：命令状态。
// 注意事项：pulses 必须在 1..BOARD_NAP_MAX_PULSES 范围内，防止单次位移过大。
StepperUM244_Status_t StepperUM244_StartNapMove(StepperUM244_Direction_t direction,
                                                uint16_t pulses,
                                                uint32_t now_ms);

// 函    数：StepperUM244_StartUntilLimit
// 参    数：direction 运动方向；max_pulses 最大搜索脉冲数；frequency_hz STEP 频率；
//           now_ms 系统毫秒时间戳。
// 返 回 值：命令状态。
// 注意事项：用于回零类运动，允许预期方向限位作为正常停止条件。
StepperUM244_Status_t StepperUM244_StartUntilLimit(StepperUM244_Direction_t direction,
                                                   uint16_t max_pulses,
                                                   uint16_t frequency_hz,
                                                   uint32_t now_ms);

// 函    数：StepperUM244_Stop
// 参    数：无
// 返 回 值：无
// 注意事项：主动停止当前运动，并回到空闲保持状态。
void StepperUM244_Stop(void);

// 函    数：StepperUM244_ClearFault
// 参    数：无
// 返 回 值：无
// 注意事项：清除步进模块内部故障状态；错误码本身仍由 error_manager 管理。
void StepperUM244_ClearFault(void);

// 函    数：StepperUM244_SetMotorRelease
// 参    数：release 非 0 释放电机，0 保持电机。
// 返 回 值：无
// 注意事项：自动模式不应调用 release=1；释放后位置可信度应由上层标记为不可信。
void StepperUM244_SetMotorRelease(uint8_t release);

// 函    数：StepperUM244_IsMotorReleased
// 参    数：无
// 返 回 值：1 表示软件记录为电机释放，0 表示保持。
// 注意事项：返回软件状态，不检测 UM244 实际输出电气状态。
uint8_t StepperUM244_IsMotorReleased(void);

// 函    数：StepperUM244_IsBusy
// 参    数：无
// 返 回 值：1 表示模块非空闲，0 表示空闲。
// 注意事项：FAULT 也属于非空闲，必须受控清除后再发新命令。
uint8_t StepperUM244_IsBusy(void);

// 函    数：StepperUM244_GetState
// 参    数：无
// 返 回 值：当前步进内部状态。
// 注意事项：供上层状态机或调试显示判断运动阶段。
StepperUM244_State_t StepperUM244_GetState(void);

// 函    数：StepperUM244_GetCompletedPulses
// 参    数：无
// 返 回 值：当前命令已完成的有效 STEP 脉冲数。
// 注意事项：计数在 TIM2 中断输出有效 STEP 沿时递增。
uint16_t StepperUM244_GetCompletedPulses(void);

// 函    数：StepperUM244_GetStopReason
// 参    数：无
// 返 回 值：最近一次停止原因。
// 注意事项：用于上层区分脉冲完成、预期限位、非预期限位和用户停止。
StepperUM244_StopReason_t StepperUM244_GetStopReason(void);

// 函    数：StepperUM244_TIM2_IRQHandler
// 参    数：无
// 返 回 值：无
// 注意事项：TIM2 中断服务入口，只允许短路径限位急停、计数和 STEP 翻转。
void StepperUM244_TIM2_IRQHandler(void);

#endif
