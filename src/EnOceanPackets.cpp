/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPackets.h"

namespace EnOcean
{

Lock::Lock(int32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::lock >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::lock);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
    _data.push_back(securityCode >> 24u);
    _data.push_back(securityCode >> 16u);
    _data.push_back(securityCode >> 8u);
    _data.push_back(securityCode);
}

PingPacket::PingPacket(int32_t destinationAddress) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::ping >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::ping);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
}

QueryIdPacket::QueryIdPacket(int32_t destinationAddress) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::queryId >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::queryId);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
    _data.push_back(0);
    _data.push_back(0);
    _data.push_back(0);
}

QueryStatusPacket::QueryStatusPacket(int32_t destinationAddress) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::queryStatus >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::queryStatus);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
}

SetLinkTable::SetLinkTable(int32_t destinationAddress, bool inbound, const std::vector<uint8_t>& table) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::setLinkTable >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::setLinkTable);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
    _data.push_back(inbound ? 0 : 0x80);
    _data.insert(_data.end(), table.begin(), table.end());
}

SetRepeaterFunctions::SetRepeaterFunctions(int32_t destinationAddress, uint8_t function, uint8_t level, uint8_t filterStructure) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::setRepeaterFunctions >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::setRepeaterFunctions);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
    _data.push_back((unsigned)(function << 6u) | ((level & 3u) << 4u) | (unsigned)((filterStructure & 1u) << 3u));
}

Unlock::Unlock(int32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress)
{
    _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::unlock >> 8u));
    _data.push_back((uint8_t)RemoteManagementFunction::unlock);
    _data.push_back(0x07); //Manufacturer MSB
    _data.push_back(0xFF); //Manufacturer LSB
    _data.push_back(securityCode >> 24u);
    _data.push_back(securityCode >> 16u);
    _data.push_back(securityCode >> 8u);
    _data.push_back(securityCode);
}

}