/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanCentral.h"
#include "Gd.h"
#include "EnOceanPackets.h"
#include "RemanFeatures.h"

#include <iomanip>
#include <memory>

namespace EnOcean {

EnOceanCentral::EnOceanCentral(ICentralEventSink *eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, Gd::bl, eventHandler) {
  init();
}

EnOceanCentral::EnOceanCentral(uint32_t deviceID, std::string serialNumber, ICentralEventSink *eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, Gd::bl, deviceID, serialNumber, -1, eventHandler) {
  init();
}

EnOceanCentral::~EnOceanCentral() {
  dispose();
}

void EnOceanCentral::dispose(bool wait) {
  try {
    if (_disposing) return;
    _disposing = true;
    {
      std::lock_guard<std::mutex> pairingModeGuard(_pairingInfo.pairingModeThreadMutex);
      _pairingInfo.stopPairingModeThread = true;
      _bl->threadManager.join(_pairingInfo.pairingModeThread);
    }

    {
      std::lock_guard<std::mutex> updateFirmwareThreadGuard(_updateFirmwareThreadMutex);
      _bl->threadManager.join(_updateFirmwareThread);
    }

    _stopWorkerThread = true;
    Gd::out.printDebug("Debug: Waiting for worker thread of device " + std::to_string(_deviceId) + "...");
    _bl->threadManager.join(_workerThread);
    _bl->threadManager.join(_pingWorkerThread);

    Gd::out.printDebug("Removing device " + std::to_string(_deviceId) + " from physical device's event queue...");
    Gd::interfaces->removeEventHandlers();

    _wildcardPeers.clear();
    _peersById.clear();
    _peersBySerial.clear();
    _peers.clear();
    _sniffedPackets.clear();
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void EnOceanCentral::init() {
  try {
    if (_initialized) return; //Prevent running init two times
    _initialized = true;
    _pairing = false;
    _pairingInfo.stopPairingModeThread = false;
    _stopWorkerThread = false;
    _timeLeftInPairingMode = 0;

    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("getMeshingInfo",
                                                                                                                                                                    std::bind(&EnOceanCentral::getMeshingInfo,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("resetMeshingTables",
                                                                                                                                                                    std::bind(&EnOceanCentral::resetMeshingTables,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("remanGetPathInfoThroughPing",
                                                                                                                                                                    std::bind(&EnOceanCentral::remanGetPathInfoThroughPing,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("remanPing",
                                                                                                                                                                    std::bind(&EnOceanCentral::remanPing,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("remanSetCode",
                                                                                                                                                                    std::bind(&EnOceanCentral::remanSetCode,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("remanSetRepeaterFunctions",
                                                                                                                                                                    std::bind(&EnOceanCentral::remanSetRepeaterFunctions,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("remanSetRepeaterFilter",
                                                                                                                                                                    std::bind(&EnOceanCentral::remanSetRepeaterFilter,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));
    _localRpcMethods.insert(std::pair<std::string, std::function<BaseLib::PVariable(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters)>>("remanSetSecurityProfile",
                                                                                                                                                                    std::bind(&EnOceanCentral::remanSetSecurityProfile,
                                                                                                                                                                              this,
                                                                                                                                                                              std::placeholders::_1,
                                                                                                                                                                              std::placeholders::_2)));

    Gd::interfaces->addEventHandlers((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink *)this);

    Gd::bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &EnOceanCentral::worker, this);
    Gd::bl->threadManager.start(_pingWorkerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &EnOceanCentral::pingWorker, this);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void EnOceanCentral::worker() {
  try {
    std::chrono::milliseconds sleepingTime(100);
    uint32_t counter = 0;
    uint64_t lastPeer;
    lastPeer = 0;
    int64_t nextFirmwareUpdateCheck = BaseLib::HelperFunctions::getTime() + BaseLib::HelperFunctions::getRandomNumber(10000, 60000);

    while (!_stopWorkerThread && !Gd::bl->shuttingDown) {
      try {
        std::this_thread::sleep_for(sleepingTime);
        if (_stopWorkerThread || Gd::bl->shuttingDown) return;
        if (counter > 1000) {
          counter = 0;

          {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            if (!_peersById.empty()) {
              int32_t windowTimePerPeer = _bl->settings.workerThreadWindow() / 8 / _peersById.size();
              sleepingTime = std::chrono::milliseconds(windowTimePerPeer);
            }
          }

          if (!Gd::bl->slaveMode) {
            // {{{ Check for and install firmware updates
            if (BaseLib::HelperFunctions::getTime() >= nextFirmwareUpdateCheck) {
              Gd::out.printInfo("Info: Checking for firmware updates.");
              auto peers = getPeers();
              std::vector<uint64_t> peersToUpdate;
              peersToUpdate.reserve(peers.size());
              for (auto &peer : peers) {
                if (peer->firmwareUpdateAvailable()) {
                  Gd::out.printInfo("Info: Adding " + std::to_string(peer->getID()) + " to list of peers to update.");
                  peersToUpdate.emplace_back(peer->getID());
                }
              }
              if (!peersToUpdate.empty()) updateFirmwares(peersToUpdate);
              nextFirmwareUpdateCheck = BaseLib::HelperFunctions::getTime() + BaseLib::HelperFunctions::getRandomNumber(1200000, 2400000);
            }
            // }}}
          }
        }

        if (!Gd::bl->slaveMode) {
          //{{{ Execute peer workers
          std::shared_ptr<EnOceanPeer> peer;

          {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            if (!_peersById.empty()) {
              if (!_peersById.empty()) {
                auto nextPeer = _peersById.find(lastPeer);
                if (nextPeer != _peersById.end()) {
                  nextPeer++;
                  if (nextPeer == _peersById.end()) nextPeer = _peersById.begin();
                } else nextPeer = _peersById.begin();
                lastPeer = nextPeer->first;
                peer = std::dynamic_pointer_cast<EnOceanPeer>(nextPeer->second);
              }
            }
          }

          if (peer && !peer->deleting) {
            peer->worker();
          }
          //}}}
        }
        Gd::interfaces->worker();
        counter++;
      }
      catch (const std::exception &ex) {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void EnOceanCentral::pingWorker() {
  try {
    std::chrono::milliseconds sleepingTime(100);
    uint32_t counter = 0;
    uint64_t lastPeer;
    lastPeer = 0;

    while (!_stopWorkerThread && !Gd::bl->shuttingDown) {
      try {
        std::this_thread::sleep_for(sleepingTime);
        if (_stopWorkerThread || Gd::bl->shuttingDown) return;
        if (counter > 1000) {
          counter = 0;

          {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            if (!_peersById.empty()) {
              int32_t windowTimePerPeer = _bl->settings.workerThreadWindow() / 8 / _peersById.size();
              sleepingTime = std::chrono::milliseconds(windowTimePerPeer);
            }
          }
        }

        if (!Gd::bl->slaveMode) {
          //{{{ Execute peer workers
          std::shared_ptr<EnOceanPeer> peer;

          {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            if (!_peersById.empty()) {
              if (!_peersById.empty()) {
                auto nextPeer = _peersById.find(lastPeer);
                if (nextPeer != _peersById.end()) {
                  nextPeer++;
                  if (nextPeer == _peersById.end()) nextPeer = _peersById.begin();
                } else nextPeer = _peersById.begin();
                lastPeer = nextPeer->first;
                peer = std::dynamic_pointer_cast<EnOceanPeer>(nextPeer->second);
              }
            }
          }

          if (peer && !peer->deleting) {
            peer->pingWorker();

            if (BaseLib::HelperFunctions::getTimeSeconds() > peer->getNextMeshingCheck()) {
              peer->setNextMeshingCheck();
              if (peer->getRssiStatus() == EnOceanPeer::RssiStatus::bad) {
                // {{{ Find peer that has best connection to this peer
                Gd::out.printInfo("Info: Peer " + std::to_string(peer->getID()) + " has bad RSSI. Trying to find a repeater.");
                auto peers = getPeers();
                int32_t bestRssi = 0;
                std::shared_ptr<EnOceanPeer> bestRepeater;
                for (auto &iterator : peers) {
                  if (iterator->getID() == peer->getID()) continue;
                  auto repeaterPeer = std::dynamic_pointer_cast<EnOceanPeer>(iterator);
                  if (!repeaterPeer) continue;
                  auto remanFeatures = repeaterPeer->getRemanFeatures();
                  if (!remanFeatures || !remanFeatures->kMeshingRepeater || !repeaterPeer->hasFreeMeshingTableSlot()) continue;
                  auto rssi = repeaterPeer->remanGetPathInfoThroughPing(peer->getAddress());
                  if (rssi != 0 && (rssi > bestRssi || bestRssi == 0)) {
                    bestRssi = rssi;
                    bestRepeater = repeaterPeer;
                  }
                  if (rssi >= -70) break; //Good reception
                }
                //}}}

                //{{{ Enable repeating if required
                if (bestRssi != 0 && bestRepeater) {
                  Gd::out.printInfo("Info: Found peer " + std::to_string(bestRepeater->getID()) + " as repeater for peer " + std::to_string(peer->getID()) + ". RSSI from repeater to peer is: " + std::to_string(bestRssi) + " dBm.");
                  bool error = false;
                  if (peer->getRepeaterId() != 0) {
                    auto oldRepeater = getPeer(peer->getRepeaterId());
                    if (oldRepeater) {
                      bool unreach = oldRepeater->serviceMessages->getUnreach();
                      for (int32_t i = 0; i < 3; i++) {
                        if (oldRepeater->removeRepeatedAddress(peer->getAddress()) && !unreach) break;
                        if (i == 2) error = true;
                      }
                    }
                  }
                  if (!error) {
                    peer->setRepeaterId(bestRepeater->getID());
                    bestRepeater->addRepeatedAddress(peer->getAddress());
                  }
                } else {
                  Gd::out.printInfo("Info: No repeater found for peer " + std::to_string(peer->getID()));
                }
                // }}}
              }
            }
          }
          //}}}
        }
        counter++;
      }
      catch (const std::exception &ex) {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void EnOceanCentral::loadPeers() {
  try {
    std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
    for (BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row) {
      int32_t peerID = row->second.at(0)->intValue;
      Gd::out.printMessage("Loading EnOcean peer " + std::to_string(peerID));
      std::shared_ptr<EnOceanPeer> peer(new EnOceanPeer(peerID, row->second.at(2)->intValue, row->second.at(3)->textValue, _deviceId, this));
      if (!peer->load(this)) continue;
      if (!peer->getRpcDevice()) continue;
      std::lock_guard<std::mutex> peersGuard(_peersMutex);
      if (!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
      _peersById[peerID] = peer;
      _peers[peer->getAddress()].push_back(peer);
      if (peer->getRpcDevice()->addressSize == 25) {
        std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
        _wildcardPeers[peer->getAddress()].push_back(peer);
      }
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::getPeer(uint64_t id) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    if (_peersById.find(id) != _peersById.end()) {
      std::shared_ptr<EnOceanPeer> peer(std::dynamic_pointer_cast<EnOceanPeer>(_peersById.at(id)));
      return peer;
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<EnOceanPeer>();
}

std::list<PMyPeer> EnOceanCentral::getPeer(int32_t address) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    auto peersIterator = _peers.find(address);
    if (peersIterator != _peers.end()) {
      return peersIterator->second;
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::list<PMyPeer>();
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::getPeer(std::string serialNumber) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    if (_peersBySerial.find(serialNumber) != _peersBySerial.end()) {
      std::shared_ptr<EnOceanPeer> peer(std::dynamic_pointer_cast<EnOceanPeer>(_peersBySerial.at(serialNumber)));
      return peer;
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<EnOceanPeer>();
}

bool EnOceanCentral::peerExists(uint64_t id) {
  return ICentral::peerExists(id);
}

bool EnOceanCentral::peerExists(std::string serialNumber) {
  return ICentral::peerExists(serialNumber);
}

bool EnOceanCentral::peerExists(int32_t address, uint64_t eep) {
  std::list<PMyPeer> peers = getPeer(address);
  if (eep == 0) return !peers.empty();
  for (auto &peer : peers) {
    if (peer->getDeviceType() == eep) return true;
  }
  return false;
}

int32_t EnOceanCentral::getFreeRfChannel(const std::string &interfaceId) {
  try {
    std::vector<std::shared_ptr<BaseLib::Systems::Peer>> peers = getPeers();
    std::set<int32_t> usedChannels;
    for (std::vector<std::shared_ptr<BaseLib::Systems::Peer >>::iterator i = peers.begin(); i != peers.end();
         ++i) {
      PMyPeer peer(std::dynamic_pointer_cast<EnOceanPeer>(*i));
      if (!peer) continue;
      if (peer->getPhysicalInterfaceId() != interfaceId) continue;
      std::vector<int32_t> channels = peer->getRfChannels();
      usedChannels.insert(channels.begin(), channels.end());
    }
    //Channel 0 and channel 1 are reserved
    for (int32_t i = 2; i < 128; ++i) {
      if (usedChannels.find(i) == usedChannels.end()) return i;
    }
    return -1;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return -1;
}

bool EnOceanCentral::onPacketReceived(std::string &senderId, std::shared_ptr<BaseLib::Systems::Packet> packet) {
  try {
    if (_disposing) return false;
    PEnOceanPacket myPacket(std::dynamic_pointer_cast<EnOceanPacket>(packet));
    if (!myPacket) return false;

    if (_bl->debugLevel >= 4)
      _bl->out.printInfo(BaseLib::HelperFunctions::getTimeString(myPacket->getTimeReceived()) + " EnOcean packet received (" + senderId + std::string(", RSSI: ") + std::to_string(myPacket->getRssi()) + " dBm" + "): "
                             + BaseLib::HelperFunctions::getHexString(myPacket->getBinary()) + " - Sender address (= EnOcean ID): 0x" + BaseLib::HelperFunctions::getHexString(myPacket->senderAddress(), 8));

    std::list<PMyPeer> peers = getPeer(myPacket->senderAddress());
    if (peers.empty()) {
      std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
      auto wildcardPeersIterator = _wildcardPeers.find(myPacket->senderAddress() & 0xFFFFFF80);
      if (wildcardPeersIterator != _wildcardPeers.end()) peers = wildcardPeersIterator->second;
    }
    if (peers.empty()) {
      if (_sniff) {
        std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
        auto sniffedPacketsIterator = _sniffedPackets.find(myPacket->senderAddress());
        if (sniffedPacketsIterator == _sniffedPackets.end()) {
          _sniffedPackets[myPacket->senderAddress()].reserve(100);
          _sniffedPackets[myPacket->senderAddress()].push_back(myPacket);
        } else {
          if (sniffedPacketsIterator->second.size() + 1 > sniffedPacketsIterator->second.capacity()) sniffedPacketsIterator->second.reserve(sniffedPacketsIterator->second.capacity() + 100);
          sniffedPacketsIterator->second.push_back(myPacket);
        }
      }

      PairingData pairingData;

      {
        std::lock_guard<std::mutex> pairingDataGuard(_pairingInfo.pairingDataMutex);
        pairingData = _pairingData;
      }
      if (_pairing && (pairingData.pairingInterface.empty() || pairingData.pairingInterface == senderId)) {
        return handlePairingRequest(senderId, myPacket, pairingData);
      }
      return false;
    }

    bool result = false;
    bool unpaired = true;
    for (auto &peer : peers) {
      auto roamingSetting = Gd::family->getFamilySetting("roaming");
      bool roaming = !roamingSetting || roamingSetting->integerValue;
      if (roaming && senderId != peer->getPhysicalInterfaceId() && peer->getPhysicalInterface()->getBaseAddress() == Gd::interfaces->getInterface(senderId)->getBaseAddress()) {
        if (myPacket->getRssi() > peer->getPhysicalInterface()->getRssi(peer->getAddress(), peer->isWildcardPeer()) + 6) {
          peer->getPhysicalInterface()->decrementRssi(peer->getAddress(), peer->isWildcardPeer()); //Reduce RSSI on current peer's interface in case it is not receiving any packets from this peer anymore
          Gd::out.printInfo("Info: Setting physical interface of peer " + std::to_string(peer->getID()) + " to " + senderId + ", because the RSSI is better.");
          peer->setPhysicalInterfaceId(senderId);
        } else peer->getPhysicalInterface()->decrementRssi(peer->getAddress(), peer->isWildcardPeer()); //Reduce RSSI on current peer's interface in case it is not receiving any packets from this peer anymore
      }
      if ((peer->getDeviceType() >> 16) == myPacket->getRorg()) unpaired = false;

      peer->packetReceived(myPacket);
      result = true;
    }
    if (unpaired && _pairing) {
      PairingData pairingData;

      {
        std::lock_guard<std::mutex> pairingDataGuard(_pairingInfo.pairingDataMutex);
        pairingData = _pairingData;
      }

      return handlePairingRequest(senderId, myPacket, pairingData);
    }
    return result;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

std::string EnOceanCentral::getFreeSerialNumber(int32_t address) {
  std::string serial;
  int32_t i = 0;
  do {
    serial = "EOD" + BaseLib::HelperFunctions::getHexString(address + i, 8);
    i++;
  } while (peerExists(serial));
  return serial;
}

bool EnOceanCentral::handlePairingRequest(const std::string &interfaceId, const PEnOceanPacket &packet, const PairingData &pairingData) {
  try {
    std::lock_guard<std::mutex> pairingGuard(_pairingInfo.pairingMutex);

    if (pairingData.minRssi != 0 && packet->getRssi() < pairingData.minRssi) {
      Gd::out.printInfo("Ignoring packet " + BaseLib::HelperFunctions::getHexString(packet->getBinary()) + " because RSSI is not good enough (" + std::to_string(packet->getRssi()) + " < " + std::to_string(pairingData.minRssi) + ").");
      return false;
    }

    std::shared_ptr<IEnOceanInterface> physicalInterface = Gd::interfaces->getInterface(interfaceId);

    std::vector<uint8_t> payload = packet->getData();
    if (!pairingData.remoteCommissioning && packet->getRorg() == 0xF6 && pairingData.eep != 0) {
      if (!peerExists(packet->senderAddress())) {
        buildPeer(pairingData.eep, packet->senderAddress(), interfaceId, false, -1);
      }
    } else if (!pairingData.remoteCommissioning && packet->getRorg() == 0xD1 && pairingData.eep != 0) {
      //Eltako actuators
      if (!peerExists(packet->senderAddress())) {
        auto rpcDevice = Gd::family->getRpcDevices()->find(pairingData.eep, 0x10, -1);
        if (rpcDevice) {
          auto channelIterator = rpcDevice->functions.find(1);
          if (channelIterator != rpcDevice->functions.end()) {
            auto variableIterator = channelIterator->second->variables->parameters.find("PAIRING");
            if (variableIterator != channelIterator->second->variables->parameters.end()) {
              if (variableIterator->second->logical->type == BaseLib::DeviceDescription::ILogical::Type::Enum::tBoolean) {
                auto peer = buildPeer(pairingData.eep, packet->senderAddress(), interfaceId, false, -1);
                if (peer) {
                  auto result = peer->setValue(std::make_shared<RpcClientInfo>(), 1, "PAIRING", std::make_shared<BaseLib::Variable>(true), true);
                  if (result->errorStruct) {
                    auto peerId = peer->getID();
                    peer.reset();
                    deletePeer(peerId);
                    return false;
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(200));
                  peer->setValue(std::make_shared<RpcClientInfo>(), 1, "PAIRING", std::make_shared<BaseLib::Variable>(true), true);
                  std::this_thread::sleep_for(std::chrono::milliseconds(200));
                  peer->setValue(std::make_shared<RpcClientInfo>(), 1, "PAIRING", std::make_shared<BaseLib::Variable>(true), true);
                }
              } else {
                auto peer = buildPeer(pairingData.eep, packet->senderAddress(), interfaceId, true, _pairingData.rfChannel);
                if (peer) {
                  auto result = peer->setValue(std::make_shared<RpcClientInfo>(), 1, "PAIRING", std::make_shared<BaseLib::Variable>((int32_t)_pairingData.rfChannel), true);
                  if (result->errorStruct) {
                    auto peerId = peer->getID();
                    peer.reset();
                    deletePeer(peerId);
                    return false;
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(200));
                  peer->setValue(std::make_shared<RpcClientInfo>(), 1, "PAIRING", std::make_shared<BaseLib::Variable>((int32_t)_pairingData.rfChannel), true);
                  std::this_thread::sleep_for(std::chrono::milliseconds(200));
                  peer->setValue(std::make_shared<RpcClientInfo>(), 1, "PAIRING", std::make_shared<BaseLib::Variable>((int32_t)_pairingData.rfChannel), true);
                }
              }
            }
          }
        }
      }
    } else if (!pairingData.remoteCommissioning && packet->getRorg() == 0xD4) {
      //UTE
      if (payload.size() < 8) return false;

      uint64_t
          eep = ((uint64_t)(uint8_t)
          payload.at(7) << 16u) | (((uint64_t)(uint8_t)
          payload.at(6)) << 8u) | ((uint8_t)payload.at(5));
      uint64_t
          manufacturer = (((uint64_t)(uint8_t)
          payload.at(4) & 0x07u) << 8u) | (uint8_t)payload.at(3);
      uint64_t manufacturerEep = (manufacturer << 24u) | eep;

      uint8_t byte1 = payload.at(1);
      if ((byte1 & 0x0Fu) != 0) return false; //Command 0 => teach-in request
      if (!(byte1 & 0x80u)) {
        std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
        _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.unsupportedUnidirectionalCommunication"));
        Gd::out.printWarning("Warning: Could not teach-in device as it expects currently unsupported unidirectional communication.");
        return false;
      }
      bool responseExpected = !(byte1 & 0x40u);
      if ((byte1 & 0x30u) == 0x10) {
        Gd::out.printWarning("Warning: Could not teach-out device as the teach-out request is currently unsupported.");
        return false;
      }

      int32_t rfChannel = 0;
      if (!peerExists(packet->senderAddress(), eep) && !peerExists(packet->senderAddress(), manufacturerEep)) {
        buildPeer(manufacturerEep, packet->senderAddress(), interfaceId, false, -1);
      } else {
        //This is for backward compatibility only. Newly paired peers don't have RF_CHANNEL set anymore.
        std::list<PMyPeer> peers = getPeer(packet->senderAddress());
        if (peers.empty()) return false;
        for (auto &peer : peers) {
          if (peer->getDeviceType() == eep || peer->getDeviceType() == manufacturerEep) {
            rfChannel = peer->getRfChannel(0);
            break;
          }
        }
      }

      if (responseExpected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        PEnOceanPacket response(new EnOceanPacket((EnOceanPacket::Type)1, packet->getRorg(), physicalInterface->getBaseAddress() | rfChannel, packet->senderAddress()));
        std::vector<uint8_t> responsePayload;
        responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 8);
        responsePayload.at(1) = (responsePayload.at(1) & 0x80u) | 0x11u; // Command 1 => teach-in response
        response->setData(responsePayload);
        physicalInterface->sendEnoceanPacket(response);
      }
      return true;
    } else if (!pairingData.remoteCommissioning && payload.size() >= 5 && packet->getRorg() == 0xA5 && (payload.at(4) & 0x88u) == 0x80) {
      //4BS teach-in, variant 3; LRN type bit needs to be set and LRN bit unset (= LRN telegram)
      uint64_t
          eep = ((uint32_t)(uint8_t)
          payload.at(0) << 16u) | (((uint32_t)(uint8_t)
          payload.at(1) >> 2u) << 8u) | (((uint8_t)payload.at(1) & 3u) << 5u) | (uint8_t)((uint8_t)payload.at(2) >> 3u);
      uint64_t manufacturer = (((uint32_t)(uint8_t)(payload.at(2) & 7u)) << 8u) | (uint8_t)payload.at(3);
      uint64_t manufacturerEep = (manufacturer << 24u) | eep;
      //In EEP version 3 we need the full bytes for the eep, so the following is deprecated
      uint64_t manufacturerEepOld = ((manufacturer & 0xFFu) << 24u) | ((manufacturer >> 9u) << 14u) | (((manufacturer >> 8u) & 1u) << 7u) | eep;
      std::string serial = getFreeSerialNumber(packet->senderAddress());

      if (!peerExists(packet->senderAddress(), eep) && !peerExists(packet->senderAddress(), manufacturerEep) && !peerExists(packet->senderAddress(), manufacturerEepOld)) {
        int32_t rfChannel = getFreeRfChannel(interfaceId);
        if (rfChannel == -1) {
          std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
          _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.noFreeRfChannels"));
          Gd::out.printError("Error: Could not pair peer, because there are no free RF channels.");
          return false;
        }
        Gd::out.printInfo("Info: Trying to pair peer with EEP " + BaseLib::HelperFunctions::getHexString(manufacturerEep) + ".");
        std::shared_ptr<EnOceanPeer> peer = createPeer(manufacturerEep, packet->senderAddress(), serial, false);
        if (!peer || !peer->getRpcDevice()) {
          Gd::out.printInfo("Info: Trying to pair peer with EEP " + BaseLib::HelperFunctions::getHexString(eep) + ".");
          peer = createPeer(manufacturerEepOld, packet->senderAddress(), serial, false);
          if (!peer || !peer->getRpcDevice()) {
            std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
            _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.unsupportedEep", std::list<std::string>{BaseLib::HelperFunctions::getHexString(eep)}));
            Gd::out.printWarning("Warning: The EEP " + BaseLib::HelperFunctions::getHexString(manufacturerEepOld) + " is currently not supported.");
            return false;
          }
        }
        try {
          std::unique_lock<std::mutex> peersGuard(_peersMutex);
          if (!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
          peersGuard.unlock();
          peer->save(true, true, false);
          peer->initializeCentralConfig();
          peer->setPhysicalInterfaceId(interfaceId);
          if (peer->hasRfChannel(0)) peer->setRfChannel(0, rfChannel);
          peersGuard.lock();
          _peers[peer->getAddress()].push_back(peer);
          _peersById[peer->getID()] = peer;
          peersGuard.unlock();
        }
        catch (const std::exception &ex) {
          Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }

        if (peer->hasRfChannel(0)) {
          PEnOceanPacket response(new EnOceanPacket((EnOceanPacket::Type)1, packet->getRorg(), physicalInterface->getBaseAddress() | peer->getRfChannel(0), 0xFFFFFFFF));
          std::vector<uint8_t> responsePayload;
          responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 5);
          responsePayload.back() = 0xF0;
          response->setData(responsePayload);
          physicalInterface->sendEnoceanPacket(response);
        }

        PVariable deviceDescriptions(new Variable(VariableType::tArray));
        deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
        std::vector<uint64_t> newIds{peer->getID()};
        raiseRPCNewDevices(newIds, deviceDescriptions);

        {
          auto pairingState = std::make_shared<PairingState>();
          pairingState->peerId = peer->getID();
          pairingState->state = "success";
          std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
          _newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
        }

        Gd::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
      } else {
        uint32_t rfChannel = 0;
        std::list<PMyPeer> peers = getPeer(packet->senderAddress());
        if (peers.empty()) return false;
        for (auto &peer : peers) {
          if (peer->getDeviceType() == eep || peer->getDeviceType() == manufacturerEep || peer->getDeviceType() == manufacturerEepOld) {
            rfChannel = peer->getRfChannel(0);
            break;
          }
        }
        PEnOceanPacket response(new EnOceanPacket((EnOceanPacket::Type)1, packet->getRorg(), (unsigned)physicalInterface->getBaseAddress() | rfChannel, 0xFFFFFFFF));
        std::vector<uint8_t> responsePayload;
        responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 5);
        responsePayload.back() = 0xF0;
        response->setData(responsePayload);
        physicalInterface->sendEnoceanPacket(response);
      }
      return true;
    } else if (pairingData.remoteCommissioning) {
      if (pairingData.remoteCommissioningWaitForSignal && packet->getRorg() != 0xD0) return false;

      {
        std::lock_guard<std::mutex> processedAddressesGuard(_pairingInfo.processedAddressesMutex);
        if (_pairingInfo.processedAddresses.find(packet->senderAddress()) != _pairingInfo.processedAddresses.end()) return false;
        _pairingInfo.processedAddresses.emplace(packet->senderAddress());
      }

      //Try remote commissioning
      if (!peerExists(packet->senderAddress())) {
        if (pairingData.remoteCommissioningDeviceAddress == 0 || pairingData.remoteCommissioningDeviceAddress == (unsigned)packet->senderAddress()) {
          Gd::out.printInfo("Info: Pushing address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 8) + " to remote commissioning queue.");
          _pairingInfo.remoteCommissioningAddressQueue.push(std::make_pair(interfaceId, packet->senderAddress()));
        }
      }
    }
    return false;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

void EnOceanCentral::savePeers(bool full) {
  try {
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    for (std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer >>::iterator i = _peersById.begin(); i != _peersById.end();
         ++i) {
      Gd::out.printInfo("Info: Saving EnOcean peer " + std::to_string(i->second->getID()));
      i->second->save(full, full, full);
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void EnOceanCentral::deletePeer(uint64_t id) {
  try {
    std::shared_ptr<EnOceanPeer> peer(getPeer(id));
    if (!peer) return;
    peer->deleting = true;
    PVariable deviceAddresses(new Variable(VariableType::tArray));
    deviceAddresses->arrayValue->push_back(std::make_shared<Variable>(peer->getSerialNumber()));

    PVariable deviceInfo(new Variable(VariableType::tStruct));
    deviceInfo->structValue->insert(StructElement("ID", std::make_shared<Variable>((int32_t)peer->getID())));
    PVariable channels(new Variable(VariableType::tArray));
    deviceInfo->structValue->insert(StructElement("CHANNELS", channels));

    for (Functions::iterator i = peer->getRpcDevice()->functions.begin(); i != peer->getRpcDevice()->functions.end(); ++i) {
      deviceAddresses->arrayValue->push_back(std::make_shared<Variable>(peer->getSerialNumber() + ":" + std::to_string(i->first)));
      channels->arrayValue->push_back(std::make_shared<Variable>(i->first));
    }

    std::vector<uint64_t> deletedIds{id};
    raiseRPCDeleteDevices(deletedIds, deviceAddresses, deviceInfo);

    if (peer->getRepeaterId() != 0) {
      auto repeaterPeer = getPeer(peer->getRepeaterId());
      if (repeaterPeer) repeaterPeer->removeRepeatedAddress(peer->getAddress());
    }

    if (peer->getRpcDevice()->addressSize == 25) {
      std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
      auto peerIterator = _wildcardPeers.find(peer->getAddress());
      if (peerIterator != _wildcardPeers.end()) {
        for (std::list<PMyPeer>::iterator element = peerIterator->second.begin(); element != peerIterator->second.end(); ++element) {
          if ((*element)->getID() == peer->getID()) {
            peerIterator->second.erase(element);
            break;
          }
        }
        if (peerIterator->second.empty()) _wildcardPeers.erase(peerIterator);
      }
    }

    {
      std::lock_guard<std::mutex> peersGuard(_peersMutex);
      if (_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
      if (_peersById.find(id) != _peersById.end()) _peersById.erase(id);
      auto peerIterator = _peers.find(peer->getAddress());
      if (peerIterator != _peers.end()) {
        for (std::list<PMyPeer>::iterator element = peerIterator->second.begin(); element != peerIterator->second.end(); ++element) {
          if ((*element)->getID() == peer->getID()) {
            peerIterator->second.erase(element);
            break;
          }
        }
        if (peerIterator->second.empty()) _peers.erase(peerIterator);
      }
    }

    int32_t i = 0;
    while (peer.use_count() > 1 && i < 600) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      i++;
    }
    if (i == 600) Gd::out.printError("Error: Peer deletion took too long.");

    peer->deleteFromDatabase();

    Gd::out.printMessage("Removed EnOcean peer " + std::to_string(peer->getID()));
  }
  catch (const std::exception &ex) {
    _peersMutex.unlock();
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::string EnOceanCentral::handleCliCommand(std::string command) {
  try {
    std::ostringstream stringStream;
    std::vector<std::string> arguments;
    bool showHelp = false;
    if (BaseLib::HelperFunctions::checkCliCommand(command, "help", "h", "", 0, arguments, showHelp)) {
      stringStream << "List of commands:" << std::endl << std::endl;
      stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
      stringStream << "pairing on (pon)           Enables pairing mode" << std::endl;
      stringStream << "pairing off (pof)          Disables pairing mode" << std::endl;
      stringStream << "peers create (pc)          Creates a new peer" << std::endl;
      stringStream << "peers list (ls)            List all peers" << std::endl;
      stringStream << "peers remove (pr)          Remove a peer" << std::endl;
      stringStream << "peers select (ps)          Select a peer" << std::endl;
      stringStream << "peers setname (pn)         Name a peer" << std::endl;
      stringStream << "interface setaddress (ia)  Set the base address of an EnOcean interface" << std::endl;
      stringStream << "process packet (pp)        Simulate reception of a packet" << std::endl;
      stringStream << "unselect (u)               Unselect this device" << std::endl;
      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "pairing on", "pon", "", 0, arguments, showHelp)) {
      if (showHelp) {
        stringStream << "Description: This command enables pairing mode." << std::endl;
        stringStream << "Usage: pairing on [DURATION]" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  DURATION: Optional duration in seconds to stay in pairing mode." << std::endl;
        return stringStream.str();
      }

      int32_t duration = 60;
      if (!arguments.empty()) {
        duration = BaseLib::Math::getNumber(arguments.at(0), false);
        if (duration < 5 || duration > 3600) return "Invalid duration. Duration has to be greater than 5 and less than 3600.\n";
      }

      setInstallMode(nullptr, true, duration, nullptr, false);
      stringStream << "Pairing mode enabled." << std::endl;
      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "pairing off", "pof", "", 0, arguments, showHelp)) {
      if (showHelp) {
        stringStream << "Description: This command disables pairing mode." << std::endl;
        stringStream << "Usage: pairing off" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  There are no parameters." << std::endl;
        return stringStream.str();
      }

      setInstallMode(nullptr, false, -1, nullptr, false);
      stringStream << "Pairing mode disabled." << std::endl;
      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "peers create", "pc", "", 3, arguments, showHelp)) {
      if (showHelp) {
        stringStream << "Description: This command creates a new peer." << std::endl;
        stringStream << "Usage: peers create INTERFACE TYPE ADDRESS" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  INTERFACE: The id of the interface to associate the new device to as defined in the familie's configuration file." << std::endl;
        stringStream << "  TYPE:      The 3 or 4 byte hexadecimal device type (for most devices the EEP number). Example: 0xF60201" << std::endl;
        stringStream << "  ADDRESS:   The 4 byte address/ID printed on the device. Example: 0x01952B7A" << std::endl;
        return stringStream.str();
      }

      std::string interfaceId = arguments.at(0);
      if (!Gd::interfaces->hasInterface(interfaceId)) return "Unknown physical interface.\n";
      uint64_t deviceType = BaseLib::Math::getUnsignedNumber64(arguments.at(1), true);
      if (deviceType == 0) return "Invalid device type. Device type has to be provided in hexadecimal format.\n";
      uint32_t address = BaseLib::Math::getNumber(arguments.at(2), true);
      std::string serial = getFreeSerialNumber(address);

      if (peerExists(address, deviceType)) stringStream << "A peer with this address and EEP is already paired to this central." << std::endl;
      else {
        std::shared_ptr<EnOceanPeer> peer = createPeer(deviceType, address, serial, false);
        if (!peer || !peer->getRpcDevice()) return "Device type not supported.\n";
        try {
          if (peer->getRpcDevice()->addressSize == 25) peer->setAddress(address & 0xFFFFFF80u);
          _peersMutex.lock();
          if (!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
          _peersMutex.unlock();
          peer->save(true, true, false);
          peer->initializeCentralConfig();
          peer->setPhysicalInterfaceId(interfaceId);
          _peersMutex.lock();
          _peers[peer->getAddress()].push_back(peer);
          _peersById[peer->getID()] = peer;
          _peersMutex.unlock();
          if (peer->getRpcDevice()->addressSize == 25) {
            std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
            _wildcardPeers[peer->getAddress()].push_back(peer);
          }
        }
        catch (const std::exception &ex) {
          _peersMutex.unlock();
          Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }

        PVariable deviceDescriptions(new Variable(VariableType::tArray));
        deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
        std::vector<uint64_t> newIds{peer->getID()};
        raiseRPCNewDevices(newIds, deviceDescriptions);

        {
          auto pairingState = std::make_shared<PairingState>();
          pairingState->peerId = peer->getID();
          pairingState->state = "success";
          std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
          _newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
        }

        Gd::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
        stringStream << "Added peer " << std::to_string(peer->getID()) << " with address 0x" << BaseLib::HelperFunctions::getHexString(peer->getAddress(), 8) << " and serial number " << serial << "." << std::dec << std::endl;
      }
      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "peers remove", "pr", "prm", 1, arguments, showHelp)) {
      if (showHelp) {
        stringStream << "Description: This command removes a peer." << std::endl;
        stringStream << "Usage: peers remove PEERID" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  PEERID: The id of the peer to remove. Example: 513" << std::endl;
        return stringStream.str();
      }

      uint64_t peerID = BaseLib::Math::getNumber(arguments.at(0), false);
      if (peerID == 0) return "Invalid id.\n";

      if (!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
      else {
        stringStream << "Removing peer " << std::to_string(peerID) << std::endl;
        deletePeer(peerID);
      }
      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "peers list", "pl", "ls", 0, arguments, showHelp)) {
      try {
        if (showHelp) {
          stringStream << "Description: This command lists information about all peers." << std::endl;
          stringStream << "Usage: peers list [FILTERTYPE] [FILTERVALUE]" << std::endl << std::endl;
          stringStream << "Parameters:" << std::endl;
          stringStream << "  FILTERTYPE:  See filter types below." << std::endl;
          stringStream << "  FILTERVALUE: Depends on the filter type. If a number is required, it has to be in hexadecimal format." << std::endl << std::endl;
          stringStream << "Filter types:" << std::endl;
          stringStream << "  ID: Filter by id." << std::endl;
          stringStream << "      FILTERVALUE: The id of the peer to filter (e. g. 513)." << std::endl;
          stringStream << "  SERIAL: Filter by serial number." << std::endl;
          stringStream << "      FILTERVALUE: The serial number of the peer to filter (e. g. JEQ0554309)." << std::endl;
          stringStream << "  ADDRESS: Filter by saddress." << std::endl;
          stringStream << "      FILTERVALUE: The address of the peer to filter (e. g. 128)." << std::endl;
          stringStream << "  NAME: Filter by name." << std::endl;
          stringStream << "      FILTERVALUE: The part of the name to search for (e. g. \"1st floor\")." << std::endl;
          stringStream << "  TYPE: Filter by device type." << std::endl;
          stringStream << "      FILTERVALUE: The 2 byte device type in hexadecimal format." << std::endl;
          return stringStream.str();
        }

        std::string filterType;
        std::string filterValue;

        if (arguments.size() >= 2) {
          filterType = BaseLib::HelperFunctions::toLower(arguments.at(0));
          filterValue = arguments.at(1);
          if (filterType == "name") BaseLib::HelperFunctions::toLower(filterValue);
        }

        auto peers = getPeers();
        if (peers.empty()) {
          stringStream << "No peers are paired to this central." << std::endl;
          return stringStream.str();
        }
        bool firmwareUpdates = false;
        std::string bar(" │ ");
        const int32_t idWidth = 8;
        const int32_t nameWidth = 25;
        const int32_t serialWidth = 13;
        const int32_t addressWidth = 8;
        const int32_t typeWidth1 = 8;
        const int32_t typeWidth2 = 45;
        const int32_t firmwareWidth = 8;
        std::string nameHeader("Name");
        nameHeader.resize(nameWidth, ' ');
        std::string typeStringHeader("Type Description");
        typeStringHeader.resize(typeWidth2, ' ');
        stringStream << std::setfill(' ')
                     << std::setw(idWidth) << "ID" << bar
                     << nameHeader << bar
                     << std::setw(serialWidth) << "Serial Number" << bar
                     << std::setw(addressWidth) << "Address" << bar
                     << std::setw(typeWidth1) << "Type" << bar
                     << typeStringHeader << bar
                     << std::setw(firmwareWidth) << "Firmware"
                     << std::endl;
        stringStream << "─────────┼───────────────────────────┼───────────────┼──────────┼──────────┼───────────────────────────────────────────────┼──────────" << std::endl;
        stringStream << std::setfill(' ')
                     << std::setw(idWidth) << " " << bar
                     << std::setw(nameWidth) << " " << bar
                     << std::setw(serialWidth) << " " << bar
                     << std::setw(addressWidth) << " " << bar
                     << std::setw(typeWidth1) << " " << bar
                     << std::setw(typeWidth2) << " " << bar
                     << std::setw(firmwareWidth)
                     << std::endl;
        for (auto &peer : peers) {
          if (filterType == "id") {
            uint64_t id = BaseLib::Math::getNumber(filterValue, false);
            if (peer->getID() != id) continue;
          } else if (filterType == "name") {
            std::string name = peer->getName();
            if ((signed)BaseLib::HelperFunctions::toLower(name).find(filterValue) == (signed)std::string::npos) continue;
          } else if (filterType == "serial") {
            if (peer->getSerialNumber() != filterValue) continue;
          } else if (filterType == "address") {
            int32_t address = BaseLib::Math::getNumber(filterValue, true);
            if (peer->getAddress() != address) continue;
          } else if (filterType == "type") {
            int32_t deviceType = BaseLib::Math::getNumber(filterValue, true);
            if ((int32_t)peer->getDeviceType() != deviceType) continue;
          }

          stringStream << std::setw(idWidth) << std::setfill(' ') << std::to_string(peer->getID()) << bar;
          std::string name = peer->getName();
          size_t nameSize = BaseLib::HelperFunctions::utf8StringSize(name);
          if (nameSize > (unsigned)nameWidth) {
            name = BaseLib::HelperFunctions::utf8Substring(name, 0, nameSize - 3);
            name += "...";
          } else name.resize(nameWidth + (name.size() - nameSize), ' ');
          stringStream << name << bar
                       << std::setw(serialWidth) << peer->getSerialNumber() << bar
                       << std::setw(addressWidth) << BaseLib::HelperFunctions::getHexString(peer->getAddress(), 8) << bar
                       << std::setw(typeWidth1) << BaseLib::HelperFunctions::getHexString(peer->getDeviceType(), 6) << bar;
          if (peer->getRpcDevice()) {
            PSupportedDevice type = peer->getRpcDevice()->getType(peer->getDeviceType(), peer->getFirmwareVersion());
            std::string typeID;
            if (type) typeID = type->description;
            auto type2Size = BaseLib::HelperFunctions::utf8StringSize(typeID);
            if (type2Size > (unsigned)typeWidth2) {
              typeID = BaseLib::HelperFunctions::utf8Substring(typeID, 0, type2Size - 3);
              typeID += "...";
            } else typeID.resize(typeWidth2 + (typeID.size() - type2Size), ' ');
            stringStream << typeID << bar;
          } else stringStream << std::setw(typeWidth2) << " ";
          if (peer->getFirmwareVersion() == 0) stringStream << std::setfill(' ') << std::setw(firmwareWidth) << " ";
          else if (peer->firmwareUpdateAvailable()) {
            stringStream << std::setfill(' ') << std::setw(firmwareWidth) << ("*" + peer->getFirmwareVersionString());
            firmwareUpdates = true;
          } else stringStream << std::setfill(' ') << std::setw(firmwareWidth) << peer->getFirmwareVersionString();
          stringStream << std::endl << std::dec;
        }
        stringStream << "─────────┴───────────────────────────┴───────────────┴──────────┴──────────┴───────────────────────────────────────────────┴──────────" << std::endl;
        if (firmwareUpdates) stringStream << std::endl << "*: Firmware update available." << std::endl;

        return stringStream.str();
      }
      catch (const std::exception &ex) {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    } else if (command.compare(0, 13, "peers setname") == 0 || command.compare(0, 2, "pn") == 0) {
      uint64_t peerID = 0;
      std::string name;

      std::stringstream stream(command);
      std::string element;
      int32_t offset = (command.at(1) == 'n') ? 0 : 1;
      int32_t index = 0;
      while (std::getline(stream, element, ' ')) {
        if (index < 1 + offset) {
          index++;
          continue;
        } else if (index == 1 + offset) {
          if (element == "help") break;
          else {
            peerID = BaseLib::Math::getNumber(element, false);
            if (peerID == 0) return "Invalid id.\n";
          }
        } else if (index == 2 + offset) name = element;
        else name += ' ' + element;
        index++;
      }
      if (index == 1 + offset) {
        stringStream << "Description: This command sets or changes the name of a peer to identify it more easily." << std::endl;
        stringStream << "Usage: peers setname PEERID NAME" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  PEERID:\tThe id of the peer to set the name for. Example: 513" << std::endl;
        stringStream << "  NAME:\tThe name to set. Example: \"1st floor light switch\"." << std::endl;
        return stringStream.str();
      }

      if (!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
      else {
        std::shared_ptr<EnOceanPeer> peer = getPeer(peerID);
        peer->setName(name);
        stringStream << "Name set to \"" << name << "\"." << std::endl;
      }
      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "interface setaddress", "ia", "", 2, arguments, showHelp)) {
      if (showHelp) {
        stringStream << "Description: This command sets the base address of an EnOcean interface. This can only be done 10 times!" << std::endl;
        stringStream << "Usage: interface setaddress INTERFACE ADDRESS" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  INTERFACE: The id of the interface to set the address for." << std::endl;
        stringStream << "  ADDRESS:   The new 4 byte address/ID starting with 0xFF the 7 least significant bits can't be set. Example: 0xFF422E80" << std::endl;
        return stringStream.str();
      }

      std::string interfaceId = arguments.at(0);
      if (!Gd::interfaces->hasInterface(interfaceId)) return "Unknown physical interface.\n";
      uint32_t address = BaseLib::Math::getUnsignedNumber(arguments.at(1), true) & 0xFFFFFF80;

      int32_t result = Gd::interfaces->getInterface(interfaceId)->setBaseAddress(address);

      if (result == -1) stringStream << "Error setting base address. See error log for more details." << std::endl;
      else stringStream << "Base address set to 0x" << BaseLib::HelperFunctions::getHexString(address) << ". Remaining changes: " << result << std::endl;

      return stringStream.str();
    } else if (BaseLib::HelperFunctions::checkCliCommand(command, "process packet", "pp", "", 2, arguments, showHelp)) {
      if (showHelp) {
        stringStream << "Description: This command processes the passed packet as it were received from an EnOcean interface" << std::endl;
        stringStream << "Usage: process packet INTERFACE PACKET" << std::endl << std::endl;
        stringStream << "Parameters:" << std::endl;
        stringStream << "  INTERFACE: The id of the interface to set the address for." << std::endl;
        stringStream << "  ADDRESS:   The hex string of the packet to process." << std::endl;
        return stringStream.str();
      }

      std::string interfaceId = arguments.at(0);
      if (!Gd::interfaces->hasInterface(interfaceId)) return "Unknown physical interface.\n";

      std::vector<uint8_t> rawPacket = BaseLib::HelperFunctions::getUBinary(arguments.at(1));
      PEnOceanPacket packet = std::make_shared<EnOceanPacket>(rawPacket);
      if (packet->getType() == EnOceanPacket::Type::RADIO_ERP1 || packet->getType() == EnOceanPacket::Type::RADIO_ERP2) {
        onPacketReceived(interfaceId, packet);
        stringStream << "Processed packet " << BaseLib::HelperFunctions::getHexString(packet->getBinary()) << std::endl;
      }

      return stringStream.str();
    } else return "Unknown command.\n";
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return "Error executing command. See log file for more details.\n";
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::createPeer(uint64_t eep, int32_t address, std::string serialNumber, bool save) {
  try {
    auto rpcDevice = Gd::family->getRpcDevices()->find(eep, 0x10, -1);
    if (!rpcDevice) {
      rpcDevice = Gd::family->getRpcDevices()->find(eep & 0xFFFFFFu, 0x10, -1);
      eep &= 0xFFFFFFu;
    }
    if (!rpcDevice) return std::shared_ptr<EnOceanPeer>();

    std::shared_ptr<EnOceanPeer> peer(new EnOceanPeer(_deviceId, this));
    peer->setDeviceType(eep);
    peer->setAddress(address);
    peer->setSerialNumber(serialNumber);
    peer->setRpcDevice(rpcDevice);
    if (!peer->getRpcDevice()) return std::shared_ptr<EnOceanPeer>();
    if (save) peer->save(true, true, false); //Save and create peerID
    return peer;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<EnOceanPeer>();
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::buildPeer(uint64_t eep, int32_t address, const std::string &interfaceId, bool requiresRfChannel, int32_t rfChannel) {
  try {
    std::string serial = getFreeSerialNumber(address);
    if (requiresRfChannel) {
      if (rfChannel == -1) rfChannel = getFreeRfChannel(interfaceId);
      if (rfChannel == -1) {
        std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
        _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.noFreeRfChannels"));
        Gd::out.printError("Error: Could not pair peer, because there are no free RF channels.");
        return std::shared_ptr<EnOceanPeer>();
      }
    }
    Gd::out.printInfo("Info: Trying to create peer with EEP " + BaseLib::HelperFunctions::getHexString(eep) + " or EEP " + BaseLib::HelperFunctions::getHexString(eep) + ".");
    std::shared_ptr<EnOceanPeer> peer = createPeer(eep, address, serial, false);
    if (!peer || !peer->getRpcDevice()) {
      std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
      _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.unsupportedEep", std::list<std::string>{BaseLib::HelperFunctions::getHexString(eep)}));
      Gd::out.printWarning("Warning: The EEP " + BaseLib::HelperFunctions::getHexString(eep) + " is currently not supported.");
      return std::shared_ptr<EnOceanPeer>();
    }
    try {
      std::unique_lock<std::mutex> peersGuard(_peersMutex);
      if (!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
      peersGuard.unlock();
      peer->save(true, true, false);
      peer->initializeCentralConfig();
      peer->setPhysicalInterfaceId(interfaceId);
      if (requiresRfChannel && peer->hasRfChannel(0)) peer->setRfChannel(0, rfChannel);
      peersGuard.lock();
      _peers[peer->getAddress()].push_back(peer);
      _peersById[peer->getID()] = peer;
      peersGuard.unlock();
    }
    catch (const std::exception &ex) {
      Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }

    PVariable deviceDescriptions(new Variable(VariableType::tArray));
    deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
    std::vector<uint64_t> newIds{peer->getID()};
    raiseRPCNewDevices(newIds, deviceDescriptions);

    {
      auto pairingState = std::make_shared<PairingState>();
      pairingState->peerId = peer->getID();
      pairingState->state = "success";
      std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
      _newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
    }

    Gd::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
    _pairing = false;

    return peer;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::shared_ptr<EnOceanPeer>();
}

PVariable EnOceanCentral::addLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannelIndex, uint64_t receiverID, int32_t receiverChannelIndex, std::string name, std::string description) {
  try {
    if (senderID == 0) return Variable::createError(-2, "Sender id is not set.");
    if (receiverID == 0) return Variable::createError(-2, "Receiver is not set.");
    if (senderID == receiverID) return Variable::createError(-2, "Sender and receiver are the same device.");
    auto sender = getPeer(senderID);
    auto receiver = getPeer(receiverID);
    if (!sender) return Variable::createError(-2, "Sender device not found.");
    if (!receiver) return Variable::createError(-2, "Receiver device not found.");
    if (senderChannelIndex < 0) senderChannelIndex = 0;
    if (receiverChannelIndex < 0) receiverChannelIndex = 0;
    auto senderRpcDevice = sender->getRpcDevice();
    auto receiverRpcDevice = receiver->getRpcDevice();
    auto senderFunctionIterator = senderRpcDevice->functions.find(senderChannelIndex);
    if (senderFunctionIterator == senderRpcDevice->functions.end()) return Variable::createError(-2, "Sender channel not found.");
    auto receiverFunctionIterator = receiverRpcDevice->functions.find(receiverChannelIndex);
    if (receiverFunctionIterator == receiverRpcDevice->functions.end()) return Variable::createError(-2, "Receiver channel not found.");
    auto senderFunction = senderFunctionIterator->second;
    auto receiverFunction = receiverFunctionIterator->second;
    if (senderFunction->linkSenderFunctionTypes.empty() || receiverFunction->linkReceiverFunctionTypes.empty()) return Variable::createError(-6, "Link not supported.");
    bool validLink = false;
    for (auto &senderFunctionType : senderFunction->linkSenderFunctionTypes) {
      for (auto &receiverFunctionType : receiverFunction->linkReceiverFunctionTypes) {
        if (senderFunctionType == receiverFunctionType) {
          validLink = true;
          break;
        }
      }
      if (validLink) break;
    }
    if (!validLink) return Variable::createError(-6, "Link not supported.");

    auto receiverRemanFeatures = receiver->getRemanFeatures();
    if (!receiverRemanFeatures->kSetLinkTable) {
      return Variable::createError(-1, "Device does not support links.");
    }
    if (receiver->getLinkCount() + 1u > receiverRemanFeatures->kInboundLinkTableSize) {
      return Variable::createError(-3, "Can't link more devices. You need to unlink a device first.");
    }

    std::shared_ptr<BaseLib::Systems::BasicPeer> senderPeer(new BaseLib::Systems::BasicPeer());
    senderPeer->address = sender->getAddress();
    senderPeer->channel = senderChannelIndex;
    senderPeer->id = sender->getID();
    senderPeer->serialNumber = sender->getSerialNumber();
    senderPeer->isSender = true;
    senderPeer->linkDescription = description;
    senderPeer->linkName = name;

    std::shared_ptr<BaseLib::Systems::BasicPeer> receiverPeer(new BaseLib::Systems::BasicPeer());
    receiverPeer->address = receiver->getAddress();
    receiverPeer->channel = receiverChannelIndex;
    receiverPeer->id = receiver->getID();
    receiverPeer->serialNumber = receiver->getSerialNumber();
    receiverPeer->linkDescription = description;
    receiverPeer->linkName = name;

    sender->addPeer(senderChannelIndex, receiverPeer);
    receiver->addPeer(receiverChannelIndex, senderPeer);

    if (!receiver->sendInboundLinkTable()) {
      sender->removePeer(senderChannelIndex, receiverPeer->address, receiverChannelIndex);
      receiver->removePeer(receiverChannelIndex, senderPeer->address, senderChannelIndex);
      return Variable::createError(-4, "Error updating link table on device.");
    }

    raiseRPCUpdateDevice(sender->getID(), senderChannelIndex, sender->getSerialNumber() + ":" + std::to_string(senderChannelIndex), 1);
    raiseRPCUpdateDevice(receiver->getID(), receiverChannelIndex, receiver->getSerialNumber() + ":" + std::to_string(receiverChannelIndex), 1);

    return std::make_shared<Variable>(VariableType::tVoid);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::removeLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannelIndex, uint64_t receiverID, int32_t receiverChannelIndex) {
  try {
    if (senderID == 0) return Variable::createError(-2, "Sender id is not set.");
    if (receiverID == 0) return Variable::createError(-2, "Receiver id is not set.");
    auto sender = getPeer(senderID);
    auto receiver = getPeer(receiverID);
    if (!sender) return Variable::createError(-2, "Sender device not found.");
    if (!receiver) return Variable::createError(-2, "Receiver device not found.");
    if (senderChannelIndex < 0) senderChannelIndex = 0;
    if (receiverChannelIndex < 0) receiverChannelIndex = 0;
    std::string senderSerialNumber = sender->getSerialNumber();
    std::string receiverSerialNumber = receiver->getSerialNumber();
    std::shared_ptr<HomegearDevice> senderRpcDevice = sender->getRpcDevice();
    std::shared_ptr<HomegearDevice> receiverRpcDevice = receiver->getRpcDevice();
    if (senderRpcDevice->functions.find(senderChannelIndex) == senderRpcDevice->functions.end()) return Variable::createError(-2, "Sender channel not found.");
    if (receiverRpcDevice->functions.find(receiverChannelIndex) == receiverRpcDevice->functions.end()) return Variable::createError(-2, "Receiver channel not found.");
    if (!sender->getPeer(senderChannelIndex, receiver->getAddress()) && !receiver->getPeer(receiverChannelIndex, sender->getAddress())) return Variable::createError(-6, "Devices are not paired to each other.");

    auto receiverPeer = sender->getPeer(senderChannelIndex, receiver->getAddress(), receiverChannelIndex);
    auto senderPeer = receiver->getPeer(receiverChannelIndex, sender->getAddress(), senderChannelIndex);

    sender->removePeer(senderChannelIndex, receiver->getAddress(), receiverChannelIndex);
    receiver->removePeer(receiverChannelIndex, sender->getAddress(), senderChannelIndex);

    if (!receiver->sendInboundLinkTable()) {
      sender->addPeer(senderChannelIndex, receiverPeer);
      receiver->addPeer(receiverChannelIndex, senderPeer);
      return Variable::createError(-4, "Error updating link table on device.");
    }

    raiseRPCUpdateDevice(sender->getID(), senderChannelIndex, senderSerialNumber + ":" + std::to_string(senderChannelIndex), 1);
    raiseRPCUpdateDevice(receiver->getID(), receiverChannelIndex, receiverSerialNumber + ":" + std::to_string(receiverChannelIndex), 1);

    return std::make_shared<Variable>(VariableType::tVoid);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::createDevice(BaseLib::PRpcClientInfo clientInfo, int32_t deviceType, std::string serialNumber, int32_t address, int32_t firmwareVersion, std::string interfaceId) {
  try {
    std::string serial = getFreeSerialNumber(address);
    if (peerExists(deviceType, address)) return Variable::createError(-5, "This peer is already paired to this central.");

    if (!interfaceId.empty() && !Gd::interfaces->hasInterface(interfaceId)) return Variable::createError(-6, "Unknown physical interface.");
    if (interfaceId.empty()) {
      if (Gd::interfaces->count() > 1) return Variable::createError(-7, "Please specify the ID of the physical interface (= communication module) to use.");
    }

    std::shared_ptr<EnOceanPeer> peer = createPeer(deviceType, address, serial, false);
    if (!peer || !peer->getRpcDevice()) return Variable::createError(-6, "Unknown device type.");

    if (peer->getRpcDevice()->addressSize == 25) peer->setAddress(address & 0xFFFFFF80);
    peer->save(true, true, false);
    peer->initializeCentralConfig();
    peer->setPhysicalInterfaceId(interfaceId);

    {
      std::lock_guard<std::mutex> peersGuard(_peersMutex);
      _peers[peer->getAddress()].push_back(peer);
      _peersById[peer->getID()] = peer;
      _peersBySerial[peer->getSerialNumber()] = peer;
    }

    if (peer->getRpcDevice()->addressSize == 25) {
      std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
      _wildcardPeers[peer->getAddress()].push_back(peer);
    }

    PVariable deviceDescriptions(new Variable(VariableType::tArray));
    deviceDescriptions->arrayValue = peer->getDeviceDescriptions(clientInfo, true, std::map<std::string, bool>());
    std::vector<uint64_t> newIds{peer->getID()};
    raiseRPCNewDevices(newIds, deviceDescriptions);

    {
      auto pairingState = std::make_shared<PairingState>();
      pairingState->peerId = peer->getID();
      pairingState->state = "success";
      std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
      _newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
    }

    Gd::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");

    return std::make_shared<Variable>((uint32_t)peer->getID());
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::createDevice(BaseLib::PRpcClientInfo clientInfo, const std::string &code) {
  try {
    if (code.empty()) return Variable::createError(-1, "Code is empty.");

    std::regex regex(R"([0-9A-F]{2}S[0-9A-F]{12}\+[0-9A-F]P[0-9A-F]{12}.*)");
    if (!std::regex_match(code, regex)) return Variable::createError(-1, "Unknown code.");

    Gd::out.printInfo("Info: OPUS BRiDGE QR code detected.");

    //OPUS BRiDGE QR code looks like this:
    // - Actuators:         30S0000050BFB53+1P004000000006+10Z00+11Z6FEC6172
    // - Motion sensor:     30S0000050E71BB+1P004000000009+10Z00+11Z535A4C2C+S---
    // - Motion sensor (2): 30S0000050E71BB+1P004000000009+10Z00+11Z535A4C2C
    // - Remote:            30S00000035E6E4+1P00401000000A

    uint32_t address = BaseLib::Math::getUnsignedNumber(code.substr(7, 8), true);
    std::string productId = code.substr(18, 12);
    uint32_t securityCode = (code.size() >= 48 ? BaseLib::Math::getUnsignedNumber(code.substr(40, 8), true) : 0);

    Gd::out.printInfo("Info: Trying to find description for product ID " + productId);

    auto eep = Gd::family->getRpcDevices()->getTypeNumberFromProductId(productId);
    if (eep == 0) return Variable::createError(-1, "Unknown device.");

    auto rpcDevice = Gd::family->getRpcDevices()->find(eep, 0x10, -1);
    if (!rpcDevice) return Variable::createError(-1, "Unknown device (2).");

    if (peerExists((int32_t)address, eep)) {
      return Variable::createError(-5, "A device with this address and eep already exists.");
    }

    if (rpcDevice->receiveModes & BaseLib::DeviceDescription::HomegearDevice::ReceiveModes::Enum::wakeUp2) {
      Gd::out.printInfo("Info: Device is a wake up device. Starting pairing mode thread for 5 minutes.");

      std::lock_guard<std::mutex> pairingModeGuard(_pairingInfo.pairingModeThreadMutex);
      _pairingInfo.stopPairingModeThread = true;
      _bl->threadManager.join(_pairingInfo.pairingModeThread);
      _pairingInfo.stopPairingModeThread = false;
      {
        std::lock_guard<std::mutex> processedAddressesGuard(_pairingInfo.processedAddressesMutex);
        _pairingInfo.processedAddresses.clear();
      }

      {
        std::lock_guard<std::mutex> pairingDataGuard(_pairingInfo.pairingDataMutex);
        _pairingData = PairingData();
        _pairingData.remoteCommissioning = true;
        _pairingData.remoteCommissioningDeviceAddress = address;
        _pairingData.eep = eep;
        _pairingData.remoteCommissioningGatewayAddress = 0;
        _pairingData.remoteCommissioningSecurityCode = securityCode;
        _pairingData.remoteCommissioningWaitForSignal = true;
        _pairingData.pairingInterface = "";
      }

      _timeLeftInPairingMode = 300; //It's important to set it here, because the thread often doesn't completely initialize before getInstallMode requests _timeLeftInPairingMode
      _bl->threadManager.start(_pairingInfo.pairingModeThread, true, &EnOceanCentral::pairingModeTimer, this, _timeLeftInPairingMode.load(), true);

      return Variable::createError(-2, "Started in background.");
    } else if (rpcDevice->receiveModes & BaseLib::DeviceDescription::HomegearDevice::ReceiveModes::Enum::always) {
      auto interface = Gd::interfaces->getDefaultInterface();
      if (interface) {
        PairingData pairingData;

        {
          std::lock_guard<std::mutex> pairingDataGuard(_pairingInfo.pairingDataMutex);
          pairingData = _pairingData;
        }

        auto peerId = remoteCommissionPeer(interface, address, pairingData);
        if (peerId != 0) return std::make_shared<BaseLib::Variable>(peerId);
      }
    } else {
      auto result = createDevice(clientInfo, (int32_t)eep, "", address, 0, Gd::interfaces->getDefaultInterface()->getID());
      if (!result->errorStruct && result->integerValue64 != 0) return std::make_shared<BaseLib::Variable>(result->integerValue64);
      else if (result->errorStruct && result->structValue->at("faultCode")->integerValue == -5) return result;
    }

    return Variable::createError(-1, "Could not create peer.");
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags) {
  try {
    if (serialNumber.empty()) return Variable::createError(-2, "Unknown device.");

    uint64_t peerId = 0;

    {
      std::shared_ptr<EnOceanPeer> peer = getPeer(serialNumber);
      if (!peer) return std::make_shared<Variable>(VariableType::tVoid);
      peerId = peer->getID();
    }

    return deleteDevice(clientInfo, peerId, flags);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, int32_t flags) {
  try {
    if (peerId == 0) return Variable::createError(-2, "Unknown device.");

    {
      std::shared_ptr<EnOceanPeer> peer = getPeer(peerId);
      if (!peer) return std::make_shared<Variable>(VariableType::tVoid);
    }

    deletePeer(peerId);

    if (peerExists(peerId)) return Variable::createError(-1, "Error deleting peer. See log for more details.");

    return std::make_shared<Variable>(VariableType::tVoid);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::getPairingState(BaseLib::PRpcClientInfo clientInfo) {
  try {
    auto states = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

    states->structValue->emplace("pairingModeEnabled", std::make_shared<BaseLib::Variable>(_pairing));
    states->structValue->emplace("pairingStarted", std::make_shared<BaseLib::Variable>(_pairingInfo.pairingStarted));
    states->structValue->emplace("pairingError", std::make_shared<BaseLib::Variable>(_pairingInfo.pairingError));
    states->structValue->emplace("pairingProgress", std::make_shared<BaseLib::Variable>(_pairingInfo.pairingProgress));
    states->structValue->emplace("pairingModeEndTime", std::make_shared<BaseLib::Variable>(BaseLib::HelperFunctions::getTimeSeconds() + _timeLeftInPairingMode));

    {
      std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);

      auto pairingMessages = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
      pairingMessages->arrayValue->reserve(_pairingMessages.size());
      for (auto &message : _pairingMessages) {
        auto pairingMessage = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
        pairingMessage->structValue->emplace("messageId", std::make_shared<BaseLib::Variable>(message->messageId));
        auto variables = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
        variables->arrayValue->reserve(message->variables.size());
        for (auto &variable : message->variables) {
          variables->arrayValue->emplace_back(std::make_shared<BaseLib::Variable>(variable));
        }
        pairingMessage->structValue->emplace("variables", variables);
        pairingMessages->arrayValue->push_back(pairingMessage);
      }
      states->structValue->emplace("general", std::move(pairingMessages));

      for (auto &element : _newPeers) {
        for (auto &peer : element.second) {
          auto peerState = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
          peerState->structValue->emplace("state", std::make_shared<BaseLib::Variable>(peer->state));
          peerState->structValue->emplace("messageId", std::make_shared<BaseLib::Variable>(peer->messageId));
          auto variables = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
          variables->arrayValue->reserve(peer->variables.size());
          for (auto &variable : peer->variables) {
            variables->arrayValue->emplace_back(std::make_shared<BaseLib::Variable>(variable));
          }
          peerState->structValue->emplace("variables", variables);
          states->structValue->emplace(std::to_string(peer->peerId), std::move(peerState));
        }
      }
    }

    return states;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::getSniffedDevices(BaseLib::PRpcClientInfo clientInfo) {
  try {
    PVariable array(new Variable(VariableType::tArray));

    std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
    array->arrayValue->reserve(_sniffedPackets.size());
    for (auto peerPackets : _sniffedPackets) {
      PVariable info(new Variable(VariableType::tStruct));
      array->arrayValue->push_back(info);

      info->structValue->insert(StructElement("FAMILYID", std::make_shared<Variable>(MY_FAMILY_ID)));
      info->structValue->insert(StructElement("ADDRESS", std::make_shared<Variable>(peerPackets.first)));
      if (!peerPackets.second.empty()) info->structValue->insert(StructElement("RORG", std::make_shared<Variable>(peerPackets.second.back()->getRorg())));
      if (!peerPackets.second.empty()) info->structValue->insert(StructElement("RSSI", std::make_shared<Variable>(peerPackets.second.back()->getRssi())));

      PVariable packets(new Variable(VariableType::tArray));
      info->structValue->insert(StructElement("PACKETS", packets));

      for (const auto &packet : peerPackets.second) {
        PVariable packetInfo(new Variable(VariableType::tStruct));
        packetInfo->structValue->insert(StructElement("TIME_RECEIVED", std::make_shared<Variable>(packet->getTimeReceived() / 1000)));
        packetInfo->structValue->insert(StructElement("PACKET", std::make_shared<Variable>(BaseLib::HelperFunctions::getHexString(packet->getBinary()))));
        packets->arrayValue->push_back(packetInfo);
      }
    }
    return array;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::invokeFamilyMethod(BaseLib::PRpcClientInfo clientInfo, std::string &method, PArray parameters) {
  try {
    auto localMethodIterator = _localRpcMethods.find(method);
    if (localMethodIterator != _localRpcMethods.end()) {
      return localMethodIterator->second(clientInfo, parameters);
    } else return BaseLib::Variable::createError(-32601, ": Requested method not found.");
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32502, "Unknown application error.");
}

void EnOceanCentral::pairingModeTimer(int32_t duration, bool debugOutput) {
  try {
    _pairing = true;
    if (debugOutput) Gd::out.printInfo("Info: Pairing mode enabled.");
    _timeLeftInPairingMode = duration;
    int64_t startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t timePassed = 0;
    while (timePassed < ((int64_t)duration * 1000) && !_pairingInfo.stopPairingModeThread) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - startTime;
      _timeLeftInPairingMode = duration - (timePassed / 1000);
      handleRemoteCommissioningQueue();
    }
    _timeLeftInPairingMode = 0;
    _pairing = false;
    if (debugOutput) Gd::out.printInfo("Info: Pairing mode disabled.");
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void EnOceanCentral::handleRemoteCommissioningQueue() {
  try {
    std::lock_guard<std::mutex> pairingGuard(_pairingInfo.recomMutex);

    int32_t deviceAddress = 0;

    PairingData pairingData;

    {
      std::lock_guard<std::mutex> pairingDataGuard(_pairingInfo.pairingDataMutex);
      pairingData = _pairingData;
    }

    std::shared_ptr<IEnOceanInterface> interface;
    if (!pairingData.pairingInterface.empty()) interface = Gd::interfaces->getInterface(pairingData.pairingInterface);

    if (!_pairingInfo.remoteCommissioningAddressQueue.empty()) {
      deviceAddress = _pairingInfo.remoteCommissioningAddressQueue.front().second;
      auto interfaceId = _pairingInfo.remoteCommissioningAddressQueue.front().first;
      _pairingInfo.remoteCommissioningAddressQueue.pop();

      if (!interface) interface = Gd::interfaces->getInterface(interfaceId);
      if (!interface) interface = Gd::interfaces->getDefaultInterface();

      if (pairingData.eep == 0) pairingData.eep = remoteManagementGetEep(interface, deviceAddress, pairingData.remoteCommissioningSecurityCode);
      if (pairingData.eep == 0) return;
    } else if (pairingData.remoteCommissioningDeviceAddress != 0 && pairingData.eep != 0 && !pairingData.remoteCommissioningWaitForSignal) {
      _pairingInfo.stopPairingModeThread = true;
      deviceAddress = pairingData.remoteCommissioningDeviceAddress;
    } else return;

    if (!interface) interface = Gd::interfaces->getDefaultInterface();

    remoteCommissionPeer(interface, deviceAddress, pairingData);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

uint64_t EnOceanCentral::remoteManagementGetEep(const std::shared_ptr<IEnOceanInterface> &interface, uint32_t deviceAddress, uint32_t securityCode) {
  try {
    if (!interface) return 0;

    uint64_t eep = 0;

    if (securityCode != 0) {
      auto unlock = std::make_shared<Unlock>(0, deviceAddress, securityCode);
      interface->sendEnoceanPacket(unlock);
      interface->sendEnoceanPacket(unlock);

      auto queryStatus = std::make_shared<QueryStatusPacket>(0, deviceAddress);
      // 0 retries to not block too long, because this fails for example when reception is enabled only after a
      // signal packet (RORG 0xD0). Before the device sends a signal packet, other packets might be sent that
      // trigger pairing without the device being able to process packets from Homegear.
      auto response = interface->sendAndReceivePacket(queryStatus,
                                                      deviceAddress,
                                                      0,
                                                      IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                      {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse}});

      if (!response) return 0;
      auto queryStatusData = response->getData();

      bool codeSet = queryStatusData.at(4) & 0x80u;
      auto lastFunctionNumber = (uint16_t)((uint16_t)(queryStatusData.at(5) & 0x0Fu) << 8u) | queryStatusData.at(6);
      //Some devices return "query status" as function number here (i. e. OPUS 563.052).
      if ((lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::unlock && lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::queryStatus)
          || (codeSet && queryStatusData.at(7) != (uint8_t)EnOceanPacket::QueryStatusReturnCode::ok)) {
        Gd::out.printWarning("Warning: Error unlocking device.");
        return 0;
      }
    }

    {
      auto ping = std::make_shared<PingPacket>(0, deviceAddress);
      auto response = interface->sendAndReceivePacket(ping,
                                                      deviceAddress,
                                                      2,
                                                      IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                      {{(uint16_t)EnOceanPacket::RemoteManagementResponse::pingResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::pingResponse}});
      if (response) {
        Gd::out.printInfo("Info: Got ping response.");
        auto pingData = response->getData();
        eep = ((uint64_t)response->getRemoteManagementManufacturer() << 24u) | (unsigned)((uint32_t)pingData.at(4) << 16u) | (unsigned)((uint16_t)(pingData.at(5) >> 2u) << 8u) | (((uint8_t)pingData.at(5) & 3u) << 5u)
            | (uint8_t)((uint8_t)pingData.at(6) >> 3u);
      }
    }

    if (eep == 0) {
      //Documentation states: From EEP 3.0 the FUNC and TYPE have the length of 8 bits. For EEP’s with FUNC > 0x3F, or TYPE > 0x7F, this RMCC will not work. In this case, the ping answer will not contain an EEP.
      //I. e. using QueryId is the recommended way of getting an EEP.
      auto queryId = std::make_shared<QueryIdPacket>(0, deviceAddress);
      auto response = interface->sendAndReceivePacket(queryId, deviceAddress, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{0x06, 0x04},
                                                                                                                                                         {0x07, 0x04}});
      if (!response) {
        if (securityCode != 0) {
          auto lock = std::make_shared<Lock>(0, deviceAddress, securityCode);
          interface->sendEnoceanPacket(lock);
          interface->sendEnoceanPacket(lock);
        }
        return 0;
      }

      Gd::out.printInfo("Info: Got query ID response.");
      auto queryIdData = response->getData();
      eep = ((uint64_t)response->getRemoteManagementManufacturer() << 24u) | (unsigned)((uint32_t)queryIdData.at(4) << 16u) | (unsigned)((uint16_t)(queryIdData.at(5) >> 2u) << 8u) | (((uint8_t)queryIdData.at(5) & 3u) << 5u)
          | (uint8_t)((uint8_t)queryIdData.at(6) >> 3u);
    }

    if (securityCode != 0) {
      auto lock = std::make_shared<Lock>(0, deviceAddress, securityCode);
      interface->sendEnoceanPacket(lock);
      interface->sendEnoceanPacket(lock);
    }

    return eep;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return 0;
}

void EnOceanCentral::updateFirmwares(std::vector<uint64_t> ids) {
  try {
    if (_updatingFirmware) return;
    _updatingFirmware = true;
    _lastFirmwareUpdate = BaseLib::HelperFunctions::getTime();
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> sortedIds;
    //{{{ Sort ids by device type
    for (auto id : ids) {
      auto peer = getPeer(id);
      if (!peer) continue;
      sortedIds[peer->getDeviceType()].emplace(id);
    }
    //}}}
    for (auto &type : sortedIds) {
      Gd::out.printInfo("Info: Updating firmware of devices with type 0x" + BaseLib::HelperFunctions::getHexString(type.first));
      updateFirmware(type.second);
    }
  } catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  _updatingFirmware = false;
}

void EnOceanCentral::updateFirmware(const std::unordered_set<uint64_t> &ids) {
  try {
    struct UpdateData {
      bool abort = false;
      uint64_t peerId = 0;
      uint32_t address = 0;
      uint8_t block = 0xA5;
      uint32_t currentBlockRetries = 0;
      uint32_t totalRetries = 0;
    };

    if (ids.empty()) return;

    std::vector<UpdateData> peersInBootloader;
    std::vector<UpdateData> peersInBootloaderOld;
    peersInBootloader.reserve(ids.size());
    peersInBootloaderOld.reserve(ids.size());

    std::shared_ptr<EnOceanPeer> firstPeer;
    for (auto &peerId : ids) {
      Gd::out.printMessage("Starting firmware update for peer " + std::to_string(peerId));
      firstPeer = getPeer(peerId);
      if (firstPeer) break;
    }
    if (!firstPeer) return;

    std::string filenamePrefix = BaseLib::HelperFunctions::getHexString(15, 4) + "." + BaseLib::HelperFunctions::getHexString(firstPeer->getDeviceType(), 8);
    auto firmwarePath = _bl->settings.firmwarePath() + filenamePrefix + ".fw";
    auto versionPath = _bl->settings.firmwarePath() + filenamePrefix + ".version";

    if (!BaseLib::Io::fileExists(firmwarePath) || !BaseLib::Io::fileExists(versionPath)) {
      Gd::out.printError("Error: No firmware file found.");
      return;
    }

    auto firmwareFile = BaseLib::Io::getUBinaryFileContent(firmwarePath);
    auto version = BaseLib::Math::getUnsignedNumber(BaseLib::Io::getFileContent(versionPath), true);
    auto interface = Gd::interfaces->getDefaultInterface();
    auto baseAddress = interface->getBaseAddress();
    auto updateAddress = baseAddress | 1;

    auto dutyCycleInfo = interface->getDutyCycleInfo();
    if (dutyCycleInfo.dutyCycleUsed > 10) interface->reset();
    dutyCycleInfo = interface->getDutyCycleInfo();
    if (dutyCycleInfo.dutyCycleUsed > 10) {
      Gd::out.printError("Error: Not enough duty cycle available.");
      return;
    }

    Gd::out.printInfo("Info: Current duty cycle used: " + std::to_string(dutyCycleInfo.dutyCycleUsed) + "%.");

    //{{{ Step 1: Enter bootloader
    for (auto &peerId : ids) {
      auto peer = getPeer(peerId);
      if (!peer) continue;

      if (peer->getDeviceType() != firstPeer->getDeviceType()) {
        Gd::out.printWarning("Warning: Cannot update all peers as passed peers if different device types.");
        continue;
      }

      uint8_t blockNumber = 0;
      int32_t rssi = 0;
      for (uint32_t retries = 0; retries < 3; retries++) {
        auto packet = std::make_shared<EnOceanPacket>(EnOceanPacket::Type::RADIO_ERP1, 0xD1, baseAddress, peer->getAddress(), std::vector<uint8_t>{0xD1, 0x03, 0x31, 0x10});
        auto response = peer->sendAndReceivePacket(packet, 2, IEnOceanInterface::EnOceanRequestFilterType::senderAddress);
        auto data = response ? response->getData() : std::vector<uint8_t>();
        if (!response || response->getRorg() != 0xD1 || (data.at(2) & 0x0F) != 4 || data.at(3) != 0) {
          continue;
        } else {
          blockNumber = data.at(4);
          rssi = response->getRssi();
          break;
        }
      }

      if (rssi > -30 || rssi < -90) {
        Gd::out.printMessage("Not updating peer " + std::to_string(peerId) + ", because RSSI is out of allowed range (RSSI is " + std::to_string(rssi) + ").");
        continue;
      }

      if (blockNumber == 0xA5) {
        //{{{ Get version
        uint32_t deviceVersion = peer->getFirmwareVersion();
        if (deviceVersion == 0) continue;
        if (deviceVersion >= version) {
          Gd::out.printInfo("Info: Peer " + std::to_string(peerId) + " already has current firmware version " + BaseLib::HelperFunctions::getHexString(deviceVersion) + ".");
          continue;
        }
        //}}}

        //Send activation telegrams
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        for (uint32_t retries = 0; retries < 20; retries++) {
          blockNumber = 0;
          bool continueLoop = false;
          for (uint32_t i = 2; i < 10; i++) {
            auto packet = std::make_shared<EnOceanPacket>(EnOceanPacket::Type::RADIO_ERP1, 0xD1, baseAddress, peer->getAddress(), std::vector<uint8_t>{0xD1, 0x03, 0x32, 0x10, (uint8_t)i});
            if (!peer->sendPacket(packet, "", 900, false, -1, "", std::vector<uint8_t>())) {
              continueLoop = true;
              break;
            }
          }
          if (continueLoop) continue;

          std::this_thread::sleep_for(std::chrono::milliseconds(2000)); //Wait for flash to be deleted

          for (uint32_t retries2 = 0; retries2 < 20; retries2++) {
            //Get first block number
            auto packet = std::make_shared<EnOceanPacket>(EnOceanPacket::Type::RADIO_ERP1, 0xD1, baseAddress, peer->getAddress(), std::vector<uint8_t>{0xD1, 0x03, 0x31, 0x10});
            auto response = peer->sendAndReceivePacket(packet, 2, IEnOceanInterface::EnOceanRequestFilterType::senderAddress);
            auto data = response ? response->getData() : std::vector<uint8_t>();
            if (!response || response->getRorg() != 0xD1 || (data.at(2) & 0x0F) != 4 || data.at(3) != 0) {
              continue;
            } else {
              blockNumber = data.at(4);
              break;
            }
          }

          if (blockNumber != 0 && blockNumber != 0xA5) {
            UpdateData updateData;
            updateData.peerId = peerId;
            updateData.address = peer->getAddress();
            updateData.block = blockNumber;
            peersInBootloader.emplace_back(updateData);
            break;
          }
        }
      } else {
        UpdateData updateData;
        updateData.peerId = peerId;
        updateData.address = peer->getAddress();
        updateData.block = blockNumber;
        peersInBootloaderOld.emplace_back(updateData);
      }
    }
    //}}}

    //Place peers already in bootloader last
    peersInBootloader.insert(peersInBootloader.end(), peersInBootloaderOld.begin(), peersInBootloaderOld.end());

    //{{{ //Update ready peers
    if (peersInBootloader.empty()) return;
    bool repeatBlock = false;
    uint32_t block = 0xA5;
    while (true) {
      bool finished = true;
      for (auto &updateData : peersInBootloader) {
        if (updateData.abort || updateData.block == 0xA5 || updateData.block < 0x0A || updateData.block > 0x7F || updateData.currentBlockRetries >= 20 || updateData.totalRetries >= 1000) continue;
        if (!repeatBlock) block = updateData.block;
        finished = false;
        break;
      }

      if (finished) break;

      for (uint32_t i = 0; i < 100; i++) {
        if (Gd::bl->shuttingDown) {
          Gd::out.printError("Error: Updates did not finish.");
          return;
        }
        dutyCycleInfo = interface->getDutyCycleInfo();
        if (dutyCycleInfo.dutyCycleUsed > 90) interface->reset();
        dutyCycleInfo = interface->getDutyCycleInfo();
        if (dutyCycleInfo.dutyCycleUsed > 90) {
          Gd::out.printInfo("Info: Waiting for duty cycle to free up. Waiting " + std::to_string(dutyCycleInfo.timeLeftInSlot) + " seconds.");
          std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        } else break;
      }

      Gd::out.printInfo("Sending block " + std::to_string(block) + "...");

      uint32_t fileStart = block * 256 - 2560;
      uint32_t count = (block == 127 ? 35 + 1 : 0x80 + 36 + 1);
      uint32_t filePos = fileStart;

      while (count != 0) {
        std::vector<uint8_t> data;
        data.reserve(10);
        data.push_back(0xD1);
        data.push_back(0x03);
        data.push_back(0x33);
        data.insert(data.end(), firmwareFile.begin() + filePos, firmwareFile.begin() + filePos + 4);
        filePos += 4;

        count--;

        if (count == 0x80) {
          data.resize(10, 0);
          count = 0;
        } else {
          data.insert(data.end(), firmwareFile.begin() + filePos, firmwareFile.begin() + filePos + 3);
          filePos += 3;
        }

        auto packet = std::make_shared<EnOceanPacket>(EnOceanPacket::Type::RADIO_ERP1, 0xD1, updateAddress, 0xFFFFFFFF, data);
        if (!interface->sendEnoceanPacket(packet)) {
          break;
        }
      }

      //{{{ Request new block
      repeatBlock = false;
      for (auto &updateData : peersInBootloader) {
        if (updateData.abort || updateData.block == 0xA5 || updateData.block < 0x0A || updateData.block > 0x7F || updateData.currentBlockRetries >= 20 || updateData.totalRetries >= 1000) continue;
        auto peer = getPeer(updateData.peerId);
        if (!peer) continue;
        //Get first block number
        auto packet = std::make_shared<EnOceanPacket>(EnOceanPacket::Type::RADIO_ERP1, 0xD1, baseAddress, peer->getAddress(), std::vector<uint8_t>{0xD1, 0x03, 0x31, 0x10});
        auto response = peer->sendAndReceivePacket(packet, 20, IEnOceanInterface::EnOceanRequestFilterType::senderAddress);
        auto data = response ? response->getData() : std::vector<uint8_t>();
        if (!response || response->getRorg() != 0xD1 || (data.at(2) & 0x0F) != 4 || data.at(3) != 0) {
          updateData.abort = true;
        } else {
          auto newBlock = data.at(4);
          if (newBlock == block) {
            updateData.currentBlockRetries++;
            repeatBlock = true;
          } else {
            updateData.block = newBlock;
            updateData.currentBlockRetries = 0;
          }
          updateData.totalRetries++;
        }
      }
      //}}}
    }
    //}}}

    for (auto &updateData : peersInBootloader) {
      if (updateData.block == 0xA5) {
        auto peer = getPeer(updateData.peerId);
        if (!peer) continue;
        peer->setFirmwareVersionString(BaseLib::HelperFunctions::getHexString(version));
        peer->setFirmwareVersion((int32_t)version);
      }
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

uint64_t EnOceanCentral::remoteCommissionPeer(const std::shared_ptr<IEnOceanInterface> &interface, uint32_t deviceAddress, const PairingData &pairingData) {
  try {
    if (!interface || pairingData.eep == 0) return 0;
    Gd::out.printInfo("Info: Trying to pair device with address " + BaseLib::HelperFunctions::getHexString(deviceAddress, 8) + " and EEP " + BaseLib::HelperFunctions::getHexString(pairingData.eep) + "...");

    if (peerExists((int32_t)deviceAddress, pairingData.eep)) {
      Gd::out.printInfo("Info: Peer is already paired to this central.");
      auto peer = getPeer((int32_t)deviceAddress);
      return !peer.empty() ? peer.front()->getID() : 0;
    }

    auto rpcDevice = Gd::family->getRpcDevices()->find(pairingData.eep, 0x10, -1);
    if (!rpcDevice) rpcDevice = Gd::family->getRpcDevices()->find(pairingData.eep & 0xFFFFFFu, 0x10, -1);
    if (!rpcDevice) {
      Gd::out.printWarning("Warning: No device description found for EEP " + BaseLib::HelperFunctions::getHexString(pairingData.eep) + " or EEP " + BaseLib::HelperFunctions::getHexString(pairingData.eep & 0xFFFFFFu) + ". Aborting pairing.");
      return 0;
    }

    auto features = RemanFeatureParser::parse(rpcDevice);
    if (!features) {
      Gd::out.printWarning("Warning: Could not parse REMAN features from device's XML file");
      return 0;
    }

    if (features->kForceEncryption && (pairingData.aesKeyInbound.empty() || pairingData.aesKeyOutbound.empty())) {
      Gd::out.printWarning("Warning: aesKeyInbound or aesKeyOutbound not specified in setInstallMode but they are required as the device enforces encryption.");
      return 0;
    }

    if (!features->kSetLinkTable || features->kInboundLinkTableSize == 0) {
      Gd::out.printInfo("Info: EEP " + BaseLib::HelperFunctions::getHexString(pairingData.eep) + " does not support \"Set Link Table\". Assuming the device is a sensor.");
    }

    _pairingInfo.pairingStarted = true;
    _pairingInfo.pairingProgress = 0;

    auto destinationAddress = features->kAddressedRemanPackets ? deviceAddress : 0xFFFFFFFFu;

    if (pairingData.remoteCommissioningSecurityCode != 0) {
      auto unlock = std::make_shared<Unlock>(0, destinationAddress, pairingData.remoteCommissioningSecurityCode);
      interface->sendEnoceanPacket(unlock);
      _pairingInfo.pairingProgress = (100 / 16) * 1;
      interface->sendEnoceanPacket(unlock);
      _pairingInfo.pairingProgress = (100 / 16) * 2;

      auto queryStatus = std::make_shared<QueryStatusPacket>(0, destinationAddress);
      auto response = interface->sendAndReceivePacket(queryStatus,
                                                      deviceAddress,
                                                      2,
                                                      IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                      {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse}});
      _pairingInfo.pairingProgress = (100 / 16) * 3;
      if (!response) {
        _pairingInfo.pairingError = true;
        return 0;
      }
      auto queryStatusData = response->getData();

      bool codeSet = queryStatusData.at(4) & 0x80u;
      auto lastFunctionNumber = (uint16_t)((uint16_t)(queryStatusData.at(5) & 0x0Fu) << 8u) | queryStatusData.at(6);
      //Some devices return "query status" as function number here (i. e. OPUS 563.052).
      if ((lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::unlock && lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::queryStatus)
          || (codeSet && queryStatusData.at(7) != (uint8_t)EnOceanPacket::QueryStatusReturnCode::ok)) {
        Gd::out.printWarning("Warning: Error unlocking device.");
        _pairingInfo.pairingError = true;
        return 0;
      }
    }

    int32_t rfChannel = 0;

    //{{{ //Set inbound link table (pairing)
    if (features->kInboundLinkTableSize != 0) {
      //Only set a rfChannel other than 0 when RF_CHANNEL exists. Most newer devices don't require RF_CHANNEL.
      auto channelIterator = rpcDevice->functions.find(0);
      if (channelIterator != rpcDevice->functions.end()) {
        auto parameterIterator = channelIterator->second->variables->parameters.find("RF_CHANNEL");
        if (parameterIterator != channelIterator->second->variables->parameters.end()) {
          rfChannel = getFreeRfChannel(interface->getID());
          if (rfChannel == -1) {
            Gd::out.printError("Error: Could not get free RF channel.");
            _pairingInfo.pairingError = true;
            return 0;
          }
        }
      }

      static constexpr uint32_t entrySize = 9;

      auto gatewayAddress = (pairingData.remoteCommissioningGatewayAddress == 0) ? (uint32_t)interface->getAddress() : pairingData.remoteCommissioningGatewayAddress;

      std::vector<uint8_t> linkTable{};
      linkTable.reserve(entrySize * features->kInboundLinkTableSize);
      linkTable.push_back(0);
      linkTable.push_back(gatewayAddress >> 24u);
      linkTable.push_back(gatewayAddress >> 16u);
      linkTable.push_back(gatewayAddress >> 8u);
      linkTable.push_back(gatewayAddress | (uint8_t)rfChannel);
      linkTable.push_back(features->kLinkTableGatewayEep >> 16u);
      linkTable.push_back(features->kLinkTableGatewayEep >> 8u);
      linkTable.push_back(features->kLinkTableGatewayEep);
      linkTable.push_back(0);
      if (features->kUnencryptedUpdates) {
        linkTable.push_back(1);
        linkTable.push_back((gatewayAddress | 1) >> 24u);
        linkTable.push_back((gatewayAddress | 1) >> 16u);
        linkTable.push_back((gatewayAddress | 1) >> 8u);
        linkTable.push_back(gatewayAddress | 1);
        linkTable.push_back(features->kLinkTableGatewayEep >> 16u);
        linkTable.push_back(features->kLinkTableGatewayEep >> 8u);
        linkTable.push_back(features->kLinkTableGatewayEep);
        linkTable.push_back(0);
      }
      for (uint32_t i = (features->kUnencryptedUpdates != 0 ? 2 : 1); i < features->kInboundLinkTableSize; i++) {
        linkTable.push_back(i);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0);
        linkTable.push_back(0);
        linkTable.push_back(0);
        linkTable.push_back(0);
      }

      if (linkTable.size() > features->kMaxDataLength) {
        std::vector<uint8_t> chunk{};
        chunk.reserve(features->kMaxDataLength);
        for (uint32_t i = 0; i < linkTable.size(); i += entrySize) {
          chunk.insert(chunk.end(), linkTable.begin() + i, linkTable.begin() + i + entrySize);
          if (chunk.size() + entrySize > features->kMaxDataLength) {
            auto setLinkTablePacket = std::make_shared<SetLinkTable>(0, destinationAddress, true, chunk);

            auto response = interface->sendAndReceivePacket(setLinkTablePacket,
                                                            deviceAddress,
                                                            2,
                                                            IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                            {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
            _pairingInfo.pairingProgress = (100 / 16) * 4;
            if (!response) {
              Gd::out.printError("Error: Could not set link table on device.");
              _pairingInfo.pairingError = true;
              return 0;
            }

            chunk.clear();
          }
        }

        if (!chunk.empty()) {
          auto setLinkTablePacket = std::make_shared<SetLinkTable>(0, destinationAddress, true, chunk);

          auto response = interface->sendAndReceivePacket(setLinkTablePacket,
                                                          deviceAddress,
                                                          2,
                                                          IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                          {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
          _pairingInfo.pairingProgress = (100 / 16) * 4;
          if (!response) {
            Gd::out.printError("Error: Could not set link table on device.");
            _pairingInfo.pairingError = true;
            return 0;
          }
        }
      } else {
        auto setLinkTablePacket = std::make_shared<SetLinkTable>(0, destinationAddress, true, linkTable);

        auto response = interface->sendAndReceivePacket(setLinkTablePacket,
                                                        deviceAddress,
                                                        2,
                                                        IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                        {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
        _pairingInfo.pairingProgress = (100 / 16) * 5;
        if (!response) {
          Gd::out.printError("Error: Could not set link table on device.");
          _pairingInfo.pairingError = true;
          return 0;
        }
      }
    }
    //}}}

    //{{{ Set outbound link table
    if (features->kSetOutboundLinkTableSize != 0) {
      static constexpr uint32_t entrySize = 9;

      std::vector<uint8_t> linkTable{};
      linkTable.reserve(entrySize * features->kSetOutboundLinkTableSize);
      for (uint32_t i = 0; i < features->kSetOutboundLinkTableSize; i++) {
        linkTable.push_back(i);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0xFFu);
        linkTable.push_back(0);
        linkTable.push_back(0);
        linkTable.push_back(0);
        linkTable.push_back(0);
      }

      if (linkTable.size() > features->kMaxDataLength) {
        std::vector<uint8_t> chunk{};
        chunk.reserve(features->kMaxDataLength);
        for (uint32_t i = 0; i < linkTable.size(); i += entrySize) {
          chunk.insert(chunk.end(), linkTable.begin() + i, linkTable.begin() + i + entrySize);
          if (chunk.size() + entrySize > features->kMaxDataLength) {
            auto setLinkTablePacket = std::make_shared<SetLinkTable>(0, destinationAddress, false, chunk);

            auto response = interface->sendAndReceivePacket(setLinkTablePacket,
                                                            deviceAddress,
                                                            2,
                                                            IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                            {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
            _pairingInfo.pairingProgress = (100 / 16) * 6;
            if (!response) {
              Gd::out.printError("Error: Could not set link table on device.");
              _pairingInfo.pairingError = true;
              return 0;
            }

            chunk.clear();
          }
        }

        if (!chunk.empty()) {
          auto setLinkTablePacket = std::make_shared<SetLinkTable>(0, destinationAddress, false, chunk);

          auto response = interface->sendAndReceivePacket(setLinkTablePacket,
                                                          deviceAddress,
                                                          2,
                                                          IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                          {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
          _pairingInfo.pairingProgress = (100 / 16) * 8;
          if (!response) {
            Gd::out.printError("Error: Could not set link table on device.");
            _pairingInfo.pairingError = true;
            return 0;
          }
        }
      } else {
        auto setLinkTable = std::make_shared<SetLinkTable>(0, destinationAddress, false, linkTable);

        auto response = interface->sendAndReceivePacket(setLinkTable,
                                                        deviceAddress,
                                                        2,
                                                        IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                        {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
        _pairingInfo.pairingProgress = (100 / 16) * 8;
        if (!response) {
          _pairingInfo.pairingError = true;
          return 0;
        }
      }
    }
    //}}}

    //{{{ //Dummy call as the Homegear actuators (e. g. HG-16A-EO) need some time before they accept new packets
    auto queryStatus = std::make_shared<QueryStatusPacket>(0, destinationAddress);
    auto response = interface->sendAndReceivePacket(queryStatus,
                                                    deviceAddress,
                                                    10,
                                                    IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                    {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse}});
    _pairingInfo.pairingProgress = (100 / 16) * 9;
    //}}}

    //{{{ Set repeater functions
    if (features->kSetRepeaterFunctions) {
      auto setRepeaterFunctions = std::make_shared<SetRepeaterFunctions>(0, destinationAddress, 0, 1, 0);
      response = interface->sendAndReceivePacket(setRepeaterFunctions,
                                                 deviceAddress,
                                                 2,
                                                 IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                 {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
      _pairingInfo.pairingProgress = (100 / 16) * 10;
      if (!response) Gd::out.printWarning("Warning: Could not set repeater functions.");
    }
    //}}}

    if (features->kForceEncryption) {
      if ((features->kSlf & 3) != 3) {
        Gd::out.printWarning("Warning: Unsupported data encryption.");
        _pairingInfo.pairingError = true;
        return 0;
      }

      if ((features->kSlf & 0x18) != 0x10) {
        Gd::out.printWarning("Warning: Unsupported MAC algorithm.");
        _pairingInfo.pairingError = true;
        return 0;
      }

      if ((features->kSlf & 0xE0) == 0 || (features->kSlf & 0xE0) == 0x20) {
        Gd::out.printWarning("Warning: Unsupported RLC algorithm.");
        _pairingInfo.pairingError = true;
        return 0;
      }

      auto setSecurityProfile = std::make_shared<SetSecurityProfile>(0, destinationAddress, features->kRecomVersion == 0x11, false, 0, features->kSlf, 0, pairingData.aesKeyInbound, deviceAddress, pairingData.remoteCommissioningGatewayAddress);
      response = interface->sendAndReceivePacket(setSecurityProfile,
                                                 deviceAddress,
                                                 2,
                                                 IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                 {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
      _pairingInfo.pairingProgress = (100 / 16) * 11;
      if (!response) {
        Gd::out.printWarning("Warning: Could not set security profile.");
        _pairingInfo.pairingError = true;
        return 0;
      } else {
        setSecurityProfile = std::make_shared<SetSecurityProfile>(0, destinationAddress, features->kRecomVersion == 0x11, true, 0, features->kSlf, 0, pairingData.aesKeyOutbound, pairingData.remoteCommissioningGatewayAddress, deviceAddress);
        response = interface->sendAndReceivePacket(setSecurityProfile,
                                                   deviceAddress,
                                                   2,
                                                   IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                   {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
        _pairingInfo.pairingProgress = (100 / 16) * 12;

        if (!response) {
          Gd::out.printWarning("Warning: Could not set security profile.");
          _pairingInfo.pairingError = true;
          return 0;
        }
      }
    }

    //{{{
    if (features->kApplyChanges) {
      auto applyChanges = std::make_shared<ApplyChanges>(0, destinationAddress, true, true);
      response = interface->sendAndReceivePacket(applyChanges,
                                                 deviceAddress,
                                                 2,
                                                 IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction,
                                                 {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
      _pairingInfo.pairingProgress = (100 / 16) * 13;
    }
    //}}}

    if (pairingData.remoteCommissioningSecurityCode != 0) {
      auto lock = std::make_shared<Lock>(0, destinationAddress, pairingData.remoteCommissioningSecurityCode);
      interface->sendEnoceanPacket(lock);
      _pairingInfo.pairingProgress = (100 / 16) * 14;
      interface->sendEnoceanPacket(lock);
      _pairingInfo.pairingProgress = (100 / 16) * 15;
    }

    auto peer = buildPeer(pairingData.eep, deviceAddress, interface->getID(), true, rfChannel);
    if (peer) {
      if (pairingData.remoteCommissioningGatewayAddress != 0) {
        peer->setGatewayAddress(pairingData.remoteCommissioningGatewayAddress);
      } else {
        peer->setGatewayAddress(interface->getBaseAddress());
      }
      if (pairingData.remoteCommissioningSecurityCode != 0) {
        peer->setSecurityCode(pairingData.remoteCommissioningSecurityCode);
      }
      if (!pairingData.aesKeyInbound.empty()) {
        peer->setAesKeyInbound(pairingData.aesKeyInbound);
      }
      if (!pairingData.aesKeyOutbound.empty()) {
        peer->setAesKeyOutbound(pairingData.aesKeyOutbound);
      }
      if (features->kForceEncryption) {
        peer->setEncryptionType(features->kSlf & 7);
        peer->setCmacSize((features->kSlf & 0x18) == 0x10 ? 4 : 3);
        if ((features->kSlf & 0xE0) == 0x40 || (features->kSlf & 0xE0) == 0x60) peer->setRollingCodeSize(2);
        else if ((features->kSlf & 0xE0) == 0x80 || (features->kSlf & 0xE0) == 0xA0) peer->setRollingCodeSize(3);
        else if ((features->kSlf & 0xE0) == 0xC0 || (features->kSlf & 0xE0) == 0xE0) peer->setRollingCodeSize(4);
        peer->setRollingCodeInbound(1);
        peer->setRollingCodeOutbound(0);
        peer->setExplicitRollingCode(features->kSlf & 0x20);
      }

      auto channelIterator = peer->configCentral.find(0);
      if (channelIterator != peer->configCentral.end()) {
        if (pairingData.remoteCommissioningSecurityCode != 0) {
          auto variableIterator = channelIterator->second.find("SECURITY_CODE");
          if (variableIterator != channelIterator->second.end() && variableIterator->second.rpcParameter) {
            auto rpcSecurityCode = std::make_shared<BaseLib::Variable>(BaseLib::HelperFunctions::getHexString(pairingData.remoteCommissioningSecurityCode, 8));
            std::vector<uint8_t> parameterData;
            variableIterator->second.rpcParameter->convertToPacket(rpcSecurityCode, variableIterator->second.mainRole(), parameterData);
            variableIterator->second.setBinaryData(parameterData);
            if (variableIterator->second.databaseId > 0) peer->saveParameter(variableIterator->second.databaseId, parameterData);
            else peer->saveParameter(0, ParameterGroup::Type::Enum::config, channelIterator->first, variableIterator->first, parameterData);
          }
        }
      }

      if (!peer->getDeviceConfiguration()) {
        Gd::out.printError("Error: Could not read current device configuration.");
      }

      _pairingInfo.pairingProgress = 100;

      return peer->getID();
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return 0;
}

std::shared_ptr<Variable> EnOceanCentral::setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput) {
  try {
    if (_disposing) return Variable::createError(-32500, "Central is disposing.");
    std::lock_guard<std::mutex> pairingModeThreadGuard(_pairingInfo.pairingModeThreadMutex);
    _pairingInfo.stopPairingModeThread = true;
    _bl->threadManager.join(_pairingInfo.pairingModeThread);
    _pairingInfo.stopPairingModeThread = false;

    _pairingInfo.pairingStarted = false;
    _pairingInfo.pairingError = false;
    _pairingInfo.pairingProgress = 0;

    {
      std::lock_guard<std::mutex> processedAddressesGuard(_pairingInfo.processedAddressesMutex);
      _pairingInfo.processedAddresses.clear();
    }

    PairingData pairingData;

    if (metadata) {
      auto metadataIterator = metadata->structValue->find("interface");
      if (metadataIterator != metadata->structValue->end()) pairingData.pairingInterface = metadataIterator->second->stringValue;
      else pairingData.pairingInterface = "";

      metadataIterator = metadata->structValue->find("type");
      if (metadataIterator != metadata->structValue->end() && metadataIterator->second->stringValue == "remoteCommissioning") {
        pairingData.remoteCommissioning = true;
        metadataIterator = metadata->structValue->find("deviceAddress");
        if (metadataIterator != metadata->structValue->end()) pairingData.remoteCommissioningDeviceAddress = metadataIterator->second->integerValue;
        metadataIterator = metadata->structValue->find("gatewayAddress");
        if (metadataIterator != metadata->structValue->end()) pairingData.remoteCommissioningGatewayAddress = metadataIterator->second->integerValue;
        metadataIterator = metadata->structValue->find("securityCode");
        if (metadataIterator != metadata->structValue->end()) pairingData.remoteCommissioningSecurityCode = BaseLib::Math::getUnsignedNumber(metadataIterator->second->stringValue, true);
        metadataIterator = metadata->structValue->find("aesKeyInbound");
        if (metadataIterator != metadata->structValue->end()) pairingData.aesKeyInbound = BaseLib::HelperFunctions::getUBinary(metadataIterator->second->stringValue);
        metadataIterator = metadata->structValue->find("aesKeyOutbound");
        if (metadataIterator != metadata->structValue->end()) pairingData.aesKeyOutbound = BaseLib::HelperFunctions::getUBinary(metadataIterator->second->stringValue);
      }

      metadataIterator = metadata->structValue->find("eep");
      if (metadataIterator != metadata->structValue->end()) {
        if (metadataIterator->second->type == BaseLib::VariableType::tString) pairingData.eep = BaseLib::Math::getUnsignedNumber64(metadataIterator->second->stringValue);
        else pairingData.eep = metadataIterator->second->integerValue64;
      }

      metadataIterator = metadata->structValue->find("rfChannel");
      if (metadataIterator != metadata->structValue->end()) pairingData.rfChannel = metadataIterator->second->integerValue;

      metadataIterator = metadata->structValue->find("rssi");
      if (metadataIterator != metadata->structValue->end()) pairingData.minRssi = metadataIterator->second->integerValue;
    } else pairingData.pairingInterface = "";

    {
      std::lock_guard<std::mutex> pairingDataGuard(_pairingInfo.pairingDataMutex);
      _pairingData = pairingData;
    }

    _timeLeftInPairingMode = 0;
    if (on && duration >= 5) {
      {
        std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
        _newPeers.clear();
        _pairingMessages.clear();
      }

      _timeLeftInPairingMode = duration; //It's important to set it here, because the thread often doesn't completely initialize before getInstallMode requests _timeLeftInPairingMode
      _bl->threadManager.start(_pairingInfo.pairingModeThread, true, &EnOceanCentral::pairingModeTimer, this, duration, debugOutput);
    }
    return std::make_shared<Variable>(VariableType::tVoid);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, std::string interfaceId) {
  try {
    std::shared_ptr<EnOceanPeer> peer(getPeer(peerId));
    if (!peer) return Variable::createError(-2, "Unknown device.");
    return peer->setInterface(clientInfo, interfaceId);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::startSniffing(BaseLib::PRpcClientInfo clientInfo) {
  std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
  _sniffedPackets.clear();
  _sniff = true;
  return std::make_shared<Variable>();
}

PVariable EnOceanCentral::stopSniffing(BaseLib::PRpcClientInfo clientInfo) {
  _sniff = false;
  return std::make_shared<Variable>();
}

PVariable EnOceanCentral::updateFirmware(PRpcClientInfo clientInfo, std::vector<uint64_t> ids, bool manual) {
  try {
    std::lock_guard<std::mutex> updateFirmwareThreadGuard(_updateFirmwareThreadMutex);
    if (_updatingFirmware) return Variable::createError(-1, "Central is already already updating a device. Please wait until the current update is finished.");
    if (_disposing) return Variable::createError(-32500, "Central is disposing.");
    _bl->threadManager.start(_updateFirmwareThread, false, &EnOceanCentral::updateFirmwares, this, ids);
    return std::make_shared<BaseLib::Variable>(true);
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

//{{{ Family RPC methods
BaseLib::PVariable EnOceanCentral::getMeshingInfo(const PRpcClientInfo &clientInfo, const PArray &parameters) {
  try {
    if (!parameters->empty()) return BaseLib::Variable::createError(-1, "Wrong parameter count.");

    auto meshingInfo = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

    auto peers = getPeers();
    for (auto &peer : peers) {
      auto enoceanPeer = std::dynamic_pointer_cast<EnOceanPeer>(peer);
      auto repeaterId = enoceanPeer->getRepeaterId();
      auto repeatedAddresses = enoceanPeer->getRepeatedAddresses();
      if (repeaterId != 0 || !repeatedAddresses.empty()) {
        auto peerStruct = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
        if (repeaterId != 0) peerStruct->structValue->emplace("repeaterPeerId", std::make_shared<BaseLib::Variable>(repeaterId));
        if (!repeatedAddresses.empty()) {
          auto addressesArray = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
          addressesArray->arrayValue->reserve(repeatedAddresses.size());
          for (auto &address : repeatedAddresses) {
            auto addressStruct = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            auto repeatedPeer = getPeer(address);
            if (!repeatedPeer.empty()) addressStruct->structValue->emplace("peerId", std::make_shared<BaseLib::Variable>(repeatedPeer.front()->getID()));
            addressStruct->structValue->emplace("address", std::make_shared<BaseLib::Variable>(address));
            addressesArray->arrayValue->emplace_back(addressStruct);
          }
          peerStruct->structValue->emplace("repeatedPeers", peerStruct);
        }
        meshingInfo->structValue->emplace(std::to_string(peer->getID()), peerStruct);
      }
    }

    return meshingInfo;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::resetMeshingTables(const PRpcClientInfo &clientInfo, const PArray &parameters) {
  try {
    if (!parameters->empty()) return BaseLib::Variable::createError(-1, "Wrong parameter count.");

    auto peers = getPeers();
    for (auto &peer : peers) {
      auto enoceanPeer = std::dynamic_pointer_cast<EnOceanPeer>(peer);
      if (enoceanPeer->getRepeaterId() != 0) enoceanPeer->setRepeaterId(0);
      enoceanPeer->resetRepeatedAddresses();
    }

    return std::make_shared<BaseLib::Variable>();
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::remanGetPathInfoThroughPing(const PRpcClientInfo &clientInfo, const PArray &parameters) {
  try {
    if (parameters->size() != 2) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
    if (parameters->at(0)->type != BaseLib::VariableType::tInteger && parameters->at(0)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 1 is not of type Integer.");
    if (parameters->at(1)->type != BaseLib::VariableType::tInteger && parameters->at(1)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 2 is not of type Integer.");

    auto peer = getPeer((uint64_t)parameters->at(0)->integerValue64);
    if (!peer) return BaseLib::Variable::createError(-1, "Unknown peer.");

    auto peer2 = getPeer((uint64_t)parameters->at(1)->integerValue64);
    if (!peer2) return BaseLib::Variable::createError(-1, "Unknown destination peer.");

    return std::make_shared<BaseLib::Variable>(peer->remanGetPathInfoThroughPing(peer2->getAddress()));
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::remanPing(const PRpcClientInfo &clientInfo, const PArray &parameters) {
  try {
    if (parameters->empty()) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
    if (parameters->at(0)->type != BaseLib::VariableType::tInteger && parameters->at(0)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 1 is not of type Integer.");

    auto peer = getPeer((uint64_t)parameters->at(0)->integerValue64);
    if (!peer) return BaseLib::Variable::createError(-1, "Unknown peer.");

    return std::make_shared<BaseLib::Variable>(peer->remanPing());
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::remanSetRepeaterFilter(const PRpcClientInfo &clientInfo, const PArray &parameters) {
  try {
    if (parameters->size() != 4) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
    if (parameters->at(0)->type != BaseLib::VariableType::tInteger && parameters->at(0)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 1 is not of type Integer.");
    if (parameters->at(1)->type != BaseLib::VariableType::tInteger && parameters->at(1)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 2 is not of type Integer.");
    if (parameters->at(2)->type != BaseLib::VariableType::tInteger && parameters->at(2)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 3 is not of type Integer.");
    if (parameters->at(3)->type != BaseLib::VariableType::tInteger && parameters->at(3)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 4 is not of type Integer.");

    auto peer = getPeer((uint64_t)parameters->at(0)->integerValue64);
    if (!peer) return BaseLib::Variable::createError(-1, "Unknown peer.");

    return std::make_shared<BaseLib::Variable>(peer->remanSetRepeaterFilter(parameters->at(1)->integerValue, parameters->at(2)->integerValue, parameters->at(3)->integerValue));
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::remanSetRepeaterFunctions(const PRpcClientInfo &clientInfo, const PArray &parameters) {
  try {
    if (parameters->size() != 4) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
    if (parameters->at(0)->type != BaseLib::VariableType::tInteger && parameters->at(0)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 1 is not of type Integer.");
    if (parameters->at(1)->type != BaseLib::VariableType::tInteger && parameters->at(1)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 2 is not of type Integer.");
    if (parameters->at(2)->type != BaseLib::VariableType::tInteger && parameters->at(2)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 3 is not of type Integer.");
    if (parameters->at(3)->type != BaseLib::VariableType::tInteger && parameters->at(3)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 4 is not of type Integer.");

    auto peer = getPeer((uint64_t)parameters->at(0)->integerValue64);
    if (!peer) return BaseLib::Variable::createError(-1, "Unknown peer.");

    return std::make_shared<BaseLib::Variable>(peer->remanSetRepeaterFunctions(parameters->at(1)->integerValue, parameters->at(2)->integerValue, parameters->at(3)->integerValue));
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::remanSetSecurityProfile(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters) {
  try {
    //bool outbound, uint8_t index, uint8_t slf, uint32_t rlc, const std::vector<uint8_t> &aesKey, uint32_t destinationId, uint32_t sourceId

    if (parameters->size() != 8) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
    if (parameters->at(0)->type != BaseLib::VariableType::tInteger && parameters->at(0)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 1 is not of type Integer.");
    if (parameters->at(1)->type != BaseLib::VariableType::tBoolean) return BaseLib::Variable::createError(-1, "Parameter 2 is not of type Boolean.");
    if (parameters->at(2)->type != BaseLib::VariableType::tInteger && parameters->at(2)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 3 is not of type Integer.");
    if (parameters->at(3)->type != BaseLib::VariableType::tInteger && parameters->at(3)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 4 is not of type Integer.");
    if (parameters->at(4)->type != BaseLib::VariableType::tInteger && parameters->at(3)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 5 is not of type Integer.");
    if (parameters->at(5)->type != BaseLib::VariableType::tString) return BaseLib::Variable::createError(-1, "Parameter 6 is not of type String.");
    if (parameters->at(6)->type != BaseLib::VariableType::tInteger && parameters->at(3)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 7 is not of type Integer.");
    if (parameters->at(7)->type != BaseLib::VariableType::tInteger && parameters->at(3)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 8 is not of type Integer.");

    auto peer = getPeer((uint64_t)parameters->at(0)->integerValue64);
    if (!peer) return BaseLib::Variable::createError(-1, "Unknown peer.");

    return std::make_shared<BaseLib::Variable>(peer->remanSetSecurityProfile(parameters->at(1)->booleanValue,
                                                                             parameters->at(2)->integerValue,
                                                                             parameters->at(3)->integerValue,
                                                                             parameters->at(4)->integerValue,
                                                                             BaseLib::HelperFunctions::getUBinary(parameters->at(5)->stringValue),
                                                                             parameters->at(6)->integerValue,
                                                                             parameters->at(7)->integerValue));
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}

BaseLib::PVariable EnOceanCentral::remanSetCode(const BaseLib::PRpcClientInfo &clientInfo, const BaseLib::PArray &parameters) {
  try {
    if (parameters->size() != 2) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
    if (parameters->at(0)->type != BaseLib::VariableType::tInteger && parameters->at(0)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 1 is not of type Integer.");
    if (parameters->at(1)->type != BaseLib::VariableType::tInteger && parameters->at(1)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Parameter 2 is not of type Integer.");

    auto peer = getPeer((uint64_t)parameters->at(0)->integerValue64);
    if (!peer) return BaseLib::Variable::createError(-1, "Unknown peer.");

    return std::make_shared<BaseLib::Variable>(peer->remanSetCode((uint32_t)parameters->at(1)->integerValue64));
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return Variable::createError(-32500, "Unknown application error.");
}
//}}}

}
