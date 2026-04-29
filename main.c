#include <fw_hal.h>

#define uchar unsigned char
#define uint unsigned int
#define SET_MAX 300
#define LVD2V0 0x00 //LVD@2.0V
#define LVD2V4 0x01 //LVD@2.4V
#define LVD2V7 0x02 //LVD@2.7V
#define LVD3V0 0x03 //LVD@3.0V
#define EEPROM_ADDR 0x0400 //EEPROM地址

unsigned int display_data[3];
volatile unsigned int set_temp = 20;     // 开机初始化温度
unsigned char ec11_stepping = 5; // 旋转编码器步进
volatile unsigned int sw_a;
unsigned char __CODE smg_duan[10] = {0xe7, 0x05, 0xe9, 0xad, 0x0f, 0xae, 0xee, 0x85, 0xef, 0xaf};

volatile unsigned char current_gear = 0;
volatile unsigned int gear_temps[4] = {0, 150, 200, 250};
volatile unsigned char eeprom_save_flag = 0;

// 定时器与状态变量
volatile unsigned int show_target_timeout = 0;
volatile unsigned int pwm_counter = 0;
volatile unsigned char display_counter = 0;
volatile unsigned char display_digit = 0;
volatile unsigned char read_temp_flag = 1;
volatile unsigned int read_temp_timer = 0;
volatile unsigned char update_display_flag = 0;
volatile unsigned char dp_blink_state = 0;
volatile unsigned int dp_blink_timer = 0;
unsigned char __CODE dp_blink_seq[6] = {2, 1, 0, 0, 1, 2};

void save_eeprom(void) {
    int i;
    IAP_CmdErase(EEPROM_ADDR);
    IAP_WriteData(0xAA); // Magic byte
    IAP_CmdWrite(EEPROM_ADDR+0);
    IAP_WriteData(current_gear);
    IAP_CmdWrite(EEPROM_ADDR+1);
    for (i = 0; i < 4; i++) {
        IAP_WriteData(gear_temps[i] >> 8);
        IAP_CmdWrite(EEPROM_ADDR + 2 + i * 2);
        IAP_WriteData(gear_temps[i] & 0xFF);
        IAP_CmdWrite(EEPROM_ADDR + 3 + i * 2);
    }
}

SBIT(SO, _P3, 4);     // P3.4口与SO相连
SBIT(SCK, _P3, 0);    // P3.0口与SCK相连
SBIT(CS, _P3, 1);     // P3.1口与CS相连
SBIT(LSA, _P3, 5);    // 数码管位选信号
SBIT(LSB, _P3, 6);    // 数码管位选信号
SBIT(LSC, _P3, 7);    // 数码管位选信号
SBIT(EC11_A, _P3, 3); // 旋转编码器A相
SBIT(EC11_B, _P3, 2); // 旋转编码器B相
SBIT(EC11_E, _P5, 4); // 旋转编码器E相
SBIT(sw, _P5, 5);     // 固态继电器控制

// PID参数 (按100倍放大为整数)
long kp = 1400;      // 比例控制 (14.00 * 100)
long ki = 9000;      // 积分控制 (90.00 * 100)
long kd = 21000;     // 微分控制 (210.00 * 100)
long integral = 0;   // 积分项
int last_error = 0;  // 上次误差

void setup(void) // 初始化函数
{
    int i;
    P1M1 = 0x00; // P1口推挽输出
    P1M0 = 0xff;
    P3M1 = 0x00; // P3口准双向
    P3M0 = 0x00;
    P5M1 = 0x00; // P5.4口准双向，P5.5口推挽输出
    P5M0 = 0x20;

    IAP_SetWaitTime();
    IAP_SetEnabled(HAL_State_ON);

    IAP_CmdRead(EEPROM_ADDR+0);
    if (IAP_ReadData() == 0xAA) {
        IAP_CmdRead(EEPROM_ADDR+1);
        current_gear = IAP_ReadData();
        if (current_gear > 3) current_gear = 0;

        for (i = 0; i < 4; i++) {
            IAP_CmdRead(EEPROM_ADDR + 2 + i * 2);
            gear_temps[i] = (IAP_ReadData() << 8);
            IAP_CmdRead(EEPROM_ADDR + 3 + i * 2);
            gear_temps[i] |= IAP_ReadData();
        }
        set_temp = gear_temps[current_gear];
    } else {
        current_gear = 0;
        gear_temps[0] = 0;
        gear_temps[1] = 150;
        gear_temps[2] = 200;
        gear_temps[3] = 250;
        set_temp = gear_temps[current_gear];
    }

    // 配置定时器0：16位自动重装载，2000Hz (0.5ms)
    TIM_Timer0_Config(HAL_State_ON, TIM_TimerMode_16BitAuto, 2000);
    EXTI_Timer0_SetIntState(HAL_State_ON);
    EXTI_Global_SetIntState(HAL_State_ON);
    TIM_Timer0_SetRunState(HAL_State_ON);
}

unsigned int max6675_read_reg(void) // 读取6675数据
{
    unsigned char i;
    unsigned int dat;

    i = 0;
    dat = 0;

    SCK = 0;
    CS = 0;

    for (i = 0; i < 16; i++)
    {
        SO = 1;
        SCK = 1;
        dat = dat << 1;
        if (SO == 1)
            dat = dat | 0x01;
        SCK = 0;
    }
    CS = 1;
    dat = dat << 1; // 读出来的数据的D3~D14是温度值
    if (dat & 0x04)
    {
        return 9999;
    }
    dat = dat >> 4;
    dat = dat / 4; // 测得的温度单位是0.25，所以要乘以0.25（即除以4）才能得到以度为单位的温度值

    return dat;
}

void update_display_data(unsigned int temp) // 更新显示数据
{
    display_data[0] = temp / 1 % 10;
    display_data[1] = temp / 10 % 10;
    display_data[2] = temp / 100 % 10;
}

void ec11_scan(void) // 旋转编码器驱动
{
    static __BIT flag = 1;
    static unsigned int time_since_last_turn = 0;
    unsigned char current_step = 1;

    if (time_since_last_turn < 60000)
    {
        time_since_last_turn++;
    }

    if ((EC11_A != EC11_B) && (flag))
    {
        if (time_since_last_turn < 200)
        {
            current_step = ec11_stepping;
        }
        else
        {
            current_step = 1;
        }
        time_since_last_turn = 0;

        if (EC11_A)
        {
            set_temp = set_temp + (int)current_step;
            if (set_temp > SET_MAX) {
                set_temp = SET_MAX;
            }
            show_target_timeout = 4000; // 提示目标温度1秒
            update_display_flag = 1;
        }
        else
        {
            if (set_temp < current_step) {
                set_temp = 0;
            } else {
                set_temp = set_temp - (int)current_step;
            }
            show_target_timeout = 4000;
            update_display_flag = 1;
        }
        flag = 0;
        pwm_counter = 0; // 重置PWM周期，达到立刻关闭加热的作用
    }
    if (EC11_A == EC11_B)
    {
        flag = EC11_A;
    }
}

INTERRUPT(timer0_isr, EXTI_VectTimer0)
{
static unsigned int ec11_btn_timer = 0;
static unsigned char ec11_btn_state = 1;
static unsigned char ec11_btn_long_pressed = 0;
unsigned char show_dp = 0;

// 1. PWM控制：周期 1200 个计数 = 0.6s
pwm_counter++;
if (pwm_counter >= 1200)
{
pwm_counter = 0;
}

if (pwm_counter > 1200 - sw_a)
{
sw = 1;
}
else
{
sw = 0;
}

// 2. 旋转编码器扫描 (0.5ms一次)
if (EC11_E == 0) {
if (ec11_btn_state == 1) {
ec11_btn_timer++;
if (ec11_btn_timer > 40) {
ec11_btn_state = 0;
ec11_btn_timer = 0;
ec11_btn_long_pressed = 0;
}
} else {
ec11_btn_timer++;
if (ec11_btn_timer > 2000 && !ec11_btn_long_pressed) {
ec11_btn_long_pressed = 1;
gear_temps[current_gear] = set_temp;
eeprom_save_flag = 1;
show_target_timeout = 4000;
update_display_flag = 1;
}
}
} else {
if (ec11_btn_state == 0) {
if (!ec11_btn_long_pressed) {
current_gear++;
if (current_gear > 3) current_gear = 0;
set_temp = gear_temps[current_gear];
eeprom_save_flag = 1;
show_target_timeout = 4000;
update_display_flag = 1;
}
ec11_btn_state = 1;
}
ec11_btn_timer = 0;
}

ec11_scan();

// 3. 数码管动态扫描 (每2ms切换一位)
display_counter++;
if (display_counter >= 4)
{
display_counter = 0;

P1 = 0x00; // 消隐
switch (display_digit)
{
case 0:
LSA = 0; LSB = 1; LSC = 1; break;
case 1:
LSA = 1; LSB = 0; LSC = 1; break;
case 2:
LSA = 1; LSB = 1; LSC = 0; break;
}

if (show_target_timeout > 0)
{
if (display_digit == dp_blink_seq[dp_blink_state]) show_dp = 1;
}
else
{
if ((2 - display_digit) < current_gear) show_dp = 1;
}

if (show_dp)
{
P1 = smg_duan[display_data[display_digit]] | 0x10; // 显示小数点
}
else
{
P1 = smg_duan[display_data[display_digit]];
}

display_digit++;
if (display_digit >= 3)
{
display_digit = 0;
}
}

// 4. 定时器更新
if (show_target_timeout > 0)
{
show_target_timeout--;
dp_blink_timer++;
if (dp_blink_timer >= 500) // 250ms 切换一次小数点位置
{
dp_blink_timer = 0;
dp_blink_state++;
if (dp_blink_state >= 6)
{
dp_blink_state = 0;
}
}
}
else
{
dp_blink_state = 0;
dp_blink_timer = 0;
}

read_temp_timer++;
if (read_temp_timer >= 500) // 250ms触发一次主循环任务
{
read_temp_timer = 0;
read_temp_flag = 1;
}
}

unsigned int pid_calc(int current_temperature, int target_temp) // PID控制
{
    int error;
    long proportional;
    long derivative;
    long output;

    error = target_temp - current_temperature; // 计算误差

    proportional = kp * error; // 计算比例项

    integral += ki * error; // 计算积分项

    // 抗积分饱和 (原为 -180 到 180，放大100倍即 -18000 到 18000)
    if (integral < -18000)
    {
        integral = -18000;
    }
    else if (integral > 18000)
    {
        integral = 18000;
    }

    derivative = kd * (error - last_error); // 计算微分项

    // 计算输出并缩小回正常范围
    output = (proportional + integral + derivative) / 100;

    last_error = error; // 保留误差

    output = (output < 0) ? 0 : (output > 1200) ? 1200 : output;

    return (unsigned int)output;
}

void main(void)
{
    unsigned int current_temp_local;
    unsigned int set_temp_local;
    unsigned int show_target_timeout_local;

    setup(); // 复位
    while (1)
    {
        // 获取16位变量的原子快照，防止读取时被中断修改
        EXTI_Global_SetIntState(HAL_State_OFF);
        set_temp_local = set_temp;
        show_target_timeout_local = show_target_timeout;
        EXTI_Global_SetIntState(HAL_State_ON);

        if (update_display_flag)
        {
            update_display_data(set_temp_local);
            update_display_flag = 0;
        }

        if (read_temp_flag)
        {
            current_temp_local = max6675_read_reg();

            if (set_temp_local > current_temp_local && 30 < set_temp_local - current_temp_local)
            {
                EXTI_Global_SetIntState(HAL_State_OFF);
                sw_a = 1200;
                EXTI_Global_SetIntState(HAL_State_ON);
            }
            else
            {
                unsigned int pid_val = pid_calc(current_temp_local, set_temp_local);
                EXTI_Global_SetIntState(HAL_State_OFF);
                sw_a = pid_val;
                EXTI_Global_SetIntState(HAL_State_ON);
            }

            if (show_target_timeout_local > 0)
            {
                update_display_data(set_temp_local);
            }
            else
            {
                update_display_data(current_temp_local);
            }

            read_temp_flag = 0;
        }

        if (eeprom_save_flag)
        {
            save_eeprom();
            eeprom_save_flag = 0;
        }
    }
}
