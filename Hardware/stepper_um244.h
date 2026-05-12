#ifndef __STEPPER_UM244_H
#define __STEPPER_UM244_H

#include "stm32f10x.h"

typedef enum
{
	STEPPER_UM244_DIRECTION_UP = 0,
	STEPPER_UM244_DIRECTION_DOWN
} StepperUM244_Direction_t;

typedef enum
{
	STEPPER_UM244_STATUS_OK = 0,
	STEPPER_UM244_STATUS_BUSY,
	STEPPER_UM244_STATUS_ERROR_PARAM,
	STEPPER_UM244_STATUS_ERROR_LIMIT,
	STEPPER_UM244_STATUS_ERROR_FAULT
} StepperUM244_Status_t;

typedef enum
{
	STEPPER_UM244_STOP_NONE = 0,
	STEPPER_UM244_STOP_PULSE_DONE,
	STEPPER_UM244_STOP_EXPECTED_LIMIT,
	STEPPER_UM244_STOP_LIMIT_FAULT,
	STEPPER_UM244_STOP_MISMATCH_FAULT,
	STEPPER_UM244_STOP_REQUESTED
} StepperUM244_StopReason_t;

typedef enum
{
	STEPPER_UM244_STATE_IDLE = 0,
	STEPPER_UM244_STATE_DIR_WAIT,
	STEPPER_UM244_STATE_RUNNING,
	STEPPER_UM244_STATE_HOLD_WAIT,
	STEPPER_UM244_STATE_FAULT
} StepperUM244_State_t;

void StepperUM244_Init(void);
void StepperUM244_Poll(uint32_t now_ms);
StepperUM244_Status_t StepperUM244_StartPulses(StepperUM244_Direction_t direction,
                                               uint16_t pulses,
                                               uint16_t frequency_hz,
                                               uint32_t now_ms);
StepperUM244_Status_t StepperUM244_StartNapMove(StepperUM244_Direction_t direction,
                                                uint16_t pulses,
                                                uint32_t now_ms);
StepperUM244_Status_t StepperUM244_StartUntilLimit(StepperUM244_Direction_t direction,
                                                   uint16_t max_pulses,
                                                   uint16_t frequency_hz,
                                                   uint32_t now_ms);
void StepperUM244_Stop(void);
void StepperUM244_ClearFault(void);
void StepperUM244_SetMotorRelease(uint8_t release);
uint8_t StepperUM244_IsMotorReleased(void);
uint8_t StepperUM244_IsBusy(void);
StepperUM244_State_t StepperUM244_GetState(void);
uint16_t StepperUM244_GetCompletedPulses(void);
StepperUM244_StopReason_t StepperUM244_GetStopReason(void);
void StepperUM244_TIM2_IRQHandler(void);

#endif
