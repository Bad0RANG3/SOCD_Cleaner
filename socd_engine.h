#pragma once

#include <cstdint>
#include <chrono>

// ─── SOCD 处理模式 ──────────────────────────────────────────────────────────

enum class SocdMode {
    Neutral = 0,   // 回中：冲突键相互抵消，无输出（赛事标准）
    LastWin,        // 后发优先：最后按下的键生效
    FirstWin,       // 先发优先：最先按下的键生效
};

// ─── 按键追踪状态 ───────────────────────────────────────────────────────────

struct KeyState {
    bool held = false;                                       // 是否按住
    int  vkCode = 0;                                        // 虚拟键码 'W' 'A' 'S' 'D'
    std::chrono::steady_clock::time_point pressTime;        // 按下时刻
    std::chrono::steady_clock::time_point releaseTime;      // 释放时刻
};

// ─── SOCD 输出结果 ──────────────────────────────────────────────────────────

struct SocdOutput {
    int vkW = 0;   // 非零表示应输出该键（值为虚拟键码）
    int vkA = 0;
    int vkS = 0;
    int vkD = 0;

    bool operator==(const SocdOutput& o) const {
        return vkW == o.vkW && vkA == o.vkA && vkS == o.vkS && vkD == o.vkD;
    }
    bool operator!=(const SocdOutput& o) const { return !(*this == o); }
};

// ─── SOCD 裁决引擎 ─────────────────────────────────────────────────────────
//
// 直接追踪 W/A/S/D 四个键的物理状态，按当前模式裁决冲突键对：
//   纵轴：W ↔ S
//   横轴：A ↔ D
//
// 持续优先原则：按住一方，另一方按下又释放，自动回到先前的按键。

class SocdEngine {
public:
    SocdEngine();
    explicit SocdEngine(SocdMode mode);

    // 模式切换
    void     SetMode(SocdMode mode);
    SocdMode GetMode() const;

    // 处理物理按键事件，返回裁决后的虚拟输出状态
    SocdOutput ProcessKey(int vkCode, bool pressed);

    // 查询
    SocdOutput GetCurrentOutput() const;
    bool       IsKeyHeld(int vkCode) const;

    // 重置（切换模式时清空所有状态）
    void ResetAllKeys();

    // 静态工具
    static bool IsWasdKey(int vkCode);
    static int  GetOppositeVk(int vkCode);   // 'W'→'S', 'A'→'D' 等
    static int  GetKeyIndex(int vkCode);      // 0=W 1=A 2=S 3=D

private:
    SocdMode   m_mode;
    KeyState   m_keys[4];                // [0]=W [1]=A [2]=S [3]=D
    SocdOutput m_currentOutput;

    static constexpr int SIMULTANEOUS_WINDOW_MS = 50;

    // 计算当前应输出的按键状态
    SocdOutput CalculateOutput();

    // 裁决一对相反键，返回应生效的键码（0=抵消）
    int ResolvePair(int idxA, int idxB, int vkA, int vkB);

    // 判断两次按键是否在"同时按下"窗口内
    bool WithinSimultaneousWindow(
        std::chrono::steady_clock::time_point t1,
        std::chrono::steady_clock::time_point t2) const;
};
