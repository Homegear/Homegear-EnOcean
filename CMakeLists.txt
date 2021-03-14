cmake_minimum_required(VERSION 3.8)
project(homegear_enocean)

set(CMAKE_CXX_STANDARD 17)

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
        src/EnOceanCentral.cpp
        src/EnOceanCentral.h
        src/EnOcean.cpp
        src/EnOcean.h
        src/EnOceanPacket.cpp
        src/EnOceanPacket.h
        src/EnOceanPeer.cpp
        src/EnOceanPeer.h
        src/Security.cpp
        src/Security.h
        src/PhysicalInterfaces/HomegearGateway.cpp src/PhysicalInterfaces/HomegearGateway.h src/PhysicalInterfaces/Hgdc.cpp src/PhysicalInterfaces/Hgdc.h src/EnOceanPackets.cpp src/EnOceanPackets.h src/RemanFeatures.h src/RemanFeatures.cpp)

add_custom_target(homegear COMMAND ../../makeAll.sh SOURCES ${SOURCE_FILES})

add_library(homegear_enocean ${SOURCE_FILES})