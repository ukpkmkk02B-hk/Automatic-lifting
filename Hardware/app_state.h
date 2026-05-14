#ifndef __APP_STATE_H
#define __APP_STATE_H

#include "stm32f10x.h"

// 模    块：主应用状态机
// 职    责：统一管理自检、暂停、自动候选、手动/维护、故障等应用状态。
// 阶段 8 安全约束：自动打盹只通过有限 STEP 脉冲执行，故障/限位/传感器异常优先停机。

typedef enum
{
	// 开机自检，等待传感器稳定并检查基础安全条件。
	APP_STATE_SELF_TEST = 0,
	// 暂停等待人工确认。
	APP_STATE_PAUSED,
	// 自动运行判定状态。
	APP_STATE_AUTO_RUN,
	// 等待下一次打盹。
	APP_STATE_NAP_WAIT,
	// 执行一次打盹有限脉冲。
	APP_STATE_NAP_MOVE,
	// 手动点动状态。
	APP_STATE_MANUAL,
	// 维护模式。
	APP_STATE_MAINTENANCE,
	// 电机释放维护子状态。
	APP_STATE_MOTOR_RELEASE,
	// 故障状态，自动运动必须停止。
	APP_STATE_FAULT
} AppState_State_t;

// 函    数：AppState_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：初始化主状态机、菜单服务、自检和打盹调度；自动恢复只在自检通过后判断。
void AppState_Init(uint32_t now_ms);

// 函    数：AppState_Update
// 参    数：now_ms 当前系统毫秒时间戳；key_events 本轮按键事件位图。
// 返 回 值：无
// 注意事项：主循环周期调用；安全故障优先于自动/手动状态。
void AppState_Update(uint32_t now_ms, uint16_t key_events);

// 函    数：AppState_GetState
// 参    数：无
// 返 回 值：当前应用状态。
// 注意事项：用于调试或后续 UI 扩展。
AppState_State_t AppState_GetState(void);

#endif
