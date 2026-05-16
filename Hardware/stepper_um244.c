#include "stepper_um244.h"
#include "board_config.h"
#include "limit.h"
#include "error_manager.h"

// TIM2 作为 STEP 翻转节拍，更新中断频率为目标脉冲频率的 2 倍。
#define STEPPER_TIMER                    TIM2
#define STEPPER_TIMER_RCC                RCC_APB1Periph_TIM2
#define STEPPER_TIMER_IRQ                TIM2_IRQn
// 定时器基础计数频率 1MHz，便于用 Hz 换算 ARR。
#define STEPPER_TIMER_TICK_HZ            1000000UL
#define STEPPER_MIN_FREQ_HZ              1U

typedef enum
{
	// 原始限位未触发。
	STEPPER_RAW_LIMIT_OK = 0,
	// 回零等预期限位停止。
	STEPPER_RAW_LIMIT_EXPECTED,
	// 非预期方向限位触发。
	STEPPER_RAW_LIMIT_FAULT,
	// 左右同方向限位不一致。
	STEPPER_RAW_LIMIT_MISMATCH
} StepperUM244_RawLimitResult_t;

static volatile StepperUM244_State_t s_state;
static volatile StepperUM244_Direction_t s_direction;
static volatile uint16_t s_target_pulses;
static volatile uint16_t s_completed_pulses;
static volatile uint16_t s_frequency_hz;
static volatile uint8_t s_step_active;
static volatile uint8_t s_expect_limit_stop;
static volatile uint8_t s_motor_released;
static volatile uint8_t s_initialized;
static volatile StepperUM244_StopReason_t s_stop_reason;
static uint32_t s_dir_ready_ms;
static uint32_t s_hold_deadline_ms;

// 函    数：StepperUM244_WriteStep
// 参    数：level 要写入 STEP GPIO 的电平。
// 返 回 值：无
// 注意事项：经光耦反相后，BOARD_UM244_STEP_ACTIVE_LEVEL 会拉低 UM244 PU-。
static void StepperUM244_WriteStep(BitAction level)
{
	GPIO_WriteBit(BOARD_UM244_STEP_GPIO, BOARD_UM244_STEP_PIN, level);
}

// 函    数：StepperUM244_WriteDir
// 参    数：direction 运动方向。
// 返 回 值：无
// 注意事项：DIR 实际高低电平由 board_config 映射，便于现场实测后修正方向。
static void StepperUM244_WriteDir(StepperUM244_Direction_t direction)
{
	if (direction == STEPPER_UM244_DIRECTION_UP)
	{
		// 上升方向：框篮向上，basket_depth_mm 变小。
		GPIO_WriteBit(BOARD_UM244_DIR_GPIO, BOARD_UM244_DIR_PIN, BOARD_UM244_DIR_UP_LEVEL);
	}
	else
	{
		GPIO_WriteBit(BOARD_UM244_DIR_GPIO, BOARD_UM244_DIR_PIN, BOARD_UM244_DIR_DOWN_LEVEL);
	}
}

// 函    数：StepperUM244_WriteMf
// 参    数：level 要写入 MF GPIO 的电平。
// 返 回 值：无
// 注意事项：MF 低电平释放电机，自动运行默认必须保持 BOARD_UM244_MF_HOLD_LEVEL。
static void StepperUM244_WriteMf(BitAction level)
{
	GPIO_WriteBit(BOARD_UM244_MF_GPIO, BOARD_UM244_MF_PIN, level);
}

// 函    数：StepperUM244_TimeElapsed
// 参    数：now_ms 当前毫秒时间戳；deadline_ms 到期时间戳。
// 返 回 值：当前时间达到或超过 deadline_ms 返回 1，否则返回 0。
// 注意事项：使用有符号差值判断，允许毫秒计数回绕。
static uint8_t StepperUM244_TimeElapsed(uint32_t now_ms, uint32_t deadline_ms)
{
	return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1U : 0U;
}

// 函    数：StepperUM244_ToLimitDirection
// 参    数：direction 步进运动方向。
// 返 回 值：对应限位模块的运动方向。
// 注意事项：上升检查上限位，下降检查下限位。
static Limit_Direction_t StepperUM244_ToLimitDirection(StepperUM244_Direction_t direction)
{
	return (direction == STEPPER_UM244_DIRECTION_UP) ? LIMIT_DIRECTION_UP : LIMIT_DIRECTION_DOWN;
}

// 函    数：StepperUM244_StopTimer
// 参    数：无
// 返 回 值：无
// 注意事项：停止 TIM2 后立即把 STEP 恢复为空闲高电平，防止残留有效脉冲。
static void StepperUM244_StopTimer(void)
{
	TIM_Cmd(STEPPER_TIMER, DISABLE);
	TIM_ClearITPendingBit(STEPPER_TIMER, TIM_IT_Update);
	StepperUM244_WriteStep(BOARD_UM244_STEP_IDLE_LEVEL);
	s_step_active = 0U;
}

// 函    数：StepperUM244_SetLimitError
// 参    数：direction 当前运动方向。
// 返 回 值：无
// 注意事项：上升触发上限位故障，下降触发下限位故障。
static void StepperUM244_SetLimitError(StepperUM244_Direction_t direction)
{
	if (direction == STEPPER_UM244_DIRECTION_UP)
	{
		ErrorManager_Set(ERROR_CODE_E_UPPER_LIMIT);
	}
	else
	{
		ErrorManager_Set(ERROR_CODE_E_LOWER_LIMIT);
	}
}

// 函    数：StepperUM244_CheckFilteredLimit
// 参    数：direction 准备启动的运动方向。
// 返 回 值：1 表示该方向被滤波限位或左右不一致禁止，0 表示允许继续检查。
// 注意事项：用于命令发出前安全检查，避免明知到限仍启动 STEP。
static uint8_t StepperUM244_CheckFilteredLimit(StepperUM244_Direction_t direction)
{
	Limit_Direction_t limit_direction;

	limit_direction = StepperUM244_ToLimitDirection(direction);

	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		// 左右同方向限位不一致表示机械或传感器不同步，禁止任何方向运动。
		ErrorManager_Set(ERROR_CODE_E_LIMIT_MISMATCH);
		return 1U;
	}

	if (Limit_IsDirectionBlocked(limit_direction) != 0U)
	{
		// 命令发出前先用滤波限位判断，避免明知到限仍启动 STEP。
		StepperUM244_SetLimitError(direction);
		return 1U;
	}

	return 0U;
}

// 函    数：StepperUM244_CheckRawLimitInIrq
// 参    数：无
// 返 回 值：原始限位检查结果。
// 注意事项：在 TIM2 中断内直读 GPIO，不等待主循环滤波确认；只做急停和错误置位短路径。
static StepperUM244_RawLimitResult_t StepperUM244_CheckRawLimitInIrq(void)
{
	uint8_t left_upper;
	uint8_t right_upper;
	uint8_t left_lower;
	uint8_t right_lower;

	left_upper = Limit_ReadRaw(LIMIT_LEFT_UPPER);
	right_upper = Limit_ReadRaw(LIMIT_RIGHT_UPPER);
	left_lower = Limit_ReadRaw(LIMIT_LEFT_LOWER);
	right_lower = Limit_ReadRaw(LIMIT_RIGHT_LOWER);

	if ((left_upper != right_upper) || (left_lower != right_lower))
	{
		// 中断内使用原始 GPIO 急停，不等待主循环滤波确认。
		ErrorManager_Set(ERROR_CODE_E_LIMIT_MISMATCH);
		s_stop_reason = STEPPER_UM244_STOP_MISMATCH_FAULT;
		return STEPPER_RAW_LIMIT_MISMATCH;
	}

	if (s_direction == STEPPER_UM244_DIRECTION_UP)
	{
		if ((left_upper != 0U) || (right_upper != 0U))
		{
			if (s_expect_limit_stop != 0U)
			{
				// 回零搜索下/上限位时，预期限位触发是正常停止条件。
				s_stop_reason = STEPPER_UM244_STOP_EXPECTED_LIMIT;
				return STEPPER_RAW_LIMIT_EXPECTED;
			}
			// 非预期上限位触发，立即停机并锁存上限位故障。
			ErrorManager_Set(ERROR_CODE_E_UPPER_LIMIT);
			s_stop_reason = STEPPER_UM244_STOP_LIMIT_FAULT;
			return STEPPER_RAW_LIMIT_FAULT;
		}
	}
	else
	{
		if ((left_lower != 0U) || (right_lower != 0U))
		{
			if (s_expect_limit_stop != 0U)
			{
				// 预期触发下限位用于回零，不作为故障。
				s_stop_reason = STEPPER_UM244_STOP_EXPECTED_LIMIT;
				return STEPPER_RAW_LIMIT_EXPECTED;
			}
			// 非预期下限位触发，立即停机并锁存下限位故障。
			ErrorManager_Set(ERROR_CODE_E_LOWER_LIMIT);
			s_stop_reason = STEPPER_UM244_STOP_LIMIT_FAULT;
			return STEPPER_RAW_LIMIT_FAULT;
		}
	}

	return STEPPER_RAW_LIMIT_OK;
}

// 函    数：StepperUM244_ConfigureTimer
// 参    数：frequency_hz 目标 STEP 脉冲频率，单位 Hz。
// 返 回 值：无
// 注意事项：TIM2 更新中断频率为目标脉冲频率的 2 倍，一次拉低和一次恢复组成一个脉冲。
static void StepperUM244_ConfigureTimer(uint16_t frequency_hz)
{
	TIM_TimeBaseInitTypeDef timer_init;
	uint32_t update_hz;
	uint32_t period_ticks;
	uint16_t prescaler;

	if (frequency_hz < STEPPER_MIN_FREQ_HZ)
	{
		frequency_hz = STEPPER_MIN_FREQ_HZ;
	}

	update_hz = (uint32_t)frequency_hz * 2UL;
	prescaler = (uint16_t)((SystemCoreClock / STEPPER_TIMER_TICK_HZ) - 1UL);
	// 每个 STEP 脉冲需要一次拉低和一次恢复，因此更新频率为脉冲频率的 2 倍。
	period_ticks = STEPPER_TIMER_TICK_HZ / update_hz;
	if (period_ticks == 0UL)
	{
		period_ticks = 1UL;
	}

	TIM_TimeBaseStructInit(&timer_init);
	timer_init.TIM_ClockDivision = TIM_CKD_DIV1;
	timer_init.TIM_CounterMode = TIM_CounterMode_Up;
	timer_init.TIM_Period = (uint16_t)(period_ticks - 1UL);
	timer_init.TIM_Prescaler = prescaler;
	timer_init.TIM_RepetitionCounter = 0U;
	TIM_TimeBaseInit(STEPPER_TIMER, &timer_init);
	TIM_ClearFlag(STEPPER_TIMER, TIM_FLAG_Update);
	TIM_ITConfig(STEPPER_TIMER, TIM_IT_Update, ENABLE);
}

// 函    数：StepperUM244_StartTimer
// 参    数：无
// 返 回 值：无
// 注意事项：重新配置 TIM2 并从 0 计数，进入 RUNNING 后 STEP 由中断输出。
static void StepperUM244_StartTimer(void)
{
	StepperUM244_ConfigureTimer((uint16_t)s_frequency_hz);
	TIM_SetCounter(STEPPER_TIMER, 0U);
	TIM_Cmd(STEPPER_TIMER, ENABLE);
	s_state = STEPPER_UM244_STATE_RUNNING;
}

// 函    数：StepperUM244_Init
// 参    数：无
// 返 回 值：无
// 注意事项：配置 GPIO 前先写安全电平：STEP 空闲、MF 保持，避免复位时误动或释放。
void StepperUM244_Init(void)
{
	GPIO_InitTypeDef gpio_init;
	NVIC_InitTypeDef nvic_init;

	RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(STEPPER_TIMER_RCC, ENABLE);

	// 配置 GPIO 前先写安全电平：STEP 空闲、MF 保持，避免复位时误动或释放。
	StepperUM244_WriteStep(BOARD_UM244_STEP_IDLE_LEVEL);
	StepperUM244_WriteMf(BOARD_UM244_MF_HOLD_LEVEL);

	gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
	gpio_init.GPIO_Pin = BOARD_UM244_STEP_PIN |
	                     BOARD_UM244_DIR_PIN |
	                     BOARD_UM244_MF_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &gpio_init);

	StepperUM244_WriteStep(BOARD_UM244_STEP_IDLE_LEVEL);
	StepperUM244_WriteDir(STEPPER_UM244_DIRECTION_UP);
	StepperUM244_WriteMf(BOARD_UM244_MF_HOLD_LEVEL);

	StepperUM244_ConfigureTimer(BOARD_STEPPER_AUTO_FREQ_HZ);
	TIM_Cmd(STEPPER_TIMER, DISABLE);

	// TIM2 中断优先级高于普通轮询，用于限位急停和有限脉冲计数。
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	nvic_init.NVIC_IRQChannel = STEPPER_TIMER_IRQ;
	nvic_init.NVIC_IRQChannelPreemptionPriority = 1U;
	nvic_init.NVIC_IRQChannelSubPriority = 1U;
	nvic_init.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic_init);

	s_state = STEPPER_UM244_STATE_IDLE;
	s_direction = STEPPER_UM244_DIRECTION_UP;
	s_target_pulses = 0U;
	s_completed_pulses = 0U;
	s_frequency_hz = BOARD_STEPPER_AUTO_FREQ_HZ;
	s_step_active = 0U;
	s_expect_limit_stop = 0U;
	s_motor_released = 0U;
	s_stop_reason = STEPPER_UM244_STOP_NONE;
	s_dir_ready_ms = 0U;
	s_hold_deadline_ms = 0U;
	s_initialized = 1U;
}

// 函    数：StepperUM244_Poll
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：主循环非阻塞调用；只处理 DIR 建立和末脉冲保持，不在这里翻转 STEP。
void StepperUM244_Poll(uint32_t now_ms)
{
	if (s_initialized == 0U)
	{
		return;
	}

	if (s_state == STEPPER_UM244_STATE_DIR_WAIT)
	{
		if (StepperUM244_TimeElapsed(now_ms, s_dir_ready_ms) != 0U)
		{
			// DIR 稳定 5ms 后再次检查滤波限位，再允许启动 STEP。
			if (StepperUM244_CheckFilteredLimit((StepperUM244_Direction_t)s_direction) != 0U)
			{
				s_state = STEPPER_UM244_STATE_FAULT;
				return;
			}
			StepperUM244_StartTimer();
		}
	}
	else if (s_state == STEPPER_UM244_STATE_HOLD_WAIT)
	{
		if (s_hold_deadline_ms == 0U)
		{
			// 最后一个 STEP 后保持 DIR/MF 至少 5ms，再回到空闲。
			s_hold_deadline_ms = now_ms + BOARD_DIR_SETUP_HOLD_MS;
		}
		if (StepperUM244_TimeElapsed(now_ms, s_hold_deadline_ms) != 0U)
		{
			s_hold_deadline_ms = 0U;
			s_state = STEPPER_UM244_STATE_IDLE;
		}
	}
}

// 函    数：StepperUM244_StartPulsesInternal
// 参    数：direction 运动方向；pulses 有限脉冲数；frequency_hz 脉冲频率；
//           now_ms 当前毫秒时间戳；expect_limit_stop 非 0 表示预期限位可作为正常停止。
// 返 回 值：命令状态。
// 注意事项：所有运动都经此函数启动，统一做参数、忙锁、滤波限位和 MF 保持检查。
static StepperUM244_Status_t StepperUM244_StartPulsesInternal(StepperUM244_Direction_t direction,
                                                              uint16_t pulses,
                                                              uint16_t frequency_hz,
                                                              uint32_t now_ms,
                                                              uint8_t expect_limit_stop)
{
	if ((s_initialized == 0U) ||
	    (pulses == 0U) ||
	    (frequency_hz < STEPPER_MIN_FREQ_HZ) ||
	    (frequency_hz > BOARD_STEPPER_MAX_FREQ_HZ))
	{
		return STEPPER_UM244_STATUS_ERROR_PARAM;
	}

	if ((direction != STEPPER_UM244_DIRECTION_UP) &&
	    (direction != STEPPER_UM244_DIRECTION_DOWN))
	{
		return STEPPER_UM244_STATUS_ERROR_PARAM;
	}

	if (s_state != STEPPER_UM244_STATE_IDLE)
	{
		// 忙锁防止 APP_NAP_MOVE 或手动命令重复触发重叠脉冲。
		if (s_state == STEPPER_UM244_STATE_FAULT)
		{
			return STEPPER_UM244_STATUS_ERROR_FAULT;
		}
		return STEPPER_UM244_STATUS_BUSY;
	}

	if (StepperUM244_CheckFilteredLimit(direction) != 0U)
	{
		// 启动前的安全检查失败时直接进入步进故障态。
		s_state = STEPPER_UM244_STATE_FAULT;
		return STEPPER_UM244_STATUS_ERROR_LIMIT;
	}

	s_direction = direction;
	s_target_pulses = pulses;
	s_completed_pulses = 0U;
	s_frequency_hz = frequency_hz;
	s_step_active = 0U;
	s_expect_limit_stop = expect_limit_stop;
	s_stop_reason = STEPPER_UM244_STOP_NONE;
	s_hold_deadline_ms = 0U;

	StepperUM244_WriteStep(BOARD_UM244_STEP_IDLE_LEVEL);
	StepperUM244_WriteDir(direction);
	// 每次运动前强制回到电机保持态，自动模式不得释放 MF。
	StepperUM244_WriteMf(BOARD_UM244_MF_HOLD_LEVEL);
	s_motor_released = 0U;

	s_dir_ready_ms = now_ms + BOARD_DIR_SETUP_HOLD_MS;
	s_state = STEPPER_UM244_STATE_DIR_WAIT;

	return STEPPER_UM244_STATUS_OK;
}

// 函    数：StepperUM244_StartPulses
// 参    数：direction 运动方向；pulses 有限脉冲数；frequency_hz 脉冲频率；now_ms 当前毫秒时间戳。
// 返 回 值：命令状态。
// 注意事项：普通有限运动不允许预期限位作为正常停止。
StepperUM244_Status_t StepperUM244_StartPulses(StepperUM244_Direction_t direction,
                                               uint16_t pulses,
                                               uint16_t frequency_hz,
                                               uint32_t now_ms)
{
	return StepperUM244_StartPulsesInternal(direction,
	                                       pulses,
	                                       frequency_hz,
	                                       now_ms,
	                                       0U);
}

// 函    数：StepperUM244_StartNapMove
// 参    数：direction 自动打盹运动方向；pulses 单次微动脉冲数；now_ms 当前毫秒时间戳。
// 返 回 值：命令状态。
// 注意事项：自动打盹一次最多 BOARD_NAP_MAX_PULSES pulse，防止单次位移过大。
StepperUM244_Status_t StepperUM244_StartNapMove(StepperUM244_Direction_t direction,
                                                uint16_t pulses,
                                                uint32_t now_ms)
{
	if ((pulses == 0U) || (pulses > BOARD_NAP_MAX_PULSES))
	{
		// 自动打盹一次最多 BOARD_NAP_MAX_PULSES pulse，防止单次位移过大。
		return STEPPER_UM244_STATUS_ERROR_PARAM;
	}

	return StepperUM244_StartPulses(direction,
	                               pulses,
	                               BOARD_STEPPER_AUTO_FREQ_HZ,
	                               now_ms);
}

// 函    数：StepperUM244_StartUntilLimit
// 参    数：direction 搜索方向；max_pulses 最大搜索脉冲；frequency_hz 脉冲频率；
//           now_ms 当前毫秒时间戳。
// 返 回 值：命令状态。
// 注意事项：回零类运动允许预期方向限位作为正常停止条件。
StepperUM244_Status_t StepperUM244_StartUntilLimit(StepperUM244_Direction_t direction,
                                                   uint16_t max_pulses,
                                                   uint16_t frequency_hz,
                                                   uint32_t now_ms)
{
	return StepperUM244_StartPulsesInternal(direction,
	                                       max_pulses,
	                                       frequency_hz,
	                                       now_ms,
	                                       1U);
}

// 函    数：StepperUM244_Stop
// 参    数：无
// 返 回 值：无
// 注意事项：主动停止当前运动，释放 STEP 有效电平，并记录停止原因为上层请求。
void StepperUM244_Stop(void)
{
	StepperUM244_StopTimer();
	s_target_pulses = 0U;
	s_state = STEPPER_UM244_STATE_IDLE;
	s_hold_deadline_ms = 0U;
	s_expect_limit_stop = 0U;
	s_stop_reason = STEPPER_UM244_STOP_REQUESTED;
}

// 函    数：StepperUM244_ClearFault
// 参    数：无
// 返 回 值：无
// 注意事项：只清除步进模块内部 FAULT 状态，错误码锁存仍由 error_manager 管理。
void StepperUM244_ClearFault(void)
{
	if (s_state == STEPPER_UM244_STATE_FAULT)
	{
		StepperUM244_StopTimer();
		s_state = STEPPER_UM244_STATE_IDLE;
		s_stop_reason = STEPPER_UM244_STOP_NONE;
	}
}

// 函    数：StepperUM244_SetMotorRelease
// 参    数：release 非 0 释放电机，0 保持电机。
// 返 回 值：无
// 注意事项：释放电机前先停止 STEP；释放后位置可信度应由上层标记为不可信。
void StepperUM244_SetMotorRelease(uint8_t release)
{
	if (release != 0U)
	{
		// 释放电机前先停止 STEP；释放后位置可信度应由上层标记为不可信。
		StepperUM244_Stop();
		StepperUM244_WriteMf(BOARD_UM244_MF_RELEASE_LEVEL);
		s_motor_released = 1U;
	}
	else
	{
		StepperUM244_WriteMf(BOARD_UM244_MF_HOLD_LEVEL);
		s_motor_released = 0U;
	}
}

// 函    数：StepperUM244_IsMotorReleased
// 参    数：无
// 返 回 值：1 表示软件记录为电机释放，0 表示保持。
// 注意事项：返回软件状态，不检测 UM244 MF- 实际电平。
uint8_t StepperUM244_IsMotorReleased(void)
{
	return s_motor_released;
}

// 函    数：StepperUM244_IsBusy
// 参    数：无
// 返 回 值：1 表示模块非空闲，0 表示空闲。
// 注意事项：FAULT、DIR_WAIT、RUNNING、HOLD_WAIT 均视为忙。
uint8_t StepperUM244_IsBusy(void)
{
	return (s_state == STEPPER_UM244_STATE_IDLE) ? 0U : 1U;
}

// 函    数：StepperUM244_GetState
// 参    数：无
// 返 回 值：当前步进内部状态。
// 注意事项：供上层状态机或调试页面查询。
StepperUM244_State_t StepperUM244_GetState(void)
{
	return s_state;
}

// 函    数：StepperUM244_GetCompletedPulses
// 参    数：无
// 返 回 值：当前命令已完成的有效 STEP 脉冲数。
// 注意事项：计数在 TIM2 中断输出 STEP 有效沿时递增。
uint16_t StepperUM244_GetCompletedPulses(void)
{
	return s_completed_pulses;
}

// 函    数：StepperUM244_GetStopReason
// 参    数：无
// 返 回 值：最近一次停止原因。
// 注意事项：上层可据此区分有限脉冲完成、预期限位、非预期限位或主动停止。
StepperUM244_StopReason_t StepperUM244_GetStopReason(void)
{
	return s_stop_reason;
}

// 函    数：StepperUM244_TIM2_IRQHandler
// 参    数：无
// 返 回 值：无
// 注意事项：中断内只允许原始限位急停、有限脉冲计数和 STEP 翻转；显示/恢复交给主循环。
void StepperUM244_TIM2_IRQHandler(void)
{
	StepperUM244_RawLimitResult_t limit_result;

	if (TIM_GetITStatus(STEPPER_TIMER, TIM_IT_Update) == RESET)
	{
		return;
	}

	TIM_ClearITPendingBit(STEPPER_TIMER, TIM_IT_Update);

	if (s_state != STEPPER_UM244_STATE_RUNNING)
	{
		// 中断内不处理复杂状态，只确保定时器停下并释放 STEP。
		StepperUM244_StopTimer();
		return;
	}

	limit_result = StepperUM244_CheckRawLimitInIrq();
	if (limit_result == STEPPER_RAW_LIMIT_EXPECTED)
	{
		// 预期限位停止不置故障，但仍立即停止 STEP。
		StepperUM244_StopTimer();
		s_expect_limit_stop = 0U;
		s_state = STEPPER_UM244_STATE_HOLD_WAIT;
		return;
	}
	if (limit_result != STEPPER_RAW_LIMIT_OK)
	{
		// 任何非预期限位或左右不一致都在中断内立刻停脉冲，显示/恢复交给主循环。
		StepperUM244_StopTimer();
		s_expect_limit_stop = 0U;
		s_state = STEPPER_UM244_STATE_FAULT;
		return;
	}

	if (s_step_active == 0U)
	{
		if (s_completed_pulses >= s_target_pulses)
		{
			StepperUM244_StopTimer();
			s_expect_limit_stop = 0U;
			s_stop_reason = STEPPER_UM244_STOP_PULSE_DONE;
			s_state = STEPPER_UM244_STATE_HOLD_WAIT;
			return;
		}

		// 输出有效沿前已经完成原始限位直读。
		StepperUM244_WriteStep(BOARD_UM244_STEP_ACTIVE_LEVEL);
		s_step_active = 1U;
		s_completed_pulses++;
	}
	else
	{
		StepperUM244_WriteStep(BOARD_UM244_STEP_IDLE_LEVEL);
		s_step_active = 0U;
		if (s_completed_pulses >= s_target_pulses)
		{
			StepperUM244_StopTimer();
			s_expect_limit_stop = 0U;
			s_stop_reason = STEPPER_UM244_STOP_PULSE_DONE;
			s_state = STEPPER_UM244_STATE_HOLD_WAIT;
		}
	}
}
