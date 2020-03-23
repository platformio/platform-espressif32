#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS := port/include aws-iot-device-sdk-embedded-C/include

COMPONENT_SRCDIRS := aws-iot-device-sdk-embedded-C/src port

# Check the submodule is initialised
COMPONENT_SUBMODULES := aws-iot-device-sdk-embedded-C
