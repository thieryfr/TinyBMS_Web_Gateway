
#pragma once
#include "tinybms_victron_bridge.h"

bool Bridge_BuildAndBegin(TinyBMS_Victron_Bridge& bridge, BridgeEventSink& sink);
bool Bridge_CreateTasks(TinyBMS_Victron_Bridge* bridge);
