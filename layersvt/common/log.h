/* Copyright (C) 2026 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#if defined(__ANDROID__)
#include <android/log.h>
#define VT_LOGI(tag, fmt, ...) __android_log_print(ANDROID_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define VT_LOGW(tag, fmt, ...) __android_log_print(ANDROID_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define VT_LOGE(tag, fmt, ...) __android_log_print(ANDROID_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define VT_LOGI(tag, fmt, ...) std::fprintf(stderr, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define VT_LOGW(tag, fmt, ...) std::fprintf(stderr, "[%s WARN] " fmt "\n", tag, ##__VA_ARGS__)
#define VT_LOGE(tag, fmt, ...) std::fprintf(stderr, "[%s ERROR] " fmt "\n", tag, ##__VA_ARGS__)
#endif
