#ifndef __SELF_TEST_H
#define __SELF_TEST_H

#include "stm32f10x.h"
#include "ui_pages.h"

// 模    块：开机自检状态机
// 职    责：在主状态机中非阻塞执行传感器稳定等待、I2C/限位/水深/参数检查。
// 安全假设：本模块不启动电机、不写 Flash；自检失败只锁存错误码，由 app_state 停止运动并进入故障。

typedef enum
{
	// 自检仍在等待传感器稳定或等待有效读数。
	SELF_TEST_STATUS_PENDING = 0,
	// 自检通过，可以进入断电恢复或暂停流程。
	SELF_TEST_STATUS_PASS,
	// 自检失败，已由本模块锁存 E_SELF_TEST_FAIL 或相关底层错误。
	SELF_TEST_STATUS_FAIL
} SelfTest_Status_t;

// 函    数：SelfTest_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：启动 10s 传感器稳定等待，并安排一次短蜂鸣；不会阻塞主循环。
void SelfTest_Init(uint32_t now_ms);

// 函    数：SelfTest_Update
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：自检状态。
// 注意事项：非阻塞推进；等待期内只返回 PENDING，等待结束后执行一次完整检查。
SelfTest_Status_t SelfTest_Update(uint32_t now_ms);

// 函    数：SelfTest_ServiceBuzzer
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：只处理开机短鸣，故障间歇报警仍由菜单/错误管理策略处理。
void SelfTest_ServiceBuzzer(uint32_t now_ms);

// 函    数：SelfTest_GetContext
// 参    数：ctx 输出 OLED 自检页面上下文。
// 返 回 值：无
// 注意事项：用于 UI 显示当前等待倒计时、I2C 初步状态和限位状态。
void SelfTest_GetContext(UiPages_SelfTestContext_t *ctx);

// 函    数：SelfTest_GetStatus
// 参    数：无
// 返 回 值：最近一次自检状态。
// 注意事项：供 app_state 查询状态，不会重新执行检查。
SelfTest_Status_t SelfTest_GetStatus(void);

#endif
