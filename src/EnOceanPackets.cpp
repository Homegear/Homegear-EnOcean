/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPackets.h"

namespace EnOcean {

ApplyChanges::ApplyChanges(uint32_t senderAddress, uint32_t destinationAddress, bool applyLinkTableChanges, bool applyConfigurationChanges) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::applyChanges;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::applyChanges >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::applyChanges);
  _data.push_back(0);
  if (applyLinkTableChanges) _data.at(3) |= 0x80;
  if (applyConfigurationChanges) _data.at(3) |= 0x40;
}

GetDeviceConfiguration::GetDeviceConfiguration(uint32_t senderAddress, uint32_t destinationAddress, uint16_t startIndex, uint16_t endIndex, uint8_t length) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::getDeviceConfiguration;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::getDeviceConfiguration >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::getDeviceConfiguration);
  _data.push_back(startIndex >> 8u);
  _data.push_back(startIndex);
  _data.push_back(endIndex >> 8u);
  _data.push_back(endIndex);
  _data.push_back(length);
}

GetLinkTable::GetLinkTable(uint32_t sender_address, uint32_t destination_address, bool inbound, uint8_t start_index, uint8_t end_index) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, sender_address, destination_address) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::getLinkTable;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::getLinkTable >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::getLinkTable);
  _data.push_back(inbound ? 0 : 0x80);
  _data.push_back(start_index);
  _data.push_back(end_index);
}

GetPathInfoThroughPing::GetPathInfoThroughPing(uint32_t senderAddress, uint32_t destinationAddress, uint32_t destinationPingDeviceId) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::getPathInfoThroughPing;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::getPathInfoThroughPing >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::getPathInfoThroughPing);
  _data.push_back(destinationPingDeviceId >> 24u);
  _data.push_back(destinationPingDeviceId >> 16u);
  _data.push_back(destinationPingDeviceId >> 8u);
  _data.push_back(destinationPingDeviceId);
}

Lock::Lock(uint32_t senderAddress, uint32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::lock;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::lock >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::lock);
  _data.push_back(securityCode >> 24u);
  _data.push_back(securityCode >> 16u);
  _data.push_back(securityCode >> 8u);
  _data.push_back(securityCode);
}

PingPacket::PingPacket(uint32_t sender_address, uint32_t destination_address) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, sender_address, destination_address) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::ping;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::ping >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::ping);
}

QueryIdPacket::QueryIdPacket(uint32_t senderAddress, uint32_t destinationAddress) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::queryId;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::queryId >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::queryId);
  _data.push_back(0);
  _data.push_back(0);
  _data.push_back(0);
}

QueryStatusPacket::QueryStatusPacket(uint32_t senderAddress, uint32_t destinationAddress) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::queryStatus;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::queryStatus >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::queryStatus);
}

SetCode::SetCode(uint32_t senderAddress, uint32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setCode;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::setCode >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::setCode);
  _data.push_back(securityCode >> 24u);
  _data.push_back(securityCode >> 16u);
  _data.push_back(securityCode >> 8u);
  _data.push_back(securityCode);
}

SetDeviceConfiguration::SetDeviceConfiguration(uint32_t senderAddress, uint32_t destinationAddress, const std::map<uint32_t, std::vector<uint8_t>> &configuration) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setDeviceConfiguration;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::setDeviceConfiguration >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::setDeviceConfiguration);
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

SetLinkTable::SetLinkTable(uint32_t senderAddress, uint32_t destinationAddress, bool inbound, const std::vector<uint8_t> &table) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setLinkTable;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::setLinkTable >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::setLinkTable);
  _data.push_back(inbound ? 0 : 0x80);
  _data.insert(_data.end(), table.begin(), table.end());
}

SetRepeaterFilter::SetRepeaterFilter(uint32_t sender_address, uint32_t destination_address, uint8_t filter_control, uint8_t filter_type, uint32_t filter_value) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, sender_address, destination_address) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setRepeaterFilter;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::setRepeaterFilter >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::setRepeaterFilter);
  _data.push_back((filter_control << 4) | (filter_type & 0x0F));
  _data.push_back(filter_value >> 24);
  _data.push_back(filter_value >> 16);
  _data.push_back(filter_value >> 8);
  _data.push_back(filter_value);
}

SetRepeaterFunctions::SetRepeaterFunctions(uint32_t senderAddress, uint32_t destinationAddress, uint8_t function, uint8_t level, uint8_t filterStructure) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setRepeaterFunctions;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::setRepeaterFunctions >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::setRepeaterFunctions);
  _data.push_back((unsigned)(function << 6u) | ((level & 3u) << 4u) | (unsigned)((filterStructure & 1u) << 3u));
}

SetSecurityProfile::SetSecurityProfile(uint32_t senderAddress, uint32_t destinationAddress, bool recomVersion11, bool hasAddresses, bool outbound, uint8_t index, uint8_t slf, uint32_t rlc, const std::vector<uint8_t> &aesKey, uint32_t destinationId, uint32_t sourceId)
    : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::setSecurityProfile;
  _data.reserve(36);
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::setSecurityProfile >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::setSecurityProfile);
  _data.push_back(outbound ? 0x80 : 0);
  _data.push_back(index);
  _data.push_back(slf);
  if (!recomVersion11) _data.push_back(rlc >> 24);
  _data.push_back(rlc >> 16);
  _data.push_back(rlc >> 8);
  _data.push_back(rlc);
  _data.insert(_data.end(), aesKey.begin(), aesKey.end());
  if (!recomVersion11 && hasAddresses) {
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

Unlock::Unlock(uint32_t senderAddress, uint32_t destinationAddress, uint32_t securityCode) : EnOceanPacket(Type::RADIO_ERP1, 0xC5, senderAddress, destinationAddress) {
  _remoteManagementFunction = (uint16_t)RemoteManagementFunction::unlock;
  _data.push_back(0x7F); //Manufacturer MSB
  _data.push_back(0xF0 | ((uint16_t)RemoteManagementFunction::unlock >> 8u)); //Manufacturer LSB
  _data.push_back((uint8_t)RemoteManagementFunction::unlock);
  _data.push_back(securityCode >> 24u);
  _data.push_back(securityCode >> 16u);
  _data.push_back(securityCode >> 8u);
  _data.push_back(securityCode);
}

}