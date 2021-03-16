/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPackets.h"

namespace EnOcean {

ApplyChanges::ApplyChanges(int32_t destinationAddress, bool applyLinkTableChanges, bool applyConfigurationChanges) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::applyChanges;
  _data.push_back(0);
  if (applyLinkTableChanges) _data.at(0) |= 0x80;
  if (applyConfigurationChanges) _data.at(0) |= 0x40;
}

GetDeviceConfiguration::GetDeviceConfiguration(int32_t destinationAddress, uint16_t startIndex, uint16_t endIndex, uint8_t length) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::getDeviceConfiguration;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::getDeviceConfiguration >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::getDeviceConfiguration);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  _data.push_back(startIndex >> 8u);
  _data.push_back(startIndex);
  _data.push_back(endIndex >> 8u);
  _data.push_back(endIndex);
  _data.push_back(length);
}

Lock::Lock(int32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::lock;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::lock >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::lock);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  _data.push_back(securityCode >> 24u);
  _data.push_back(securityCode >> 16u);
  _data.push_back(securityCode >> 8u);
  _data.push_back(securityCode);
}

PingPacket::PingPacket(int32_t destinationAddress) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::ping;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::ping >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::ping);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
}

QueryIdPacket::QueryIdPacket(int32_t destinationAddress) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::queryId;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::queryId >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::queryId);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  _data.push_back(0);
  _data.push_back(0);
  _data.push_back(0);
}

QueryStatusPacket::QueryStatusPacket(int32_t destinationAddress) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::queryStatus;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::queryStatus >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::queryStatus);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
}

SetDeviceConfiguration::SetDeviceConfiguration(int32_t destinationAddress, const std::map<uint32_t, std::vector<uint8_t>> &configuration) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setDeviceConfiguration;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::setDeviceConfiguration >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::setDeviceConfiguration);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  uint32_t currentBitPosition = _data.size() * 8;
  for (auto &element : configuration) {
    if (element.second.empty()) continue;
    BaseLib::BitReaderWriter::setPositionBE(currentBitPosition, 16, _data, {(uint8_t)(element.first >> 8u), (uint8_t)element.first});
    currentBitPosition += 16;
    BaseLib::BitReaderWriter::setPositionBE(currentBitPosition, 8, _data, {(uint8_t)element.second.size()});
    currentBitPosition += 8;
    BaseLib::BitReaderWriter::setPositionBE(currentBitPosition, element.second.size() * 8, _data, element.second);
    currentBitPosition += element.second.size() * 8;
  }
}

SetLinkTable::SetLinkTable(int32_t destinationAddress, bool inbound, const std::vector<uint8_t> &table) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setLinkTable;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::setLinkTable >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::setLinkTable);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  _data.push_back(inbound ? 0 : 0x80);
  _data.insert(_data.end(), table.begin(), table.end());
}

SetRepeaterFunctions::SetRepeaterFunctions(int32_t destinationAddress, uint8_t function, uint8_t level, uint8_t filterStructure) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setRepeaterFunctions;
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::setRepeaterFunctions >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::setRepeaterFunctions);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  _data.push_back((unsigned)(function << 6u) | ((level & 3u) << 4u) | (unsigned)((filterStructure & 1u) << 3u));
}

SetSecurityProfile::SetSecurityProfile(int32_t destinationAddress, bool fitIn22Byte, bool outbound, uint8_t index, uint8_t slf, uint32_t rlc, const std::vector<uint8_t> &aesKey, uint32_t destinationId, uint32_t sourceId)
    : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setSecurityProfile;
  _data.reserve(36);
  _data.push_back((unsigned)((uint16_t)RemoteManagementFunction::setSecurityProfile >> 8u));
  _data.push_back((uint8_t)RemoteManagementFunction::setSecurityProfile);
  _data.push_back(0x07); //Manufacturer MSB
  _data.push_back(0xFF); //Manufacturer LSB
  _data.push_back(outbound ? 0x80 : 0);
  _data.push_back(index);
  _data.push_back(slf);
  if (!fitIn22Byte) _data.push_back(rlc >> 24);
  _data.push_back(rlc >> 16);
  _data.push_back(rlc >> 8);
  _data.push_back(rlc);
  _data.insert(_data.end(), aesKey.begin(), aesKey.end());
  if (!fitIn22Byte) {
    _data.push_back(destinationId >> 24);
    _data.push_back(destinationId >> 16);
    _data.push_back(destinationId >> 8);
    _data.push_back(destinationId);
    _data.push_back(sourceId >> 24);
    _data.push_back(sourceId >> 16);
    _data.push_back(sourceId >> 8);
    _data.push_back(sourceId);
  }
}

Unlock::Unlock(int32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::REMOTE_MAN_COMMAND, 0xC5, 0, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::unlock;
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