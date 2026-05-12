#ifndef __ERROR_MANAGER_H
#define __ERROR_MANAGER_H

#include "error_code.h"

void ErrorManager_Init(void);
void ErrorManager_Set(ErrorCode_t code);
void ErrorManager_Clear(ErrorCode_t code);
void ErrorManager_ClearAll(void);
uint8_t ErrorManager_IsActive(ErrorCode_t code);
uint8_t ErrorManager_HasFault(void);
ErrorCode_t ErrorManager_GetPrimary(void);
ErrorLevel_t ErrorManager_GetLevel(ErrorCode_t code);
void ErrorManager_SetBuzzerMuted(uint8_t muted);
uint8_t ErrorManager_IsBuzzerMuted(void);
const char *ErrorManager_GetName(ErrorCode_t code);

#endif
