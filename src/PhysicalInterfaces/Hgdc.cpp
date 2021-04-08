/* Copyright 2013-2019 Homegear GmbH */

#include "../Gd.h"
#include "Hgdc.h"

namespace EnOcean {

Hgdc::Hgdc(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IEnOceanInterface(settings) {
  _settings = settings;
  _out.init(Gd::bl);
  _out.setPrefix(Gd::out.getPrefix() + "EnOcean HGDC \"" + settings->id + "\": ");

  signal(SIGPIPE, SIG_IGN);

  _stopped = true;
}

Hgdc::~Hgdc() {
  stopListening();
  _bl->threadManager.join(_initThread);
}

void Hgdc::startListening() {
  try {
    Gd::bl->hgdc->unregisterPacketReceivedEventHandler(_packetReceivedEventHandlerId);
    _packetReceivedEventHandlerId = Gd::bl->hgdc->registerPacketReceivedEventHandler(MY_FAMILY_ID,
                                                                                     std::function < void(int64_t,
    const std::string &, const std::vector<uint8_t> &)>(std::bind(&Hgdc::processPacket,
                                                                  this,
                                                                  std::placeholders::_1,
                                                                  std::placeholders::_2,
                                                                  std::placeholders::_3)));
    IPhysicalInterface::startListening();

    _stopped = false;
    init();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Hgdc::stopListening() {
  try {
    _stopped = true;
    IPhysicalInterface::stopListening();
    Gd::bl->hgdc->unregisterPacketReceivedEventHandler(_packetReceivedEventHandlerId);
    _packetReceivedEventHandlerId = -1;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Hgdc::init() {
  try {
    _initComplete = false;
    if (!Gd::bl->hgdc->isMaster()) {
      _out.printInfo("Info: Not initializing module as we are not master.");
      _initComplete = true;
      return;
    }

    uint8_t remainingChanges = 0;
    for (int32_t i = 0; i < 10; i++) {
      std::vector<uint8_t> data{0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00};
      addCrc8(data);
      std::vector<uint8_t> response;
      getResponse(0x02, data, response);
      if (response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0) {
        if (i < 9) continue;
        _out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(response));
        _stopped = true;
        return;
      }
      _baseAddress = ((uint32_t)(uint8_t)
      response[7] << 24) | ((uint32_t)(uint8_t)
      response[8] << 16) | ((uint32_t)(uint8_t)
      response[9] << 8) | (uint8_t)response[10];
      remainingChanges = response[11];
      break;
    }

    std::string appVersion;
    std::string apiVersion;
    uint32_t chipId = 0;
    std::string appDescription;
    for (int32_t i = 0; i < 10; i++) {
      // Get info
      std::vector<uint8_t> data{0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x03, 0x00};
      addCrc8(data);
      std::vector<uint8_t> response;
      getResponse(0x02, data, response);
      if (response.size() != 40 || response[1] != 0 || response[2] != 33 || response[3] != 0 || response[6] != 0) {
        if (i < 9) continue;
        _out.printError("Error reading info from device: " + BaseLib::HelperFunctions::getHexString(response));
        _stopped = true;
        return;
      }
      appVersion = std::to_string(response[7]) + '.' + std::to_string(response[8]) + '.' + std::to_string(response[9]) + '.' + std::to_string(response[10]);
      apiVersion = std::to_string(response[11]) + '.' + std::to_string(response[12]) + '.' + std::to_string(response[13]) + '.' + std::to_string(response[14]);
      chipId = ((uint32_t)(uint8_t)
      response[15] << 24) | ((uint32_t)(uint8_t)
      response[16] << 16) | ((uint32_t)(uint8_t)
      response[17] << 8) | (uint8_t)response[18];
      appDescription.insert(appDescription.end(), response.begin() + 23, response.begin() + 23 + 16);
      appDescription.resize(strlen(appDescription.c_str())); //Trim to null terminator
      break;
    }

    _out.printInfo(
        "Info: Init complete.\n  - Base address: 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + " (remaining changes: " + std::to_string(remainingChanges) + ")\n  - App version: " + appVersion + "\n  - API version: " + apiVersion
            + "\n  - Chip address: 0x" + BaseLib::HelperFunctions::getHexString(chipId, 8) + "\n  - App description: " + appDescription);

    auto roamingSetting = Gd::family->getFamilySetting("forcebaseid");
    if (roamingSetting) {
      uint32_t newBaseId = (uint32_t)roamingSetting->integerValue & 0xFFFFFF80;
      if (newBaseId >= 0xFF800000) {
        setBaseAddress(newBaseId);
      } else {
        Gd::out.printWarning(R"(Warning: Invalid base ID specified in setting "forceBaseId" in "enocean.conf".)");
      }
    }

    _initComplete = true;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Hgdc::reset() {
  try {
    Gd::bl->hgdc->moduleReset(_settings->serialNumber);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

int32_t Hgdc::setBaseAddress(uint32_t value) {
  try {
    if ((value & 0xFF000000) != 0xFF000000) {
      _out.printError("Error: Could not set base address. Address must start with 0xFF.");
      return -1;
    }

    Gd::out.printInfo("Info: Changing base address to: " + BaseLib::HelperFunctions::getHexString(value));

    std::vector<uint8_t> response;

    {
      // Set address - only possible 10 times, Must start with "0xFF"
      std::vector<uint8_t> data{0x55, 0x00, 0x05, 0x00, 0x05, 0x00, 0x07, (uint8_t)(value >> 24), (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF), 0x00};
      addCrc8(data);
      getResponse(0x02, data, response);
      if (response.size() != 8 || response[1] != 0 || response[2] != 1 || response[3] != 0 || response[4] != 2 || response[6] != 0) {
        _out.printError("Error setting address on device: " + BaseLib::HelperFunctions::getHexString(response));
        _stopped = true;
        return -1;
      }
    }

    for (int32_t i = 0; i < 3; i++) {
      std::vector<uint8_t> data{0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00};
      addCrc8(data);
      getResponse(0x02, data, response);
      if (response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0) {
        if (i < 9) continue;
        _out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(data));
        _stopped = true;
        return -1;
      }
      _baseAddress = ((int32_t)(uint8_t)
      response[7] << 24) | ((int32_t)(uint8_t)
      response[8] << 16) | ((int32_t)(uint8_t)
      response[9] << 8) | (uint8_t)response[10];
      break;
    }

    _out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ". Remaining changes: " + std::to_string(response[11]));

    return response[11];
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return -1;
}

IEnOceanInterface::DutyCycleInfo Hgdc::getDutyCycleInfo() {
  try {
    std::vector<uint8_t> response;
    for (int32_t i = 0; i < 10; i++) {
      std::vector<uint8_t> data{0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x23, 0x00};
      addCrc8(data);
      getResponse(0x02, data, response);
      if (response.size() != 15 || response[1] != 0 || response[2] != 8 || response[3] != 0 || response[6] != 0) {
        if (i < 9) continue;
        _out.printError("Error reading duty cycle information from device: " + BaseLib::HelperFunctions::getHexString(data));
        _stopped = true;
        return DutyCycleInfo();
      }

      DutyCycleInfo info;
      info.dutyCycleUsed = response[7];
      info.slotPeriod = (((uint32_t)response[9]) << 8) | response[10];
      info.timeLeftInSlot = (((uint32_t)response[11]) << 8) | response[12];

      return info;
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return DutyCycleInfo();
}

bool Hgdc::sendEnoceanPacket(const PEnOceanPacket &packet) {
  try {
    IEnOceanInterface::sendEnoceanPacket(packet);

    std::vector<uint8_t> data = std::move(packet->getBinary());
    addCrc8(data);
    std::vector<uint8_t> response;
    getResponse(0x02, data, response);
    if (response.size() != 8 || (response.size() >= 7 && response[6] != 0)) {
      if (response.size() >= 7 && response[6] != 0) {
        auto statusIterator = _responseStatusCodes.find(response[6]);
        if (statusIterator != _responseStatusCodes.end()) _out.printError("Error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\": " + statusIterator->second);
        else _out.printError("Unknown error (" + std::to_string(response[6]) + ") sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
      } else _out.printError("Unknown error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
      return false;
    }
    _lastPacketSent = BaseLib::HelperFunctions::getTime();
    return true;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

void Hgdc::rawSend(std::vector<uint8_t> &packet) {
  try {
    IEnOceanInterface::rawSend(packet);
    if (!Gd::bl->hgdc->sendPacket(_settings->serialNumber, packet)) {
      _out.printError("Error sending packet " + BaseLib::HelperFunctions::getHexString(packet) + ".");
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Hgdc::processPacket(int64_t familyId, const std::string &serialNumber, const std::vector<uint8_t> &data) {
  try {
    if (serialNumber != _settings->serialNumber) return;

    if (data.size() < 6) {
      _out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
      return;
    }

    {
      uint8_t crc8 = 0;
      for (int32_t i = 1; i < 5; i++) {
        crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
      }
      if (crc8 != data[5]) {
        _out.printError("Error: CRC (0x" + BaseLib::HelperFunctions::getHexString(crc8, 2) + ") failed for header: " + BaseLib::HelperFunctions::getHexString(data));
        return;
      }

      crc8 = 0;
      for (uint32_t i = 6; i < data.size() - 1; i++) {
        crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
      }
      if (crc8 != data.back()) {
        _out.printError("Error: CRC failed for packet: " + BaseLib::HelperFunctions::getHexString(data));
        return;
      }
    }

    _lastPacketReceived = BaseLib::HelperFunctions::getTime();

    if (checkForSerialRequest(data)) return;

    auto packet = std::make_shared<EnOceanPacket>(data);
    if (checkForEnOceanRequest(packet)) return;
    if (packet->getType() == EnOceanPacket::Type::RADIO_ERP1 || packet->getType() == EnOceanPacket::Type::RADIO_ERP2) {
      if ((packet->senderAddress() & 0xFFFFFF80) == _baseAddress) _out.printInfo("Info: Ignoring packet from myself: " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));
      else raisePacketReceived(packet);
    } else {
      _out.printInfo("Info: Not processing packet: " + BaseLib::HelperFunctions::getHexString(data));
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

}