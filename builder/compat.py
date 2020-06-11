# Copyright 2014-present PlatformIO <contact@platformio.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from SCons.Script import AlwaysBuild, Import


Import("env")


# Added in PIO Core 4.4.0
if not hasattr(env, "AddPlatformTarget"):

    def AddPlatformTarget(
        env,
        name,
        dependencies,
        actions,
        title=None,
        description=None,
        always_build=True,
    ):
        target = env.Alias(name, dependencies, actions)
        if always_build:
            AlwaysBuild(target)
        return target

    env.AddMethod(AddPlatformTarget)
