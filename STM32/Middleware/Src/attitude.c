#include    "app_config.h"   /* 工程内编译：取模块开关值（独立移植时删除本行） */
#include    "attitude.h"
#include    <math.h>

/* =====================================================================
 * attitude.c — Mahony 互补滤波实现
 *
 * 每步 attitude_update 完成：
 *   1. 加速度归一化（只用方向，不用模长）
 *   2. 用当前四元数预测重力在机体系的分量
 *   3. 预测 × 实测 叉积 = 姿态误差（小角度下近似失准角）
 *   4. PI 校正角速度（P 立即拉回，I 累计补偿陀螺零漂）
 *   5. 四元数一阶积分（q += 0.5*dt * q ⊗ ω）
 *   6. 四元数归一化（防数值误差漂移）
 *
 * 参考：Mahony R, et al. "A complementary filter for attitude
 *       estimation of heading and inertial sensors." ICRA 2008.
 * ===================================================================== */

#if CFG_ENABLE_ATTITUDE

/* 弧度 → 度 */
#define RAD2DEG     57.2957795f

/* 内部状态：姿态四元数（初始为单位四元数 = 水平朝北的姿态） */
static attitude_quat_t s_q = {1.0f, 0.0f, 0.0f, 0.0f};

/* 积分项：累计的陀螺零偏补偿 */
static float s_int_x = 0.0f;
static float s_int_y = 0.0f;
static float s_int_z = 0.0f;

void attitude_init(void)
{
    s_q.q0 = 1.0f;
    s_q.q1 = 0.0f;
    s_q.q2 = 0.0f;
    s_q.q3 = 0.0f;

    s_int_x = 0.0f;
    s_int_y = 0.0f;
    s_int_z = 0.0f;
}

void attitude_update(float gx, float gy, float gz,
                     float ax, float ay, float az, float dt)
{
    float norm;
    float vx, vy, vz;       /* 重力在机体系的预测值 */
    float ex, ey, ez;       /* 叉积误差 */
    float half_dt;

    if (dt <= 0.0f)
    {
        return;             /* 非法采样间隔：跳过本次更新 */
    }
    half_dt = 0.5f * dt;

    /* ---------- 1. 加速度归一化 ---------- */
    norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm < 1e-6f)
    {
        /* 加速度模长接近 0（自由落体）：方向不可信，跳过校正，
           仅靠陀螺积分保持姿态 */
        ax = ay = az = 0.0f;
    }
    else
    {
        ax /= norm;
        ay /= norm;
        az /= norm;
    }

    /* ---------- 2. 预测重力方向（姿态矩阵第三行，zyx 约定） ---------- */
    vx = 2.0f * (s_q.q1 * s_q.q3 - s_q.q0 * s_q.q2);
    vy = 2.0f * (s_q.q0 * s_q.q1 + s_q.q2 * s_q.q3);
    vz = s_q.q0 * s_q.q0 - s_q.q1 * s_q.q1
       - s_q.q2 * s_q.q2 + s_q.q3 * s_q.q3;

    /* ---------- 3. 叉积误差：预测 × 实测 ---------- */
    ex = (ay * vz - az * vy);
    ey = (az * vx - ax * vz);
    ez = (ax * vy - ay * vx);

    /* ---------- 4. PI 校正 ---------- */
    if (ATTITUDE_KI > 0.0f)
    {
        s_int_x += ATTITUDE_KI * ex * dt;
        s_int_y += ATTITUDE_KI * ey * dt;
        s_int_z += ATTITUDE_KI * ez * dt;

        gx += s_int_x;
        gy += s_int_y;
        gz += s_int_z;
    }

    gx += ATTITUDE_KP * ex;
    gy += ATTITUDE_KP * ey;
    gz += ATTITUDE_KP * ez;

    /* ---------- 5. 四元数一阶积分 ---------- */
    s_q.q0 += half_dt * (-s_q.q1 * gx - s_q.q2 * gy - s_q.q3 * gz);
    s_q.q1 += half_dt * ( s_q.q0 * gx + s_q.q2 * gz - s_q.q3 * gy);
    s_q.q2 += half_dt * ( s_q.q0 * gy - s_q.q1 * gz + s_q.q3 * gx);
    s_q.q3 += half_dt * ( s_q.q0 * gz + s_q.q1 * gy - s_q.q2 * gx);

    /* ---------- 6. 归一化 ---------- */
    norm = sqrtf(s_q.q0 * s_q.q0 + s_q.q1 * s_q.q1
               + s_q.q2 * s_q.q2 + s_q.q3 * s_q.q3);
    if (norm > 1e-6f)
    {
        s_q.q0 /= norm;
        s_q.q1 /= norm;
        s_q.q2 /= norm;
        s_q.q3 /= norm;
    }
}

const attitude_quat_t *attitude_get_quat(void)
{
    return &s_q;
}

void attitude_get_euler(attitude_euler_t *e)
{
    const float q0 = s_q.q0, q1 = s_q.q1, q2 = s_q.q2, q3 = s_q.q3;

    if (e == NULL)
    {
        return;
    }

    /* zyx 顺序（航空航天惯例），与步骤 2 的重力预测同一约定：
       roll  = atan2(2(q0q1+q2q3), 1-2(q1²+q2²))
       pitch = asin(2(q0q2-q3q1))
       yaw   = atan2(2(q0q3+q1q2), 1-2(q2²+q3²)) */

    e->roll  = atan2f(2.0f * (q0 * q1 + q2 * q3),
                      1.0f - 2.0f * (q1 * q1 + q2 * q2)) * RAD2DEG;
    e->pitch = asinf(2.0f * (q0 * q2 - q3 * q1)) * RAD2DEG;
    e->yaw   = atan2f(2.0f * (q0 * q3 + q1 * q2),
                      1.0f - 2.0f * (q2 * q2 + q3 * q3)) * RAD2DEG;
}

#endif /* CFG_ENABLE_ATTITUDE */
