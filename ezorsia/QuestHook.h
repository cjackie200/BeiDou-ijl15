#pragma once

bool HandleQuestHookIncoming(void* packet);
bool TryHandleQuestHookSend(void* socket, void* edx, void* packet);
void HookQuestActionClick(bool enable);
void QuestHookTrace(const char* format, ...);
