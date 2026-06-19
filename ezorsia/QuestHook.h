#pragma once

bool HandleQuestHookIncoming(void* packet);
bool TryHandleQuestHookSend(void* socket, void* edx, void* packet);
