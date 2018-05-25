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

"""Pumbaa

Pumbaa is Python on top of Simba.

The implementation is a port of MicroPython, designed for embedded
devices with limited amount of RAM and code memory.

http://pumbaa.readthedocs.org

"""

from os.path import join, sep

from SCons.Script import DefaultEnvironment, SConscript

from platformio.builder.tools import platformio as platformio_tool

#
# Backward compatibility with PlatformIO 2.0
#
platformio_tool.SRC_DEFAULT_FILTER = " ".join([
    "+<*>", "-<.git%s>" % sep, "-<svn%s>" % sep,
    "-<example%s>" % sep, "-<examples%s>" % sep,
    "-<test%s>" % sep, "-<tests%s>" % sep
])


def LookupSources(env, variant_dir, src_dir, duplicate=True, src_filter=None):
    return env.CollectBuildFiles(variant_dir, src_dir, src_filter, duplicate)


def VariantDirWrap(env, variant_dir, src_dir, duplicate=False):
    env.VariantDir(variant_dir, src_dir, duplicate)


env = DefaultEnvironment()

env.AddMethod(LookupSources)
env.AddMethod(VariantDirWrap)

env.Replace(
    PLATFORMFW_DIR=env.PioPlatform().get_package_dir("framework-pumbaa"),
    UPLOADERFLAGS=[]  # Backward compatibility for obsolete build script
)

SConscript(
    [env.subst(join("$PLATFORMFW_DIR", "make", "platformio.sconscript"))])

env.Replace(
    FLASH_EXTRA_IMAGES=[
        ("0x1000", join("$PLATFORMFW_DIR", "simba", "3pp", "esp32",
                        "bin", "bootloader.bin")),
        ("0x8000", join("$PLATFORMFW_DIR", "simba", "3pp", "esp32",
                        "bin", "partitions_singleapp.bin"))
    ]
)
