#include "stdafx.h"
#include "ElementalTooltip.h"
#include "Client.h"
#include "QuestHook.h"
#include "MapleClientCollectionTypes/ZXString.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace {

constexpr DWORD kHeightHookAddr = 0x008EEEAF;
constexpr DWORD kHeightHookRetn = 0x008EEEB4;
constexpr DWORD kDrawHookAddr = 0x008EF2A4;
constexpr DWORD kDrawHookRetn = 0x008EF2A9;
constexpr DWORD kEquipTooltipCandidateAddr = 0x008F2276;
constexpr DWORD kEquipTooltipCandidateRetn = 0x008F227B;
constexpr DWORD kEquipTooltipDetailAddr = 0x008EDFC7;
constexpr DWORD kEquipTooltipDetailRetn = 0x008EDFCC;
constexpr DWORD kMakeLayerAddr = 0x008E6E52;
constexpr DWORD kEquipMakeLayerCallAddr = 0x008E8E43;
constexpr DWORD kEquipMakeLayerCallRetn = 0x008E8E48;
constexpr DWORD kEquipDrawStatsCallAddr = 0x008E8E67;
constexpr DWORD kEquipDrawStatsCallRetn = 0x008E8E6C;
constexpr DWORD kEquipDrawStatsProc = 0x008ECA0C;
constexpr DWORD kEquipDrawStatLineProc = 0x008E7836;
constexpr DWORD kEquipMagicAttackStatCallAddr = 0x008ECCC2;
constexpr DWORD kEquipMagicAttackStatCallRetn = 0x008ECCC7;
constexpr DWORD kReadSecIntAddr = 0x0042873D;
constexpr DWORD kTooltipDrawTextLineA = 0x008F466D;
constexpr DWORD kTooltipDrawTextLineB = 0x008F4818;
constexpr DWORD kTooltipDrawTextLineC = 0x008F49BC;
constexpr DWORD kEquipTooltipFrameProc = 0x00AD55C8;
constexpr DWORD kEquipTooltipDetailFrameProc = 0x00AD4F8F;

constexpr int kElementTooltipRows = 2;
constexpr int kElementTooltipLineHeight = 0x10;
constexpr int kElementTooltipExtraHeight = 0x10;
constexpr int kTooltipWidth = 0x122;
constexpr int kPercentStatLineMode = 2;

using TooltipDrawTextLine_t = void(__fastcall*)(void* pThis, void* edx, int a1, int a2, int a3);
using EquipDrawStatLine_t = void(__fastcall*)(void* pThis, void* edx, int mode, int value, DWORD label, DWORD formattedValue);
using ReadSecInt_t = int(__fastcall*)(void* pThis, void* edx);
TooltipDrawTextLine_t g_TooltipDrawTextLineA = reinterpret_cast<TooltipDrawTextLine_t>(kTooltipDrawTextLineA);
TooltipDrawTextLine_t g_TooltipDrawTextLineB = reinterpret_cast<TooltipDrawTextLine_t>(kTooltipDrawTextLineB);
TooltipDrawTextLine_t g_TooltipDrawTextLineC = reinterpret_cast<TooltipDrawTextLine_t>(kTooltipDrawTextLineC);
EquipDrawStatLine_t g_EquipDrawStatLine = reinterpret_cast<EquipDrawStatLine_t>(kEquipDrawStatLineProc);
ReadSecInt_t g_ReadSecInt = reinterpret_cast<ReadSecInt_t>(kReadSecIntAddr);

struct ElementTooltipInfo {
    const char* prefix;
    int bonus;
    int mismatch;
};

static const char kFirePrefix[] = "\xBB\xF0\xCA\xF4\xD0\xD4\xA3\xBA";
static const char kPoisonPrefix[] = "\xB6\xBE\xCA\xF4\xD0\xD4\xA3\xBA";
static const char kIcePrefix[] = "\xB1\xF9\xCA\xF4\xD0\xD4\xA3\xBA";
static const char kLightningPrefix[] = "\xC0\xD7\xCA\xF4\xD0\xD4\xA3\xBA";
static const char kHolyPrefix[] = "\xCA\xA5\xCA\xF4\xD0\xD4\xA3\xBA";
static const char kMismatchPrefix[] = "\xD2\xEC\xCA\xF4\xD0\xD4\xA3\xBA";

int g_debugLogCount = 0;
int g_probeLogCount = 0;
int g_heightTraceCount = 0;
int g_drawTraceCount = 0;
int g_argsTraceCount = 0;
int g_textLineTraceCount = 0;
int g_textLineHookTraceCount = 0;
int g_candidateTraceCount = 0;
int g_detailTraceCount = 0;
int g_equipTraceCount = 0;
int g_textFaultTraceCount = 0;

void TooltipTrace(const char* format, ...) {
    char logPath[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, logPath, MAX_PATH) == 0) {
        strcpy_s(logPath, "elemental-tooltip.log");
    } else {
        char* slash = strrchr(logPath, '\\');
        if (slash == nullptr) {
            strcpy_s(logPath, "elemental-tooltip.log");
        } else {
            strcpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - logPath), "elemental-tooltip.log");
        }
    }

    FILE* file = nullptr;
    if (fopen_s(&file, logPath, "ab") != 0 || file == nullptr) {
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

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);

    std::fprintf(file, "\r\n");
    std::fclose(file);
}

void SafeCStringPreview(int value, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    buffer[0] = '\0';

    const char* text = reinterpret_cast<const char*>(value);
    if (text == nullptr || reinterpret_cast<unsigned int>(text) < 0x10000) {
        sprintf_s(buffer, bufferSize, "<null>");
        return;
    }

    __try {
        size_t i = 0;
        for (; i + 1 < bufferSize && i < 48; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == '\0') {
                break;
            }
            if (c < 0x20 || c > 0x7E) {
                buffer[i] = '.';
                continue;
            }
            buffer[i] = static_cast<char>(c);
        }
        buffer[i] = '\0';
        if (i == 0) {
            sprintf_s(buffer, bufferSize, "<empty>");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sprintf_s(buffer, bufferSize, "<fault>");
    }
}

void TraceTextLineCall(const char* name, void* tooltip, int a1, int a2, int a3) {
    if (g_textLineHookTraceCount >= 1200) {
        return;
    }

    char a1Text[64] = {};
    char a2Text[64] = {};
    char a3Text[64] = {};
    SafeCStringPreview(a1, a1Text, sizeof(a1Text));
    SafeCStringPreview(a2, a2Text, sizeof(a2Text));
    SafeCStringPreview(a3, a3Text, sizeof(a3Text));
    TooltipTrace("%s tooltip=%p a1=%d/0x%08X s1=%s a2=%d/0x%08X s2=%s a3=%d/0x%08X s3=%s",
        name,
        tooltip,
        a1,
        static_cast<unsigned int>(a1),
        a1Text,
        a2,
        static_cast<unsigned int>(a2),
        a2Text,
        a3,
        static_cast<unsigned int>(a3),
        a3Text);
    ++g_textLineHookTraceCount;
}

bool TryGetElementTooltipInfo(int itemId, ElementTooltipInfo& info) {
    const char* prefix = nullptr;
    int bonus = 0;
    int mismatch = 0;

    switch (itemId) {
    case 1372035: prefix = kFirePrefix; bonus = 140; mismatch = 90; break;
    case 1372036: prefix = kPoisonPrefix; bonus = 140; mismatch = 90; break;
    case 1372037: prefix = kIcePrefix; bonus = 140; mismatch = 90; break;
    case 1372038: prefix = kLightningPrefix; bonus = 140; mismatch = 90; break;
    case 1372047: prefix = kHolyPrefix; bonus = 140; mismatch = 90; break;

    case 1382045: prefix = kFirePrefix; bonus = 155; mismatch = 80; break;
    case 1382046: prefix = kPoisonPrefix; bonus = 155; mismatch = 80; break;
    case 1382047: prefix = kIcePrefix; bonus = 155; mismatch = 80; break;
    case 1382048: prefix = kLightningPrefix; bonus = 155; mismatch = 80; break;
    case 1382061: prefix = kHolyPrefix; bonus = 155; mismatch = 80; break;

    case 1372039: prefix = kFirePrefix; bonus = 170; mismatch = 70; break;
    case 1372040: prefix = kPoisonPrefix; bonus = 170; mismatch = 70; break;
    case 1372041: prefix = kIcePrefix; bonus = 170; mismatch = 70; break;
    case 1372042: prefix = kLightningPrefix; bonus = 170; mismatch = 70; break;
    case 1372048: prefix = kHolyPrefix; bonus = 170; mismatch = 70; break;

    case 1382049: prefix = kFirePrefix; bonus = 185; mismatch = 60; break;
    case 1382050: prefix = kPoisonPrefix; bonus = 185; mismatch = 60; break;
    case 1382051: prefix = kIcePrefix; bonus = 185; mismatch = 60; break;
    case 1382052: prefix = kLightningPrefix; bonus = 185; mismatch = 60; break;
    case 1382063: prefix = kHolyPrefix; bonus = 185; mismatch = 60; break;

    case 1372059: prefix = kFirePrefix; bonus = 200; mismatch = 50; break;
    case 1372060: prefix = kPoisonPrefix; bonus = 200; mismatch = 50; break;
    case 1372061: prefix = kIcePrefix; bonus = 200; mismatch = 50; break;
    case 1372062: prefix = kLightningPrefix; bonus = 200; mismatch = 50; break;
    case 1372063: prefix = kHolyPrefix; bonus = 200; mismatch = 50; break;
    default:
        return false;
    }

    info.prefix = prefix;
    info.bonus = bonus;
    info.mismatch = mismatch;
    return true;
}

bool IsElementalWeaponId(DWORD value) {
    ElementTooltipInfo info;
    return TryGetElementTooltipInfo(static_cast<int>(value), info);
}

bool IsMageWeaponId(DWORD value) {
    return value >= 1370000 && value <= 1389999;
}

bool ScanPointerForWeaponId(DWORD pointer, const char*& label, DWORD& offset, DWORD& value);

int ReadItemIdFromEquipPointer(DWORD itemPointer) {
    if (IsElementalWeaponId(itemPointer) || IsMageWeaponId(itemPointer)) {
        return static_cast<int>(itemPointer);
    }

    if (itemPointer < 0x10000 || itemPointer > 0x7FFF0000) {
        return 0;
    }

    __try {
        const int itemId = g_ReadSecInt(reinterpret_cast<void*>(itemPointer + 0x0C), nullptr);
        if (IsElementalWeaponId(static_cast<DWORD>(itemId)) || IsMageWeaponId(static_cast<DWORD>(itemId))) {
            return itemId;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    DWORD offset = 0;
    DWORD value = 0;
    const char* label = nullptr;
    if (ScanPointerForWeaponId(itemPointer, label, offset, value)) {
        return static_cast<int>(value);
    }

    return 0;
}

bool ScanPointerForWeaponId(DWORD pointer, const char*& label, DWORD& offset, DWORD& value) {
    if (pointer < 0x10000 || pointer > 0x7FFF0000) {
        return false;
    }

    __try {
        const DWORD* fields = reinterpret_cast<const DWORD*>(pointer);
        for (DWORD i = 0; i < 0x100; ++i) {
            const DWORD candidate = fields[i];
            if (IsElementalWeaponId(candidate) || IsMageWeaponId(candidate)) {
                offset = i * sizeof(DWORD);
                value = candidate;
                label = IsElementalWeaponId(candidate) ? "elemental" : "mage-range";
                return true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return false;
}

void FormatPointerPreview(DWORD pointer, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    buffer[0] = '\0';

    if (pointer < 0x10000 || pointer > 0x7FFF0000) {
        sprintf_s(buffer, bufferSize, "<not-ptr>");
        return;
    }

    __try {
        const DWORD* fields = reinterpret_cast<const DWORD*>(pointer);
        sprintf_s(buffer,
            bufferSize,
            "%08X %08X %08X %08X %08X %08X %08X %08X",
            fields[0],
            fields[1],
            fields[2],
            fields[3],
            fields[4],
            fields[5],
            fields[6],
            fields[7]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sprintf_s(buffer, bufferSize, "<fault>");
    }
}

extern "C" void __cdecl TraceEquipTooltipCandidate(
    DWORD tooltip,
    DWORD a08,
    DWORD a0c,
    DWORD a10,
    DWORD a14,
    DWORD a18) {
    DWORD foundPointer = 0;
    DWORD foundOffset = 0;
    DWORD foundValue = 0;
    const char* foundLabel = nullptr;
    const char* foundName = nullptr;
    const DWORD values[] = { tooltip, a08, a0c, a10, a14, a18 };
    const char* names[] = { "this", "a08", "a0c", "a10", "a14", "a18" };

    for (int i = 0; i < 6; ++i) {
        if (IsElementalWeaponId(values[i]) || IsMageWeaponId(values[i])) {
            foundLabel = IsElementalWeaponId(values[i]) ? "direct-elemental" : "direct-mage-range";
            foundName = names[i];
            foundValue = values[i];
            break;
        }

        DWORD offset = 0;
        DWORD value = 0;
        const char* label = nullptr;
        if (ScanPointerForWeaponId(values[i], label, offset, value)) {
            foundPointer = values[i];
            foundOffset = offset;
            foundValue = value;
            foundLabel = label;
            foundName = names[i];
            break;
        }
    }

    if (g_candidateTraceCount >= 256) {
        return;
    }

    if (foundLabel == nullptr && g_candidateTraceCount >= 96) {
        return;
    }

    char thisPreview[128] = {};
    char a10Preview[128] = {};
    FormatPointerPreview(tooltip, thisPreview, sizeof(thisPreview));
    FormatPointerPreview(a10, a10Preview, sizeof(a10Preview));

    TooltipTrace("candidate 8F2276 this=0x%08X a08=%u/0x%08X a0c=%u/0x%08X a10=%u/0x%08X a14=%u/0x%08X a18=%u/0x%08X found=%s name=%s ptr=0x%08X off=0x%X val=%u/0x%08X thisFields=%s a10Fields=%s",
        tooltip,
        a08,
        a08,
        a0c,
        a0c,
        a10,
        a10,
        a14,
        a14,
        a18,
        a18,
        foundLabel == nullptr ? "0" : foundLabel,
        foundName == nullptr ? "-" : foundName,
        foundPointer,
        foundOffset,
        foundValue,
        foundValue,
        thisPreview,
        a10Preview);
    ++g_candidateTraceCount;
}

extern "C" void __cdecl TraceEquipTooltipDetail(
    DWORD tooltip,
    DWORD a04,
    DWORD a08,
    DWORD a0c,
    DWORD a10,
    DWORD a14,
    DWORD a18,
    DWORD a1c,
    DWORD a20,
    DWORD a24,
    DWORD a28,
    DWORD a2c) {
    if (g_detailTraceCount >= 320) {
        return;
    }

    DWORD foundPointer = 0;
    DWORD foundOffset = 0;
    DWORD foundValue = 0;
    const char* foundLabel = nullptr;
    const char* foundName = nullptr;
    const DWORD values[] = {
        tooltip, a04, a08, a0c, a10, a14, a18, a1c, a20, a24, a28, a2c
    };
    const char* names[] = {
        "this", "a04", "a08", "a0c", "a10", "a14", "a18", "a1c", "a20", "a24", "a28", "a2c"
    };

    for (int i = 0; i < 12; ++i) {
        if (IsElementalWeaponId(values[i]) || IsMageWeaponId(values[i])) {
            foundLabel = IsElementalWeaponId(values[i]) ? "direct-elemental" : "direct-mage-range";
            foundName = names[i];
            foundValue = values[i];
            break;
        }

        DWORD offset = 0;
        DWORD value = 0;
        const char* label = nullptr;
        if (ScanPointerForWeaponId(values[i], label, offset, value)) {
            foundPointer = values[i];
            foundOffset = offset;
            foundValue = value;
            foundLabel = label;
            foundName = names[i];
            break;
        }
    }

    if (foundLabel == nullptr && g_detailTraceCount >= 128) {
        return;
    }

    char a0cPreview[128] = {};
    char a10Preview[128] = {};
    FormatPointerPreview(a0c, a0cPreview, sizeof(a0cPreview));
    FormatPointerPreview(a10, a10Preview, sizeof(a10Preview));

    TooltipTrace("detail 8EDFC7 this=0x%08X a04=%u/0x%08X a08=%u/0x%08X a0c=%u/0x%08X a10=%u/0x%08X a14=%u/0x%08X a18=%u/0x%08X a1c=%u/0x%08X a20=%u/0x%08X a24=%u/0x%08X a28=%u/0x%08X a2c=%u/0x%08X found=%s name=%s ptr=0x%08X off=0x%X val=%u/0x%08X a0cFields=%s a10Fields=%s",
        tooltip,
        a04,
        a04,
        a08,
        a08,
        a0c,
        a0c,
        a10,
        a10,
        a14,
        a14,
        a18,
        a18,
        a1c,
        a1c,
        a20,
        a20,
        a24,
        a24,
        a28,
        a28,
        a2c,
        a2c,
        foundLabel == nullptr ? "0" : foundLabel,
        foundName == nullptr ? "-" : foundName,
        foundPointer,
        foundOffset,
        foundValue,
        foundValue,
        a0cPreview,
        a10Preview);
    ++g_detailTraceCount;
}

int PickElementalItemId(
    int a08,
    int a0c,
    int a10,
    int a14,
    int a18,
    int a1c,
    int a20,
    int a24) {
    const int candidates[] = { a10, a08, a0c, a14, a18, a1c, a20, a24 };
    ElementTooltipInfo info;
    for (int candidate : candidates) {
        if (TryGetElementTooltipInfo(candidate, info)) {
            return candidate;
        }
    }
    return a10;
}

void TraceTooltipArgs(
    const char* phase,
    int a08,
    int a0c,
    int a10,
    int a14,
    int a18,
    int a1c,
    int a20,
    int a24,
    int picked,
    bool matched) {
    if (g_argsTraceCount >= 128 && !matched) {
        return;
    }

    TooltipTrace("%s args 08=%d 0C=%d 10=%d 14=%d 18=%d 1C=%d 20=%d 24=%d picked=%d matched=%d",
        phase,
        a08,
        a0c,
        a10,
        a14,
        a18,
        a1c,
        a20,
        a24,
        picked,
        matched ? 1 : 0);
    ++g_argsTraceCount;
}

void DrawEquipStatLineSafe(void* tooltip, int mode, int value, DWORD label, const char* labelText) {
    __try {
        g_EquipDrawStatLine(tooltip, nullptr, mode, value, label, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_textFaultTraceCount < 16) {
            TooltipTrace("draw stat fault tooltip=%p mode=%d value=%d label=%s",
                tooltip,
                mode,
                value,
                labelText == nullptr ? "" : labelText);
            ++g_textFaultTraceCount;
        }
    }
}

void DrawEquipStatLine(void* tooltip, const char* labelText, int value) {
    if (tooltip == nullptr || labelText == nullptr || labelText[0] == '\0') {
        return;
    }

    ZXString<char> label(labelText);
    DWORD labelValue = *reinterpret_cast<DWORD*>(&label);
    if (labelValue == 0) {
        return;
    }

    *reinterpret_cast<DWORD*>(&label) = 0;
    DrawEquipStatLineSafe(tooltip, kPercentStatLineMode, value, labelValue, labelText);
}

void __fastcall TooltipDrawTextLineAHook(void* pThis, void* edx, int a1, int a2, int a3) {
    TraceTextLineCall("line466D", pThis, a1, a2, a3);
    g_TooltipDrawTextLineA(pThis, edx, a1, a2, a3);
}

void __fastcall TooltipDrawTextLineBHook(void* pThis, void* edx, int a1, int a2, int a3) {
    TraceTextLineCall("line4818", pThis, a1, a2, a3);
    g_TooltipDrawTextLineB(pThis, edx, a1, a2, a3);
}

void __fastcall TooltipDrawTextLineCHook(void* pThis, void* edx, int a1, int a2, int a3) {
    TraceTextLineCall("line49BC", pThis, a1, a2, a3);
    g_TooltipDrawTextLineC(pThis, edx, a1, a2, a3);
}

extern "C" int __cdecl GetElementalEquipTooltipExtraHeight(DWORD itemPointer) {
    const int itemId = ReadItemIdFromEquipPointer(itemPointer);
    ElementTooltipInfo info;
    if (!TryGetElementTooltipInfo(itemId, info)) {
        return 0;
    }

    if (g_equipTraceCount < 16) {
        TooltipTrace("equip height item=%d ptr=0x%08X extra=%d",
            itemId,
            itemPointer,
            kElementTooltipExtraHeight);
        ++g_equipTraceCount;
    }

    return kElementTooltipExtraHeight;
}

extern "C" void __cdecl DrawElementalEquipTooltipRows(void* tooltip, DWORD itemPointer) {
    const int itemId = ReadItemIdFromEquipPointer(itemPointer);
    ElementTooltipInfo info;
    if (tooltip == nullptr || !TryGetElementTooltipInfo(itemId, info)) {
        return;
    }

    if (g_equipTraceCount < 32) {
        TooltipTrace("equip draw item=%d ptr=0x%08X tooltip=%p bonus=%d mismatch=%d",
            itemId,
            itemPointer,
            tooltip,
            info.bonus,
            info.mismatch);
        ++g_equipTraceCount;
    }

    DrawEquipStatLine(tooltip, info.prefix, info.bonus);
    DrawEquipStatLine(tooltip, kMismatchPrefix, info.mismatch);
}

extern "C" int __cdecl GetElementalTooltipExtraHeight(
    int a08,
    int a0c,
    int a10,
    int a14,
    int a18,
    int a1c,
    int a20,
    int a24) {
    const int itemId = PickElementalItemId(a08, a0c, a10, a14, a18, a1c, a20, a24);
    ElementTooltipInfo info;
    if (!TryGetElementTooltipInfo(itemId, info)) {
        TraceTooltipArgs("height", a08, a0c, a10, a14, a18, a1c, a20, a24, itemId, false);
        if (g_heightTraceCount < 64) {
            TooltipTrace("height item=%d matched=0", itemId);
            ++g_heightTraceCount;
        }
        if (Client::debug && g_probeLogCount < 16) {
            QuestHookTrace("ElementalTooltip probe no-match item=%d", itemId);
            ++g_probeLogCount;
        }
        return 0;
    }

    TraceTooltipArgs("height", a08, a0c, a10, a14, a18, a1c, a20, a24, itemId, true);
    if (g_heightTraceCount < 64) {
        TooltipTrace("height item=%d matched=1 bonus=%d mismatch=%d extra=%d",
            itemId,
            info.bonus,
            info.mismatch,
            kElementTooltipExtraHeight);
        ++g_heightTraceCount;
    }
    return kElementTooltipExtraHeight;
}

extern "C" int __cdecl DrawElementalTooltipRows(
    void* tooltip,
    int y,
    int a08,
    int a0c,
    int a10,
    int a14,
    int a18,
    int a1c,
    int a20,
    int a24) {
    const int itemId = PickElementalItemId(a08, a0c, a10, a14, a18, a1c, a20, a24);
    ElementTooltipInfo info;
    if (tooltip == nullptr || !TryGetElementTooltipInfo(itemId, info)) {
        TraceTooltipArgs("draw", a08, a0c, a10, a14, a18, a1c, a20, a24, itemId, false);
        if (g_drawTraceCount < 64) {
            TooltipTrace("draw item=%d tooltip=%p y=%d matched=0", itemId, tooltip, y);
            ++g_drawTraceCount;
        }
        return y;
    }

    TraceTooltipArgs("draw", a08, a0c, a10, a14, a18, a1c, a20, a24, itemId, true);
    if (g_drawTraceCount < 64) {
        TooltipTrace("draw item=%d tooltip=%p y=%d matched=1 bonus=%d mismatch=%d",
            itemId,
            tooltip,
            y,
            info.bonus,
            info.mismatch);
        ++g_drawTraceCount;
    }

    if (Client::debug && g_debugLogCount < 8) {
        QuestHookTrace("ElementalTooltip draw item=%d y=%d main=%d mismatch=%d", itemId, y, info.bonus, info.mismatch);
        ++g_debugLogCount;
    }

    DrawEquipStatLine(tooltip, info.prefix, info.bonus);
    y += kElementTooltipLineHeight;
    DrawEquipStatLine(tooltip, kMismatchPrefix, info.mismatch);
    y += kElementTooltipLineHeight;

    return y;
}

__declspec(naked) void ElementalTooltipHeightHook() {
    __asm {
        pushad
        push dword ptr[ebp + 0x24]
        push dword ptr[ebp + 0x20]
        push dword ptr[ebp + 0x1C]
        push dword ptr[ebp + 0x18]
        push dword ptr[ebp + 0x14]
        push dword ptr[ebp + 0x10]
        push dword ptr[ebp + 0x0C]
        push dword ptr[ebp + 0x08]
        call GetElementalTooltipExtraHeight
        add esp, 0x20
        test eax, eax
        je done
        add dword ptr[esp + 0x20], eax

    done:
        popad
        push kTooltipWidth
        jmp dword ptr[kHeightHookRetn]
    }
}

__declspec(naked) void ElementalTooltipDrawHook() {
    __asm {
        pushad
        push dword ptr[ebp + 0x24]
        push dword ptr[ebp + 0x20]
        push dword ptr[ebp + 0x1C]
        push dword ptr[ebp + 0x18]
        push dword ptr[ebp + 0x14]
        push dword ptr[ebp + 0x10]
        push dword ptr[ebp + 0x0C]
        push dword ptr[ebp + 0x08]
        push ebx
        push esi
        call DrawElementalTooltipRows
        add esp, 0x28
        mov dword ptr[esp + 0x10], eax
        popad
        mov eax, dword ptr[ebp - 0x54]
        test eax, eax
        jmp dword ptr[kDrawHookRetn]
    }
}

__declspec(naked) void EquipMakeLayerHook() {
    __asm {
        pushad
        push edi
        call GetElementalEquipTooltipExtraHeight
        add esp, 0x04
        test eax, eax
        je done
        add dword ptr[esp + 0x28], eax

    done:
        popad
        mov eax, kMakeLayerAddr
        call eax
        jmp dword ptr[kEquipMakeLayerCallRetn]
    }
}

__declspec(naked) void EquipDrawStatsHook() {
    __asm {
        mov eax, kEquipDrawStatsProc
        call eax
        pushad
        push edi
        push dword ptr[ebp - 0x24]
        call DrawElementalEquipTooltipRows
        add esp, 0x08
        popad
        jmp dword ptr[kEquipDrawStatsCallRetn]
    }
}

__declspec(naked) void EquipMagicAttackStatHook() {
    __asm {
        mov eax, kEquipDrawStatLineProc
        call eax
        pushad
        push edi
        push esi
        call DrawElementalEquipTooltipRows
        add esp, 0x08
        popad
        jmp dword ptr[kEquipMagicAttackStatCallRetn]
    }
}

__declspec(naked) void EquipTooltipCandidateHook() {
    __asm {
        pushad
        mov eax, dword ptr[esp + 0x0C]
        mov edx, dword ptr[esp + 0x18]
        push dword ptr[eax + 0x14]
        push dword ptr[eax + 0x10]
        push dword ptr[eax + 0x0C]
        push dword ptr[eax + 0x08]
        push dword ptr[eax + 0x04]
        push edx
        call TraceEquipTooltipCandidate
        add esp, 0x18
        popad
        mov eax, kEquipTooltipFrameProc
        jmp dword ptr[kEquipTooltipCandidateRetn]
    }
}

__declspec(naked) void EquipTooltipDetailHook() {
    __asm {
        pushad
        mov eax, dword ptr[esp + 0x0C]
        mov edx, dword ptr[esp + 0x18]
        push dword ptr[eax + 0x2C]
        push dword ptr[eax + 0x28]
        push dword ptr[eax + 0x24]
        push dword ptr[eax + 0x20]
        push dword ptr[eax + 0x1C]
        push dword ptr[eax + 0x18]
        push dword ptr[eax + 0x14]
        push dword ptr[eax + 0x10]
        push dword ptr[eax + 0x0C]
        push dword ptr[eax + 0x08]
        push dword ptr[eax + 0x04]
        push edx
        call TraceEquipTooltipDetail
        add esp, 0x30
        popad
        mov eax, kEquipTooltipDetailFrameProc
        jmp dword ptr[kEquipTooltipDetailRetn]
    }
}

} // namespace

namespace ElementalTooltip {

void Install() {
    static bool installed = false;
    if (installed) {
        QuestHookTrace("ElementalTooltip hooks skipped: already installed");
        return;
    }
    installed = true;

    QuestHookTrace("ElementalTooltip hooks installing equipHeight=0x%08X magicStat=0x%08X",
        kEquipMakeLayerCallAddr,
        kEquipMagicAttackStatCallAddr);
    Memory::CodeCave(EquipMakeLayerHook, kEquipMakeLayerCallAddr, 5);
    Memory::CodeCave(EquipMagicAttackStatHook, kEquipMagicAttackStatCallAddr, 5);
    TooltipTrace("equip hooks height=0x%08X bytes=%02X %02X %02X %02X %02X magicStat=0x%08X bytes=%02X %02X %02X %02X %02X",
        kEquipMakeLayerCallAddr,
        *reinterpret_cast<unsigned char*>(kEquipMakeLayerCallAddr),
        *reinterpret_cast<unsigned char*>(kEquipMakeLayerCallAddr + 1),
        *reinterpret_cast<unsigned char*>(kEquipMakeLayerCallAddr + 2),
        *reinterpret_cast<unsigned char*>(kEquipMakeLayerCallAddr + 3),
        *reinterpret_cast<unsigned char*>(kEquipMakeLayerCallAddr + 4),
        kEquipMagicAttackStatCallAddr,
        *reinterpret_cast<unsigned char*>(kEquipMagicAttackStatCallAddr),
        *reinterpret_cast<unsigned char*>(kEquipMagicAttackStatCallAddr + 1),
        *reinterpret_cast<unsigned char*>(kEquipMagicAttackStatCallAddr + 2),
        *reinterpret_cast<unsigned char*>(kEquipMagicAttackStatCallAddr + 3),
        *reinterpret_cast<unsigned char*>(kEquipMagicAttackStatCallAddr + 4));
    QuestHookTrace("ElementalTooltip hooks installed equipHeight=0x%08X magicStat=0x%08X",
        kEquipMakeLayerCallAddr,
        kEquipMagicAttackStatCallAddr);
}

} // namespace ElementalTooltip
