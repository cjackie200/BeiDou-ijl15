#pragma once

#include <string>

bool HandleQuestHookIncoming(void* packet);
bool TryHandleQuestHookSend(void* socket, void* edx, void* packet);
bool ReplaceQuestHookProgressMarkers(const char* input, std::string& output);
bool GetKillProgressTooltipText(std::string& outText);
void InstallQuestDiagnostics();
void HookQuestActionClick(bool enable);
void HookInputAutoKeyDownFix(bool enable);
void QuestHookTrace(const char* format, ...);
