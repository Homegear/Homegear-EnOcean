/* Copyright 2013-2019 Homegear GmbH */

#include "IEnOceanInterface.h"
#include "../Gd.h"
#include "../EnOceanPacket.h"

namespace EnOcean {

IEnOceanInterface::IEnOceanInterface(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(Gd::bl, Gd::family->getFamily(), settings) {
  _bl = Gd::bl;

  if (settings->listenThreadPriority == -1) {
    settings->listenThreadPriority = 0;
    settings->listenThreadPolicy = SCHED_OTHER;
  }

  _responseStatusCodes[0] = "RET_OK";
  _responseStatusCodes[1] = "RET_ERROR";
  _responseStatusCodes[2] = "RET_NOT_SUPPORTED";
  _responseStatusCodes[3] = "RET_WRONG_PARAM";
  _responseStatusCodes[4] = "RET_OPERATION_DENIED";
  _responseStatusCodes[5] = "RET_LOCK_SET - Duty cycle limit reached.";
  _responseStatusCodes[6] = "RET_BUFFER_TOO_SMALL";
  _responseStatusCodes[7] = "RET_NO_FREE_BUFFER";
}

IEnOceanInterface::~IEnOceanInterface() {

}

void IEnOceanInterface::getResponse(uint8_t packetType, std::vector<uint8_t> &requestPacket, std::vector<uint8_t> &responsePacket) {
  try {
    if (_stopped) return;
    responsePacket.clear();

    std::shared_ptr<SerialRequest> request = std::make_shared<SerialRequest>();
    std::unique_lock<std::mutex> getResponseGuard(_getResponseMutex, std::defer_lock);
    std::unique_lock<std::mutex> requestsGuard(_serialRequestsMutex, std::defer_lock);
    std::lock(getResponseGuard, requestsGuard);

    _serialRequests[packetType] = request;
    requestsGuard.unlock();

    try {
      rawSend(requestPacket);
    }
    catch (const BaseLib::SocketOperationException &ex) {
      _out.printError("Error sending packet: " + std::string(ex.what()));
      requestsGuard.lock();
      _serialRequests.erase(packetType);
      requestsGuard.unlock();
      return;
    }

    std::unique_lock<std::mutex> lock(request->mutex);
    if (!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(1000), [&] { return request->mutexReady; })) {
      _out.printError("Error: No serial ACK received to packet: " + BaseLib::HelperFunctions::getHexString(requestPacket));
    }
    responsePacket = request->response;

    requestsGuard.lock();
    _serialRequests.erase(packetType);
    requestsGuard.unlock();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

bool IEnOceanInterface::checkForSerialRequest(const std::vector<uint8_t> &packet) {
  try {
    uint8_t packetType = packet.at(4);
    std::unique_lock<std::mutex> requestsGuard(_serialRequestsMutex);
    auto requestIterator = _serialRequests.find(packetType);
    if (requestIterator != _serialRequests.end()) {
      auto request = requestIterator->second;
      requestsGuard.unlock();
      request->response = packet;
      {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->mutexReady = true;
      }
      request->conditionVariable.notify_all();
      return true;
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

bool IEnOceanInterface::checkForEnOceanRequest(PEnOceanPacket &packet) {
  try {
    std::unique_lock<std::mutex> requestsGuard(_enoceanRequestsMutex);
    auto requestIterator = _enoceanRequests.find(packet->senderAddress());
    if (requestIterator != _enoceanRequests.end()) {
      for (auto &element : requestIterator->second) {
        auto request = element.second;
        if (request->filterType == EnOceanRequestFilterType::remoteManagementFunction) {
          bool found = false;
          for (auto &filterData : request->filterData) {
            if (filterData.size() >= 2) {
              uint16_t function = (uint16_t)((uint16_t)filterData[0] << 8u) | filterData[1];
              if (packet->getRemoteManagementFunction() != function) continue;
              if (request->filterData.size() >= 4) {
                uint16_t manufacturer = (uint16_t)((uint16_t)filterData[2] << 8u) | filterData[3];
                if (packet->getRemoteManagementManufacturer() != manufacturer) continue;
              }
              found = true;
            }
            if (found) break;
          }
          if (!found) continue;
        }
        requestsGuard.unlock();

        _out.printInfo("Info: Response packet received (RSSI: " + std::to_string(packet->getRssi()) + " dBm): " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));

        request->response = packet;
        {
          std::lock_guard<std::mutex> lock(request->mutex);
          request->mutexReady = true;
        }
        request->conditionVariable.notify_all();
        return true;
      }
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

void IEnOceanInterface::addCrc8(std::vector<uint8_t> &packet) {
  try {
    if (packet.size() < 6) return;

    uint8_t crc8 = 0;
    for (int32_t i = 1; i < 5; i++) {
      crc8 = _crc8Table[crc8 ^ (uint8_t)packet[i]];
    }
    packet[5] = crc8;

    crc8 = 0;
    for (uint32_t i = 6; i < packet.size() - 1; i++) {
      crc8 = _crc8Table[crc8 ^ (uint8_t)packet[i]];
    }
    packet.back() = crc8;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void IEnOceanInterface::raisePacketReceived(std::shared_ptr<BaseLib::Systems::Packet> packet) {
  try {
    PEnOceanPacket myPacket(std::dynamic_pointer_cast<EnOceanPacket>(packet));
    if (!myPacket) return;

    if (myPacket->senderAddress() != (int32_t)_baseAddress) {
      std::lock_guard<std::mutex> rssiGuard(_rssiMutex);
      if (_rssi.size() > 10000 || _wildcardRssi.size() > 10000) {
        _out.printWarning("Warning: More than 10000 RSSI values are stored. Clearing them...");
        _rssi.clear();
        _wildcardRssi.clear();
      }

      auto rssiIterator = _rssi.find(myPacket->senderAddress());
      if (rssiIterator == _rssi.end()) rssiIterator = _rssi.emplace(myPacket->senderAddress(), DeviceInfo()).first;
      rssiIterator->second.rssi = myPacket->getRssi();
      //rssiIterator->second.packetReceivedTimes.push(myPacket->timeReceived());
      //while(rssiIterator->second.packetReceivedTimes.size() > 5) rssiIterator->second.packetReceivedTimes.pop();

      rssiIterator = _wildcardRssi.find(myPacket->senderAddress());
      if (rssiIterator == _wildcardRssi.end()) rssiIterator = _wildcardRssi.emplace(myPacket->senderAddress() & 0xFFFFFF80, DeviceInfo()).first;
      rssiIterator->second.rssi = myPacket->getRssi();
      //rssiIterator->second.packetReceivedTimes.push(myPacket->timeReceived());
      //while(rssiIterator->second.packetReceivedTimes.size() > 5) rssiIterator->second.packetReceivedTimes.pop();
    }

    BaseLib::Systems::IPhysicalInterface::raisePacketReceived(packet);
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

int32_t IEnOceanInterface::getRssi(int32_t address, bool wildcardPeer) {
  try {
    std::lock_guard<std::mutex> rssiGuard(_rssiMutex);
    if (wildcardPeer) {
      auto rssiIterator = _wildcardRssi.find(address & 0xFFFFFF80);
      if (rssiIterator != _wildcardRssi.end()) return rssiIterator->second.rssi;
    } else {
      auto rssiIterator = _rssi.find(address);
      if (rssiIterator != _rssi.end()) return rssiIterator->second.rssi;
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return 0;
}

void IEnOceanInterface::decrementRssi(uint32_t address, bool wildcardPeer) {
  try {
    std::lock_guard<std::mutex> rssiGuard(_rssiMutex);
    if (wildcardPeer) {
      auto rssiIterator = _wildcardRssi.find(address & 0xFFFFFF80u);
      if (rssiIterator != _wildcardRssi.end()) rssiIterator->second.rssi -= 5;
    } else {
      auto rssiIterator = _rssi.find(address);
      if (rssiIterator != _rssi.end()) rssiIterator->second.rssi -= 5;
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void IEnOceanInterface::rawSend(std::vector<uint8_t> &packet) {
  try {
    if (packet.size() > 7 && packet.at(6) == 0xD1) return; //Send MSC packets immediately (e. g. firmware update)
    std::lock_guard<std::mutex> rawSendGuard(_rawSendMutex);
    auto time = BaseLib::HelperFunctions::getTime();
    if (time - _lastRawPacketSent < 80) {
      std::this_thread::sleep_for(std::chrono::milliseconds((_lastRawPacketSent + BaseLib::HelperFunctions::getRandomNumber(80, 150) - time)));
    }
    _lastRawPacketSent = BaseLib::HelperFunctions::getTime();
  } catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

bool IEnOceanInterface::sendEnoceanPacket(const PEnOceanPacket &packet) {
  try {
    if (_stopped || !packet) return {};

    uint8_t sequence_counter = _sequence_counter;
    sequence_counter++;
    if (sequence_counter > 3 || sequence_counter < 1) sequence_counter = 1;
    _sequence_counter = sequence_counter;

    return sendEnoceanPacket(packet->getChunks(sequence_counter));
  } catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

PEnOceanPacket IEnOceanInterface::sendAndReceivePacket(const PEnOceanPacket &packet, uint32_t device_enocean_id, uint32_t retries, EnOceanRequestFilterType filter_type, const std::vector<std::vector<uint8_t>> &filter_data, uint32_t timeout) {
  try {
    if (_stopped || !packet) return {};

    uint8_t sequence_counter = _sequence_counter;
    sequence_counter++;
    if (sequence_counter > 3 || sequence_counter < 1) sequence_counter = 1;
    _sequence_counter = sequence_counter;

    return sendAndReceivePacket(packet->getChunks(sequence_counter), device_enocean_id, retries, filter_type, filter_data, timeout);
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return {};
}

PEnOceanPacket IEnOceanInterface::sendAndReceivePacket(const std::vector<PEnOceanPacket> &packets, uint32_t device_enocean_id, uint32_t retries, EnOceanRequestFilterType filter_type, const std::vector<std::vector<uint8_t>> &filter_data, uint32_t timeout) {
  try {
    if (_stopped || packets.empty() || !packets.at(0)) return {};

    std::shared_ptr<EnOceanRequest> request = std::make_shared<EnOceanRequest>();
    request->filterType = filter_type;
    request->filterData = filter_data;

    std::unique_lock<std::mutex> requestsGuard(_enoceanRequestsMutex);
    uint32_t packetId = _packetId++;
    _enoceanRequests[device_enocean_id][packetId] = request;
    requestsGuard.unlock();

    std::unique_lock<std::mutex> lock(request->mutex);

    for (uint32_t i = 0; i < retries + 1; i++) {
      if (!sendEnoceanPacket(packets)) {
        requestsGuard.lock();
        auto requestsIterator = _enoceanRequests.find(device_enocean_id);
        if (requestsIterator != _enoceanRequests.end()) {
          requestsIterator->second.erase(packetId);
          if (requestsIterator->second.empty()) _enoceanRequests.erase(device_enocean_id);
        }
        requestsGuard.unlock();
        return {};
      }

      if (!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return request->mutexReady; })) {
        if (i < retries) _out.printInfo("Info: No EnOcean response received to packet: " + BaseLib::HelperFunctions::getHexString(packets.at(0)->getBinary()) + ". Retrying...");
        else _out.printError("Error: No EnOcean response received to packet: " + BaseLib::HelperFunctions::getHexString(packets.at(0)->getBinary()));
      }

      if (request->response) break;
    }

    requestsGuard.lock();
    auto requestsIterator = _enoceanRequests.find(device_enocean_id);
    if (requestsIterator != _enoceanRequests.end()) {
      requestsIterator->second.erase(packetId);
      if (requestsIterator->second.empty()) _enoceanRequests.erase(device_enocean_id);
    }
    requestsGuard.unlock();

    return request->response;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return {};
}

}
