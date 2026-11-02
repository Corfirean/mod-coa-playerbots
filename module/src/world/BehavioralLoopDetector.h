/*
 * mod-coa-playerbots
 *
 * Per-bot behavioral loop and deadlock detector for open-world autonomy.
 * Detects and breaks higher-level behavioral loops that occur without net objective progress:
 *   1. TargetApproachLoop: repeatedly selecting/approaching the same target without credit/loot.
 *   2. AreaPingPong: alternating between objective areas (A -> B -> A -> B -> A) without progress.
 *   3. TaskOscillation: flipping between quests/objectives (Q1 -> Q2 -> Q1 -> Q2 -> Q1) without progress.
 *
 * Pure data structure: time and IDs in, LoopDiagnosis out. Unit-tested in world_logic_tests.cpp.
 */

#ifndef COA_PLAYERBOTS_BEHAVIORAL_LOOP_DETECTOR_H
#define COA_PLAYERBOTS_BEHAVIORAL_LOOP_DETECTOR_H

#include "Define.h"
#include <array>

enum class LoopKind : uint8
{
    None = 0,
    TargetApproachLoop,
    AreaPingPong,
    TaskOscillation,
};

inline char const* LoopKindName(LoopKind kind)
{
    switch (kind)
    {
        case LoopKind::None:               return "none";
        case LoopKind::TargetApproachLoop: return "target-approach-loop";
        case LoopKind::AreaPingPong:       return "area-ping-pong";
        case LoopKind::TaskOscillation:    return "task-oscillation";
        default:                           return "?";
    }
}

struct LoopDiagnosis
{
    LoopKind kind = LoopKind::None;
    uint64 offendingId = 0;

    explicit operator bool() const { return kind != LoopKind::None; }
};

class BehavioralLoopDetector
{
public:
    static constexpr size_t HISTORY_CAPACITY = 8;
    static constexpr uint8 TARGET_LOOP_THRESHOLD = 3;
    static constexpr uint8 AREA_PINGPONG_THRESHOLD = 3;
    static constexpr uint8 TASK_OSCILLATION_THRESHOLD = 3;

    void OnProgress(uint32 nowMs)
    {
        _lastProgressMs = nowMs;
        _targetCount = 0;
        _targetHead = 0;
        _areaCount = 0;
        _areaHead = 0;
        _taskCount = 0;
        _taskHead = 0;
    }

    LoopDiagnosis RecordTargetAttempt(uint64 targetId, uint32 nowMs, uint32 windowMs = 60000)
    {
        if (targetId == 0)
            return {};

        uint8 hits = 1;
        for (size_t i = 0; i < _targetCount; ++i)
        {
            if (_targets[i].id == targetId && (nowMs >= _targets[i].atMs) && (nowMs - _targets[i].atMs <= windowMs))
                ++hits;
        }

        _targets[_targetHead] = Stamp{targetId, nowMs};
        _targetHead = (_targetHead + 1) % HISTORY_CAPACITY;
        if (_targetCount < HISTORY_CAPACITY)
            ++_targetCount;

        if (hits >= TARGET_LOOP_THRESHOLD)
        {
            ++_loopsDetected;
            _lastLoopKind = LoopKind::TargetApproachLoop;
            _lastOffendingId = targetId;
            return LoopDiagnosis{LoopKind::TargetApproachLoop, targetId};
        }
        return {};
    }

    LoopDiagnosis RecordAreaSwitch(uint32 areaId, uint32 nowMs, uint32 windowMs = 120000)
    {
        if (areaId == 0)
            return {};

        if (_areaCount > 0)
        {
            size_t lastIdx = (_areaHead + HISTORY_CAPACITY - 1) % HISTORY_CAPACITY;
            if (_areas[lastIdx].id == areaId)
                return {};
        }

        uint8 visits = 1;
        for (size_t i = 0; i < _areaCount; ++i)
        {
            if (_areas[i].id == areaId && (nowMs >= _areas[i].atMs) && (nowMs - _areas[i].atMs <= windowMs))
                ++visits;
        }

        _areas[_areaHead] = Stamp{areaId, nowMs};
        _areaHead = (_areaHead + 1) % HISTORY_CAPACITY;
        if (_areaCount < HISTORY_CAPACITY)
            ++_areaCount;

        if (visits >= AREA_PINGPONG_THRESHOLD)
        {
            ++_loopsDetected;
            _lastLoopKind = LoopKind::AreaPingPong;
            _lastOffendingId = areaId;
            return LoopDiagnosis{LoopKind::AreaPingPong, areaId};
        }
        return {};
    }

    LoopDiagnosis RecordTaskChoice(uint32 questId, uint8 objectiveIndex, uint32 nowMs, uint32 windowMs = 90000)
    {
        if (questId == 0)
            return {};

        uint64 key = (uint64(questId) << 8) | uint64(objectiveIndex);
        if (_taskCount > 0)
        {
            size_t lastIdx = (_taskHead + HISTORY_CAPACITY - 1) % HISTORY_CAPACITY;
            if (_tasks[lastIdx].id == key)
                return {};
        }

        uint8 selections = 1;
        for (size_t i = 0; i < _taskCount; ++i)
        {
            if (_tasks[i].id == key && (nowMs >= _tasks[i].atMs) && (nowMs - _tasks[i].atMs <= windowMs))
                ++selections;
        }

        _tasks[_taskHead] = Stamp{key, nowMs};
        _taskHead = (_taskHead + 1) % HISTORY_CAPACITY;
        if (_taskCount < HISTORY_CAPACITY)
            ++_taskCount;

        if (selections >= TASK_OSCILLATION_THRESHOLD)
        {
            ++_loopsDetected;
            _lastLoopKind = LoopKind::TaskOscillation;
            _lastOffendingId = questId;
            return LoopDiagnosis{LoopKind::TaskOscillation, questId};
        }
        return {};
    }

    void Clear()
    {
        *this = BehavioralLoopDetector();
    }

    uint32 LoopsDetected() const { return _loopsDetected; }
    LoopKind LastLoopKind() const { return _lastLoopKind; }
    uint64 LastOffendingId() const { return _lastOffendingId; }
    uint32 LastProgressMs() const { return _lastProgressMs; }

private:
    struct Stamp
    {
        uint64 id = 0;
        uint32 atMs = 0;
    };

    std::array<Stamp, HISTORY_CAPACITY> _targets{};
    size_t _targetHead = 0;
    size_t _targetCount = 0;

    std::array<Stamp, HISTORY_CAPACITY> _areas{};
    size_t _areaHead = 0;
    size_t _areaCount = 0;

    std::array<Stamp, HISTORY_CAPACITY> _tasks{};
    size_t _taskHead = 0;
    size_t _taskCount = 0;

    uint32 _lastProgressMs = 0;
    uint32 _loopsDetected = 0;
    LoopKind _lastLoopKind = LoopKind::None;
    uint64 _lastOffendingId = 0;
};

#endif // COA_PLAYERBOTS_BEHAVIORAL_LOOP_DETECTOR_H
