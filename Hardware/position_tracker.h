#ifndef __POSITION_TRACKER_H
#define __POSITION_TRACKER_H

#include "stm32f10x.h"
#include "stepper_um244.h"

typedef enum
{
	POSITION_TRACKER_UNTRUSTED_NONE = 0,
	POSITION_TRACKER_UNTRUSTED_NOT_HOMED,
	POSITION_TRACKER_UNTRUSTED_HOMING_STARTED,
	POSITION_TRACKER_UNTRUSTED_MOTOR_RELEASED,
	POSITION_TRACKER_UNTRUSTED_LIMIT_MISMATCH,
	POSITION_TRACKER_UNTRUSTED_STALL,
	POSITION_TRACKER_UNTRUSTED_RANGE
} PositionTracker_UntrustedReason_t;

void PositionTracker_Init(void);
void PositionTracker_MarkHomed(void);
void PositionTracker_MarkUntrusted(PositionTracker_UntrustedReason_t reason);
uint8_t PositionTracker_IsTrusted(void);
uint8_t PositionTracker_CanAutoRun(void);
int32_t PositionTracker_GetPulses(void);
int32_t PositionTracker_GetMmX10(void);
PositionTracker_UntrustedReason_t PositionTracker_GetUntrustedReason(void);
void PositionTracker_ApplyCompletedMove(StepperUM244_Direction_t direction, uint16_t pulses);
void PositionTracker_ServiceSafety(void);

#endif
