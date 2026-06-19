#include "stdafx.h"
#include "QuestHook.h"

#include <atomic>
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

constexpr int kVersion = 3;
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

constexpr int kActionMaskAny = 0;
constexpr int kActionMaskQueryStart = 1;
constexpr int kActionMaskQueryProgress = 1 << 2;
constexpr int kActionMaskQueryComplete = 1 << 3;

constexpr int kDialogContextNone = 0;
constexpr int kDialogContextNpc = 1;
constexpr int kDialogStateNone = 0;
constexpr int kDialogStateOpen = 1;

constexpr int kResultFallbackOriginal = 2;

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

using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, COutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);

static std::mutex g_stateMutex;
static std::vector<InteractionHookRule> g_rules;
static std::unordered_map<int, PendingPacket> g_pendingPackets;
static std::unordered_map<int, int> g_npcIdByObjectId;
static bool g_rulesLoaded = false;
static int g_currentDialogNpcId = 0;
static int g_currentDialogState = kDialogStateNone;
static std::atomic<int> g_nextRequestId{ 1 };

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
    if (rule.actionMask != kActionMaskAny && (rule.actionMask & actionMask) == 0) {
        return false;
    }
    if (rule.selectionId != kAnyId && event.selection >= 0 && rule.selectionId != event.selection) {
        return false;
    }
    return true;
}

static bool HasEventRuleLocked(int eventType) {
    const int mask = EventMask(eventType);
    for (const InteractionHookRule& rule : g_rules) {
        if ((rule.eventMask & mask) != 0) {
            return true;
        }
    }
    return false;
}

static bool ShouldIntercept(const HookEvent& event, int actionMask) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_rulesLoaded) {
        return false;
    }
    for (const InteractionHookRule& rule : g_rules) {
        if (RuleMatches(rule, event, actionMask)) {
            return true;
        }
    }

    // If the object->NPC mapping is unavailable, let the server validate NPC clicks.
    return event.eventType == kEventNpcClick && event.clientNpcId <= 0 && HasEventRuleLocked(kEventNpcClick);
}

static bool ApplyRules(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 10) {
        return false;
    }
    const unsigned short clientSubCommand = ReadU16(payload);
    const int version = ReadI32(payload + 2);
    const int count = ReadI32(payload + 6);
    if (clientSubCommand != kCustomInteractionHookEvent || version != kVersion || count < 0 || count > 20000) {
        return false;
    }

    const unsigned long rulesBytes = static_cast<unsigned long>(count) * 28UL;
    if (payloadSize < 10UL + rulesBytes) {
        return false;
    }

    std::vector<InteractionHookRule> next;
    next.reserve(static_cast<std::size_t>(count));
    const unsigned char* cursor = payload + 10;
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
            next.push_back(rule);
        }
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_rules.swap(next);
    g_pendingPackets.clear();
    g_currentDialogNpcId = 0;
    g_currentDialogState = kDialogStateNone;
    g_rulesLoaded = true;
    return true;
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

static void ReplayPendingPacket(int requestId) {
    PendingPacket pending{};
    if (!TakePendingPacket(requestId, pending) || pending.bytes.empty()) {
        return;
    }

    COutPacket out{};
    out.Loopback = 0;
    out.Data = pending.bytes.data();
    out.Size = static_cast<unsigned long>(pending.bytes.size());
    out.Offset = 0;
    out.EncryptedByShanda = 0;
    g_SendPacket(pending.socket, pending.edx, &out);
}

static void DropPendingPacket(int requestId) {
    PendingPacket ignored{};
    TakePendingPacket(requestId, ignored);
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
    if (!ShouldIntercept(event, actionMask)) {
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
        return false;
    }

    int selection = kAnyId;
    if (packet->Size >= 8) {
        selection = ReadI32(data + 4);
    } else if (packet->Size > 4) {
        selection = static_cast<int>(data[4]);
    }
    if (selection < 0) {
        return false;
    }

    const int npcId = CurrentDialogNpcId();
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
        return false;
    }

    int npcId = 0;
    if (packet->Size >= 9) {
        npcId = ReadI32(data + 5);
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
    event.dialogContext = kDialogContextNone;
    event.dialogState = kDialogStateNone;
    return TrySendHookEvent(socket, edx, packet, event, actionMask);
}

static void HandleResult(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 12) {
        return;
    }
    const int version = ReadI32(payload);
    const int requestId = ReadI32(payload + 4);
    const int resultCode = ReadI32(payload + 8);
    if (version != kVersion || requestId <= 0) {
        return;
    }
    if (resultCode == kResultFallbackOriginal) {
        ReplayPendingPacket(requestId);
        return;
    }
    DropPendingPacket(requestId);
}

static IncomingResult HandleIncomingAtOffset(CInPacket* packet, unsigned long headerOffset) {
    const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
    if (packet->Size < headerOffset + 2) {
        return IncomingResult::None;
    }

    const unsigned short opcode = ReadU16(data + headerOffset);
    const unsigned char* payload = data + headerOffset + 2;
    const unsigned long payloadSize = packet->Size - headerOffset - 2;

    if (opcode == kS2CInteractionHookRules) {
        ApplyRules(payload, payloadSize);
        return IncomingResult::Consumed;
    }
    if (opcode == kS2CInteractionHookResult) {
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
        handled = false;
    }
    return handled;
}
