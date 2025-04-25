# Copyright (c) 2014-present PlatformIO <contact@platformio.org>
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

import os
import re
import subprocess
import sys

from platformio.exception import PlatformioException
from platformio.public import (
    DeviceMonitorFilterBase,
    load_build_metadata,
)

# By design, __init__ is called inside miniterm and we can't pass context to it.
# pylint: disable=attribute-defined-outside-init

IS_WINDOWS = sys.platform.startswith("win")


class Esp32ExceptionDecoder(DeviceMonitorFilterBase):
    NAME = "esp32_exception_decoder"

    ADDR_PATTERN = re.compile(r"((?:0x[0-9a-fA-F]{8}[: ]?)+)")
    ADDR_SPLIT = re.compile(r"[ :]")
    PREFIX_RE = re.compile(r"^ *")

    def __call__(self):
        self.buffer = ""

        self.firmware_path = None
        self.addr2line_path = None
        self.enabled = self.setup_paths()

        if self.config.get("env:" + self.environment, "build_type") != "debug":
            print(
                """
Please build project in debug configuration to get more details about an exception.
See https://docs.platformio.org/page/projectconf/build_configurations.html

"""
            )

        return self

    def setup_paths(self):
        self.project_dir = os.path.abspath(self.project_dir)
        try:
            data = load_build_metadata(self.project_dir, self.environment, cache=True)

            self.firmware_path = data["prog_path"]
            if not os.path.isfile(self.firmware_path):
                sys.stderr.write(
                    "%s: firmware at %s does not exist, rebuild the project?\n"
                    % (self.__class__.__name__, self.firmware_path)
                )
                return False

            cc_path = data.get("cc_path", "")
            if "-gcc" in cc_path:
                path = cc_path.replace("-gcc", "-addr2line")
                if os.path.isfile(path):
                    self.addr2line_path = path
                    return True
        except PlatformioException as e:
            sys.stderr.write(
                "%s: disabling, exception while looking for addr2line: %s\n"
                % (self.__class__.__name__, e)
            )
            return False
        sys.stderr.write(
            "%s: disabling, failed to find addr2line.\n" % self.__class__.__name__
        )
        return False

    def rx(self, text):
        if not self.enabled:
            return text

        last = 0
        while True:
            idx = text.find("\n", last)
            if idx == -1:
                if len(self.buffer) < 4096:
                    self.buffer += text[last:]
                break

            line = text[last:idx]
            if self.buffer:
                line = self.buffer + line
                self.buffer = ""
            last = idx + 1

            m = self.ADDR_PATTERN.search(line)
            if m is None:
                continue

            trace = self.build_backtrace(line, m.group(1))
            if trace:
                text = text[: idx + 1] + trace + text[idx + 1 :]
                last += len(trace)
        return text

    def is_address_ignored(self, address):
        return address in ("", "0x00000000")

    def filter_addresses(self, adresses_str):
        addresses = self.ADDR_SPLIT.split(adresses_str)
        size = len(addresses)
        while size > 1 and self.is_address_ignored(addresses[size-1]):
            size -= 1
        return addresses[:size]

    def build_backtrace(self, line, address_match):
        addresses = self.filter_addresses(address_match)
        if not addresses:
            return ""

        prefix_match = self.PREFIX_RE.match(line)
        prefix = prefix_match.group(0) if prefix_match is not None else ""

        trace = ""
        enc = "mbcs" if IS_WINDOWS else "utf-8"
        args = [self.addr2line_path, u"-fipC", u"-e", self.firmware_path]
        try:
            i = 0
            for addr in addresses:
                output = (
                    subprocess.check_output(args + [addr])
                    .decode(enc)
                    .strip()
                )

                # newlines happen with inlined methods
                output = output.replace(
                    "\n", "\n     "
                )

                # throw out addresses not from ELF
                if output == "?? ??:0":
                    continue

                output = self.strip_project_dir(output)
                trace += "%s  #%-2d %s in %s\n" % (prefix, i, addr, output)
                i += 1
        except subprocess.CalledProcessError as e:
            sys.stderr.write(
                "%s: failed to call %s: %s\n"
                % (self.__class__.__name__, self.addr2line_path, e)
            )

        return trace + "\n" if trace else ""

    def strip_project_dir(self, trace):
        while True:
            idx = trace.find(self.project_dir)
            if idx == -1:
                break
            trace = trace[:idx] + trace[idx + len(self.project_dir) + 1 :]
        return trace
