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

from os import SEEK_CUR, SEEK_END
from os.path import basename, isfile, join
import shutil

from SCons.Script import Builder

from platformio.util import cd

Import("env")


#
# TXT files helpers
#


def prepare_files(files):
    if not files:
        return

    for f in files:
        shutil.copy(env.subst(f), env.subst(f+".org"))
        with open(env.subst(f), "rb+") as fp:
            fp.seek(-1, SEEK_END)
            if fp.read(1) != '\0':
                fp.seek(0, SEEK_CUR)
                fp.write(b'\0')

def fix_files(files):
    if not files:
        return

    for f in files:
        shutil.move(env.subst(f+".org"), env.subst(f));


def extract_files(cppdefines, type):
    for define in cppdefines:
        if type not in define:
            continue

        if not isinstance(define, tuple):
            print("Warning! %s macro cannot be empty!" % type)
            return []

        with cd(env.subst("$PROJECT_DIR")):
            value = define[1]
            if not isinstance(value, str):
                print("Warning! %s macro must contain "
                      "a list of files separated by ':'" % type)
                return []

            result = []
            for f in value.split(':'):
                if not isfile(f):
                    print("Warning! Could not find file %s" % f)
                    continue
                result.append(join("$PROJECT_DIR", f))

            return result


def remove_config_define(cppdefines, type):
    for define in cppdefines:
        if type in define:
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
if "COMPONENT_EMBED_TXTFILES" in env.Flatten(flags):
    files = extract_files(flags, "COMPONENT_EMBED_TXTFILES")
    prepare_files(files)
    embed_files(files)
    fix_files(files)
    remove_config_define(flags, "COMPONENT_EMBED_TXTFILES")

if "COMPONENT_EMBED_FILES" in env.Flatten(flags):
    files = extract_files(flags, "COMPONENT_EMBED_FILES")
    embed_files(files)
    remove_config_define(flags, "COMPONENT_EMBED_FILES")
