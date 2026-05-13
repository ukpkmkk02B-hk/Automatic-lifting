#ifndef __WF5805F_H
#define __WF5805F_H

#include "stm32f10x.h"

// 模    块：WF5805F 绝对压力传感器驱动
// 硬件假设：三颗 4 引脚 WF5805F 模块地址固定为 7-bit 0x6D。
// 总线约束：P_air/P_basket/P_tank 分别位于独立软件 I2C-A/B/C，不能挂在同一总线。
// 输出单位：压力为 hPa_x100，温度为 摄氏度_x100，时间戳为 ms。

typedef enum
{
	// 空气参考传感器 P_air，安装在控制盒内接触空气。
	WF5805F_SENSOR_AIR = 0,
	// 框篮传感器 P_basket，随框篮移动，用于计算框篮水深。
	WF5805F_SENSOR_BASKET,
	// 鱼缸传感器 P_tank，固定在鱼缸底部，用于整体水深安全判断。
	WF5805F_SENSOR_TANK,
	WF5805F_SENSOR_COUNT
} WF5805F_Sensor_t;

typedef enum
{
	// 读数有效或操作成功。
	WF5805F_OK = 0,
	// 转换尚未完成，调用方应稍后再读。
	WF5805F_PENDING,
	// 参数非法。
	WF5805F_ERROR_PARAM,
	// I2C 起始、地址、寄存器或数据阶段失败。
	WF5805F_ERROR_I2C,
	// 等待传感器转换完成或总线电平释放超时。
	WF5805F_ERROR_TIMEOUT,
	// 传感器状态寄存器报告内部错误。
	WF5805F_ERROR_STATUS
} WF5805F_Status_t;

typedef struct
{
	// 绝对压力，单位 hPa_x100。水深模块会用 P_sensor - P_air 做差压。
	int32_t pressure_hpa_x100;
	// 温度，单位 摄氏度_x100，用于诊断传感器状态。
	int16_t temperature_c_x100;
	// 原始 5 字节：3 字节压力 + 2 字节温度，保留便于调试。
	uint8_t raw[5];
	// 最近一次状态寄存器值。
	uint8_t status;
	// 1 表示读数有效，0 表示正在等待或最近一次读取失败。
	uint8_t valid;
	// 最近一次有效读数的系统毫秒时间戳。
	uint32_t timestamp_ms;
} WF5805F_Reading_t;

// 函    数：WF5805F_InitAll
// 参    数：无
// 返 回 值：无
// 注意事项：初始化三颗传感器的独立软件 I2C 总线和内部状态；初始读数为无效。
void WF5805F_InitAll(void);

// 函    数：WF5805F_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：非阻塞轮询三颗传感器：启动转换、轮询状态、读取 5 字节数据。
void WF5805F_Update(uint32_t now_ms);

// 函    数：WF5805F_GetReading
// 参    数：sensor 传感器编号，范围为 WF5805F_Sensor_t。
// 参    数：reading 输出最近一次读数的结构体指针。
// 返 回 值：WF5805F_OK 表示读数有效；读数无效时返回最近错误码或 PENDING。
// 注意事项：调用方不能在非 OK 时使用旧值伪装有效读数。
WF5805F_Status_t WF5805F_GetReading(WF5805F_Sensor_t sensor, WF5805F_Reading_t *reading);

// 函    数：WF5805F_GetFailureCount
// 参    数：sensor 传感器编号，范围为 WF5805F_Sensor_t。
// 返 回 值：连续读数失败次数，非法传感器返回 0。
// 注意事项：水深模块按阈值把连续失败升级为传感器故障。
uint16_t WF5805F_GetFailureCount(WF5805F_Sensor_t sensor);

// 函    数：WF5805F_GetRecoveryFailureCount
// 参    数：sensor 传感器编号，范围为 WF5805F_Sensor_t。
// 返 回 值：I2C 总线恢复失败次数，非法传感器返回 0。
// 注意事项：水深模块按阈值把连续恢复失败升级为对应 I2C 故障。
uint16_t WF5805F_GetRecoveryFailureCount(WF5805F_Sensor_t sensor);

// 函    数：WF5805F_ResetBus
// 参    数：sensor 传感器编号，范围为 WF5805F_Sensor_t。
// 返 回 值：WF5805F_OK 表示恢复成功，其它值表示参数或 I2C 恢复失败。
// 注意事项：手动恢复指定传感器总线，成功后重新回到空闲采样状态。
WF5805F_Status_t WF5805F_ResetBus(WF5805F_Sensor_t sensor);

#endif
