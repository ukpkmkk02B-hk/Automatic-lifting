#include "buzzer.h"
#include "board_config.h"

static uint8_t s_buzzer_on;

// 函    数：Buzzer_Init
// 参    数：无
// 返 回 值：无
// 注意事项：PA0 低电平会鸣叫，配置 GPIO 前先写高电平，减少上电误鸣风险。
void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOA, ENABLE);

    // 推挽输出配置前先把输出数据寄存器置为关闭电平，避免 GPIO 模式切换瞬间误鸣。
    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_OFF_LEVEL);

    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Pin = BOARD_BUZZER_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_BUZZER_GPIO, &gpio_init);

    // 初始化完成后再次确认静音；默认不在自动模式启动时产生持续报警。
    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_OFF_LEVEL);
    s_buzzer_on = 0U;
}

// 函    数：Buzzer_On
// 参    数：无
// 返 回 值：无
// 注意事项：蜂鸣器只负责声响输出，故障置位由 error_manager 或上层状态机完成。
void Buzzer_On(void)
{
    // 低电平触发有源蜂鸣器模块。
    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_ON_LEVEL);
    s_buzzer_on = 1U;
}

// 函    数：Buzzer_Off
// 参    数：无
// 返 回 值：无
// 注意事项：静音不能清除 error_manager 中的故障锁存。
void Buzzer_Off(void)
{
    // 高电平关闭有源蜂鸣器模块。
    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_OFF_LEVEL);
    s_buzzer_on = 0U;
}

// 函    数：Buzzer_Set
// 参    数：on 非 0 表示鸣叫，0 表示关闭。
// 返 回 值：无
// 注意事项：该接口只根据参数切换 PA0 电平，不改变故障锁存或静音策略。
void Buzzer_Set(uint8_t on)
{
    if (on != 0U)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }
}

// 函    数：Buzzer_IsOn
// 参    数：无
// 返 回 值：1 表示软件记录为鸣叫，0 表示软件记录为关闭。
// 注意事项：用于 OLED 或状态逻辑查询输出命令，不代表实际声压或模块电气健康。
uint8_t Buzzer_IsOn(void)
{
    return s_buzzer_on;
}
