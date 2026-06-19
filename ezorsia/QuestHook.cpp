#include "stdafx.h"
#include "QuestHook.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr WORD kRecvNpcTalk = 0x003A;
constexpr WORD kRecvNpcTalkMore = 0x003C;
constexpr WORD kRecvQuestAction = 0x006B;
constexpr WORD kRecvCustomPacket = 0x3713;
constexpr WORD kCustomInteractionHookEvent = 0x1003;

constexpr WORD kSendSpawnNpc = 0x0101;
constexpr WORD kSendRemoveNpc = 0x0102;
constexpr WORD kSendSpawnNpcController = 0x0103;
constexpr WORD kSendNpcTalk = 0x0130;
constexpr WORD kSendSetField = 0x007D;
constexpr WORD kS2CInteractionHookRules = 0x1001;
constexpr WORD kS2CInteractionHookResult = 0x1002;
constexpr DWORD kClientSocketPtr = 0x00BE7914;
constexpr DWORD kQuestActionClickAddr = 0x00716FE1;

constexpr int kLegacyRulesVersion = 3;
constexpr int kVersion = 4;
constexpr int kAnyId = -1;

constexpr int kEventNpcClick = 1;
constexpr int kEventNpcDialogSelection = 2;
constexpr int kEventQuestAction = 3;
constexpr int kEventMaskNpcClick = 1;
constexpr int kEventMaskNpcDialogSelection = 1 << 1;
constexpr int kEventMaskQuestAction = 1 << 2;

constexpr int kTargetAny = 0;
constexpr int kTargetNpc = 1;
constexpr int kTargetQuest = 2;
constexpr int kTargetDialogSelection = 3;

constexpr int kQuestStateNone = 0;
constexpr int kQuestStateNotStarted = 1;
constexpr int kQuestStateStarted = 2;
constexpr int kQuestStateCompleted = 3;
constexpr int kQuestStateMaskAny = 0;
constexpr int kQuestStateMaskNotStarted = 1;
constexpr int kQuestStateMaskStarted = 1 << 1;
constexpr int kQuestStateMaskCompleted = 1 << 2;

constexpr int kActionMaskAny = 0;
constexpr int kActionMaskQueryStart = 1;
constexpr int kActionMaskQueryProgress = 1 << 2;
constexpr int kActionMaskQueryComplete = 1 << 3;

constexpr int kDialogContextNone = 0;
constexpr int kDialogContextNpc = 1;
constexpr int kDialogStateNone = 0;
constexpr int kDialogStateOpen = 1;

constexpr int kResultFallbackOriginal = 2;

constexpr int kScopeAllRules = 0;
constexpr int kScopeCharacterQuestRules = 1;
constexpr int kScopeMapNpcRules = 2;
constexpr int kScopeDialogTempRules = 3;

constexpr int kReplaceScope = 1;
constexpr int kClearScope = 2;
constexpr int kMaxRulesPerPacket = 100;
constexpr int kMaxRuleCount = 20000;
constexpr ULONGLONG kPendingRuleBatchTimeoutMs = 5000;

struct COutPacket {
    int Loopback;
    union {
        unsigned char* Data;
        void* Unk;
        unsigned short* Header;
    };
    unsigned long Size;
    unsigned int Offset;
    int EncryptedByShanda;
};

struct CInPacket {
    bool Loopback;
    int State;
    void* Data;
    unsigned long Size;
    unsigned short RawSeq;
    unsigned short DataLen;
    unsigned short Unknown;
    unsigned int Offset;
    void* Unk;
};

struct InteractionHookRule {
    int eventMask;
    int targetType;
    int targetId;
    int questId;
    int questStateMask;
    int actionMask;
    int selectionId;
};

struct HookEvent {
    int requestId;
    int eventType;
    int targetType;
    int targetId;
    int objectId;
    int clientNpcId;
    int questId;
    int questState;
    int rawAction;
    int selection;
    int dialogContext;
    int dialogState;
    int reserved;
};

struct PendingPacket {
    void* socket;
    void* edx;
    std::vector<unsigned char> bytes;
};

struct PendingLocalQuestAction {
    void* thisPtr;
    int arg;
};

struct PendingRuleBatch {
    int batchId;
    int batchCount;
    ULONGLONG createdAt;
    std::vector<bool> received;
    std::vector<std::vector<InteractionHookRule>> chunks;
};

using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, COutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);

using QuestActionClick_t = void(__fastcall*)(void* pThis, void* edx, int arg);
static QuestActionClick_t g_QuestActionClick = reinterpret_cast<QuestActionClick_t>(kQuestActionClickAddr);

static std::mutex g_stateMutex;
static std::vector<InteractionHookRule> g_characterQuestRules;
static std::vector<InteractionHookRule> g_mapNpcRules;
static std::vector<InteractionHookRule> g_dialogTempRules;
static std::unordered_map<int, PendingRuleBatch> g_pendingRuleBatches;
static std::unordered_map<int, PendingPacket> g_pendingPackets;
static std::unordered_map<int, PendingLocalQuestAction> g_pendingLocalQuestActions;
static std::unordered_map<int, int> g_npcIdByObjectId;
static int g_lastAppliedRuleBatchIds[4] = {};
static bool g_rulesLoaded = false;
static int g_currentDialogNpcId = 0;
static int g_currentDialogState = kDialogStateNone;
static bool g_replayingLocalQuestAction = false;
static std::atomic<int> g_nextRequestId{ 1 };
static std::mutex g_traceMutex;

enum class IncomingResult {
    None,
    Consumed
};

static unsigned short ReadU16(const unsigned char* ptr) {
    return static_cast<unsigned short>(ptr[0] | (ptr[1] << 8));
}

static int ReadI32(const unsigned char* ptr) {
    return static_cast<int>(static_cast<unsigned int>(ptr[0])
        | (static_cast<unsigned int>(ptr[1]) << 8)
        | (static_cast<unsigned int>(ptr[2]) << 16)
        | (static_cast<unsigned int>(ptr[3]) << 24));
}

static void WriteU16(std::vector<unsigned char>& out, unsigned short value) {
    out.push_back(static_cast<unsigned char>(value & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

static void WriteI32(std::vector<unsigned char>& out, int value) {
    out.push_back(static_cast<unsigned char>(value & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
}

static bool TryReadDword(DWORD address, DWORD& out) {
    __try {
        out = *reinterpret_cast<DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

static unsigned long IncomingPacketSize(CInPacket* packet) {
    if (packet == nullptr) {
        return 0;
    }
    if (packet->DataLen > 0) {
        return packet->DataLen;
    }
    return packet->Size;
}

static void TraceV(const char* format, va_list args) {
    std::lock_guard<std::mutex> lock(g_traceMutex);
    FILE* file = nullptr;
    if (fopen_s(&file, "interaction-hook.log", "ab") != 0 || file == nullptr) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);
    std::vfprintf(file, format, args);
    std::fprintf(file, "\r\n");
    std::fclose(file);
}

static void Trace(const char* format, ...) {
    va_list args;
    va_start(args, format);
    TraceV(format, args);
    va_end(args);
}

static const char* ScopeName(int scope) {
    switch (scope) {
        case kScopeAllRules:
            return "ALL_RULES";
        case kScopeCharacterQuestRules:
            return "CHARACTER_QUEST_RULES";
        case kScopeMapNpcRules:
            return "MAP_NPC_RULES";
        case kScopeDialogTempRules:
            return "DIALOG_TEMP_RULES";
        default:
            return "UNKNOWN";
    }
}

static const char* ReplaceModeName(int replaceMode) {
    switch (replaceMode) {
        case kReplaceScope:
            return "REPLACE_SCOPE";
        case kClearScope:
            return "CLEAR_SCOPE";
        default:
            return "UNKNOWN";
    }
}

static bool IsValidScope(int scope) {
    return scope == kScopeAllRules
        || scope == kScopeCharacterQuestRules
        || scope == kScopeMapNpcRules
        || scope == kScopeDialogTempRules;
}

static bool IsBusinessScope(int scope) {
    return scope == kScopeCharacterQuestRules
        || scope == kScopeMapNpcRules
        || scope == kScopeDialogTempRules;
}

static std::vector<InteractionHookRule>* RulesForScopeLocked(int scope) {
    switch (scope) {
        case kScopeCharacterQuestRules:
            return &g_characterQuestRules;
        case kScopeMapNpcRules:
            return &g_mapNpcRules;
        case kScopeDialogTempRules:
            return &g_dialogTempRules;
        default:
            return nullptr;
    }
}

static int ActiveRuleCountLocked() {
    return static_cast<int>(g_characterQuestRules.size()
        + g_mapNpcRules.size()
        + g_dialogTempRules.size());
}

static void RefreshRulesLoadedLocked() {
    g_rulesLoaded = ActiveRuleCountLocked() > 0;
}

static bool RulesLoaded() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_rulesLoaded;
}

static int RuleCount() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return ActiveRuleCountLocked();
}

static void ClearAllRulesLocked() {
    g_characterQuestRules.clear();
    g_mapNpcRules.clear();
    g_dialogTempRules.clear();
    g_pendingRuleBatches.clear();
    g_pendingPackets.clear();
    g_pendingLocalQuestActions.clear();
    g_npcIdByObjectId.clear();
    g_currentDialogNpcId = 0;
    g_currentDialogState = kDialogStateNone;
    for (int& batchId : g_lastAppliedRuleBatchIds) {
        batchId = 0;
    }
    g_rulesLoaded = false;
}

static void CleanupExpiredRuleBatchesLocked() {
    const ULONGLONG now = GetTickCount64();
    for (auto it = g_pendingRuleBatches.begin(); it != g_pendingRuleBatches.end();) {
        if (now >= it->second.createdAt && now - it->second.createdAt > kPendingRuleBatchTimeoutMs) {
            Trace("ApplyRules v4 pending batch timeout scope=%s batchId=%d",
                ScopeName(it->first),
                it->second.batchId);
            it = g_pendingRuleBatches.erase(it);
            continue;
        }
        ++it;
    }
}

static int EventMask(int eventType) {
    switch (eventType) {
        case kEventNpcClick:
            return kEventMaskNpcClick;
        case kEventNpcDialogSelection:
            return kEventMaskNpcDialogSelection;
        case kEventQuestAction:
            return kEventMaskQuestAction;
        default:
            return 0;
    }
}

static bool RuleTargetMatches(const InteractionHookRule& rule, const HookEvent& event) {
    if (rule.targetType == kTargetAny || rule.targetId == kAnyId) {
        return true;
    }
    switch (rule.targetType) {
        case kTargetNpc:
            return event.clientNpcId > 0 && rule.targetId == event.clientNpcId;
        case kTargetQuest:
            return event.questId > 0 && rule.targetId == event.questId;
        case kTargetDialogSelection:
            return event.selection >= 0 && rule.targetId == event.selection;
        default:
            return false;
    }
}

static int QuestStateMask(int questState) {
    switch (questState) {
        case kQuestStateNotStarted:
            return kQuestStateMaskNotStarted;
        case kQuestStateStarted:
            return kQuestStateMaskStarted;
        case kQuestStateCompleted:
            return kQuestStateMaskCompleted;
        default:
            return kQuestStateMaskAny;
    }
}

static bool RuleMatches(const InteractionHookRule& rule, const HookEvent& event, int actionMask) {
    if ((rule.eventMask & EventMask(event.eventType)) == 0) {
        return false;
    }
    if (!RuleTargetMatches(rule, event)) {
        return false;
    }
    if (rule.questId > 0 && event.questId > 0 && rule.questId != event.questId) {
        return false;
    }
    const int eventQuestStateMask = QuestStateMask(event.questState);
    if (eventQuestStateMask != kQuestStateMaskAny
            && rule.questStateMask != kQuestStateMaskAny
            && (rule.questStateMask & eventQuestStateMask) == 0) {
        return false;
    }
    if (rule.actionMask != kActionMaskAny && (rule.actionMask & actionMask) == 0) {
        return false;
    }
    if (rule.selectionId != kAnyId && event.selection >= 0 && rule.selectionId != event.selection) {
        return false;
    }
    return true;
}

static bool RuleSetMatchesLocked(const std::vector<InteractionHookRule>& rules, const HookEvent& event, int actionMask) {
    for (const InteractionHookRule& rule : rules) {
        if (RuleMatches(rule, event, actionMask)) {
            return true;
        }
    }
    return false;
}

static bool RuleSetHasEventLocked(const std::vector<InteractionHookRule>& rules, int eventType) {
    const int mask = EventMask(eventType);
    for (const InteractionHookRule& rule : rules) {
        if ((rule.eventMask & mask) != 0) {
            return true;
        }
    }
    return false;
}

static bool HasEventRuleLocked(int eventType) {
    return RuleSetHasEventLocked(g_characterQuestRules, eventType)
        || RuleSetHasEventLocked(g_mapNpcRules, eventType)
        || RuleSetHasEventLocked(g_dialogTempRules, eventType);
}

static bool ShouldIntercept(const HookEvent& event, int actionMask) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_rulesLoaded) {
        return false;
    }
    if (RuleSetMatchesLocked(g_characterQuestRules, event, actionMask)
            || RuleSetMatchesLocked(g_mapNpcRules, event, actionMask)
            || RuleSetMatchesLocked(g_dialogTempRules, event, actionMask)) {
        return true;
    }

    // If the object->NPC mapping is unavailable, let the server validate NPC clicks.
    return event.eventType == kEventNpcClick && event.clientNpcId <= 0 && HasEventRuleLocked(kEventNpcClick);
}

static bool ParseRules(const unsigned char* cursor, int count, unsigned long availableBytes,
                       std::vector<InteractionHookRule>& out) {
    if (count < 0 || count > kMaxRuleCount) {
        return false;
    }
    const unsigned long rulesBytes = static_cast<unsigned long>(count) * 28UL;
    if (availableBytes < rulesBytes) {
        return false;
    }

    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        InteractionHookRule rule{};
        rule.eventMask = ReadI32(cursor);
        rule.targetType = ReadI32(cursor + 4);
        rule.targetId = ReadI32(cursor + 8);
        rule.questId = ReadI32(cursor + 12);
        rule.questStateMask = ReadI32(cursor + 16);
        rule.actionMask = ReadI32(cursor + 20);
        rule.selectionId = ReadI32(cursor + 24);
        cursor += 28;
        if (rule.eventMask != 0) {
            out.push_back(rule);
        }
    }
    return true;
}

static bool ApplyLegacyRules(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 10) {
        Trace("ApplyRules v3 reject payloadSize=%lu reason=short", payloadSize);
        return false;
    }

    const int count = ReadI32(payload + 6);
    std::vector<InteractionHookRule> next;
    if (!ParseRules(payload + 10, count, payloadSize - 10, next)) {
        Trace("ApplyRules v3 reject payloadSize=%lu count=%d reason=rules-bytes", payloadSize, count);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    ClearAllRulesLocked();
    g_characterQuestRules.swap(next);
    RefreshRulesLoadedLocked();
    Trace("ApplyRules v3 ok count=%d accepted=%d total=%d",
        count,
        static_cast<int>(g_characterQuestRules.size()),
        ActiveRuleCountLocked());
    return true;
}

static bool ApplyClearScopeV4(int scope, int batchId) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    CleanupExpiredRuleBatchesLocked();

    if (scope == kScopeAllRules) {
        ClearAllRulesLocked();
        g_lastAppliedRuleBatchIds[kScopeAllRules] = batchId;
        Trace("ApplyRules v4 clear scope=%s batchId=%d total=%d",
            ScopeName(scope),
            batchId,
            ActiveRuleCountLocked());
        return true;
    }

    if (!IsBusinessScope(scope)) {
        Trace("ApplyRules v4 clear reject scope=%d batchId=%d reason=scope", scope, batchId);
        return false;
    }
    if (batchId <= g_lastAppliedRuleBatchIds[scope]) {
        Trace("ApplyRules v4 clear ignore scope=%s batchId=%d last=%d",
            ScopeName(scope),
            batchId,
            g_lastAppliedRuleBatchIds[scope]);
        return true;
    }

    std::vector<InteractionHookRule>* activeRules = RulesForScopeLocked(scope);
    if (activeRules == nullptr) {
        return false;
    }
    activeRules->clear();
    g_pendingRuleBatches.erase(scope);
    g_lastAppliedRuleBatchIds[scope] = batchId;
    RefreshRulesLoadedLocked();
    Trace("ApplyRules v4 clear scope=%s batchId=%d total=%d",
        ScopeName(scope),
        batchId,
        ActiveRuleCountLocked());
    return true;
}

static bool BatchCompleteLocked(const PendingRuleBatch& batch) {
    for (bool received : batch.received) {
        if (!received) {
            return false;
        }
    }
    return true;
}

static bool ApplyReplaceScopeV4(int scope, int batchId, int batchIndex, int batchCount,
                                std::vector<InteractionHookRule>& packetRules) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    CleanupExpiredRuleBatchesLocked();

    if (!IsBusinessScope(scope)) {
        Trace("ApplyRules v4 replace reject scope=%d batchId=%d reason=scope", scope, batchId);
        return false;
    }
    if (batchId <= g_lastAppliedRuleBatchIds[scope]) {
        Trace("ApplyRules v4 replace ignore scope=%s batchId=%d last=%d",
            ScopeName(scope),
            batchId,
            g_lastAppliedRuleBatchIds[scope]);
        return true;
    }

    auto it = g_pendingRuleBatches.find(scope);
    if (it == g_pendingRuleBatches.end() || it->second.batchId != batchId) {
        PendingRuleBatch batch{};
        batch.batchId = batchId;
        batch.batchCount = batchCount;
        batch.createdAt = GetTickCount64();
        batch.received.assign(static_cast<std::size_t>(batchCount), false);
        batch.chunks.resize(static_cast<std::size_t>(batchCount));
        g_pendingRuleBatches.erase(scope);
        it = g_pendingRuleBatches.emplace(scope, std::move(batch)).first;
    }

    PendingRuleBatch& batch = it->second;
    if (batch.batchCount != batchCount || batchIndex < 0 || batchIndex >= batch.batchCount) {
        Trace("ApplyRules v4 replace reject scope=%s batchId=%d batchIndex=%d batchCount=%d existingCount=%d",
            ScopeName(scope),
            batchId,
            batchIndex,
            batchCount,
            batch.batchCount);
        return false;
    }
    if (batch.received[static_cast<std::size_t>(batchIndex)]) {
        Trace("ApplyRules v4 replace duplicate scope=%s batchId=%d batchIndex=%d",
            ScopeName(scope),
            batchId,
            batchIndex);
        return true;
    }

    batch.chunks[static_cast<std::size_t>(batchIndex)].swap(packetRules);
    batch.received[static_cast<std::size_t>(batchIndex)] = true;
    if (!BatchCompleteLocked(batch)) {
        Trace("ApplyRules v4 replace pending scope=%s batchId=%d batch=%d/%d accepted=%d total=%d",
            ScopeName(scope),
            batchId,
            batchIndex + 1,
            batchCount,
            static_cast<int>(batch.chunks[static_cast<std::size_t>(batchIndex)].size()),
            ActiveRuleCountLocked());
        return true;
    }

    std::vector<InteractionHookRule> next;
    std::size_t total = 0;
    for (const std::vector<InteractionHookRule>& chunk : batch.chunks) {
        total += chunk.size();
    }
    next.reserve(total);
    for (const std::vector<InteractionHookRule>& chunk : batch.chunks) {
        next.insert(next.end(), chunk.begin(), chunk.end());
    }

    std::vector<InteractionHookRule>* activeRules = RulesForScopeLocked(scope);
    if (activeRules == nullptr) {
        return false;
    }
    activeRules->swap(next);
    g_lastAppliedRuleBatchIds[scope] = batchId;
    g_pendingRuleBatches.erase(scope);
    RefreshRulesLoadedLocked();
    Trace("ApplyRules v4 replace applied scope=%s batchId=%d accepted=%d total=%d",
        ScopeName(scope),
        batchId,
        static_cast<int>(activeRules->size()),
        ActiveRuleCountLocked());
    return true;
}

static bool ApplyRulesV4(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 30) {
        Trace("ApplyRules v4 reject payloadSize=%lu reason=short", payloadSize);
        return false;
    }

    const int scope = ReadI32(payload + 6);
    const int batchId = ReadI32(payload + 10);
    const int batchIndex = ReadI32(payload + 14);
    const int batchCount = ReadI32(payload + 18);
    const int replaceMode = ReadI32(payload + 22);
    const int ruleCount = ReadI32(payload + 26);
    if (!IsValidScope(scope)
            || batchId <= 0
            || batchIndex < 0
            || batchCount <= 0
            || batchIndex >= batchCount
            || (replaceMode != kReplaceScope && replaceMode != kClearScope)
            || ruleCount < 0
            || ruleCount > kMaxRulesPerPacket) {
        Trace("ApplyRules v4 reject scope=%d batchId=%d batchIndex=%d batchCount=%d mode=%d count=%d reason=header",
            scope,
            batchId,
            batchIndex,
            batchCount,
            replaceMode,
            ruleCount);
        return false;
    }

    if (replaceMode == kClearScope) {
        if (batchIndex != 0 || batchCount != 1 || ruleCount != 0) {
            Trace("ApplyRules v4 clear reject scope=%s batchId=%d batchIndex=%d batchCount=%d count=%d",
                ScopeName(scope),
                batchId,
                batchIndex,
                batchCount,
                ruleCount);
            return false;
        }
        return ApplyClearScopeV4(scope, batchId);
    }

    if (scope == kScopeAllRules || batchCount > (kMaxRuleCount + kMaxRulesPerPacket - 1) / kMaxRulesPerPacket) {
        Trace("ApplyRules v4 replace reject scope=%s batchId=%d batchCount=%d reason=scope-or-count",
            ScopeName(scope),
            batchId,
            batchCount);
        return false;
    }

    const unsigned long rulesBytes = static_cast<unsigned long>(ruleCount) * 28UL;
    if (payloadSize < 30UL + rulesBytes) {
        Trace("ApplyRules v4 reject payloadSize=%lu scope=%s batchId=%d count=%d reason=rules-bytes",
            payloadSize,
            ScopeName(scope),
            batchId,
            ruleCount);
        return false;
    }

    std::vector<InteractionHookRule> packetRules;
    if (!ParseRules(payload + 30, ruleCount, payloadSize - 30, packetRules)) {
        Trace("ApplyRules v4 reject scope=%s batchId=%d count=%d reason=parse",
            ScopeName(scope),
            batchId,
            ruleCount);
        return false;
    }

    Trace("ApplyRules v4 packet scope=%s batchId=%d batch=%d/%d mode=%s count=%d accepted=%d",
        ScopeName(scope),
        batchId,
        batchIndex + 1,
        batchCount,
        ReplaceModeName(replaceMode),
        ruleCount,
        static_cast<int>(packetRules.size()));
    return ApplyReplaceScopeV4(scope, batchId, batchIndex, batchCount, packetRules);
}

static bool ApplyRules(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 6) {
        Trace("ApplyRules reject payloadSize=%lu reason=short", payloadSize);
        return false;
    }
    const unsigned short clientSubCommand = ReadU16(payload);
    const int version = ReadI32(payload + 2);
    if (clientSubCommand != kCustomInteractionHookEvent) {
        Trace("ApplyRules reject payloadSize=%lu sub=0x%04X version=%d reason=sub",
            payloadSize,
            clientSubCommand,
            version);
        return false;
    }
    if (version == kLegacyRulesVersion) {
        return ApplyLegacyRules(payload, payloadSize);
    }
    if (version == kVersion) {
        return ApplyRulesV4(payload, payloadSize);
    }
    Trace("ApplyRules reject payloadSize=%lu sub=0x%04X version=%d reason=version",
        payloadSize,
        clientSubCommand,
        version);
    return false;
}

static int ResolveNpcIdByObjectId(int objectId) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_npcIdByObjectId.find(objectId);
    return it == g_npcIdByObjectId.end() ? 0 : it->second;
}

static int CurrentDialogNpcId() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_currentDialogNpcId;
}

static int CurrentDialogState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_currentDialogState;
}

static void TrackNpcSpawn(int objectId, int npcId) {
    if (objectId <= 0 || npcId <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_npcIdByObjectId[objectId] = npcId;
    Trace("TrackNpcSpawn objectId=%d npcId=%d", objectId, npcId);
}

static void TrackNpcRemove(int objectId) {
    if (objectId <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_npcIdByObjectId.erase(objectId);
}

static void ResetFieldState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_npcIdByObjectId.clear();
    g_currentDialogNpcId = 0;
    g_currentDialogState = kDialogStateNone;
}

static void TrackNpcTalkPacket(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 6) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_currentDialogNpcId = ReadI32(payload + 1);
    g_currentDialogState = kDialogStateOpen;
    Trace("TrackNpcTalk npcId=%d payloadSize=%lu", g_currentDialogNpcId, payloadSize);
}

static void TrackIncomingPacket(unsigned short opcode, const unsigned char* payload, unsigned long payloadSize) {
    if (opcode == kSendSetField) {
        ResetFieldState();
        return;
    }
    if (opcode == kSendSpawnNpc && payloadSize >= 8) {
        TrackNpcSpawn(ReadI32(payload), ReadI32(payload + 4));
        return;
    }
    if (opcode == kSendSpawnNpcController && payloadSize >= 5) {
        const int objectId = ReadI32(payload + 1);
        if (payload[0] == 0) {
            TrackNpcRemove(objectId);
        } else if (payloadSize >= 9) {
            TrackNpcSpawn(objectId, ReadI32(payload + 5));
        }
        return;
    }
    if (opcode == kSendRemoveNpc && payloadSize >= 4) {
        TrackNpcRemove(ReadI32(payload));
        return;
    }
    if (opcode == kSendNpcTalk) {
        TrackNpcTalkPacket(payload, payloadSize);
    }
}

static void StorePendingPacket(int requestId, void* socket, void* edx, COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size == 0) {
        return;
    }
    PendingPacket pending{};
    pending.socket = socket;
    pending.edx = edx;
    pending.bytes.assign(packet->Data, packet->Data + packet->Size);

    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_pendingPackets[requestId] = std::move(pending);
}

static bool TakePendingPacket(int requestId, PendingPacket& out) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_pendingPackets.find(requestId);
    if (it == g_pendingPackets.end()) {
        return false;
    }
    out = std::move(it->second);
    g_pendingPackets.erase(it);
    return true;
}

static bool TakePendingLocalQuestAction(int requestId, PendingLocalQuestAction& out) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_pendingLocalQuestActions.find(requestId);
    if (it == g_pendingLocalQuestActions.end()) {
        return false;
    }
    out = it->second;
    g_pendingLocalQuestActions.erase(it);
    return true;
}

static void StorePendingLocalQuestAction(int requestId, void* thisPtr, int arg) {
    if (thisPtr == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_pendingLocalQuestActions[requestId] = PendingLocalQuestAction{ thisPtr, arg };
}

static bool ReplayPendingPacket(int requestId) {
    PendingPacket pending{};
    if (!TakePendingPacket(requestId, pending) || pending.bytes.empty()) {
        return false;
    }

    COutPacket out{};
    out.Loopback = 0;
    out.Data = pending.bytes.data();
    out.Size = static_cast<unsigned long>(pending.bytes.size());
    out.Offset = 0;
    out.EncryptedByShanda = 0;
    g_SendPacket(pending.socket, pending.edx, &out);
    return true;
}

static bool ReplayPendingLocalQuestAction(int requestId) {
    PendingLocalQuestAction pending{};
    if (!TakePendingLocalQuestAction(requestId, pending) || pending.thisPtr == nullptr) {
        return false;
    }
    Trace("ReplayLocalQuestAction requestId=%d this=%p arg=%d", requestId, pending.thisPtr, pending.arg);
    g_replayingLocalQuestAction = true;
    g_QuestActionClick(pending.thisPtr, nullptr, pending.arg);
    g_replayingLocalQuestAction = false;
    return true;
}

static void DropPendingPacket(int requestId) {
    PendingPacket ignored{};
    TakePendingPacket(requestId, ignored);
    PendingLocalQuestAction localIgnored{};
    TakePendingLocalQuestAction(requestId, localIgnored);
}

static void SendInteractionHookEvent(void* socket, void* edx, const HookEvent& event) {
    std::vector<unsigned char> payload;
    payload.reserve(58);
    WriteU16(payload, kRecvCustomPacket);
    WriteU16(payload, kCustomInteractionHookEvent);
    WriteI32(payload, event.requestId);
    WriteI32(payload, event.eventType);
    WriteI32(payload, event.targetType);
    WriteI32(payload, event.targetId);
    WriteI32(payload, event.objectId);
    WriteI32(payload, event.clientNpcId);
    WriteI32(payload, event.questId);
    WriteI32(payload, event.questState);
    WriteI32(payload, event.rawAction);
    WriteI32(payload, event.selection);
    WriteI32(payload, event.dialogContext);
    WriteI32(payload, event.dialogState);
    WriteI32(payload, event.reserved);

    COutPacket out{};
    out.Loopback = 0;
    out.Data = payload.data();
    out.Size = static_cast<unsigned long>(payload.size());
    out.Offset = 0;
    out.EncryptedByShanda = 0;
    g_SendPacket(socket, edx, &out);
    Trace("SendHookEvent requestId=%d eventType=%d targetType=%d targetId=%d objectId=%d npcId=%d questId=%d state=%d rawAction=%d selection=%d",
        event.requestId,
        event.eventType,
        event.targetType,
        event.targetId,
        event.objectId,
        event.clientNpcId,
        event.questId,
        event.questState,
        event.rawAction,
        event.selection);
}

static bool ParseQuestAction(unsigned char nativeAction, int& actionMask, int& questState) {
    switch (nativeAction) {
        case 1:
        case 4:
            actionMask = kActionMaskQueryStart;
            questState = kQuestStateNotStarted;
            return true;
        case 2:
            actionMask = kActionMaskQueryComplete;
            questState = kQuestStateStarted;
            return true;
        case 5:
            actionMask = kActionMaskQueryProgress;
            questState = kQuestStateStarted;
            return true;
        default:
            return false;
    }
}

static bool TrySendHookEvent(void* socket, void* edx, COutPacket* packet, HookEvent event, int actionMask) {
    const bool intercept = ShouldIntercept(event, actionMask);
    Trace("ShouldIntercept eventType=%d targetType=%d targetId=%d objectId=%d npcId=%d questId=%d state=%d rawAction=%d actionMask=%d selection=%d rulesLoaded=%d ruleCount=%d intercept=%d",
        event.eventType,
        event.targetType,
        event.targetId,
        event.objectId,
        event.clientNpcId,
        event.questId,
        event.questState,
        event.rawAction,
        actionMask,
        event.selection,
        RulesLoaded() ? 1 : 0,
        RuleCount(),
        intercept ? 1 : 0);
    if (!intercept) {
        return false;
    }

    event.requestId = g_nextRequestId.fetch_add(1);
    StorePendingPacket(event.requestId, socket, edx, packet);
    SendInteractionHookEvent(socket, edx, event);
    return true;
}

static bool TryInterceptNpcTalk(void* socket, void* edx, COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size < 6) {
        return false;
    }

    const unsigned char* data = packet->Data;
    if (ReadU16(data) != kRecvNpcTalk) {
        return false;
    }

    const int objectId = ReadI32(data + 2);
    const int npcId = ResolveNpcIdByObjectId(objectId);
    Trace("Outgoing NPC_TALK size=%lu objectId=%d resolvedNpcId=%d", packet->Size, objectId, npcId);
    HookEvent event{};
    event.eventType = kEventNpcClick;
    event.targetType = kTargetNpc;
    event.targetId = npcId;
    event.objectId = objectId;
    event.clientNpcId = npcId;
    event.questId = 0;
    event.questState = kQuestStateNone;
    event.rawAction = 0;
    event.selection = kAnyId;
    event.dialogContext = kDialogContextNone;
    event.dialogState = kDialogStateNone;
    return TrySendHookEvent(socket, edx, packet, event, kActionMaskAny);
}

static bool TryInterceptNpcTalkMore(void* socket, void* edx, COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size < 4) {
        return false;
    }

    const unsigned char* data = packet->Data;
    if (ReadU16(data) != kRecvNpcTalkMore) {
        return false;
    }

    const unsigned char lastMsg = data[2];
    const unsigned char action = data[3];
    if (lastMsg == 2) {
        Trace("Outgoing NPC_MORE ignored text-input size=%lu lastMsg=%u action=%u", packet->Size, lastMsg, action);
        return false;
    }

    int selection = kAnyId;
    if (packet->Size >= 8) {
        selection = ReadI32(data + 4);
    } else if (packet->Size > 4) {
        selection = static_cast<int>(data[4]);
    }
    if (selection < 0) {
        Trace("Outgoing NPC_MORE ignored no-selection size=%lu lastMsg=%u action=%u selection=%d", packet->Size, lastMsg, action, selection);
        return false;
    }

    const int npcId = CurrentDialogNpcId();
    Trace("Outgoing NPC_MORE size=%lu lastMsg=%u action=%u selection=%d currentNpcId=%d",
        packet->Size, lastMsg, action, selection, npcId);
    HookEvent event{};
    event.eventType = kEventNpcDialogSelection;
    event.targetType = kTargetDialogSelection;
    event.targetId = selection;
    event.objectId = 0;
    event.clientNpcId = npcId;
    event.questId = 0;
    event.questState = kQuestStateNone;
    event.rawAction = static_cast<int>(action);
    event.selection = selection;
    event.dialogContext = kDialogContextNpc;
    event.dialogState = CurrentDialogState();
    return TrySendHookEvent(socket, edx, packet, event, kActionMaskAny);
}

static bool TryInterceptQuestAction(void* socket, void* edx, COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size < 5) {
        return false;
    }

    const unsigned char* data = packet->Data;
    if (ReadU16(data) != kRecvQuestAction) {
        return false;
    }

    const unsigned char nativeAction = data[2];
    const int questId = ReadU16(data + 3);
    int actionMask = 0;
    int questState = 0;
    if (!ParseQuestAction(nativeAction, actionMask, questState)) {
        Trace("Outgoing QUEST_ACTION ignored nativeAction=%u questId=%d size=%lu",
            nativeAction, questId, packet->Size);
        return false;
    }

    int npcId = 0;
    if (packet->Size >= 9) {
        npcId = ReadI32(data + 5);
    }
    Trace("Outgoing QUEST_ACTION size=%lu action=%u questId=%d npcId=%d actionMask=%d questState=%d",
        packet->Size, nativeAction, questId, npcId, actionMask, questState);

    HookEvent event{};
    event.eventType = kEventQuestAction;
    event.targetType = kTargetQuest;
    event.targetId = questId;
    event.objectId = 0;
    event.clientNpcId = npcId;
    event.questId = questId;
    event.questState = questState;
    event.rawAction = static_cast<int>(nativeAction);
    event.selection = kAnyId;
    event.dialogContext = kDialogContextNone;
    event.dialogState = kDialogStateNone;
    return TrySendHookEvent(socket, edx, packet, event, actionMask);
}

static bool TryReadLocalQuestAction(void* thisPtr, int& questId, int& npcId, int& nativeAction, int& actionMask, int& questState) {
    if (thisPtr == nullptr) {
        return false;
    }

    __try {
        const unsigned char* base = reinterpret_cast<const unsigned char*>(thisPtr);
        questId = static_cast<int>(*reinterpret_cast<const unsigned short*>(base + 0x0C));
        npcId = *reinterpret_cast<const int*>(base + 0x10);
        const int questEntryState = *reinterpret_cast<const int*>(base + 0x14);
        if (questEntryState == 0) {
            nativeAction = 4;
        } else if (questEntryState == 1) {
            nativeAction = 5;
        } else {
            nativeAction = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        questId = 0;
        npcId = 0;
        nativeAction = 0;
        return false;
    }

    if (questId <= 0 || !ParseQuestAction(static_cast<unsigned char>(nativeAction), actionMask, questState)) {
        return false;
    }
    return true;
}

static bool TryInterceptLocalQuestAction(void* thisPtr, int arg) {
    int questId = 0;
    int npcId = 0;
    int nativeAction = 0;
    int actionMask = 0;
    int questState = 0;
    if (!TryReadLocalQuestAction(thisPtr, questId, npcId, nativeAction, actionMask, questState)) {
        return false;
    }

    HookEvent event{};
    event.eventType = kEventQuestAction;
    event.targetType = kTargetQuest;
    event.targetId = questId;
    event.objectId = 0;
    event.clientNpcId = npcId;
    event.questId = questId;
    event.questState = questState;
    event.rawAction = nativeAction;
    event.selection = kAnyId;
    event.dialogContext = kDialogContextNone;
    event.dialogState = kDialogStateNone;

    const bool intercept = ShouldIntercept(event, actionMask);
    Trace("Local QUEST_ACTION this=%p arg=%d action=%d questId=%d npcId=%d actionMask=%d questState=%d rulesLoaded=%d ruleCount=%d intercept=%d",
        thisPtr,
        arg,
        nativeAction,
        questId,
        npcId,
        actionMask,
        questState,
        RulesLoaded() ? 1 : 0,
        RuleCount(),
        intercept ? 1 : 0);
    if (!intercept) {
        return false;
    }

    DWORD socketPtr = 0;
    if (!TryReadDword(kClientSocketPtr, socketPtr) || socketPtr == 0) {
        Trace("Local QUEST_ACTION reject questId=%d reason=no-socket", questId);
        return false;
    }

    event.requestId = g_nextRequestId.fetch_add(1);
    StorePendingLocalQuestAction(event.requestId, thisPtr, arg);
    SendInteractionHookEvent(reinterpret_cast<void*>(socketPtr), nullptr, event);
    return true;
}

static void HandleResult(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 12) {
        Trace("HandleResult reject payloadSize=%lu reason=short", payloadSize);
        return;
    }
    const int version = ReadI32(payload);
    const int requestId = ReadI32(payload + 4);
    const int resultCode = ReadI32(payload + 8);
    if ((version != kVersion && version != kLegacyRulesVersion) || requestId <= 0) {
        Trace("HandleResult reject version=%d requestId=%d result=%d",
            version, requestId, resultCode);
        return;
    }
    Trace("HandleResult requestId=%d result=%d", requestId, resultCode);
    if (resultCode == kResultFallbackOriginal) {
        if (!ReplayPendingPacket(requestId)) {
            ReplayPendingLocalQuestAction(requestId);
        }
        return;
    }
    DropPendingPacket(requestId);
}

static IncomingResult HandleIncomingAtOffset(CInPacket* packet, unsigned long headerOffset) {
    const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
    const unsigned long packetSize = IncomingPacketSize(packet);
    if (packetSize < headerOffset + 2) {
        return IncomingResult::None;
    }

    const unsigned short opcode = ReadU16(data + headerOffset);
    const unsigned char* payload = data + headerOffset + 2;
    const unsigned long payloadSize = packetSize - headerOffset - 2;

    if (opcode == kS2CInteractionHookRules) {
        Trace("Incoming hook rules opcode offset=%lu payloadSize=%lu", headerOffset, payloadSize);
        ApplyRules(payload, payloadSize);
        return IncomingResult::Consumed;
    }
    if (opcode == kS2CInteractionHookResult) {
        Trace("Incoming hook result opcode offset=%lu payloadSize=%lu", headerOffset, payloadSize);
        HandleResult(payload, payloadSize);
        return IncomingResult::Consumed;
    }

    TrackIncomingPacket(opcode, payload, payloadSize);
    return IncomingResult::None;
}

static bool HandleIncoming(CInPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr) {
        return false;
    }

    IncomingResult result = HandleIncomingAtOffset(packet, 4);
    if (result != IncomingResult::None) {
        return result == IncomingResult::Consumed;
    }
    result = HandleIncomingAtOffset(packet, 0);
    return result == IncomingResult::Consumed;
}

static bool TryInterceptOutgoing(void* socket, void* edx, COutPacket* packet) {
    if (TryInterceptNpcTalk(socket, edx, packet)) {
        return true;
    }
    if (TryInterceptNpcTalkMore(socket, edx, packet)) {
        return true;
    }
    return TryInterceptQuestAction(socket, edx, packet);
}

} // namespace

bool HandleQuestHookIncoming(void* packet) {
    bool handled = false;
    __try {
        handled = HandleIncoming(reinterpret_cast<CInPacket*>(packet));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Trace("HandleQuestHookIncoming exception");
        handled = false;
    }
    return handled;
}

bool TryHandleQuestHookSend(void* socket, void* edx, void* packet) {
    bool handled = false;
    __try {
        COutPacket* outPacket = reinterpret_cast<COutPacket*>(packet);
        handled = TryInterceptOutgoing(socket, edx, outPacket);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Trace("TryHandleQuestHookSend exception");
        handled = false;
    }
    return handled;
}

static void __fastcall QuestActionClick_Hook(void* pThis, void* edx, int arg) {
    if (g_replayingLocalQuestAction) {
        g_QuestActionClick(pThis, edx, arg);
        return;
    }
    if (TryInterceptLocalQuestAction(pThis, arg)) {
        return;
    }
    g_QuestActionClick(pThis, edx, arg);
}

void HookQuestActionClick(bool enable) {
    const bool ok = Memory::SetHook(enable, reinterpret_cast<void**>(&g_QuestActionClick), QuestActionClick_Hook);
    Trace("QuestActionClick hook enable=%d ok=%d addr=0x%08X", enable ? 1 : 0, ok ? 1 : 0, kQuestActionClickAddr);
}

void QuestHookTrace(const char* format, ...) {
    va_list args;
    va_start(args, format);
    TraceV(format, args);
    va_end(args);
}
