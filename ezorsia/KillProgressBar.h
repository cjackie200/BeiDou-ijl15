#pragma once

class KillProgressBar
{
public:
    static void Hook();
private:
    static char aKillProgressToolTip[1304];
    static bool bKillProgressVisible;

    static void HookUpdate();
    static void HookInitField();
    static void HookDisposeField();

    static void SetToolTip_String(int instance, int x, int y, const char* sToolTip);
    static void ClearToolTip(int instance);
    static void DisposeToolTip(int instance);
    static void CreateToolTip(int instance);

    static void DrawKillProgressIfNeeded();
    static int GetViewPortWidth();
};
