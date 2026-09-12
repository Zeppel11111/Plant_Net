#ifndef     ATTITUDE_H
#define     ATTITUDE_H

#include    <stdint.h>

/* =====================================================================
 * attitude.h — 6 轴姿态解算模块（Mahony 互补滤波）
 *
 * 纯 C 实现，只依赖标准库 <math.h>，不依赖任何 HAL / RTOS / 具体传感器，
 * 可跨平台编译（STM32 / ESP32 / nRF52 / PC 仿真均可直接使用）。
 *
 * 输入：角速度 rad/s、加速度 g（物理单位）
 * 输出：四元数（内部状态）+ 欧拉角（度）
 *
 * 模块开关：工程内由 app_config.h 提供，须先于本文件包含；
 *           独立移植到其他平台时未定义则默认启用（开箱即用）。
 *
 * 注意：6 轴（无磁力计）只能约束 roll / pitch（重力方向有绝对参考），
 *       yaw 是陀螺自由积分，会缓慢漂移——物理限制，非算法缺陷。
 * ===================================================================== */

#ifndef CFG_ENABLE_ATTITUDE
#define CFG_ENABLE_ATTITUDE 1
#endif

typedef struct
{
    float q0, q1, q2, q3;    /* 四元数：q = q0 + q1*i + q2*j + q3*k */
} attitude_quat_t;

typedef struct
{
    float roll;              /* 横滚角，度，范围 (-180, 180] */
    float pitch;             /* 俯仰角，度，范围 [-90, 90] */
    float yaw;               /* 偏航角，度，范围 (-180, 180] */
} attitude_euler_t;

/* 滤波增益：
 *   Kp 越大，加速度误差拉回越快（但受震动噪声影响越大），0.5 是常见起步值
 *   Ki 用于补偿陀螺零漂；静止时角度若缓慢漂移，可逐步增大（如 0.005）
 */
#define ATTITUDE_KP    0.5f
#define ATTITUDE_KI    0.0f

#if CFG_ENABLE_ATTITUDE

/* 初始化/复位四元数与积分项（调用 attitude_update 前必须先调用一次） */
void attitude_init(void);

/* 核心更新：每次传感器采样调用一次
 *   gx/gy/gz — 陀螺仪角速度，rad/s
 *   ax/ay/az — 加速度计，g（静止时矢量模长 ≈ 1）
 *   dt       — 距上次调用的时间间隔，秒（如 0.02 = 50Hz） */
void attitude_update(float gx, float gy, float gz,
                     float ax, float ay, float az, float dt);

/* 取当前四元数（内部状态指针，只读勿改） */
const attitude_quat_t *attitude_get_quat(void);

/* 欧拉角（度）换算：zyx 顺序，pitch 被限制在 ±90°，无万向锁问题 */
void attitude_get_euler(attitude_euler_t *e);

#else
/* ===== 裁剪桩：开关=0 时模块不编译，调用点无需 #if，直接编成空操作 ===== */
static inline void attitude_init(void) {}

static inline void attitude_update(float gx, float gy, float gz,
                                   float ax, float ay, float az, float dt)
{
    (void)gx; (void)gy; (void)gz;
    (void)ax; (void)ay; (void)az; (void)dt;
}

static inline const attitude_quat_t *attitude_get_quat(void) { return 0; }

static inline void attitude_get_euler(attitude_euler_t *e)
{
    if (e) { e->roll = 0.0f; e->pitch = 0.0f; e->yaw = 0.0f; }
}
#endif /* CFG_ENABLE_ATTITUDE */

#endif /* ATTITUDE_H */
