# Copyright 2019-present PlatformIO <contact@platformio.org>
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

from os import remove
from os.path import exists, join

from shutil import copyfile

from SCons.Script import Builder, Import, Return

Import("env")

ulp_env = env.Clone()

platform = ulp_env.PioPlatform()
FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
ULP_TOOLCHAIN_DIR = platform.get_package_dir("toolchain-esp32ulp")
ULP_BUILD_DIR = join("$BUILD_DIR", "ulp_app")

# ULP toolchain binaries should be in PATH
ulp_env.PrependENVPath("PATH", join(ULP_TOOLCHAIN_DIR, "bin"))


def bin_converter(target, source, env):
    # A workaround that helps avoid changing working directory
    # in order to generate symbols that are irrespective of path
    temp_source = join(env.subst("$PROJECT_DIR"), source[0].name)
    copyfile(source[0].get_path(), temp_source)

    command = " ".join([
        "xtensa-esp32-elf-objcopy", "--input-target",
        "binary", "--output-target",
        "elf32-xtensa-le", "--binary-architecture",
        "xtensa", "--rename-section",
        ".data=.rodata.embedded",
        source[0].name, target[0].get_path()
    ])

    ulp_env.Execute(command)

    if exists(temp_source):
        remove(temp_source)

    return None


ulp_env.Append(
    CPPPATH=["$PROJECTSRC_DIR"],

    BUILDERS=dict(
        BuildElf=Builder(
            action=ulp_env.VerboseAction(" ".join([
                "esp32ulp-elf-ld",
                "-o", "$TARGET",
                "-A", "elf32-esp32ulp",
                "-L", ULP_BUILD_DIR,
                "-T", "ulp_main.common.ld",
                "$SOURCES"
            ]), "Linking $TARGET"),
            suffix=".elf"
        ),
        UlpElfToBin=Builder(
            action=ulp_env.VerboseAction(" ".join([
                "esp32ulp-elf-objcopy",
                "-O", "binary",
                "$SOURCE", "$TARGET"
            ]), "Building $TARGET"),
            suffix=".bin"
        ),
        ConvertBin=Builder(
            action=bin_converter,
            suffix=".bin.bin.o"
        ),
        PreprocAs=Builder(
            action=ulp_env.VerboseAction(" ".join([
                "xtensa-esp32-elf-gcc",
                "-DESP_PLATFORM", "-MMD", "-MP", "-DGCC_NOT_5_2_0=0",
                "-DWITH_POSIX", "-DHAVE_CONFIG_H",
                "-MT", "${TARGET}.o",
                "-DMBEDTLS_CONFIG_FILE=\"mbedtls/esp_config.h\"",
                "-I %s" % join(
                    FRAMEWORK_DIR, "components", "soc", "esp32", "include"),
                "-E", "-P", "-xc",
                "-o", "$TARGET", "-D__ASSEMBLER__", "$SOURCE"
            ]), "Preprocessing $TARGET"),
            single_source=True,
            suffix=".ulp.pS"
        ),
        AsToObj=Builder(
            action=ulp_env.VerboseAction(" ".join([
                "esp32ulp-elf-as",
                "-o", "$TARGET", "$SOURCE"
            ]), "Compiling $TARGET"),
            single_source=True,
            suffix=".o"
        )
    )
)


def preprocess_ld_script():
    arguments = ("-DESP_PLATFORM", "-MMD", "-MP", "-DGCC_NOT_5_2_0=0",
                 "-DWITH_POSIX", "-D__ASSEMBLER__",
                 '-DMBEDTLS_CONFIG_FILE="mbedtls/esp_config.h"',
                 "-DHAVE_CONFIG_H", "-MT", "$TARGET", "-E", "-P", "-xc", "-o",
                 "$TARGET", "-I $PROJECTSRC_DIR", "$SOURCE")

    return ulp_env.Command(
        join(ULP_BUILD_DIR, "ulp_main.common.ld"),
        join(FRAMEWORK_DIR, "components", "ulp", "ld", "esp32.ulp.ld"),
        ulp_env.VerboseAction('xtensa-esp32-elf-gcc %s' % " ".join(arguments),
                              "Preprocessing linker script $TARGET"))


def collect_src_files(src_path):
    return ulp_env.CollectBuildFiles(ULP_BUILD_DIR, src_path)


def generate_global_symbols(elf_file):
    return ulp_env.Command(
        join(ULP_BUILD_DIR, "ulp_main.sym"), elf_file,
        ulp_env.VerboseAction(
            "esp32ulp-elf-nm -g -f posix $SOURCE > $TARGET",
            "Generating global symbols $TARGET"))


def generate_export_files(symbol_file):
    # generates ld script and header file
    gen_script = join(FRAMEWORK_DIR, "components", "ulp", "esp32ulp_mapgen.py")
    build_suffix = join(ULP_BUILD_DIR, "ulp_main")
    return ulp_env.Command(
        [join(ULP_BUILD_DIR, "ulp_main.ld"),
         join(ULP_BUILD_DIR, "ulp_main.h")], symbol_file,
        ulp_env.VerboseAction(
            '"$PYTHONEXE" "%s" -s $SOURCE -o %s' % (gen_script, build_suffix),
            "Exporting ULP linker and header files"))


def create_static_lib(bin_file):
    return ulp_env.StaticLibrary(join(ULP_BUILD_DIR, "ulp_main"), [bin_file])


ulp_src_files = collect_src_files(
    join(ulp_env.subst("$PROJECT_DIR"), "ulp"))
objects = ulp_env.AsToObj(ulp_env.PreprocAs(ulp_src_files))
ulp_elf = ulp_env.BuildElf(join(ULP_BUILD_DIR, "ulp_main"), objects)
raw_ulp_binary = ulp_env.UlpElfToBin(ulp_elf)
ulp_bin = ulp_env.ConvertBin(raw_ulp_binary)
global_symbols = generate_global_symbols(ulp_elf)
export_files = generate_export_files(global_symbols)

ulp_lib = create_static_lib(ulp_bin)

ulp_env.Depends(ulp_lib, export_files)
ulp_env.Depends(ulp_elf, preprocess_ld_script())

# ULP sources must be built before the files in "src" folder
ulp_env.Requires(join("$BUILD_DIR", "${PROGNAME}.elf"), ulp_lib)

Return("ulp_lib")
