from os import SEEK_CUR, SEEK_END
from os.path import basename, isfile, join

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
        with open(env.subst(f), "rb+") as fp:
            fp.seek(-1, SEEK_END)
            if fp.read(1) != '\0':
                fp.seek(0, SEEK_CUR)
                fp.write('\0')


def extract_files(cppdefines):
    for define in cppdefines:
        if "EMBED_TXT_FILES" not in define:
            continue

        if not isinstance(define, tuple):
            print("Warning! EMBED_TXT_FILES config cannot be empty!")
            return []

        with cd(env.subst("$PROJECT_DIR")):
            value = define[1]
            if not isinstance(value, str):
                print("Warning! EMBED_TXT_FILES config must contain "
                      "a list of files separated by ':'")
                return []

            result = []
            for f in value.split(':'):
                if not isfile(f):
                    print("Warning! Could not find file %s" % f)
                    continue
                result.append(join("$PROJECT_DIR", f))

            return result


def remove_config_define(cppdefines):
    for define in cppdefines:
        if "EMBED_TXT_FILES" in define:
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
if "EMBED_TXT_FILES" in env.Flatten(flags):
    files = extract_files(flags)
    prepare_files(files)
    embed_files(files)
    remove_config_define(flags)
