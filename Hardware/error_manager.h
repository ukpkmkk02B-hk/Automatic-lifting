#ifndef __ERROR_MANAGER_H
#define __ERROR_MANAGER_H

#include "error_code.h"

// 模    块：错误锁存管理
// 职    责：记录、查询和清除错误码；实际停机、OLED 显示和蜂鸣器输出由上层处理。
// 安全约束：严重故障一旦置位会保持锁存，蜂鸣器静音不会清除故障。

// 函    数：ErrorManager_Init
// 参    数：无
// 返 回 值：无
// 注意事项：清空错误锁存和蜂鸣器静音状态，通常仅在系统初始化时调用。
void ErrorManager_Init(void);

// 函    数：ErrorManager_Set
// 参    数：code 需要锁存的错误码。
// 返 回 值：无
// 注意事项：非法或 E_NONE 参数会被忽略；有效错误采用锁存方式保存。
void ErrorManager_Set(ErrorCode_t code);

// 函    数：ErrorManager_Clear
// 参    数：code 需要清除的错误码。
// 返 回 值：无
// 注意事项：严重故障应在原因消除并人工确认后才调用。
void ErrorManager_Clear(ErrorCode_t code);

// 函    数：ErrorManager_ClearAll
// 参    数：无
// 返 回 值：无
// 注意事项：清除全部错误锁存；仅用于受控恢复流程或初始化，不应由蜂鸣器静音触发。
void ErrorManager_ClearAll(void);

// 函    数：ErrorManager_IsActive
// 参    数：code 需要查询的错误码。
// 返 回 值：1 表示该错误码处于锁存状态，0 表示未锁存或参数非法。
// 注意事项：只查询错误锁存，不判断错误级别。
uint8_t ErrorManager_IsActive(ErrorCode_t code);

// 函    数：ErrorManager_HasFault
// 参    数：无
// 返 回 值：存在任一严重故障返回 1，否则返回 0。
// 注意事项：上层状态机应在返回 1 时停止自动运动。
uint8_t ErrorManager_HasFault(void);

// 函    数：ErrorManager_GetPrimary
// 参    数：无
// 返 回 值：当前最高优先级错误码；优先严重故障，再返回警告。
// 注意事项：主要供 OLED 和报警显示使用。
ErrorCode_t ErrorManager_GetPrimary(void);

// 函    数：ErrorManager_GetLevel
// 参    数：code 需要判断级别的错误码。
// 返 回 值：错误级别：警告、严重故障或无错误。
// 注意事项：参数类问题为警告，其它有效错误码当前按严重故障处理。
ErrorLevel_t ErrorManager_GetLevel(ErrorCode_t code);

// 函    数：ErrorManager_SetBuzzerMuted
// 参    数：muted 非 0 表示静音，0 表示取消静音。
// 返 回 值：无
// 注意事项：该标志只影响声音策略，不改变任何错误锁存位。
void ErrorManager_SetBuzzerMuted(uint8_t muted);

// 函    数：ErrorManager_IsBuzzerMuted
// 参    数：无
// 返 回 值：1 表示蜂鸣器静音标志已置位，0 表示未静音。
// 注意事项：静音状态独立于故障锁存。
uint8_t ErrorManager_IsBuzzerMuted(void);

// 函    数：ErrorManager_GetName
// 参    数：code 需要转换为 ASCII 名称的错误码。
// 返 回 值：错误码名称字符串，未知值返回 E_UNKNOWN。
// 注意事项：供 OLED 或调试输出使用，字符串为静态常量。
const char *ErrorManager_GetName(ErrorCode_t code);

#endif
