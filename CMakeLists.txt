cmake_minimum_required(VERSION 3.8)
project(homegear_enocean)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/PhysicalInterfaces/IEnOceanInterface.cpp
        src/PhysicalInterfaces/IEnOceanInterface.h
        src/PhysicalInterfaces/Usb300.cpp
        src/PhysicalInterfaces/Usb300.h
        src/Factory.cpp
        src/Factory.h
        src/GD.cpp
        src/GD.h
        src/Interfaces.cpp
        src/Interfaces.h
        src/MyCentral.cpp
        src/MyCentral.h
        src/MyFamily.cpp
        src/MyFamily.h
        src/MyPacket.cpp
        src/MyPacket.h
        src/MyPeer.cpp
        src/MyPeer.h
        src/Security.cpp
        src/Security.h
        config.h)

add_custom_target(homegear COMMAND ../../makeAll.sh SOURCES ${SOURCE_FILES})

add_library(homegear_enocean ${SOURCE_FILES})