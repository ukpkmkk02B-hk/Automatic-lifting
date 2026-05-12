#ifndef __HOMING_H
#define __HOMING_H

#include "stm32f10x.h"

typedef enum
{
	HOMING_STATUS_OK = 0,
	HOMING_STATUS_BUSY,
	HOMING_STATUS_ERROR_PARAM,
	HOMING_STATUS_ERROR_LIMIT,
	HOMING_STATUS_ERROR_FAULT,
	HOMING_STATUS_ERROR_TIMEOUT,
	HOMING_STATUS_CANCELLED
} Homing_Status_t;

typedef enum
{
	HOMING_STATE_IDLE = 0,
	HOMING_STATE_START_DOWN_FIRST,
	HOMING_STATE_DOWN_FIRST,
	HOMING_STATE_START_BACKOFF,
	HOMING_STATE_BACKOFF_UP,
	HOMING_STATE_WAIT_RELEASE,
	HOMING_STATE_START_DOWN_SECOND,
	HOMING_STATE_DOWN_SECOND,
	HOMING_STATE_COMPLETE,
	HOMING_STATE_FAULT,
	HOMING_STATE_CANCELLED
} Homing_State_t;

void Homing_Init(void);
Homing_Status_t Homing_Start(uint32_t now_ms);
void Homing_Update(uint32_t now_ms);
void Homing_Cancel(void);
uint8_t Homing_IsBusy(void);
uint8_t Homing_IsComplete(void);
Homing_State_t Homing_GetState(void);
Homing_Status_t Homing_GetLastStatus(void);
uint32_t Homing_GetSearchPulses(void);

#endif
