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

namespace layersvt {

/**
 * @brief Ensures the layer shared library is pinned with RTLD_NODELETE on Android.
 *
 * Calling this explicitly guarantees that the dynamic linker does not unload the
 * layer shared object when dlclose is called, preventing crashes during teardown.
 * On non-Android platforms, this is a no-op.
 */
void EnsureLayerKeepAlive();

}  // namespace layersvt
