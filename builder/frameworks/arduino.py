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

"""
Arduino

Arduino Wiring-based Framework allows writing cross-platform software to
control devices attached to a wide range of Arduino boards to create all
kinds of creative coding, interactive objects, spaces or physical experiences.

http://arduino.cc/en/Reference/HomePage
"""

from os.path import isdir, join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()

FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")
assert isdir(FRAMEWORK_DIR)

env.Prepend(
    CPPDEFINES=[
        "ARDUINO=10600",
        "ARDUINO_ARCH_ESP32"
    ],

    CPPPATH=[
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "config"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "bt"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "driver"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "esp32"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "freertos"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "log"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "lwip"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "newlib"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "nvs_flash"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "spi_flash"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "tcpip_adapter"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "vfs"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "expat"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "json"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "mbedtls"),
        join(FRAMEWORK_DIR, "tools", "sdk", "include", "nghttp"),
        join(FRAMEWORK_DIR, "cores", env.BoardConfig().get("build.core"))
    ],
    LIBPATH=[
        join(FRAMEWORK_DIR, "tools", "sdk", "lib"),
        join(FRAMEWORK_DIR, "tools", "sdk", "ld"),
    ],
    LIBS=[
        "hal", "core", "net80211", "phy", "rtc", "pp", "wpa",
        "smartconfig", "btdm_app", "bt", "driver", "esp32", "crypto", "expat",
        "freertos", "json", "log", "lwip", "mbedtls", "nghttp", "nvs_flash",
        "spi_flash", "tcpip_adapter", "newlib", "vfs", "gcc", "m", "c"
    ]
)

env.Append(
    LIBSOURCE_DIRS=[
        join(FRAMEWORK_DIR, "libraries")
    ],

    LINKFLAGS=[
        "-T", "esp32.common.ld",
        "-T", "esp32.rom.ld",
        "-T", "esp32.peripherals.ld"
    ]
)

#
# Target: Build Core Library
#

libs = []

if "build.variant" in env.BoardConfig():
    env.Append(
        CPPPATH=[
            join(FRAMEWORK_DIR, "variants",
                 env.BoardConfig().get("build.variant"))
        ]
    )
    libs.append(env.BuildLibrary(
        join("$BUILD_DIR", "FrameworkArduinoVariant"),
        join(FRAMEWORK_DIR, "variants", env.BoardConfig().get("build.variant"))
    ))

envsafe = env.Clone()

libs.append(envsafe.BuildLibrary(
    join("$BUILD_DIR", "FrameworkArduino"),
    join(FRAMEWORK_DIR, "cores", env.BoardConfig().get("build.core"))
))

env.Prepend(LIBS=libs)
