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

import shutil
from os import SEEK_CUR, SEEK_END, makedirs
from os.path import basename, isfile, isdir, join

from SCons.Script import Builder

from platformio.util import cd

Import("env")

#
# Embedded files helpers
#


def prepare_files(files):
    if not files:
        return

    fixed_files = []
    build_dir = env.subst("$BUILD_DIR")
    if not isdir(build_dir):
        makedirs(build_dir)
    for f in files:
        fixed_file = join(build_dir, basename(f))
        shutil.copy(env.subst(f), fixed_file)
        with open(fixed_file, "rb+") as fp:
            fp.seek(-1, SEEK_END)
            if fp.read(1) != '\0':
                fp.seek(0, SEEK_CUR)
                fp.write(b'\0')

        fixed_files.append(fixed_file)

    return fixed_files


def extract_files(cppdefines, files_type):
    for define in cppdefines:
        if files_type not in define:
            continue

        if not isinstance(define, tuple):
            print("Warning! %s macro cannot be empty!" % files_type)
            return []

        with cd(env.subst("$PROJECT_DIR")):
            value = define[1]
            if not isinstance(value, str):
                print("Warning! %s macro must contain "
                      "a list of files separated by ':'" % files_type)
                return []

            result = []
            for f in value.split(':'):
                if not isfile(f):
                    print("Warning! Could not find file %s" % f)
                    continue
                result.append(join("$PROJECT_DIR", f))

            return result


def remove_config_define(cppdefines, files_type):
    for define in cppdefines:
        if files_type in define:
            env.ProcessUnFlags("-D%s" % "=".join(str(d) for d in define))
            return


def embed_files(files):
    for f in files:
        filename = basename(f) + ".txt.o"
        file_target = env.TxtToBin(join("$BUILD_DIR", filename), f)
        env.Depends("$PIOMAINPROG", file_target)
        env.Append(PIOBUILDFILES=[env.File(join("$BUILD_DIR", filename))])


env.Append(
    BUILDERS=dict(
        TxtToBin=Builder(
            action=env.VerboseAction(" ".join([
                "xtensa-esp32-elf-objcopy",
                "--input-target", "binary",
                "--output-target", "elf32-xtensa-le",
                "--binary-architecture", "xtensa",
                "--rename-section", ".data=.rodata.embedded",
                "$SOURCE", "$TARGET"
            ]), "Converting $TARGET"),
            suffix=".txt.o"))
)



flags = env.get("CPPDEFINES")
for component_files in ("COMPONENT_EMBED_TXTFILES", "COMPONENT_EMBED_FILES"):
    if component_files not in env.Flatten(flags):
        continue
    files = extract_files(flags, component_files)
    if component_files == "COMPONENT_EMBED_TXTFILES":
        files = prepare_files(files)
    embed_files(files)
    remove_config_define(flags, component_files)
