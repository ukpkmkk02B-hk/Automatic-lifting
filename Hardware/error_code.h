#ifndef __ERROR_CODE_H
#define __ERROR_CODE_H

#include "stm32f10x.h"

// 模    块：固定错误码表
// 命名约定：W_ 前缀为一般警告，E_ 前缀为严重故障。
// 安全约束：严重故障应停止自动运动；蜂鸣器静音不改变这些错误码的锁存状态。

typedef enum
{
	// 无错误。
	ERROR_CODE_E_NONE = 0,
	// Flash 参数无效后使用默认值，属于可提示警告。
	ERROR_CODE_W_PARAM_DEFAULT,
	// 用户参数越界或非法，拒绝保存。
	ERROR_CODE_W_PARAM_REJECTED,
	// P_air 连续读数失败。
	ERROR_CODE_E_SENSOR_AIR_FAIL,
	// P_tank 连续读数失败。
	ERROR_CODE_E_SENSOR_TANK_FAIL,
	// P_basket 连续读数失败。
	ERROR_CODE_E_SENSOR_BASKET_FAIL,
	// I2C-A 总线连续恢复失败，对应 P_air。
	ERROR_CODE_E_I2C_A_FAIL,
	// I2C-B 总线连续恢复失败，对应 P_basket。
	ERROR_CODE_E_I2C_B_FAIL,
	// I2C-C 总线连续恢复失败，对应 P_tank。
	ERROR_CODE_E_I2C_C_FAIL,
	// 鱼缸整体水深低于安全下限。
	ERROR_CODE_E_TANK_LOW,
	// 鱼缸整体水深高于合理上限。
	ERROR_CODE_E_TANK_HIGH,
	// 框篮内可活动水深低于安全下限。
	ERROR_CODE_E_BASKET_LOW,
	// 框篮内可活动水深高于合理上限。
	ERROR_CODE_E_BASKET_HIGH,
	// 1 分钟内水深突变超过阈值。
	ERROR_CODE_E_WATER_JUMP,
	// 压力差换算水深小于 -2mm 等物理异常。
	ERROR_CODE_E_PRESSURE_PHYSICAL,
	// 上升方向触发上限位。
	ERROR_CODE_E_UPPER_LIMIT,
	// 下降方向触发下限位。
	ERROR_CODE_E_LOWER_LIMIT,
	// 左右同方向限位状态不一致。
	ERROR_CODE_E_LIMIT_MISMATCH,
	// 机械位置未经回零或因异常变为不可信。
	ERROR_CODE_E_POSITION_UNTRUSTED,
	// 累计位移后水深趋势不符合预期，判断为卡滞风险。
	ERROR_CODE_E_STALL,
	// 自动目标水深超出 ±1mm 控制容差，或断电恢复水深差异超过 3mm。
	ERROR_CODE_E_DEPTH_TRACKING,
	// 开机自检失败。
	ERROR_CODE_E_SELF_TEST_FAIL,
	// 电机释放后请求自动运行，位置和保持状态不安全。
	ERROR_CODE_E_MOTOR_RELEASED,
	ERROR_CODE_COUNT
} ErrorCode_t;

typedef enum
{
	// 无活动错误。
	ERROR_LEVEL_NONE = 0,
	// 警告：可提示用户，但不一定要求立即停机。
	ERROR_LEVEL_WARNING,
	// 严重故障：自动运动必须停止并等待人工确认。
	ERROR_LEVEL_FAULT
} ErrorLevel_t;

#endif
