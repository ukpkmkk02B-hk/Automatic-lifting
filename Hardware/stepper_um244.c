#include "stepper_um244.h"
#include "board_config.h"
#include "limit.h"
#include "error_manager.h"

#define STEPPER_TIMER                    TIM2
#define STEPPER_TIMER_RCC                RCC_APB1Periph_TIM2
#define STEPPER_TIMER_IRQ                TIM2_IRQn
#define STEPPER_TIMER_TICK_HZ            1000000UL
#define STEPPER_MIN_FREQ_HZ              1U

typedef enum
{
	STEPPER_RAW_LIMIT_OK = 0,
	STEPPER_RAW_LIMIT_EXPECTED,
	STEPPER_RAW_LIMIT_FAULT,
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

static void StepperUM244_WriteStep(BitAction level)
{
	GPIO_WriteBit(BOARD_UM244_STEP_GPIO, BOARD_UM244_STEP_PIN, level);
}

static void StepperUM244_WriteDir(StepperUM244_Direction_t direction)
{
	if (direction == STEPPER_UM244_DIRECTION_UP)
	{
		GPIO_WriteBit(BOARD_UM244_DIR_GPIO, BOARD_UM244_DIR_PIN, BOARD_UM244_DIR_UP_LEVEL);
	}
	else
	{
		GPIO_WriteBit(BOARD_UM244_DIR_GPIO, BOARD_UM244_DIR_PIN, BOARD_UM244_DIR_DOWN_LEVEL);
	}
}

static void StepperUM244_WriteMf(BitAction level)
{
	GPIO_WriteBit(BOARD_UM244_MF_GPIO, BOARD_UM244_MF_PIN, level);
}

static uint8_t StepperUM244_TimeElapsed(uint32_t now_ms, uint32_t deadline_ms)
{
	return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1U : 0U;
}

static Limit_Direction_t StepperUM244_ToLimitDirection(StepperUM244_Direction_t direction)
{
	return (direction == STEPPER_UM244_DIRECTION_UP) ? LIMIT_DIRECTION_UP : LIMIT_DIRECTION_DOWN;
}

static void StepperUM244_StopTimer(void)
{
	TIM_Cmd(STEPPER_TIMER, DISABLE);
	TIM_ClearITPendingBit(STEPPER_TIMER, TIM_IT_Update);
	StepperUM244_WriteStep(BOARD_UM244_STEP_IDLE_LEVEL);
	s_step_active = 0U;
}

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

static uint8_t StepperUM244_CheckFilteredLimit(StepperUM244_Direction_t direction)
{
	Limit_Direction_t limit_direction;

	limit_direction = StepperUM244_ToLimitDirection(direction);

	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_LIMIT_MISMATCH);
		return 1U;
	}

	if (Limit_IsDirectionBlocked(limit_direction) != 0U)
	{
		StepperUM244_SetLimitError(direction);
		return 1U;
	}

	return 0U;
}

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
				s_stop_reason = STEPPER_UM244_STOP_EXPECTED_LIMIT;
				return STEPPER_RAW_LIMIT_EXPECTED;
			}
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
				s_stop_reason = STEPPER_UM244_STOP_EXPECTED_LIMIT;
				return STEPPER_RAW_LIMIT_EXPECTED;
			}
			ErrorManager_Set(ERROR_CODE_E_LOWER_LIMIT);
			s_stop_reason = STEPPER_UM244_STOP_LIMIT_FAULT;
			return STEPPER_RAW_LIMIT_FAULT;
		}
	}

	return STEPPER_RAW_LIMIT_OK;
}

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

static void StepperUM244_StartTimer(void)
{
	StepperUM244_ConfigureTimer((uint16_t)s_frequency_hz);
	TIM_SetCounter(STEPPER_TIMER, 0U);
	TIM_Cmd(STEPPER_TIMER, ENABLE);
	s_state = STEPPER_UM244_STATE_RUNNING;
}

void StepperUM244_Init(void)
{
	GPIO_InitTypeDef gpio_init;
	NVIC_InitTypeDef nvic_init;

	RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(STEPPER_TIMER_RCC, ENABLE);

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
			s_hold_deadline_ms = now_ms + BOARD_DIR_SETUP_HOLD_MS;
		}
		if (StepperUM244_TimeElapsed(now_ms, s_hold_deadline_ms) != 0U)
		{
			s_hold_deadline_ms = 0U;
			s_state = STEPPER_UM244_STATE_IDLE;
		}
	}
}

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
		if (s_state == STEPPER_UM244_STATE_FAULT)
		{
			return STEPPER_UM244_STATUS_ERROR_FAULT;
		}
		return STEPPER_UM244_STATUS_BUSY;
	}

	if (StepperUM244_CheckFilteredLimit(direction) != 0U)
	{
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
	StepperUM244_WriteMf(BOARD_UM244_MF_HOLD_LEVEL);
	s_motor_released = 0U;

	s_dir_ready_ms = now_ms + BOARD_DIR_SETUP_HOLD_MS;
	s_state = STEPPER_UM244_STATE_DIR_WAIT;

	return STEPPER_UM244_STATUS_OK;
}

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

StepperUM244_Status_t StepperUM244_StartNapMove(StepperUM244_Direction_t direction,
                                                uint16_t pulses,
                                                uint32_t now_ms)
{
	if ((pulses == 0U) || (pulses > BOARD_NAP_MAX_PULSES))
	{
		return STEPPER_UM244_STATUS_ERROR_PARAM;
	}

	return StepperUM244_StartPulses(direction,
	                               pulses,
	                               BOARD_STEPPER_AUTO_FREQ_HZ,
	                               now_ms);
}

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

void StepperUM244_Stop(void)
{
	StepperUM244_StopTimer();
	s_target_pulses = 0U;
	s_state = STEPPER_UM244_STATE_IDLE;
	s_hold_deadline_ms = 0U;
	s_expect_limit_stop = 0U;
	s_stop_reason = STEPPER_UM244_STOP_REQUESTED;
}

void StepperUM244_ClearFault(void)
{
	if (s_state == STEPPER_UM244_STATE_FAULT)
	{
		StepperUM244_StopTimer();
		s_state = STEPPER_UM244_STATE_IDLE;
		s_stop_reason = STEPPER_UM244_STOP_NONE;
	}
}

void StepperUM244_SetMotorRelease(uint8_t release)
{
	if (release != 0U)
	{
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

uint8_t StepperUM244_IsMotorReleased(void)
{
	return s_motor_released;
}

uint8_t StepperUM244_IsBusy(void)
{
	return (s_state == STEPPER_UM244_STATE_IDLE) ? 0U : 1U;
}

StepperUM244_State_t StepperUM244_GetState(void)
{
	return s_state;
}

uint16_t StepperUM244_GetCompletedPulses(void)
{
	return s_completed_pulses;
}

StepperUM244_StopReason_t StepperUM244_GetStopReason(void)
{
	return s_stop_reason;
}

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
		StepperUM244_StopTimer();
		return;
	}

	limit_result = StepperUM244_CheckRawLimitInIrq();
	if (limit_result == STEPPER_RAW_LIMIT_EXPECTED)
	{
		StepperUM244_StopTimer();
		s_expect_limit_stop = 0U;
		s_state = STEPPER_UM244_STATE_HOLD_WAIT;
		return;
	}
	if (limit_result != STEPPER_RAW_LIMIT_OK)
	{
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
