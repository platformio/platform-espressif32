Import("env")
platform = env.PioPlatform()
env.Replace( MKSPIFFSTOOL=platform.get_package_dir("tool-mklittlefs") + '/mklittlefs' )  # PlatformIO now believes it has actually created a SPIFFS
