#include "stdafx.h"
#include "QuestHook.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr WORD kRecvNpcTalk = 0x003A;
constexpr WORD kRecvNpcTalkMore = 0x003C;
constexpr WORD kRecvQuestAction = 0x006B;
constexpr WORD kRecvCloseRangeAttack = 0x002C;
constexpr WORD kRecvRangedAttack = 0x002D;
constexpr WORD kRecvMagicAttack = 0x002E;
constexpr WORD kRecvCustomPacket = 0x3713;
constexpr WORD kCustomInteractionHookEvent = 0x1003;

constexpr WORD kSendSpawnNpc = 0x0101;
constexpr WORD kSendRemoveNpc = 0x0102;
constexpr WORD kSendSpawnNpcController = 0x0103;
constexpr WORD kSendNpcTalk = 0x0130;
constexpr WORD kSendSetField = 0x007D;
constexpr WORD kSendStatChanged = 0x001F;
constexpr WORD kS2CInteractionHookRules = 0x1001;
constexpr WORD kS2CInteractionHookResult = 0x1002;
constexpr WORD kS2CInteractionHookProgress = 0x1004;
constexpr WORD kS2CClientRuntimeConfig = 0x1005;
constexpr DWORD kClientSocketPtr = 0x00BE7914;
constexpr DWORD kQuestActionClickAddr = 0x00716FE1;
constexpr DWORD kGenerateAutoKeyDownAddr = 0x0059B2D2;
constexpr unsigned int kKeyRepeatCountMask = 0x0000FFFF;
constexpr unsigned int kPreviousKeyStateMask = 0x40000000;
constexpr unsigned int kTransitionStateMask = 0x80000000;

constexpr int kLegacyRulesVersion = 3;
constexpr int kVersion = 5; // multi-condition progress (conditionCount per entry)
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
constexpr int kDialogContextQuest = 2;
constexpr int kDialogContextInteractionHook = 3;
constexpr int kDialogStateNone = 0;
constexpr int kDialogStateOpen = 1;
constexpr int kDialogStateSuppressedAfterNativeNpc = 100;

constexpr int kMapleAdministratorNpc = 9010000;

constexpr int kResultHandledDialog = 0;
constexpr int kResultHandledUpdate = 1;
constexpr int kResultFallbackOriginal = 2;
constexpr int kResultRejected = 3;
constexpr int kResultError = 4;

constexpr int kScopeAllRules = 0;
constexpr int kScopeCharacterQuestRules = 1;
constexpr int kScopeMapNpcRules = 2;
constexpr int kScopeDialogTempRules = 3;

constexpr int kReplaceScope = 1;
constexpr int kClearScope = 2;
constexpr int kMaxRulesPerPacket = 100;
constexpr int kMaxRuleCount = 20000;
constexpr ULONGLONG kPendingRuleBatchTimeoutMs = 5000;
constexpr ULONGLONG kIgnoreNextQuestActionTimeoutMs = 1500;

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

struct ISMSG {
    unsigned int message;
    unsigned int wParam;
    int lParam;
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

enum class PendingKind {
    None,
    Packet,
    LocalQuestAction
};

enum class StorePendingResult {
    Stored,
    Duplicate,
    Failed
};

struct ActivePending {
    int requestId;
    int eventType;
    int expectedNpcId;
    int questId;
    int npcId;
    int rawAction;
    PendingKind kind;
    PendingPacket packet;
    PendingLocalQuestAction localQuestAction;
};

struct IgnoredQuestAction {
    bool active;
    int questId;
    int npcId;
    int rawAction;
    ULONGLONG expiresAt;
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

using GenerateAutoKeyDown_t = int(__fastcall*)(void* pThis, void* edx, ISMSG* message);
static GenerateAutoKeyDown_t g_GenerateAutoKeyDown = reinterpret_cast<GenerateAutoKeyDown_t>(kGenerateAutoKeyDownAddr);

static std::mutex g_stateMutex;
static std::vector<InteractionHookRule> g_characterQuestRules;
static std::vector<InteractionHookRule> g_mapNpcRules;
static std::vector<InteractionHookRule> g_dialogTempRules;
static std::unordered_map<int, std::string> g_progressTextByQuestId;
static std::unordered_map<int, std::vector<std::pair<int, int>>> g_progressConditionsByQuestId;
static std::unordered_map<int, int> g_progressQuestState;
static std::unordered_map<int, PendingRuleBatch> g_pendingRuleBatches;
static ActivePending g_activePending{};
static IgnoredQuestAction g_ignoreNextOutgoingQuestAction{};
static std::unordered_map<int, int> g_npcIdByObjectId;
static int g_lastAppliedRuleBatchIds[4] = {};
static bool g_rulesLoaded = false;
static int g_currentDialogNpcId = 0;
static int g_currentDialogContext = kDialogContextNone;
static int g_currentDialogState = kDialogStateNone;
static int g_expectedInteractionHookNpcTalkNpcId = 0;
static ULONGLONG g_expectedInteractionHookNpcTalkUntil = 0;
static bool g_replayingLocalQuestAction = false;
static std::atomic<int> g_nextRequestId{ 1 };
static std::mutex g_traceMutex;
static PVOID g_exceptionHandler = nullptr;
static HANDLE g_keyTraceThread = nullptr;
static std::atomic<bool> g_keyTraceRunning{ false };
static std::atomic<bool> g_autoKeyDownFixEnabled{ true };

static void Trace(const char* format, ...);

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

static const char* AttackOpcodeName(unsigned short opcode) {
    switch (opcode) {
    case kRecvCloseRangeAttack:
        return "close";
    case kRecvRangedAttack:
        return "ranged";
    case kRecvMagicAttack:
        return "magic";
    default:
        return "unknown";
    }
}

static bool IsAttackOpcode(unsigned short opcode) {
    return opcode == kRecvCloseRangeAttack
        || opcode == kRecvRangedAttack
        || opcode == kRecvMagicAttack;
}

static int IsKeyDown(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0 ? 1 : 0;
}

static void AppendHeldKey(std::string& out, int virtualKey, const char* name) {
    if (!IsKeyDown(virtualKey)) {
        return;
    }

    if (!out.empty()) {
        out += '+';
    }
    out += name;
}

static std::string GetHeldKeys() {
    std::string held;
    char keyName[] = { '\0', '\0' };
    for (int key = 'A'; key <= 'Z'; key++) {
        keyName[0] = static_cast<char>(key);
        AppendHeldKey(held, key, keyName);
    }
    for (int key = '0'; key <= '9'; key++) {
        keyName[0] = static_cast<char>(key);
        AppendHeldKey(held, key, keyName);
    }

    char functionName[4] = {};
    for (int key = VK_F1; key <= VK_F12; key++) {
        std::snprintf(functionName, sizeof(functionName), "F%d", key - VK_F1 + 1);
        AppendHeldKey(held, key, functionName);
    }

    AppendHeldKey(held, VK_INSERT, "Insert");
    AppendHeldKey(held, VK_HOME, "Home");
    AppendHeldKey(held, VK_PRIOR, "PageUp");
    AppendHeldKey(held, VK_DELETE, "Delete");
    AppendHeldKey(held, VK_END, "End");
    AppendHeldKey(held, VK_NEXT, "PageDown");
    AppendHeldKey(held, VK_CONTROL, "Ctrl");
    AppendHeldKey(held, VK_SHIFT, "Shift");
    AppendHeldKey(held, VK_MENU, "Alt");
    AppendHeldKey(held, VK_SPACE, "Space");
    if (held.empty()) {
        return "none";
    }
    return held;
}

static std::string VirtualKeyName(unsigned int virtualKey) {
    if ((virtualKey >= 'A' && virtualKey <= 'Z') || (virtualKey >= '0' && virtualKey <= '9')) {
        char keyName[] = { static_cast<char>(virtualKey), '\0' };
        return keyName;
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
        char keyName[4] = {};
        std::snprintf(keyName, sizeof(keyName), "F%u", virtualKey - VK_F1 + 1);
        return keyName;
    }

    switch (virtualKey) {
    case VK_INSERT:
        return "Insert";
    case VK_HOME:
        return "Home";
    case VK_PRIOR:
        return "PageUp";
    case VK_DELETE:
        return "Delete";
    case VK_END:
        return "End";
    case VK_NEXT:
        return "PageDown";
    case VK_CONTROL:
        return "Ctrl";
    case VK_SHIFT:
        return "Shift";
    case VK_MENU:
        return "Alt";
    case VK_SPACE:
        return "Space";
    default:
        char keyName[12] = {};
        std::snprintf(keyName, sizeof(keyName), "VK_%02X", virtualKey & 0xFF);
        return keyName;
    }
}

static bool NormalizeAutoKeyDownMessage(ISMSG* message, unsigned int& before, unsigned int& after) {
    if (message == nullptr || (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)) {
        before = 0;
        after = 0;
        return false;
    }

    before = static_cast<unsigned int>(message->lParam);
    after = (before & ~(kKeyRepeatCountMask | kPreviousKeyStateMask | kTransitionStateMask)) | 1;
    message->lParam = static_cast<int>(after);
    return before != after;
}

static void TraceOutgoingAttackPacket(COutPacket* packet) {
    if (!Client::debug || packet == nullptr || packet->Data == nullptr || packet->Size < 8) {
        return;
    }

    const unsigned char* data = packet->Data;
    const unsigned short opcode = ReadU16(data);
    if (!IsAttackOpcode(opcode)) {
        return;
    }

    const unsigned char attackedAndDamage = data[3];
    const int numAttacked = (attackedAndDamage >> 4) & 0x0F;
    const int numDamage = attackedAndDamage & 0x0F;
    const int skillId = ReadI32(data + 4);
    const std::string heldKeys = GetHeldKeys();

    Trace("Outgoing ATTACK type=%s opcode=0x%04X size=%lu skillId=%d numAttacked=%d numDamage=%d tick=%lu heldKeys=%s",
        AttackOpcodeName(opcode),
        opcode,
        packet->Size,
        skillId,
        numAttacked,
        numDamage,
        GetTickCount(),
        heldKeys.c_str());
}

static bool ReadPacketString(const unsigned char*& cursor, const unsigned char* end, std::string& out) {
    if (cursor == nullptr || end == nullptr || cursor + 2 > end) {
        return false;
    }

    const int length = ReadU16(cursor);
    cursor += 2;
    if (length < 0 || cursor + length > end) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(cursor), static_cast<size_t>(length));
    cursor += length;
    return true;
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
    if (!Client::debug) return;
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

static void TraceModuleForAddress(const char* label, DWORD address) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0) {
        Trace("%s address=0x%08X module=<VirtualQuery failed>", label, address);
        return;
    }

    char modulePath[MAX_PATH]{};
    const DWORD moduleBase = reinterpret_cast<DWORD>(mbi.AllocationBase);
    if (GetModuleFileNameA(reinterpret_cast<HMODULE>(moduleBase), modulePath, MAX_PATH) == 0) {
        Trace("%s address=0x%08X moduleBase=0x%08X offset=0x%08X module=<unknown>",
            label,
            address,
            moduleBase,
            address - moduleBase);
        return;
    }

    Trace("%s address=0x%08X moduleBase=0x%08X offset=0x%08X module=%s",
        label,
        address,
        moduleBase,
        address - moduleBase,
        modulePath);
}

static void TraceStackDwords(DWORD esp) {
    DWORD values[16]{};
    bool ok = true;
    __try {
        const DWORD* stack = reinterpret_cast<const DWORD*>(esp);
        for (int i = 0; i < 16; ++i) {
            values[i] = stack[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    if (!ok) {
        Trace("Crash stack esp=0x%08X read-failed", esp);
        return;
    }

    Trace("Crash stack esp=0x%08X dwords=%08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
        esp,
        values[0], values[1], values[2], values[3],
        values[4], values[5], values[6], values[7],
        values[8], values[9], values[10], values[11],
        values[12], values[13], values[14], values[15]);
}

static bool IsCrashLikeException(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION
        || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED
        || code == EXCEPTION_DATATYPE_MISALIGNMENT
        || code == EXCEPTION_FLT_DIVIDE_BY_ZERO
        || code == EXCEPTION_ILLEGAL_INSTRUCTION
        || code == EXCEPTION_IN_PAGE_ERROR
        || code == EXCEPTION_INT_DIVIDE_BY_ZERO
        || code == EXCEPTION_PRIV_INSTRUCTION
        || code == EXCEPTION_STACK_OVERFLOW;
}

static LONG WINAPI QuestDiagnosticsExceptionHandler(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr || info->ContextRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (!IsCrashLikeException(code)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD exceptionAddress = reinterpret_cast<DWORD>(info->ExceptionRecord->ExceptionAddress);
    const DWORD faultAddress = info->ExceptionRecord->NumberParameters > 1
        ? static_cast<DWORD>(info->ExceptionRecord->ExceptionInformation[1])
        : 0;

    Trace("Crash exception code=0x%08X flags=0x%08X exceptionAddress=0x%08X faultAddress=0x%08X eip=0x%08X esp=0x%08X ebp=0x%08X eax=0x%08X ebx=0x%08X ecx=0x%08X edx=0x%08X esi=0x%08X edi=0x%08X",
        code,
        info->ExceptionRecord->ExceptionFlags,
        exceptionAddress,
        faultAddress,
        info->ContextRecord->Eip,
        info->ContextRecord->Esp,
        info->ContextRecord->Ebp,
        info->ContextRecord->Eax,
        info->ContextRecord->Ebx,
        info->ContextRecord->Ecx,
        info->ContextRecord->Edx,
        info->ContextRecord->Esi,
        info->ContextRecord->Edi);
    TraceModuleForAddress("Crash eip-module", info->ContextRecord->Eip);
    if (exceptionAddress != info->ContextRecord->Eip) {
        TraceModuleForAddress("Crash exception-module", exceptionAddress);
    }
    if (faultAddress != 0) {
        TraceModuleForAddress("Crash fault-module", faultAddress);
    }
    TraceStackDwords(info->ContextRecord->Esp);

    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI QuestKeyTraceThreadProc(LPVOID) {
    bool qWasDown = false;
    Trace("Quest diagnostics key trace thread started");
    while (g_keyTraceRunning.load()) {
        const bool qDown = (GetAsyncKeyState('Q') & 0x8000) != 0;
        if (qDown != qWasDown) {
            HWND foreground = GetForegroundWindow();
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            Trace("KeyTrace Q %s hwnd=%p pid=%lu tick=%lu",
                qDown ? "down" : "up",
                foreground,
                static_cast<unsigned long>(pid),
                GetTickCount());
            qWasDown = qDown;
        }
        Sleep(20);
    }
    Trace("Quest diagnostics key trace thread stopped");
    return 0;
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

static void ClearDialogStateLocked() {
    g_currentDialogNpcId = 0;
    g_currentDialogContext = kDialogContextNone;
    g_currentDialogState = kDialogStateNone;
}

static void ClearIgnoreNextQuestActionLocked() {
    g_ignoreNextOutgoingQuestAction = IgnoredQuestAction{};
}

static void ClearExpectedInteractionHookNpcTalkLocked() {
    g_expectedInteractionHookNpcTalkNpcId = 0;
    g_expectedInteractionHookNpcTalkUntil = 0;
}

static void StoreExpectedInteractionHookNpcTalkLocked(int npcId) {
    g_expectedInteractionHookNpcTalkNpcId = npcId;
    g_expectedInteractionHookNpcTalkUntil = GetTickCount64() + kIgnoreNextQuestActionTimeoutMs;
}

static void ClearAllRulesLocked() {
    g_characterQuestRules.clear();
    g_mapNpcRules.clear();
    g_dialogTempRules.clear();
    g_progressTextByQuestId.clear();
    g_progressConditionsByQuestId.clear();
    g_pendingRuleBatches.clear();
    g_activePending = ActivePending{};
    ClearIgnoreNextQuestActionLocked();
    ClearExpectedInteractionHookNpcTalkLocked();
    g_npcIdByObjectId.clear();
    ClearDialogStateLocked();
    for (int& batchId : g_lastAppliedRuleBatchIds) {
        batchId = 0;
    }
    g_rulesLoaded = false;
}

static std::string ProgressTextForQuestLocked(int questId, bool& hit) {
    auto it = g_progressTextByQuestId.find(questId);
    if (it == g_progressTextByQuestId.end() || it->second.empty()) {
        hit = false;
        return "";
    }
    hit = true;
    return it->second;
}

static bool FindNextProgressMarker(const std::string& source, size_t searchFrom, size_t& markerStart,
                                   std::string& prefix) {
    static const char* prefixes[] = {
        "@@BD_IH_PROGRESS:",
        "@@DB_IH_PROGRESS:",
        "@@BD_LP_PROGRESS:",
    };
    markerStart = std::string::npos;
    prefix.clear();
    for (const char* candidate : prefixes) {
        const std::string candidatePrefix(candidate);
        size_t pos = source.find(candidatePrefix, searchFrom);
        if (pos == std::string::npos) {
            continue;
        }
        if (markerStart == std::string::npos || pos < markerStart) {
            markerStart = pos;
            prefix = candidatePrefix;
        }
    }
    return markerStart != std::string::npos;
}

static bool ParseProgressMarker(const std::string& source, size_t prefixPos, const std::string& prefix, int& questId,
                                size_t& markerEnd) {
    questId = 0;
    markerEnd = std::string::npos;

    size_t pos = prefixPos + prefix.size();
    if (pos >= source.size() || !std::isdigit(static_cast<unsigned char>(source[pos]))) {
        return false;
    }

    while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) {
        questId = questId * 10 + (source[pos] - '0');
        ++pos;
    }
    if (questId <= 0 || pos >= source.size() || source[pos] != '@') {
        return false;
    }

    while (pos < source.size() && source[pos] == '@') {
        ++pos;
    }
    markerEnd = pos;
    return true;
}

static size_t MalformedProgressMarkerEnd(const std::string& source, size_t prefixPos, const std::string& prefix) {
    const size_t contentStart = prefixPos + prefix.size();
    size_t markerEnd = source.find("@@", contentStart);
    if (markerEnd == std::string::npos) {
        markerEnd = source.find('@', contentStart);
        if (markerEnd == std::string::npos) {
            return source.size();
        }
        while (markerEnd < source.size() && source[markerEnd] == '@') {
            ++markerEnd;
        }
        return markerEnd;
    }
    return markerEnd + 2;
}

static bool ReplaceProgressMarkers(const char* input, std::string& output) {
    if (input == nullptr) {
        return false;
    }

    std::string source(input);
    size_t searchFrom = 0;
    bool replaced = false;
    output.clear();
    while (true) {
        size_t markerStart = std::string::npos;
        std::string prefix;
        if (!FindNextProgressMarker(source, searchFrom, markerStart, prefix)) {
            output.append(source, searchFrom, std::string::npos);
            break;
        }

        int questId = 0;
        size_t markerEnd = std::string::npos;
        if (!ParseProgressMarker(source, markerStart, prefix, questId, markerEnd)) {
            size_t malformedEnd = MalformedProgressMarkerEnd(source, markerStart, prefix);
            output.append(source, searchFrom, markerStart - searchFrom);
            Trace("InteractionHook progress marker malformed prefix=%s text=%s", prefix.c_str(),
                source.substr(markerStart, malformedEnd - markerStart).c_str());
            searchFrom = malformedEnd;
            replaced = true;
            continue;
        }

        std::string replacement;
        bool hit = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            replacement = ProgressTextForQuestLocked(questId, hit);
        }
        output.append(source, searchFrom, markerStart - searchFrom);
        output.append(replacement);
        Trace("InteractionHook progress marker replaced questId=%d hit=%d text=%s",
            questId,
            hit ? 1 : 0,
            replacement.c_str());
        searchFrom = markerEnd;
        replaced = true;
    }
    if (replaced) {
        Trace("InteractionHook progress marker text source=%s output=%s",
            source.c_str(),
            output.c_str());
    }
    return replaced;
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

static int CurrentDialogContext() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_currentDialogContext;
}

static int CurrentDialogState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_currentDialogState;
}

static void ClearExpectedInteractionHookNpcTalkForOutgoingClick() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_expectedInteractionHookNpcTalkNpcId <= 0) {
        return;
    }
    Trace("ExpectedInteractionHookNpcTalk cleared by outgoing NPC_TALK npcId=%d",
        g_expectedInteractionHookNpcTalkNpcId);
    ClearExpectedInteractionHookNpcTalkLocked();
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
    ClearIgnoreNextQuestActionLocked();
    ClearExpectedInteractionHookNpcTalkLocked();
    ClearDialogStateLocked();
}

static int ExpectedNpcTalkAckId(const HookEvent& event) {
    switch (event.eventType) {
        case kEventNpcClick:
        case kEventNpcDialogSelection:
            return event.clientNpcId > 0 ? event.clientNpcId : 0;
        case kEventQuestAction:
            return event.clientNpcId > 0 ? event.clientNpcId : kMapleAdministratorNpc;
        default:
            return 0;
    }
}

static void ClearActivePendingLocked() {
    g_activePending = ActivePending{};
}

static void DropActivePendingOnNpcTalkLocked(int npcId) {
    if (g_activePending.requestId <= 0 || g_activePending.expectedNpcId <= 0) {
        return;
    }
    if (g_activePending.expectedNpcId != npcId) {
        return;
    }
    Trace("DropPendingOnNpcTalk requestId=%d eventType=%d npcId=%d",
        g_activePending.requestId,
        g_activePending.eventType,
        npcId);
    ClearActivePendingLocked();
}

static bool ActivePendingMatchesNpcTalkLocked(int npcId) {
    return g_activePending.requestId > 0
        && g_activePending.expectedNpcId > 0
        && g_activePending.expectedNpcId == npcId;
}

static bool ConsumeExpectedInteractionHookNpcTalkLocked(int npcId) {
    if (g_expectedInteractionHookNpcTalkNpcId <= 0) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    if (now >= g_expectedInteractionHookNpcTalkUntil) {
        Trace("ExpectedInteractionHookNpcTalk expired npcId=%d incomingNpcId=%d",
            g_expectedInteractionHookNpcTalkNpcId,
            npcId);
        ClearExpectedInteractionHookNpcTalkLocked();
        return false;
    }
    if (g_expectedInteractionHookNpcTalkNpcId != npcId) {
        return false;
    }
    ClearExpectedInteractionHookNpcTalkLocked();
    return true;
}

static void TrackDialogEndLocked() {
    if (g_currentDialogState == kDialogStateNone) {
        return;
    }
    if (g_currentDialogContext == kDialogContextInteractionHook) {
        Trace("TrackDialogEnd context=INTERACTION_HOOK npcId=%d state=NONE", g_currentDialogNpcId);
        ClearExpectedInteractionHookNpcTalkLocked();
        ClearDialogStateLocked();
        return;
    }
    if (g_currentDialogContext == kDialogContextNpc) {
        g_currentDialogState = kDialogStateSuppressedAfterNativeNpc;
        Trace("TrackDialogEnd context=NPC npcId=%d state=SUPPRESSED_AFTER_NATIVE_NPC", g_currentDialogNpcId);
    }
}

static void TrackNpcMoreDialogEnd(unsigned char lastMsg, unsigned char action, int selection) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    Trace("TrackNpcMoreEnd lastMsg=%u action=%u selection=%d context=%d state=%d npcId=%d",
        lastMsg,
        action,
        selection,
        g_currentDialogContext,
        g_currentDialogState,
        g_currentDialogNpcId);
    TrackDialogEndLocked();
}

static void TrackNpcTalkPacket(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 6) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    const int npcId = ReadI32(payload + 1);
    const bool isHookNpcTalk = ActivePendingMatchesNpcTalkLocked(npcId)
        || ConsumeExpectedInteractionHookNpcTalkLocked(npcId);
    g_currentDialogNpcId = npcId;
    g_currentDialogContext = isHookNpcTalk ? kDialogContextInteractionHook : kDialogContextNpc;
    g_currentDialogState = kDialogStateOpen;
    ClearIgnoreNextQuestActionLocked();
    DropActivePendingOnNpcTalkLocked(g_currentDialogNpcId);
    Trace("TrackNpcTalk npcId=%d payloadSize=%lu context=%d hook=%d",
        g_currentDialogNpcId,
        payloadSize,
        g_currentDialogContext,
        isHookNpcTalk ? 1 : 0);
}

static void TrackStatChangedPacket(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 1 || payload[0] == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_currentDialogState == kDialogStateSuppressedAfterNativeNpc) {
        Trace("TrackEnableActions state=SUPPRESSED_AFTER_NATIVE_NPC npcId=%d ignored=1",
            g_currentDialogNpcId);
    }
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
        return;
    }
    if (opcode == kSendStatChanged) {
        TrackStatChangedPacket(payload, payloadSize);
    }
}

static StorePendingResult StorePendingPacket(int requestId, void* socket, void* edx, COutPacket* packet,
                                             const HookEvent& event) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size == 0) {
        return StorePendingResult::Failed;
    }
    PendingPacket pending{};
    pending.socket = socket;
    pending.edx = edx;
    pending.bytes.assign(packet->Data, packet->Data + packet->Size);

    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_activePending.requestId > 0) {
        Trace("StorePendingPacket duplicate activeRequestId=%d newRequestId=%d",
            g_activePending.requestId,
            requestId);
        return StorePendingResult::Duplicate;
    }
    g_activePending = ActivePending{};
    g_activePending.requestId = requestId;
    g_activePending.eventType = event.eventType;
    g_activePending.expectedNpcId = ExpectedNpcTalkAckId(event);
    g_activePending.questId = event.questId;
    g_activePending.npcId = event.clientNpcId;
    g_activePending.rawAction = event.rawAction;
    g_activePending.kind = PendingKind::Packet;
    g_activePending.packet = std::move(pending);
    Trace("StorePendingPacket requestId=%d eventType=%d expectedNpcId=%d",
        requestId,
        event.eventType,
        g_activePending.expectedNpcId);
    return StorePendingResult::Stored;
}

static bool TakePendingPacket(int requestId, PendingPacket& out) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_activePending.requestId != requestId || g_activePending.kind != PendingKind::Packet) {
        return false;
    }
    out = std::move(g_activePending.packet);
    ClearActivePendingLocked();
    return true;
}

static bool TakePendingLocalQuestAction(int requestId, PendingLocalQuestAction& out) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_activePending.requestId != requestId || g_activePending.kind != PendingKind::LocalQuestAction) {
        return false;
    }
    out = g_activePending.localQuestAction;
    ClearActivePendingLocked();
    return true;
}

static void StoreIgnoredQuestActionLocked(int questId, int npcId, int rawAction);

static StorePendingResult StorePendingLocalQuestAction(int requestId, void* thisPtr, int arg,
                                                       const HookEvent& event) {
    if (thisPtr == nullptr) {
        return StorePendingResult::Failed;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_activePending.requestId > 0) {
        Trace("StorePendingLocalQuestAction duplicate activeRequestId=%d newRequestId=%d",
            g_activePending.requestId,
            requestId);
        return StorePendingResult::Duplicate;
    }
    g_activePending = ActivePending{};
    g_activePending.requestId = requestId;
    g_activePending.eventType = event.eventType;
    g_activePending.expectedNpcId = ExpectedNpcTalkAckId(event);
    g_activePending.questId = event.questId;
    g_activePending.npcId = event.clientNpcId;
    g_activePending.rawAction = event.rawAction;
    g_activePending.kind = PendingKind::LocalQuestAction;
    g_activePending.localQuestAction = PendingLocalQuestAction{ thisPtr, arg };
    Trace("StorePendingLocalQuestAction requestId=%d eventType=%d expectedNpcId=%d",
        requestId,
        event.eventType,
        g_activePending.expectedNpcId);
    return StorePendingResult::Stored;
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

static void DropPendingPacket(int requestId, bool suppressQuestAction = false) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_activePending.requestId != requestId) {
        Trace("DropPending ignored requestId=%d activeRequestId=%d",
            requestId,
            g_activePending.requestId);
        return;
    }
    Trace("DropPending requestId=%d eventType=%d expectedNpcId=%d questId=%d npcId=%d rawAction=%d suppressQuestAction=%d",
        g_activePending.requestId,
        g_activePending.eventType,
        g_activePending.expectedNpcId,
        g_activePending.questId,
        g_activePending.npcId,
        g_activePending.rawAction,
        suppressQuestAction ? 1 : 0);
    if (suppressQuestAction
        && g_activePending.eventType == kEventQuestAction
        && g_activePending.questId > 0
        && g_activePending.rawAction > 0) {
        StoreIgnoredQuestActionLocked(g_activePending.questId, g_activePending.npcId, g_activePending.rawAction);
        if (g_activePending.expectedNpcId > 0) {
            StoreExpectedInteractionHookNpcTalkLocked(g_activePending.expectedNpcId);
        }
    }
    ClearActivePendingLocked();
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
    Trace("SendHookEvent requestId=%d eventType=%d targetType=%d targetId=%d objectId=%d npcId=%d questId=%d state=%d rawAction=%d selection=%d dialogContext=%d dialogState=%d",
        event.requestId,
        event.eventType,
        event.targetType,
        event.targetId,
        event.objectId,
        event.clientNpcId,
        event.questId,
        event.questState,
        event.rawAction,
        event.selection,
        event.dialogContext,
        event.dialogState);
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

static void CleanupExpiredIgnoredQuestActionLocked() {
    if (!g_ignoreNextOutgoingQuestAction.active) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_ignoreNextOutgoingQuestAction.expiresAt) {
        return;
    }
    Trace("IgnoreNext QUEST_ACTION expired questId=%d npcId=%d rawAction=%d state=%d",
        g_ignoreNextOutgoingQuestAction.questId,
        g_ignoreNextOutgoingQuestAction.npcId,
        g_ignoreNextOutgoingQuestAction.rawAction,
        g_currentDialogState);
    ClearIgnoreNextQuestActionLocked();
    if (g_currentDialogState == kDialogStateSuppressedAfterNativeNpc) {
        ClearDialogStateLocked();
    }
}

static bool MatchesIgnoredQuestActionLocked(int questId, int npcId, int rawAction) {
    if (!g_ignoreNextOutgoingQuestAction.active) {
        return false;
    }
    return g_ignoreNextOutgoingQuestAction.questId == questId
        && g_ignoreNextOutgoingQuestAction.npcId == npcId
        && g_ignoreNextOutgoingQuestAction.rawAction == rawAction;
}

static void StoreIgnoredQuestActionLocked(int questId, int npcId, int rawAction) {
    g_ignoreNextOutgoingQuestAction.active = true;
    g_ignoreNextOutgoingQuestAction.questId = questId;
    g_ignoreNextOutgoingQuestAction.npcId = npcId;
    g_ignoreNextOutgoingQuestAction.rawAction = rawAction;
    g_ignoreNextOutgoingQuestAction.expiresAt = GetTickCount64() + kIgnoreNextQuestActionTimeoutMs;
}

static bool ShouldSkipOutgoingQuestHookForDialog(int questId, int npcId, int rawAction) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    CleanupExpiredIgnoredQuestActionLocked();
    if (MatchesIgnoredQuestActionLocked(questId, npcId, rawAction)) {
        Trace("Outgoing QUEST_ACTION ignored reason=ignore-next questId=%d npcId=%d rawAction=%d",
            questId,
            npcId,
            rawAction);
        ClearIgnoreNextQuestActionLocked();
        ClearExpectedInteractionHookNpcTalkLocked();
        ClearDialogStateLocked();
        return true;
    }
    if (g_currentDialogState == kDialogStateNone) {
        return false;
    }
    if (g_currentDialogState == kDialogStateSuppressedAfterNativeNpc) {
        Trace("Outgoing QUEST_ACTION released reason=suppressed-after-native-npc-mismatch questId=%d npcId=%d rawAction=%d suppressedNpcId=%d",
            questId,
            npcId,
            rawAction,
            g_currentDialogNpcId);
        ClearIgnoreNextQuestActionLocked();
        ClearExpectedInteractionHookNpcTalkLocked();
        ClearDialogStateLocked();
        return false;
    }
    // v1.2.3 fix: only block when hook dialog is open
    if (g_currentDialogContext != kDialogContextInteractionHook) {
        Trace("Outgoing QUEST_ACTION allowed reason=native-npc-dialog questId=%d npcId=%d rawAction=%d context=%d state=%d currentNpcId=%d",
            questId,
            npcId,
            rawAction,
            g_currentDialogContext,
            g_currentDialogState,
            g_currentDialogNpcId);
        return false;
    }
    Trace("Outgoing QUEST_ACTION ignored reason=hook-dialog-open questId=%d npcId=%d rawAction=%d context=%d state=%d currentNpcId=%d",
        questId,
        npcId,
        rawAction,
        g_currentDialogContext,
        g_currentDialogState,
        g_currentDialogNpcId);
    return true;
}

static bool ShouldSkipLocalQuestHookForDialog(int questId, int npcId, int rawAction) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    CleanupExpiredIgnoredQuestActionLocked();
    if (g_currentDialogState == kDialogStateNone) {
        return false;
    }
    if (g_currentDialogState == kDialogStateSuppressedAfterNativeNpc) {
        if (MatchesIgnoredQuestActionLocked(questId, npcId, rawAction)) {
            Trace("Local QUEST_ACTION ignored reason=ignore-next questId=%d npcId=%d rawAction=%d",
                questId,
                npcId,
                rawAction);
            ClearIgnoreNextQuestActionLocked();
            ClearExpectedInteractionHookNpcTalkLocked();
            ClearDialogStateLocked();
            return true;
        }
        Trace("Local QUEST_ACTION released reason=suppressed-after-native-npc-mismatch questId=%d npcId=%d rawAction=%d suppressedNpcId=%d",
            questId,
            npcId,
            rawAction,
            g_currentDialogNpcId);
        ClearIgnoreNextQuestActionLocked();
        ClearExpectedInteractionHookNpcTalkLocked();
        ClearDialogStateLocked();
        return false;
    }
    // v1.2.3 fix: only block when hook dialog is open
    if (g_currentDialogContext != kDialogContextInteractionHook) {
        Trace("Local QUEST_ACTION allowed reason=native-npc-dialog questId=%d npcId=%d rawAction=%d context=%d state=%d currentNpcId=%d",
            questId,
            npcId,
            rawAction,
            g_currentDialogContext,
            g_currentDialogState,
            g_currentDialogNpcId);
        return false;
    }
    Trace("Local QUEST_ACTION ignored reason=hook-dialog-open questId=%d npcId=%d rawAction=%d context=%d state=%d currentNpcId=%d",
        questId,
        npcId,
        rawAction,
        g_currentDialogContext,
        g_currentDialogState,
        g_currentDialogNpcId);
    return true;
}

static bool PrepareLocalQuestHookInterceptLocked(int questId, int npcId, int rawAction) {
    CleanupExpiredIgnoredQuestActionLocked();
    if (MatchesIgnoredQuestActionLocked(questId, npcId, rawAction)) {
        Trace("Local QUEST_ACTION ignored reason=ignore-next questId=%d npcId=%d rawAction=%d",
            questId,
            npcId,
            rawAction);
        ClearIgnoreNextQuestActionLocked();
        ClearExpectedInteractionHookNpcTalkLocked();
        ClearDialogStateLocked();
        return false;
    }
    if (g_currentDialogState == kDialogStateNone) {
        return true;
    }
    if (g_currentDialogState == kDialogStateSuppressedAfterNativeNpc) {
        Trace("Local QUEST_ACTION intercept overrides suppressed-after-native-npc questId=%d npcId=%d rawAction=%d suppressedNpcId=%d",
            questId,
            npcId,
            rawAction,
            g_currentDialogNpcId);
        ClearIgnoreNextQuestActionLocked();
        ClearExpectedInteractionHookNpcTalkLocked();
        ClearDialogStateLocked();
        return true;
    }
    // v1.2.3 fix: only block when hook dialog is open
    if (g_currentDialogContext != kDialogContextInteractionHook) {
        Trace("Local QUEST_ACTION intercept allowed reason=native-npc-dialog questId=%d npcId=%d rawAction=%d context=%d state=%d currentNpcId=%d",
            questId,
            npcId,
            rawAction,
            g_currentDialogContext,
            g_currentDialogState,
            g_currentDialogNpcId);
        return true;
    }
    Trace("Local QUEST_ACTION ignored reason=hook-dialog-open questId=%d npcId=%d rawAction=%d context=%d state=%d currentNpcId=%d",
        questId,
        npcId,
        rawAction,
        g_currentDialogContext,
        g_currentDialogState,
        g_currentDialogNpcId);
    return false;
}

static bool PrepareLocalQuestHookIntercept(int questId, int npcId, int rawAction) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return PrepareLocalQuestHookInterceptLocked(questId, npcId, rawAction);
}

static bool TrySendHookEvent(void* socket, void* edx, COutPacket* packet, HookEvent event, int actionMask) {
    const bool intercept = ShouldIntercept(event, actionMask);
    Trace("ShouldIntercept eventType=%d targetType=%d targetId=%d objectId=%d npcId=%d questId=%d state=%d rawAction=%d actionMask=%d selection=%d dialogContext=%d dialogState=%d rulesLoaded=%d ruleCount=%d intercept=%d",
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
        event.dialogContext,
        event.dialogState,
        RulesLoaded() ? 1 : 0,
        RuleCount(),
        intercept ? 1 : 0);
    if (!intercept) {
        return false;
    }

    event.requestId = g_nextRequestId.fetch_add(1);
    StorePendingResult storeResult = StorePendingPacket(event.requestId, socket, edx, packet, event);
    if (storeResult == StorePendingResult::Duplicate) {
        return true;
    }
    if (storeResult == StorePendingResult::Failed) {
        Trace("StorePendingPacket failed requestId=%d eventType=%d", event.requestId, event.eventType);
        return false;
    }
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
    ClearExpectedInteractionHookNpcTalkForOutgoingClick();
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
        TrackNpcMoreDialogEnd(lastMsg, action, selection);
        Trace("Outgoing NPC_MORE ignored no-selection size=%lu lastMsg=%u action=%u selection=%d", packet->Size, lastMsg, action, selection);
        return false;
    }

    const int npcId = CurrentDialogNpcId();
    const int dialogContext = CurrentDialogContext();
    const int dialogState = CurrentDialogState();
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
    event.dialogContext = dialogContext;
    event.dialogState = dialogState;
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
    if (ShouldSkipOutgoingQuestHookForDialog(questId, npcId, static_cast<int>(nativeAction))) {
        return true;
    }

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
    event.dialogContext = kDialogContextQuest;
    event.dialogState = kDialogStateNone;
    return TrySendHookEvent(socket, edx, packet, event, actionMask);
}

static bool TryReadLocalQuestAction(void* thisPtr, int& questId, int& npcId, int& nativeAction, int& actionMask, int& questState) {
    if (thisPtr == nullptr) {
        return false;
    }

    int questEntryState = 0;
    __try {
        unsigned char* base = reinterpret_cast<unsigned char*>(thisPtr);
        questId = static_cast<int>(*reinterpret_cast<const unsigned short*>(base + 0x0C));
        npcId = *reinterpret_cast<const int*>(base + 0x10);
        questEntryState = *reinterpret_cast<const int*>(base + 0x14);

        // Check progress conditions and override entry state for NPC icon
        if (questEntryState == 1) {
            bool allMet = false;
            // lock_guard cant be used inside __try, use manual lock/unlock
            g_stateMutex.lock();
            auto it = g_progressConditionsByQuestId.find(questId);
            if (it != g_progressConditionsByQuestId.end() && !it->second.empty()) {
                allMet = true;
                for (const auto& cond : it->second) {
                    if (cond.first < cond.second) {
                        allMet = false;
                        break;
                    }
                }
            }
            g_stateMutex.unlock();
            if (allMet) {
                *reinterpret_cast<int*>(base + 0x14) = 2;
                questEntryState = 2;
                Trace("LocalQuestAction questEntryState override questId=%d 1->2", questId);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        questId = 0;
        npcId = 0;
        nativeAction = 0;
        return false;
    }

    if (questEntryState == 0) {
        nativeAction = 4;
    } else if (questEntryState == 1) {
        nativeAction = 5;
    } else {
        nativeAction = 1;
    }

    if (questId <= 0 || !ParseQuestAction(static_cast<unsigned char>(nativeAction), actionMask, questState)) {
        return false;
    }

    // Overridden entry state (2) -> nativeAction=1 (QUERY_START)
    // ParseQuestAction sets questState to kQuestStateNotStarted for action=1
    // Force questState to STARTED so server questStateMask(STARTED) still matches
    if (questEntryState == 2 && nativeAction == 1) {
        questState = kQuestStateStarted;
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
    event.dialogContext = kDialogContextQuest;
    event.dialogState = kDialogStateNone;

    const bool intercept = ShouldIntercept(event, actionMask);
    Trace("Local QUEST_ACTION this=%p arg=%d action=%d questId=%d npcId=%d actionMask=%d questState=%d dialogContext=%d dialogState=%d rulesLoaded=%d ruleCount=%d intercept=%d",
        thisPtr,
        arg,
        nativeAction,
        questId,
        npcId,
        actionMask,
        questState,
        event.dialogContext,
        event.dialogState,
        RulesLoaded() ? 1 : 0,
        RuleCount(),
        intercept ? 1 : 0);
    if (!intercept) {
        if (ShouldSkipLocalQuestHookForDialog(questId, npcId, nativeAction)) {
            return true;
        }
        return false;
    }
    if (!PrepareLocalQuestHookIntercept(questId, npcId, nativeAction)) {
        return true;
    }

    DWORD socketPtr = 0;
    if (!TryReadDword(kClientSocketPtr, socketPtr) || socketPtr == 0) {
        Trace("Local QUEST_ACTION reject questId=%d reason=no-socket", questId);
        return false;
    }

    event.requestId = g_nextRequestId.fetch_add(1);
    StorePendingResult storeResult = StorePendingLocalQuestAction(event.requestId, thisPtr, arg, event);
    if (storeResult == StorePendingResult::Duplicate) {
        return true;
    }
    if (storeResult == StorePendingResult::Failed) {
        Trace("StorePendingLocalQuestAction failed requestId=%d eventType=%d", event.requestId, event.eventType);
        return false;
    }
    SendInteractionHookEvent(reinterpret_cast<void*>(socketPtr), nullptr, event);
    return true;
}

static bool IsKnownResultCode(int resultCode) {
    return resultCode == kResultHandledDialog
        || resultCode == kResultHandledUpdate
        || resultCode == kResultFallbackOriginal
        || resultCode == kResultRejected
        || resultCode == kResultError;
}

static bool HandleResult(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 12) {
        Trace("HandleResult reject payloadSize=%lu reason=short", payloadSize);
        return false;
    }
    const int version = ReadI32(payload);
    const int requestId = ReadI32(payload + 4);
    const int resultCode = ReadI32(payload + 8);
    if ((version != kVersion && version != kLegacyRulesVersion) || requestId <= 0 || !IsKnownResultCode(resultCode)) {
        Trace("HandleResult reject version=%d requestId=%d result=%d",
            version, requestId, resultCode);
        return false;
    }
    Trace("HandleResult requestId=%d result=%d", requestId, resultCode);
    if (resultCode == kResultFallbackOriginal) {
        if (!ReplayPendingPacket(requestId)) {
            if (!ReplayPendingLocalQuestAction(requestId)) {
                DropPendingPacket(requestId);
            }
        }
        return true;
    }
    DropPendingPacket(requestId, resultCode == kResultHandledDialog || resultCode == kResultHandledUpdate);
    return true;
}

static bool ApplyClientRuntimeConfig(const unsigned char* payload, unsigned long payloadSize) {
    if (payload == nullptr || payloadSize < 8) {
        Trace("ApplyClientRuntimeConfig reject payloadSize=%lu reason=short", payloadSize);
        return false;
    }

    const int version = ReadI32(payload);
    const int enableAutoKeyDownFix = ReadI32(payload + 4);
    if (version != kVersion || (enableAutoKeyDownFix != 0 && enableAutoKeyDownFix != 1)) {
        Trace("ApplyClientRuntimeConfig reject version=%d enableAutoKeyDownFix=%d",
            version,
            enableAutoKeyDownFix);
        return false;
    }

    g_autoKeyDownFixEnabled.store(enableAutoKeyDownFix != 0);
    Trace("ApplyClientRuntimeConfig ok enableAutoKeyDownFix=%d", enableAutoKeyDownFix);
    return true;
}

static bool ApplyProgress(const unsigned char* payload, unsigned long payloadSize) {
    constexpr int kMaxProgressEntries = 1000;
    if (payload == nullptr || payloadSize < 8) {
        Trace("ApplyProgress reject payloadSize=%lu reason=short", payloadSize);
        return false;
    }

    const int version = ReadI32(payload);
    const int count = ReadI32(payload + 4);
    if (version != kVersion || count < 0 || count > kMaxProgressEntries) {
        Trace("ApplyProgress reject version=%d count=%d", version, count);
        return false;
    }

    const unsigned char* cursor = payload + 8;
    const unsigned char* end = payload + payloadSize;
    std::unordered_map<int, std::string> nextText;
    std::unordered_map<int, std::vector<std::pair<int, int>>> nextConditions;
    std::unordered_map<int, int> nextQuestState;
    for (int i = 0; i < count; ++i) {
        if (cursor + 12 > end) {
            Trace("ApplyProgress reject count=%d index=%d reason=entry-short", count, i);
            return false;
        }
        const int questId = ReadI32(cursor);
        cursor += 4;
        const int state = ReadI32(cursor);
        cursor += 4;
        const int conditionCount = ReadI32(cursor);
        cursor += 4;
        if (conditionCount < 0 || conditionCount > 20) {
            Trace("ApplyProgress reject questId=%d conditionCount=%d reason=bad-cond-count", questId, conditionCount);
            return false;
        }

        std::string combinedText;
        std::vector<std::pair<int, int>> conditionValues;
        for (int ci = 0; ci < conditionCount; ++ci) {
            if (cursor + 8 > end) {
                Trace("ApplyProgress reject questId=%d cond=%d reason=cond-short", questId, ci);
                return false;
            }
            const int current = ReadI32(cursor);
            cursor += 4;
            const int required = ReadI32(cursor);
            cursor += 4;

            conditionValues.push_back(std::make_pair(current, required));

            std::string text;
            if (!ReadPacketString(cursor, end, text)) {
                Trace("ApplyProgress reject questId=%d cond=%d reason=text", questId, ci);
                return false;
            }
            if (ci > 0) {
                combinedText += "\r\n";
            }
            combinedText += text;
            Trace("ApplyProgress entry questId=%d cond=%d state=%d current=%d required=%d text=%s",
                questId, ci, state, current, required, text.c_str());
        }
        if (questId <= 0) {
            Trace("ApplyProgress reject questId=%d index=%d reason=quest", questId, i);
            return false;
        }
        nextText[questId] = combinedText;
        nextConditions[questId] = conditionValues;
        nextQuestState[questId] = state;
    }
    if (cursor != end) {
        Trace("ApplyProgress warning count=%d reason=trailing-bytes bytes=%lu",
            count,
            static_cast<unsigned long>(end - cursor));
    }

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_progressTextByQuestId.swap(nextText);
        g_progressConditionsByQuestId.swap(nextConditions);
        g_progressQuestState.swap(nextQuestState);
    }
    Trace("ApplyProgress ok count=%d", count);
    return true;
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
        return ApplyRules(payload, payloadSize) ? IncomingResult::Consumed : IncomingResult::None;
    }
    if (opcode == kS2CInteractionHookResult) {
        Trace("Incoming hook result opcode offset=%lu payloadSize=%lu", headerOffset, payloadSize);
        return HandleResult(payload, payloadSize) ? IncomingResult::Consumed : IncomingResult::None;
    }
    if (opcode == kS2CInteractionHookProgress) {
        Trace("Incoming hook progress opcode offset=%lu payloadSize=%lu", headerOffset, payloadSize);
        return ApplyProgress(payload, payloadSize) ? IncomingResult::Consumed : IncomingResult::None;
    }
    if (opcode == kS2CClientRuntimeConfig) {
        Trace("Incoming client runtime config opcode offset=%lu payloadSize=%lu", headerOffset, payloadSize);
        return ApplyClientRuntimeConfig(payload, payloadSize) ? IncomingResult::Consumed : IncomingResult::None;
    }

    TrackIncomingPacket(opcode, payload, payloadSize);
    return IncomingResult::None;
}

static bool HandleIncoming(CInPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr) {
        return false;
    }

    IncomingResult result = HandleIncomingAtOffset(packet, 4);
    return result == IncomingResult::Consumed;
}

static bool TryInterceptOutgoing(void* socket, void* edx, COutPacket* packet) {
    TraceOutgoingAttackPacket(packet);

    if (TryInterceptNpcTalk(socket, edx, packet)) {
        return true;
    }
    if (TryInterceptNpcTalkMore(socket, edx, packet)) {
        return true;
    }
    return TryInterceptQuestAction(socket, edx, packet);
}

static bool GetKillProgressTooltipTextImpl(std::string& outText) {
    std::lock_guard<std::mutex> lock(g_stateMutex);

    for (const auto& entry : g_progressTextByQuestId) {
        const int questId = entry.first;
        const std::string& text = entry.second;

        // Life proof quest block: 5100..5974
        if (questId < 5100 || questId > 5974) continue;

        // OPTION_SLOT fixed slots: offset 12/13/14 within 25-slot block
        const int offset = (questId - 5100) % 25;
        if (offset < 12 || offset > 14) continue;

        // Must be in STARTED state
        auto stateIt = g_progressQuestState.find(questId);
        if (stateIt == g_progressQuestState.end() || stateIt->second != 1) continue;

        // Must have non-zero current progress, not yet completed
        auto condIt = g_progressConditionsByQuestId.find(questId);
        if (condIt == g_progressConditionsByQuestId.end() || condIt->second.empty()) continue;
        const int current = condIt->second[0].first;
        const int required = condIt->second[0].second;
        if (current <= 0 || current >= required) continue;

        // Strip WZ macros: all patterns like #b, #k, #r, #e, #n, #i, #t, #o, #p, #c etc.
        std::string displayText = text;
        size_t pos = 0;
        while (pos < displayText.size()) {
            if (displayText[pos] == '#' && pos + 1 < displayText.size()
                    && std::isalpha(static_cast<unsigned char>(displayText[pos + 1]))) {
                size_t skip = 2;
                if (pos + 2 < displayText.size()
                        && std::isalpha(static_cast<unsigned char>(displayText[pos + 2]))) {
                    skip = 3;
                }
                displayText.erase(pos, skip);
            } else {
                ++pos;
            }
        }

        outText = displayText;
        return true;
    }
    return false;
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

bool ReplaceQuestHookProgressMarkers(const char* input, std::string& output) {
    bool replaced = false;
    __try {
        replaced = ReplaceProgressMarkers(input, output);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Trace("ReplaceQuestHookProgressMarkers exception");
        output.clear();
        replaced = false;
    }
    return replaced;
}

bool GetKillProgressTooltipText(std::string& outText) {
    bool result = false;
    __try {
        result = GetKillProgressTooltipTextImpl(outText);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outText.clear();
        result = false;
    }
    return result;
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

static int __fastcall GenerateAutoKeyDown_Hook(void* pThis, void* edx, ISMSG* message) {
    unsigned int lParamBefore = 0;
    unsigned int lParamAfter = 0;
    bool normalized = false;
    if (message != nullptr
            && (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)) {
        const unsigned int before = static_cast<unsigned int>(message->lParam);
        lParamBefore = before;
        const unsigned int repeatCount = before & kKeyRepeatCountMask;
        // 只在长按重复（repeatCount>0）时归一化，首次按键不动
        if (repeatCount > 0 && g_autoKeyDownFixEnabled.load()) {
            // 保留 scan code(16-23)、extended(24)、context(29)、prev state(30)
            // 清零 repeat count(0-15) 和 transition(31)，设 repeat=1
            constexpr unsigned int kKeepMask = ~(kKeyRepeatCountMask | kTransitionStateMask);
            lParamAfter = (before & kKeepMask) | 1;
            message->lParam = static_cast<int>(lParamAfter);
            normalized = true;
        } else {
            lParamAfter = before;
        }
    }

    const int result = g_GenerateAutoKeyDown(pThis, edx, message);

    if (result == 0 || message == nullptr) {
        return result;
    }

    if (!Client::debug) {
        return result;
    }

    const std::string keyName = VirtualKeyName(message->wParam);
    const std::string heldKeys = GetHeldKeys();
    Trace("AutoKeyDown result=%d enabled=%d normalized=%d this=%p message=0x%04X wParam=%u key=%s lParamBefore=0x%08X lParamAfter=0x%08X tick=%lu heldKeys=%s",
        result,
        g_autoKeyDownFixEnabled.load() ? 1 : 0,
        normalized ? 1 : 0,
        pThis,
        message->message,
        message->wParam,
        keyName.c_str(),
        lParamBefore,
        lParamAfter,
        GetTickCount(),
        heldKeys.c_str());
    return result;
}

void HookQuestActionClick(bool enable) {
    const bool ok = Memory::SetHook(enable, reinterpret_cast<void**>(&g_QuestActionClick), QuestActionClick_Hook);
    Trace("QuestActionClick hook enable=%d ok=%d addr=0x%08X", enable ? 1 : 0, ok ? 1 : 0, kQuestActionClickAddr);
}

void HookInputAutoKeyDownFix(bool enable) {
    const bool ok = Memory::SetHook(enable, reinterpret_cast<void**>(&g_GenerateAutoKeyDown), GenerateAutoKeyDown_Hook);
    Trace("InputAutoKeyDown fix hook enable=%d ok=%d runtimeEnabled=%d addr=0x%08X",
        enable ? 1 : 0,
        ok ? 1 : 0,
        g_autoKeyDownFixEnabled.load() ? 1 : 0,
        kGenerateAutoKeyDownAddr);
}

void InstallQuestDiagnostics() {
    if (g_exceptionHandler == nullptr) {
        g_exceptionHandler = AddVectoredExceptionHandler(1, QuestDiagnosticsExceptionHandler);
        Trace("Quest diagnostics exception handler installed ok=%d handler=%p",
            g_exceptionHandler != nullptr ? 1 : 0,
            g_exceptionHandler);
    }

    bool expected = false;
    if (g_keyTraceRunning.compare_exchange_strong(expected, true)) {
        g_keyTraceThread = CreateThread(nullptr, 0, QuestKeyTraceThreadProc, nullptr, 0, nullptr);
        Trace("Quest diagnostics key trace create ok=%d thread=%p",
            g_keyTraceThread != nullptr ? 1 : 0,
            g_keyTraceThread);
    }
}

void QuestHookTrace(const char* format, ...) {
    va_list args;
    va_start(args, format);
    TraceV(format, args);
    va_end(args);
}
