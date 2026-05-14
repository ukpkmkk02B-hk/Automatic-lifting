#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"
#include "ui_pages.h"

// 模    块：OLED 菜单与按键交互
// 职    责：消费 key_scan 事件，驱动 ui_pages 显示，处理参数编辑和维护页面交互。
// 安全假设：阶段 8 起 app_state 提供应用模式快照；菜单不拥有自动运行状态。

typedef struct
{
	// 1 表示快照有效；无效时菜单沿用阶段 7 的本地显示数据。
	uint8_t valid;
	// 1 表示强制显示自检页面，防止启动期页面切换覆盖自检状态。
	uint8_t force_self_test_page;
	// app_state 提供的当前应用显示模式。
	UiPages_Mode_t mode;
	// 自检页面上下文，含倒计时、I2C 初步状态和限位状态。
	UiPages_SelfTestContext_t self_test;
	// 主页面目标水深，单位 mm_x10。
	uint8_t target_depth_valid;
	int32_t target_depth_mm_x10;
	// 上电累计运行天数，显示范围 1..999。
	uint16_t run_days;
	// 当前运动/等待文本，必须为 4 字符以内 ASCII。
	const char *motion_text;
	// 下次打盹倒计时，单位 second。
	uint8_t next_nap_valid;
	uint32_t next_nap_remaining_s;
	// 当天已完成自动脉冲数和单次打盹脉冲数，单位 pulse。
	uint16_t today_done_pulses;
	uint16_t nap_pulses;
} Menu_AppSnapshot_t;

typedef struct
{
	// 1 表示用户请求从暂停进入自动；app_state 必须再次检查故障、限位、位置和水深。
	uint8_t start_auto;
	// 1 表示用户请求暂停自动或手动运动；app_state 负责停止 STEP 并保存运行状态。
	uint8_t pause;
	// 1 表示 PB10 报警确认：首次只静音，再次由 app_state 受控清除故障并进入暂停。
	uint8_t alarm_ack;
	// 1 表示用户确认进入维护模式。
	uint8_t enter_maintenance;
	// 1 表示用户退出维护模式。
	uint8_t exit_maintenance;
	// 1 表示维护模式下请求空气参考校准。
	uint8_t air_calibrate;
	// 1 表示维护模式下请求回零流程。
	uint8_t home_zero;
	// 1 表示维护模式下请求切换 MF 电机释放/保持。
	uint8_t motor_release_toggle;
	// 1 表示参数页已经成功保存，app_state 需要重新加载参数并刷新打盹调度。
	uint8_t params_saved;
	// 1 表示手动页面中 PB11 保持按下，app_state 可发上升有限脉冲小段。
	uint8_t manual_up_hold;
	// 1 表示手动页面中 PB1 保持按下，app_state 可发下降有限脉冲小段。
	uint8_t manual_down_hold;
} Menu_Intents_t;

// 函    数：Menu_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：初始化菜单状态并读取 Flash 参数缓存；不主动启动自动运动。
void Menu_Init(uint32_t now_ms);

// 函    数：Menu_SetAppSnapshot
// 参    数：snapshot app_state 提供的应用显示快照。
// 返 回 值：无
// 注意事项：只复制显示数据，不启动电机、不写 Flash。
void Menu_SetAppSnapshot(const Menu_AppSnapshot_t *snapshot);

// 函    数：Menu_GetIntents
// 参    数：intents 输出并清除菜单产生的一次性用户意图。
// 返 回 值：无
// 注意事项：手动保持类字段按当前按键稳定状态实时输出；本函数不启动电机、不写 Flash。
void Menu_GetIntents(Menu_Intents_t *intents);

// 函    数：Menu_ReloadParams
// 参    数：无
// 返 回 值：无
// 注意事项：app_state 修改参数后调用，刷新参数页显示缓存。
void Menu_ReloadParams(void);

// 函    数：Menu_Update
// 参    数：now_ms 当前系统毫秒时间戳；key_events KeyScan_GetEvents() 读取并清除后的事件位图。
// 返 回 值：无
// 注意事项：非阻塞轮询接口；需要在主循环中周期调用，不能在中断中调用。
void Menu_Update(uint32_t now_ms, uint16_t key_events);

#endif
