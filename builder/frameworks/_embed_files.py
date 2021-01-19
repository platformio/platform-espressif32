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
from os import SEEK_CUR, SEEK_END
from os.path import basename, isfile, join

from SCons.Script import Builder

Import("env")

board = env.BoardConfig()

#
# Embedded files helpers
#


def extract_files(cppdefines, files_type):
    files = []
    if "build." + files_type in board:
        files.extend(
            [
                join("$PROJECT_DIR", f)
                for f in board.get("build." + files_type, "").split()
                if f
            ]
        )
    else:
        files_define = "COMPONENT_" + files_type.upper()
        for define in cppdefines:
            if files_define not in define:
                continue

            value = define[1]
            if not isinstance(define, tuple):
                print("Warning! %s macro cannot be empty!" % files_define)
                return []

            if not isinstance(value, str):
                print(
                    "Warning! %s macro must contain "
                    "a list of files separated by ':'" % files_define
                )
                return []

            for f in value.split(":"):
                if not f:
                    continue
                files.append(join("$PROJECT_DIR", f))

    for f in files:
        if not isfile(env.subst(f)):
            print('Warning! Could not find file "%s"' % basename(f))

    return files


def remove_config_define(cppdefines, files_type):
    for define in cppdefines:
        if files_type in define:
            env.ProcessUnFlags("-D%s" % "=".join(str(d) for d in define))
            return


def prepare_file(source, target, env):
    filepath = source[0].get_abspath()
    shutil.copy(filepath, filepath + ".piobkp")

    with open(filepath, "rb+") as fp:
        fp.seek(-1, SEEK_END)
        if fp.read(1) != "\0":
            fp.seek(0, SEEK_CUR)
            fp.write(b"\0")


def revert_original_file(source, target, env):
    filepath = source[0].get_abspath()
    if isfile(filepath + ".piobkp"):
        shutil.move(filepath + ".piobkp", filepath)


def embed_files(files, files_type):
    for f in files:
        filename = basename(f) + ".txt.o"
        file_target = env.TxtToBin(join("$BUILD_DIR", filename), f)
        env.Depends("$PIOMAINPROG", file_target)
        if files_type == "embed_txtfiles":
            env.AddPreAction(file_target, prepare_file)
            env.AddPostAction(file_target, revert_original_file)
        env.AppendUnique(PIOBUILDFILES=[env.File(join("$BUILD_DIR", filename))])


def transform_to_asm(target, source, env):
    files = [join("$BUILD_DIR", s.name + ".S") for s in source]
    env.AppendUnique(PIOBUILDFILES=files)
    return files, source

env.Append(
    BUILDERS=dict(
        TxtToBin=Builder(
            action=env.VerboseAction(
                " ".join(
                    [
                        "xtensa-esp32-elf-objcopy",
                        "--input-target",
                        "binary",
                        "--output-target",
                        "elf32-xtensa-le",
                        "--binary-architecture",
                        "xtensa",
                        "--rename-section",
                        ".data=.rodata.embedded",
                        "$SOURCE",
                        "$TARGET",
                    ]
                ),
                "Converting $TARGET",
            ),
            suffix=".txt.o",
        ),
        TxtToAsm=Builder(
            action=env.VerboseAction(
                " ".join(
                    [
                        join(
                            env.PioPlatform().get_package_dir("tool-cmake") or "",
                            "bin",
                            "cmake",
                        ),
                        "-DDATA_FILE=$SOURCE",
                        "-DSOURCE_FILE=$TARGET",
                        "-DFILE_TYPE=TEXT",
                        "-P",
                        join(
                            env.PioPlatform().get_package_dir("framework-espidf") or "",
                            "tools",
                            "cmake",
                            "scripts",
                            "data_file_embed_asm.cmake",
                        ),
                    ]
                ),
                "Generating assembly for $TARGET",
            ),
            emitter=transform_to_asm,
            single_source=True
        ),
    )
)


flags = env.get("CPPDEFINES")
for files_type in ("embed_txtfiles", "embed_files"):
    if (
        "COMPONENT_" + files_type.upper() not in env.Flatten(flags)
        and "build." + files_type not in board
    ):
        continue

    files = extract_files(flags, files_type)
    if "espidf" in env.subst("$PIOFRAMEWORK"):
        env.Requires(join("$BUILD_DIR", "${PROGNAME}.elf"), env.TxtToAsm(files))
    else:
        embed_files(files, files_type)
        remove_config_define(flags, files_type)
