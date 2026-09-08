/* Copyright (c) 2015-2023 The Khronos Group Inc.
 * Copyright (c) 2015-2023 Valve Corporation
 * Copyright (c) 2015-2023 LunarG, Inc.
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

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <cstring>

namespace {

// Function with constructor attribute executes during library load.
// Re-open with RTLD_NODELETE to ensure the layer shared library stays resident in process memory
// across Vulkan loader queries.
__attribute__((constructor)) void LayerKeepAlive() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&LayerKeepAlive), &info) != 0 && info.dli_fname != nullptr &&
        info.dli_fname[0] != '\0' && std::strstr(info.dli_fname, ".so") != nullptr) {
        (void)dlopen(info.dli_fname, RTLD_NOW | RTLD_NODELETE);
    }
}

}  // namespace
#endif
