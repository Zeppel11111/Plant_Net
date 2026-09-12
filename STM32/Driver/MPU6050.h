#ifndef     MPU6050_H
#define     MPU6050_H

#include    <stdint.h>
#include    "bsp_iic.h"
#include    "app_config.h"     /* 模块总开关：必须先于本文件包含 */

#define	MPU6050_SMPLRT_DIV		0x19
#define	MPU6050_CONFIG			0x1A
#define	MPU6050_GYRO_CONFIG		0x1B
#define	MPU6050_ACCEL_CONFIG	0x1C

#define	MPU6050_ACCEL_XOUT_H	0x3B
#define	MPU6050_ACCEL_XOUT_L	0x3C
#define	MPU6050_ACCEL_YOUT_H	0x3D
#define	MPU6050_ACCEL_YOUT_L	0x3E
#define	MPU6050_ACCEL_ZOUT_H	0x3F
#define	MPU6050_ACCEL_ZOUT_L	0x40
#define	MPU6050_TEMP_OUT_H		0x41
#define	MPU6050_TEMP_OUT_L		0x42
#define	MPU6050_GYRO_XOUT_H		0x43
#define	MPU6050_GYRO_XOUT_L		0x44
#define	MPU6050_GYRO_YOUT_H		0x45
#define	MPU6050_GYRO_YOUT_L		0x46
#define	MPU6050_GYRO_ZOUT_H		0x47
#define	MPU6050_GYRO_ZOUT_L		0x48

#define	MPU6050_PWR_MGMT_1		0x6B
#define	MPU6050_PWR_MGMT_2		0x6C
#define	MPU6050_WHO_AM_I		0x75

#define   MPU6050_ADDR1         0x68
#define   MPU6050_ADDR2         0x69

/* WHO_AM_I 读回的身份值（恒为 0x68，与 AD0 接法无关） */
#define   MPU6050_WHO_AM_I_VAL  0x68

/* 器件句柄：记录该器件挂在哪路 I2C 以及探测到的 7 位地址 */
typedef struct{
    bsp_iic_t iicx;
    uint8_t dev_addr;       /* 7 位器件地址（0x68 / 0x69），初始化时探测得到 */
}MPU6050_t;

typedef struct{
    int16_t accel_x, accel_y, accel_z;
    int16_t temp;                    /* 原始温度值 */
    int16_t gyro_x, gyro_y, gyro_z;
}MPU6050_data_t;

#if CFG_ENABLE_MPU6050

int MPU6050_init(bsp_iic_t iicx);

/* 一次突发读全部原始数据（加速度 6 + 温度 2 + 陀螺 6 = 14 字节），成功返回 0 */
int MPU6050_read_data(MPU6050_data_t *raw);

#else
/* ===== 裁剪桩：开关=0 时模块不编译，调用点无需 #if，直接编成空操作 ===== */
static inline int MPU6050_init(bsp_iic_t iicx)       { (void)iicx; return -1; }
static inline int MPU6050_read_data(MPU6050_data_t *raw) { (void)raw; return -1; }
#endif /* CFG_ENABLE_MPU6050 */

#endif
