/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPacket.h"

#include "Gd.h"

namespace EnOcean {
EnOceanPacket::EnOceanPacket() {
}

EnOceanPacket::EnOceanPacket(const std::vector<uint8_t> &espPacket) : _packet(espPacket) {
  _timeReceived = BaseLib::HelperFunctions::getTime();
  if (espPacket.size() < 6) return;
  uint32_t dataSize = ((uint16_t)espPacket[1] << 8) | espPacket[2];
  uint32_t optionalSize = espPacket[3];
  uint32_t fullSize = dataSize + optionalSize;
  if (espPacket.size() != fullSize + 7 || fullSize == 0) {
    Gd::out.printWarning("Warning: Tried to import packet with wrong size information: " + BaseLib::HelperFunctions::getHexString(espPacket));
    return;
  }
  _type = (Type)espPacket[4];
  _data.insert(_data.end(), espPacket.begin() + 6, espPacket.begin() + 6 + dataSize);
  _optionalData.insert(_optionalData.end(), espPacket.begin() + 6 + dataSize, espPacket.begin() + 6 + dataSize + optionalSize);

  if (_type == Type::RADIO_ERP1 || _type == Type::RADIO_ERP2) {
    if (!_data.empty()) _rorg = (uint8_t)_data[0];
    if (_data.size() >= 6) _senderAddress = (((int32_t)(uint8_t)_data[_data.size() - 5]) << 24) | (((int32_t)(uint8_t)_data[_data.size() - 4]) << 16) | (((int32_t)(uint8_t)_data[_data.size() - 3]) << 8) | ((int32_t)(uint8_t)_data[_data.size() - 2]);
    //Destination address is unset for RADIO_ERP2
    if (_optionalData.size() >= 5) _destinationAddress = (((int32_t)(uint8_t)_optionalData[1]) << 24) | (((int32_t)(uint8_t)_optionalData[2]) << 16) | (((int32_t)(uint8_t)_optionalData[3]) << 8) | (int32_t)(uint8_t)_optionalData[4];
    if (_optionalData.size() >= 2) _rssi = _type == Type::RADIO_ERP1 ? -((int32_t)_optionalData[_optionalData.size() - 2]) : -((int32_t)_optionalData.back());
  } else if (_type == Type::REMOTE_MAN_COMMAND && _data.size() >= 4 && _optionalData.size() >= 10) {
    _remoteManagementFunction = (uint16_t)((uint16_t)_data[0] << 8u) | _data[1];
    _remoteManagementManufacturer = (uint16_t)((uint16_t)_data[2] << 8u) | _data[3];
    _destinationAddress = (((int32_t)(uint8_t)_optionalData[0]) << 24) | (((int32_t)(uint8_t)_optionalData[1]) << 16) | (((int32_t)(uint8_t)_optionalData[2]) << 8) | (int32_t)(uint8_t)_optionalData[3];
    _senderAddress = (((int32_t)(uint8_t)_optionalData[4]) << 24) | (((int32_t)(uint8_t)_optionalData[5]) << 16) | (((int32_t)(uint8_t)_optionalData[6]) << 8) | (int32_t)(uint8_t)_optionalData[7];
    _rssi = -((int32_t)_optionalData[8]);
  }
}

EnOceanPacket::EnOceanPacket(Type type, uint8_t rorg, int32_t senderAddress, int32_t destinationAddress) : _type(type), _rorg(rorg) {
  _senderAddress = senderAddress;
  _destinationAddress = ((destinationAddress & 0xFFFFFF80u) == (senderAddress & 0xFFFFFF80u) ? 0xFFFFFFFFu : destinationAddress);
  _data.reserve(20);
  if (_type == Type::RADIO_ERP1 || _type == Type::RADIO_ERP2) {
    _appendAddressAndStatus = true;
    _data.push_back(rorg);
  }
  if (type == Type::RADIO_ERP1) _optionalData =
                                    std::vector<uint8_t>{3, (uint8_t)((_destinationAddress >> 24u) & 0xFFu), (uint8_t)((_destinationAddress >> 16u) & 0xFFu), (uint8_t)((_destinationAddress >> 8u) & 0xFFu), (uint8_t)(_destinationAddress & 0xFFu),
                                                         0xFF, 0};
  else if (type == Type::RADIO_ERP2) _optionalData = std::vector<uint8_t>{3, (uint8_t)0xFF};
  else if (type == Type::REMOTE_MAN_COMMAND) _optionalData = std::vector<uint8_t>{(uint8_t)((_destinationAddress >> 24u) & 0xFFu), (uint8_t)((_destinationAddress >> 16u) & 0xFFu), (uint8_t)((_destinationAddress >> 8u) & 0xFFu),
                                                                                  (uint8_t)(_destinationAddress & 0xFFu), (uint8_t)((_senderAddress >> 24u) & 0xFFu), (uint8_t)((_senderAddress >> 16u) & 0xFFu),
                                                                                  (uint8_t)((_senderAddress >> 8u) & 0xFFu), (uint8_t)(_senderAddress & 0xFFu), 0xFF, 0};
}

EnOceanPacket::~EnOceanPacket() {
  _packet.clear();
  _data.clear();
  _optionalData.clear();
}

std::vector<uint8_t> EnOceanPacket::getBinary() {
  try {
    if (!_packet.empty()) return _packet;
    if (_appendAddressAndStatus) {
      _data.push_back((uint8_t)(_senderAddress >> 24u));
      _data.push_back((uint8_t)((_senderAddress >> 16u) & 0xFFu));
      _data.push_back((uint8_t)((_senderAddress >> 8u) & 0xFFu));
      _data.push_back((uint8_t)(_senderAddress & 0xFFu));
      _data.push_back(_rorg == 0xF6 ? 0x30 : 0);
    }
    if (_data.empty() && _optionalData.empty()) return std::vector<uint8_t>();
    _packet.reserve(7 + _data.size() + _optionalData.size());
    _packet.push_back(0x55);
    _packet.push_back((uint8_t)(_data.size() >> 8u));
    _packet.push_back((uint8_t)(_data.size() & 0xFFu));
    _packet.push_back((uint8_t)_optionalData.size());
    _packet.push_back((uint8_t)_type);
    _packet.push_back(0);
    _packet.insert(_packet.end(), _data.begin(), _data.end());
    _packet.insert(_packet.end(), _optionalData.begin(), _optionalData.end());
    _packet.push_back(0);
    return _packet;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<uint8_t>();
}

std::vector<uint8_t> EnOceanPacket::getPosition(uint32_t position, uint32_t size) {
  try {
    return BaseLib::BitReaderWriter::getPosition(_data, position, size);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<uint8_t>();
}

void EnOceanPacket::setPosition(uint32_t position, uint32_t size, const std::vector<uint8_t> &source) {
  try {
    BaseLib::BitReaderWriter::setPositionBE(position, size, _data, source);
  }
  catch (const std::exception &ex) {
    Gd::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

}
