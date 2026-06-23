#include "socd_engine.h"

// ═══════════════════════════════════════════════════════════════════════════════
//  静态工具函数
// ═══════════════════════════════════════════════════════════════════════════════

bool SocdEngine::IsWasdKey(int vkCode) {
    return vkCode == 'W' || vkCode == 'A' || vkCode == 'S' || vkCode == 'D';
}

int SocdEngine::GetOppositeVk(int vkCode) {
    switch (vkCode) {
        case 'W': return 'S';
        case 'S': return 'W';
        case 'A': return 'D';
        case 'D': return 'A';
        default:  return 0;
    }
}

int SocdEngine::GetKeyIndex(int vkCode) {
    switch (vkCode) {
        case 'W': return 0;
        case 'A': return 1;
        case 'S': return 2;
        case 'D': return 3;
        default:  return -1;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  构造
// ═══════════════════════════════════════════════════════════════════════════════

SocdEngine::SocdEngine()
    : m_mode(SocdMode::LastWin)
{
    m_keys[0].vkCode = 'W';
    m_keys[1].vkCode = 'A';
    m_keys[2].vkCode = 'S';
    m_keys[3].vkCode = 'D';
}

SocdEngine::SocdEngine(SocdMode mode)
    : m_mode(mode)
{
    m_keys[0].vkCode = 'W';
    m_keys[1].vkCode = 'A';
    m_keys[2].vkCode = 'S';
    m_keys[3].vkCode = 'D';
}

// ═══════════════════════════════════════════════════════════════════════════════
//  模式管理
// ═══════════════════════════════════════════════════════════════════════════════

void SocdEngine::SetMode(SocdMode mode) {
    m_mode = mode;
    m_currentOutput = CalculateOutput();
}

SocdMode SocdEngine::GetMode() const {
    return m_mode;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  状态查询
// ═══════════════════════════════════════════════════════════════════════════════

bool SocdEngine::IsKeyHeld(int vkCode) const {
    int idx = GetKeyIndex(vkCode);
    return (idx >= 0) ? m_keys[idx].held : false;
}

SocdOutput SocdEngine::GetCurrentOutput() const {
    return m_currentOutput;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  同时按下检测（50ms 窗口）
// ═══════════════════════════════════════════════════════════════════════════════

bool SocdEngine::WithinSimultaneousWindow(
    std::chrono::steady_clock::time_point t1,
    std::chrono::steady_clock::time_point t2) const
{
    if (t1 == std::chrono::steady_clock::time_point{} ||
        t2 == std::chrono::steady_clock::time_point{}) {
        return false;
    }
    auto diff = (t1 > t2)
        ? std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t2)
        : std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
    return diff.count() <= SIMULTANEOUS_WINDOW_MS;
}

void SocdEngine::ResetAllKeys() {
    for (int i = 0; i < 4; ++i) {
        m_keys[i].held = false;
        m_keys[i].pressTime = std::chrono::steady_clock::time_point{};
        m_keys[i].releaseTime = std::chrono::steady_clock::time_point{};
    }
    m_currentOutput = SocdOutput{};
}

// ═══════════════════════════════════════════════════════════════════════════════
//  冲突裁决 —— SOCD 核心
// ═══════════════════════════════════════════════════════════════════════════════
//
//  输入：一对相反键的索引和键码（如 A 和 D）
//  输出：应生效的键码，0 表示抵消
//
//  裁决规则（按模式）：
//    回中    — 两键抵消，输出 0
//    后发优先 — 比较时间戳，取后按的键
//    先发优先 — 比较时间戳，取先按的键
//
//  持续优先：单键按住时直接输出，不受模式影响。

int SocdEngine::ResolvePair(int idxA, int idxB, int vkA, int vkB) {
    bool aHeld = m_keys[idxA].held;
    bool bHeld = m_keys[idxB].held;

    // 单键按住 → 直接输出（持续优先）
    if (aHeld && !bHeld) return vkA;
    if (!aHeld && bHeld) return vkB;
    if (!aHeld && !bHeld) return 0;

    // 两键同时按住 → 按模式裁决
    bool aNewer = m_keys[idxA].pressTime > m_keys[idxB].pressTime;

    switch (m_mode) {
        case SocdMode::Neutral:
            return 0;                    // 抵消

        case SocdMode::LastWin:
            return aNewer ? vkA : vkB;   // 后发优先

        case SocdMode::FirstWin:
            return aNewer ? vkB : vkA;   // 先发优先
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  输出计算
// ═══════════════════════════════════════════════════════════════════════════════

SocdOutput SocdEngine::CalculateOutput() {
    SocdOutput out;

    // 纵轴：W ↔ S
    int vert = ResolvePair(0, 2, 'W', 'S');
    if (vert == 'W') out.vkW = 'W';
    if (vert == 'S') out.vkS = 'S';

    // 横轴：A ↔ D
    int horiz = ResolvePair(1, 3, 'A', 'D');
    if (horiz == 'A') out.vkA = 'A';
    if (horiz == 'D') out.vkD = 'D';

    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  事件处理入口
// ═══════════════════════════════════════════════════════════════════════════════

SocdOutput SocdEngine::ProcessKey(int vkCode, bool pressed) {
    int idx = GetKeyIndex(vkCode);
    if (idx < 0) return m_currentOutput;

    auto now = std::chrono::steady_clock::now();

    if (pressed) {
        // 忽略自动重复（按住状态下 held 保持 true，不更新时间戳）
        if (!m_keys[idx].held) {
            m_keys[idx].held = true;
            m_keys[idx].pressTime = now;
        }
    } else {
        m_keys[idx].held = false;
        m_keys[idx].releaseTime = now;
    }

    SocdOutput desired = CalculateOutput();
    m_currentOutput = desired;
    return desired;
}
