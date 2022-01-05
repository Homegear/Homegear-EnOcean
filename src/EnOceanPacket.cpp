/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPacket.h"

#include "Gd.h"

namespace EnOcean {
EnOceanPacket::EnOceanPacket() {
}

EnOceanPacket::EnOceanPacket(const std::vector<uint8_t> &espPacket) : _packet(espPacket) {
  if (espPacket.size() < 6) return;
  uint32_t dataSize = ((uint16_t)espPacket[1] << 8) | espPacket[2];
  uint32_t optionalSize = espPacket[3];
  uint32_t fullSize = dataSize + optionalSize;
  if (espPacket.size() != fullSize + 7 || fullSize == 0) {
    Gd::out.printWarning("Warning: Tried to import packet with wrong size information: " + BaseLib::HelperFunctions::getHexString(espPacket));
    return;
  }
  _timeReceived = BaseLib::HelperFunctions::getTime();
  _type = (Type)espPacket[4];
  _data.insert(_data.end(), espPacket.begin() + 6, espPacket.begin() + 6 + dataSize);
  _optionalData.insert(_optionalData.end(), espPacket.begin() + 6 + dataSize, espPacket.begin() + 6 + dataSize + optionalSize);

  if (_type == Type::RADIO_ERP1 || _type == Type::RADIO_ERP2) {
    if (!_data.empty()) _rorg = (uint8_t)_data[0];
    if (_data.size() >= 6) {
      _senderAddress = (((int32_t)(uint8_t)_data[_data.size() - 5]) << 24) | (((int32_t)(uint8_t)_data[_data.size() - 4]) << 16) | (((int32_t)(uint8_t)_data[_data.size() - 3]) << 8) | ((int32_t)(uint8_t)_data[_data.size() - 2]);
      //Bit 7 tells us which hash function is used (0 for summation based checksum and 1 for CRC)
      //Bit 0 to 3 is the repeating status
      _status = (uint8_t)_data[_data.size() - 1];
      _repeatingStatus = (RepeatingStatus)(_status & 0x0F);
    }
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

EnOceanPacket::EnOceanPacket(Type type, uint8_t rorg, int32_t senderAddress, int32_t destinationAddress, const std::vector<uint8_t> &data) : _type(type), _rorg(rorg) {
  _senderAddress = senderAddress;
  _destinationAddress = ((destinationAddress & 0xFFFFFF80u) == (senderAddress & 0xFFFFFF80u) ? 0xFFFFFFFFu : destinationAddress);
  if (data.empty()) _data.reserve(20);
  else setData(data);
  if (_type == Type::RADIO_ERP1 || _type == Type::RADIO_ERP2) {
    _appendAddressAndStatus = true;
    if (data.empty() && rorg != 0xC5) _data.push_back(rorg);
  }
  if (type == Type::RADIO_ERP1) {
    _optionalData =
        std::vector<uint8_t>{3, (uint8_t)((_destinationAddress >> 24u) & 0xFFu), (uint8_t)((_destinationAddress >> 16u) & 0xFFu), (uint8_t)((_destinationAddress >> 8u) & 0xFFu), (uint8_t)(_destinationAddress & 0xFFu),
                             0xFF, 0};
  } else if (type == Type::RADIO_ERP2) {
    _optionalData = std::vector<uint8_t>{3, (uint8_t)0xFF};
  } else if (type == Type::REMOTE_MAN_COMMAND) {
    _optionalData = std::vector<uint8_t>{(uint8_t)((_destinationAddress >> 24u) & 0xFFu), (uint8_t)((_destinationAddress >> 16u) & 0xFFu), (uint8_t)((_destinationAddress >> 8u) & 0xFFu),
                                         (uint8_t)(_destinationAddress & 0xFFu), (uint8_t)((_senderAddress >> 24u) & 0xFFu), (uint8_t)((_senderAddress >> 16u) & 0xFFu),
                                         (uint8_t)((_senderAddress >> 8u) & 0xFFu), (uint8_t)(_senderAddress & 0xFFu), 0xFF, 0};
  }
}

EnOceanPacket::~EnOceanPacket() {
  _packet.clear();
  _data.clear();
  _optionalData.clear();
}

void EnOceanPacket::setData(const std::vector<uint8_t> &value, uint32_t offset) {
  _packet.clear();
  _data.clear();
  _data.insert(_data.end(), value.begin() + offset, value.end());
  if (!_data.empty() && _rorg == 0) _rorg = (uint8_t)_data.at(0);
}

std::vector<uint8_t> EnOceanPacket::getBinary() {
  try {
    if (!_packet.empty()) return _packet;
    if (_data.empty() && _optionalData.empty()) return {};
    _packet.reserve(7 + _data.size() + (_appendAddressAndStatus ? 5 : 0) + _optionalData.size());
    _packet.push_back(0x55);
    _packet.push_back((uint8_t)((_data.size() + (_appendAddressAndStatus ? 5 : 0)) >> 8u));
    _packet.push_back((uint8_t)((_data.size() + (_appendAddressAndStatus ? 5 : 0)) & 0xFFu));
    _packet.push_back((uint8_t)_optionalData.size());
    _packet.push_back((uint8_t)_type);
    _packet.push_back(0);
    _packet.insert(_packet.end(), _data.begin(), _data.end());
    if (_appendAddressAndStatus) {
      _packet.push_back((uint8_t)(_senderAddress >> 24u));
      _packet.push_back((uint8_t)((_senderAddress >> 16u) & 0xFFu));
      _packet.push_back((uint8_t)((_senderAddress >> 8u) & 0xFFu));
      _packet.push_back((uint8_t)(_senderAddress & 0xFFu));
      _packet.push_back(_rorg == 0xF6 ? 0x30 : 0);
    }
    _packet.insert(_packet.end(), _optionalData.begin(), _optionalData.end());
    _packet.push_back(0);
    return _packet;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return {};
}

std::vector<uint8_t> EnOceanPacket::getPosition(uint32_t position, uint32_t size) {
  try {
    return BaseLib::BitReaderWriter::getPosition(_data, position, size);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return {};
}

void EnOceanPacket::setPosition(uint32_t position, uint32_t size, const std::vector<uint8_t> &source) {
  try {
    BaseLib::BitReaderWriter::setPositionBE(position, size, _data, source);
  }
  catch (const std::exception &ex) {
    Gd::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::vector<std::shared_ptr<EnOceanPacket>> EnOceanPacket::getChunks(uint8_t sequence_counter) {
  try {
    std::vector<PEnOceanPacket> packets;

    if ((((unsigned)_destinationAddress != 0xFFFFFFFFu && _data.size() <= 5) || ((unsigned)_destinationAddress == 0xFFFFFFFFu && _data.size() <= 9) || _type == Type::REMOTE_MAN_COMMAND) && (_rorg != 0xC5 || _type == Type::REMOTE_MAN_COMMAND)) {
      auto result_packet = std::make_shared<EnOceanPacket>();
      *result_packet = *this;
      result_packet->setData(_data);
      packets.push_back(result_packet);
    } else {
      //Split packet
      packets.reserve((_data.size() / 8) + 2);

      std::vector<uint8_t> chunk;
      chunk.reserve(10);
      if (_rorg == 0xC5) {
        chunk.push_back(0xC5);
        chunk.push_back((sequence_counter << 6));
        chunk.push_back((_data.size() - 3) >> 1);
        chunk.push_back(((_data.size() - 3) << 7) | _data.at(0));
        if (_data.size() >= 7) chunk.insert(chunk.end(), _data.begin() + 1, _data.begin() + 7);
        else {
          chunk.insert(chunk.end(), _data.begin() + 1, _data.end());
          chunk.resize(10, 0);
        }
      } else {
        chunk.push_back(0x40);
        chunk.push_back((sequence_counter << 6));
        chunk.push_back((_data.size() - 1) >> 8);
        chunk.push_back(_data.size() - 1);
        chunk.insert(chunk.end(), _data.begin(), _data.begin() + 6);
      }
      auto result_packet = std::make_shared<EnOceanPacket>();
      *result_packet = *this;
      result_packet->setData(chunk);
      packets.push_back(result_packet);
      chunk.clear();

      uint8_t index = 2;
      chunk.push_back(_rorg == 0xC5 ? 0xC5 : 0x40);
      chunk.push_back((sequence_counter << 6) | 1);
      for (uint32_t i = _rorg == 0xC5 ? 7 : 6; i < _data.size(); i++) {
        chunk.push_back(_data.at(i));
        if (chunk.size() == 10) {
          auto result_packet2 = std::make_shared<EnOceanPacket>();
          *result_packet2 = *this;
          result_packet2->setData(chunk);
          packets.push_back(result_packet2);
          chunk.clear();
          chunk.push_back(_rorg == 0xC5 ? 0xC5 : 0x40);
          chunk.push_back((sequence_counter << 6) | index);
          index++;
        }
      }

      if (chunk.size() > 2) {
        if (_rorg == 0xC5) chunk.resize(10, 0);

        auto result_packet2 = std::make_shared<EnOceanPacket>();
        *result_packet2 = *this;
        result_packet2->setData(chunk);
        packets.push_back(result_packet2);
      }
    }

    return packets;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return {};
}

}
