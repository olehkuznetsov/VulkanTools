/*
 * Copyright (c) 2020-2025 Valve Corporation
 * Copyright (c) 2020-2025 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 * - Christophe Riccio <christophe@lunarg.com>
 */

#pragma once

#include "type_platform.h"
#include "type_status.h"
#include "json.h"
#include "path.h"

#include <string>

enum SettingView {
    SETTING_VIEW_STANDARD = 0,
    SETTING_VIEW_ADVANCED,
    SETTING_VIEW_HIDDEN,

    SETTING_VIEW_FIRST = SETTING_VIEW_STANDARD,
    SETTING_VIEW_LAST = SETTING_VIEW_HIDDEN
};

enum { SETTING_VIEW_COUNT = SETTING_VIEW_LAST - SETTING_VIEW_FIRST + 1 };

SettingView GetSettingView(const char* token);
const char* GetToken(SettingView state);

struct Header {
    Header() : status(STATUS_STABLE), view(SETTING_VIEW_STANDARD), platform_flags(PLATFORM_DESKTOP_BIT), expanded(true) {}

    std::string label;
    std::string description;
    std::string detailed;
    Path url;
    StatusType status;
    std::string deprecated_by_key;
    SettingView view;
    int platform_flags;
    bool expanded;
};

void LoadMetaHeader(Header& header, const QJsonObject& json_object);
