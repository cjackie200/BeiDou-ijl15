#include "stdafx.h"
#include "KillProgressBar.h"
#include "QuestHook.h"
#include "Memory.h"

#include <string>

// Game addresses (same as BossHP)
const DWORD dwCField__Init_KP    = 0x00528DBC; // CField::CField
const DWORD dwCField__Dispose_KP = 0x00529035; // CField::~CField
const DWORD dwCUIToolTip__SetToolTip_String_KP = 0x008E6E7D;
const DWORD dwCUIToolTip__ClearToolTip_KP      = 0x008E6E23;
const DWORD dwCUIToolTip__DisposeToolTip_KP    = 0x008E6BA3;
const DWORD dwCUIToolTip__CreateToolTip_KP     = 0x008E49B5;
const DWORD dwCUserLocal__Update_KP = 0x0094A144;
const DWORD dwViewPortWidth_KP = 0x009DFE68;

const int KILL_PROGRESS_Y = 72;

char KillProgressBar::aKillProgressToolTip[1304];
bool KillProgressBar::bKillProgressVisible = false;

// --- ReadInt helper (same pattern as BossHP) ---

static int ReadInt(DWORD address) {
    DWORD oldProtect;
    VirtualProtect(reinterpret_cast<LPVOID>(address), 4, PAGE_EXECUTE_READWRITE, &oldProtect);
    int value = *reinterpret_cast<int*>(address);
    VirtualProtect(reinterpret_cast<LPVOID>(address), 4, oldProtect, &oldProtect);
    return value;
}

// --- CUIToolTip wrappers (same calling convention as BossHP) ---

typedef void(__fastcall* UIToolTip__SetToolTip_String_Type)(int pThis, void* edx, int x, int y, const char* sToolTip);
static auto _UIToolTip__SetToolTip_String = reinterpret_cast<UIToolTip__SetToolTip_String_Type>(dwCUIToolTip__SetToolTip_String_KP);

void KillProgressBar::SetToolTip_String(int instance, int x, int y, const char* sToolTip) {
    _UIToolTip__SetToolTip_String(instance, 0, x, y, sToolTip);
}

typedef void(__fastcall* UIToolTip__ClearToolTip_Type)(int pThis, void* edx);
static auto _UIToolTip__ClearToolTip = reinterpret_cast<UIToolTip__ClearToolTip_Type>(dwCUIToolTip__ClearToolTip_KP);

void KillProgressBar::ClearToolTip(int instance) {
    _UIToolTip__ClearToolTip(instance, 0);
}

typedef void(__fastcall* UIToolTip__DisposeToolTip_Type)(int pThis, void* edx);
static auto _UIToolTip__DisposeToolTip = reinterpret_cast<UIToolTip__DisposeToolTip_Type>(dwCUIToolTip__DisposeToolTip_KP);

void KillProgressBar::DisposeToolTip(int instance) {
    _UIToolTip__DisposeToolTip(instance, 0);
}

typedef void(__fastcall* UIToolTip__CreateToolTip_Type)(int pThis, void* edx);
static auto _UIToolTip__CreateToolTip = reinterpret_cast<UIToolTip__CreateToolTip_Type>(dwCUIToolTip__CreateToolTip_KP);

void KillProgressBar::CreateToolTip(int instance) {
    _UIToolTip__CreateToolTip(instance, 0);
}

// --- Viewport accessor ---

int KillProgressBar::GetViewPortWidth() {
    return ReadInt(dwViewPortWidth_KP);
}

// --- Main rendering ---

void KillProgressBar::DrawKillProgressIfNeeded() {
    std::string progressText;
    if (GetKillProgressTooltipText(progressText)) {
        SetToolTip_String(
            reinterpret_cast<int>(&aKillProgressToolTip),
            GetViewPortWidth() / 2 - 100,
            KILL_PROGRESS_Y,
            progressText.c_str());
        bKillProgressVisible = true;
    } else if (bKillProgressVisible) {
        ClearToolTip(reinterpret_cast<int>(&aKillProgressToolTip));
        bKillProgressVisible = false;
    }
}

// --- Hooks (independent from BossHP, using own static function pointers) ---

void KillProgressBar::HookUpdate() {
    typedef void(__fastcall* UserLocal__Update_type)(void* pThis, void* edx);
    static auto _UserLocal__Update = reinterpret_cast<UserLocal__Update_type>(dwCUserLocal__Update_KP);

    UserLocal__Update_type Hook = [](void* pThis, void* edx) -> void {
        _UserLocal__Update(pThis, edx);
        DrawKillProgressIfNeeded();
    };

    Memory::SetHook(true, reinterpret_cast<void**>(&_UserLocal__Update), Hook);
}

void KillProgressBar::HookInitField() {
    typedef void(__fastcall* Field__Init_Type)(void* pThis, void* edx);
    static auto _Field__Init = reinterpret_cast<Field__Init_Type>(dwCField__Init_KP);

    Field__Init_Type Hook = [](void* pThis, void* edx) -> void {
        ClearToolTip(reinterpret_cast<int>(&aKillProgressToolTip));
        DisposeToolTip(reinterpret_cast<int>(&aKillProgressToolTip));
        CreateToolTip(reinterpret_cast<int>(&aKillProgressToolTip));
        bKillProgressVisible = false;
        _Field__Init(pThis, edx);
    };

    Memory::SetHook(true, reinterpret_cast<void**>(&_Field__Init), Hook);
}

void KillProgressBar::HookDisposeField() {
    typedef void(__fastcall* Field__Dispose_Type)(void* pThis, void* edx);
    static auto _Field__Dispose = reinterpret_cast<Field__Dispose_Type>(dwCField__Dispose_KP);

    Field__Dispose_Type Hook = [](void* pThis, void* edx) -> void {
        ClearToolTip(reinterpret_cast<int>(&aKillProgressToolTip));
        bKillProgressVisible = false;
        _Field__Dispose(pThis, edx);
    };

    Memory::SetHook(true, reinterpret_cast<void**>(&_Field__Dispose), Hook);
}

// --- Public entry point ---

void KillProgressBar::Hook() {
    HookUpdate();
    HookInitField();
    HookDisposeField();
}
