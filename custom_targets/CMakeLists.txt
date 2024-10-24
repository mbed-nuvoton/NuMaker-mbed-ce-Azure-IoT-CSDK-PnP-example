## CMakeLists which sets up the custom targets in this project

# To create a custom target, you first need to figure out the CMake name of the target
# from its Mbed name.  This name is calculated as:
#
# 1. Convert target to lowercase
# 2. Replace all underscores (_) with hyphens (-)
# 3. add "mbed-" prefix.
#
# So, the target named 'NUMAKER_IOT_M467' will be called mbed-numaker-iot-m467 in CMake.
# The Mbed build system will look for a target of this name when configuring, so make sure to create it.

add_library(mbed-numaker-iot-m487-dev INTERFACE)
target_link_libraries(mbed-numaker-iot-m487-dev INTERFACE mbed-numaker-iot-m487)
