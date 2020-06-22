/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanCentral.h"
#include "GD.h"
#include "EnOceanPackets.h"

#include <iomanip>

namespace EnOcean {

EnOceanCentral::EnOceanCentral(ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, GD::bl, eventHandler)
{
	init();
}

EnOceanCentral::EnOceanCentral(uint32_t deviceID, std::string serialNumber, ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, GD::bl, deviceID, serialNumber, -1, eventHandler)
{
	init();
}

EnOceanCentral::~EnOceanCentral()
{
	dispose();
}

void EnOceanCentral::dispose(bool wait)
{
	try
	{
		if(_disposing) return;
		_disposing = true;
		{
			std::lock_guard<std::mutex> pairingModeGuard(_pairingModeThreadMutex);
			_stopPairingModeThread = true;
			_bl->threadManager.join(_pairingModeThread);
		}

		_stopWorkerThread = true;
		GD::out.printDebug("Debug: Waiting for worker thread of device " + std::to_string(_deviceId) + "...");
		GD::bl->threadManager.join(_workerThread);

		GD::out.printDebug("Removing device " + std::to_string(_deviceId) + " from physical device's event queue...");
        GD::interfaces->removeEventHandlers();
	}
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanCentral::init()
{
	try
	{
		if(_initialized) return; //Prevent running init two times
		_initialized = true;
		_pairing = false;
		_stopPairingModeThread = false;
		_stopWorkerThread = false;
		_timeLeftInPairingMode = 0;
        GD::interfaces->addEventHandlers((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink*)this);

		GD::bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &EnOceanCentral::worker, this);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanCentral::worker()
{
	try
	{
		std::chrono::milliseconds sleepingTime(100);
		uint32_t counter = 0;
		uint64_t lastPeer;
		lastPeer = 0;

		while(!_stopWorkerThread && !GD::bl->shuttingDown)
		{
			try
			{
				std::this_thread::sleep_for(sleepingTime);
				if(_stopWorkerThread || GD::bl->shuttingDown) return;
				if(counter > 1000)
				{
					counter = 0;

					{
						std::lock_guard<std::mutex> peersGuard(_peersMutex);
						if(_peersById.size() > 0)
						{
							int32_t windowTimePerPeer = _bl->settings.workerThreadWindow() / 8 / _peersById.size();
							sleepingTime = std::chrono::milliseconds(windowTimePerPeer);
						}
					}
				}

				std::shared_ptr<EnOceanPeer> peer;

				{
					std::lock_guard<std::mutex> peersGuard(_peersMutex);
					if(!_peersById.empty())
					{
						if(!_peersById.empty())
						{
							std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator nextPeer = _peersById.find(lastPeer);
							if(nextPeer != _peersById.end())
							{
								nextPeer++;
								if(nextPeer == _peersById.end()) nextPeer = _peersById.begin();
							}
							else nextPeer = _peersById.begin();
							lastPeer = nextPeer->first;
							peer = std::dynamic_pointer_cast<EnOceanPeer>(nextPeer->second);
						}
					}
				}

				if(peer && !peer->deleting) peer->worker();
				GD::interfaces->worker();
				counter++;
			}
			catch(const std::exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanCentral::loadPeers()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			int32_t peerID = row->second.at(0)->intValue;
			GD::out.printMessage("Loading EnOcean peer " + std::to_string(peerID));
			std::shared_ptr<EnOceanPeer> peer(new EnOceanPeer(peerID, row->second.at(2)->intValue, row->second.at(3)->textValue, _deviceId, this));
			if(!peer->load(this)) continue;
			if(!peer->getRpcDevice()) continue;
			std::lock_guard<std::mutex> peersGuard(_peersMutex);
			if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
			_peersById[peerID] = peer;
			_peers[peer->getAddress()].push_back(peer);
			if(peer->getRpcDevice()->addressSize == 25)
			{
				std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
				_wildcardPeers[peer->getAddress()].push_back(peer);
			}
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::getPeer(uint64_t id)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		if(_peersById.find(id) != _peersById.end())
		{
			std::shared_ptr<EnOceanPeer> peer(std::dynamic_pointer_cast<EnOceanPeer>(_peersById.at(id)));
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::shared_ptr<EnOceanPeer>();
}

std::list<PMyPeer> EnOceanCentral::getPeer(int32_t address)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		auto peersIterator = _peers.find(address);
		if(peersIterator != _peers.end())
		{
			return peersIterator->second;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::list<PMyPeer>();
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::getPeer(std::string serialNumber)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		if(_peersBySerial.find(serialNumber) != _peersBySerial.end())
		{
			std::shared_ptr<EnOceanPeer> peer(std::dynamic_pointer_cast<EnOceanPeer>(_peersBySerial.at(serialNumber)));
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::shared_ptr<EnOceanPeer>();
}

bool EnOceanCentral::peerExists(uint64_t id)
{
	return ICentral::peerExists(id);
}

bool EnOceanCentral::peerExists(std::string serialNumber)
{
	return ICentral::peerExists(serialNumber);
}

bool EnOceanCentral::peerExists(int32_t address, uint64_t eep)
{
	std::list<PMyPeer> peers = getPeer(address);
	for(auto& peer : peers)
	{
		if(peer->getDeviceType() == eep) return true;
	}
	return false;
}

int32_t EnOceanCentral::getFreeRfChannel(const std::string& interfaceId)
{
	try
	{
		std::vector<std::shared_ptr<BaseLib::Systems::Peer>> peers = getPeers();
		std::set<int32_t> usedChannels;
		for(std::vector<std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = peers.begin(); i != peers.end(); ++i)
		{
			PMyPeer peer(std::dynamic_pointer_cast<EnOceanPeer>(*i));
			if(!peer) continue;
			if(peer->getPhysicalInterfaceId() != interfaceId) continue;
			std::vector<int32_t> channels = peer->getRfChannels();
			usedChannels.insert(channels.begin(), channels.end());
		}
		for(int32_t i = 0; i < 128; ++i)
		{
			if(usedChannels.find(i) == usedChannels.end()) return i;
		}
		return -1;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return -1;
}

bool EnOceanCentral::onPacketReceived(std::string& senderId, std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(_disposing) return false;
		PEnOceanPacket myPacket(std::dynamic_pointer_cast<EnOceanPacket>(packet));
		if(!myPacket) return false;

		if(_bl->debugLevel >= 4) _bl->out.printInfo(BaseLib::HelperFunctions::getTimeString(myPacket->getTimeReceived()) + " EnOcean packet received (" + senderId + std::string(", RSSI: ") + std::to_string(myPacket->getRssi()) + " dBm" + "): " + BaseLib::HelperFunctions::getHexString(myPacket->getBinary()) + " - Sender address (= EnOcean ID): 0x" + BaseLib::HelperFunctions::getHexString(myPacket->senderAddress(), 8));

		std::list<PMyPeer> peers = getPeer(myPacket->senderAddress());
		if(peers.empty())
		{
			std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
			auto wildcardPeersIterator = _wildcardPeers.find(myPacket->senderAddress() & 0xFFFFFF80);
			if(wildcardPeersIterator != _wildcardPeers.end()) peers = wildcardPeersIterator->second;
		}
		if(peers.empty())
		{
			if(_sniff)
			{
				std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
				auto sniffedPacketsIterator = _sniffedPackets.find(myPacket->senderAddress());
				if(sniffedPacketsIterator == _sniffedPackets.end())
				{
					_sniffedPackets[myPacket->senderAddress()].reserve(100);
					_sniffedPackets[myPacket->senderAddress()].push_back(myPacket);
				}
				else
				{
					if(sniffedPacketsIterator->second.size() + 1 > sniffedPacketsIterator->second.capacity()) sniffedPacketsIterator->second.reserve(sniffedPacketsIterator->second.capacity() + 100);
					sniffedPacketsIterator->second.push_back(myPacket);
				}
			}
			if(_pairing && (_pairingInterface.empty() || _pairingInterface == senderId)) return handlePairingRequest(senderId, myPacket);
			return false;
		}

		bool result = false;
		bool unpaired = true;
		for(auto& peer : peers)
		{
            std::string settingName = "roaming";
            auto roamingSetting = GD::family->getFamilySetting(settingName);
            bool roaming = roamingSetting ? roamingSetting->integerValue : true;
			if(roaming && senderId != peer->getPhysicalInterfaceId() && peer->getPhysicalInterface()->getBaseAddress() == GD::interfaces->getInterface(senderId)->getBaseAddress())
			{
                if(myPacket->getRssi() > peer->getPhysicalInterface()->getRssi(peer->getAddress(), peer->isWildcardPeer()) + 6)
                {
                    peer->getPhysicalInterface()->decrementRssi(peer->getAddress(), peer->isWildcardPeer()); //Reduce RSSI on current peer's interface in case it is not receiving any packets from this peer anymore
                    GD::out.printInfo("Info: Setting physical interface of peer " + std::to_string(peer->getID()) + " to " + senderId + ", because the RSSI is better.");
                    peer->setPhysicalInterfaceId(senderId);
                }
                else peer->getPhysicalInterface()->decrementRssi(peer->getAddress(), peer->isWildcardPeer()); //Reduce RSSI on current peer's interface in case it is not receiving any packets from this peer anymore
			}
			if((peer->getDeviceType() >> 16) == myPacket->getRorg()) unpaired = false;

			peer->packetReceived(myPacket);
			result = true;
		}
		if(unpaired && _pairing) return handlePairingRequest(senderId, myPacket);
		return result;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

std::string EnOceanCentral::getFreeSerialNumber(int32_t address)
{
	std::string serial;
	int32_t i = 0;
	do
	{
		serial = "EOD" + BaseLib::HelperFunctions::getHexString(address + i, 8);
		i++;
	} while(peerExists(serial));
	return serial;
}

bool EnOceanCentral::handlePairingRequest(std::string& interfaceId, PEnOceanPacket packet)
{
	try
	{
        std::lock_guard<std::mutex> pairingGuard(_pairingMutex);

		std::shared_ptr<IEnOceanInterface> physicalInterface = GD::interfaces->getInterface(interfaceId);

		std::vector<uint8_t> payload = packet->getData();
		if(packet->getRorg() == 0xD4) //UTE
		{
			if(payload.size() < 8) return false;

			uint64_t eep = ((uint64_t)(uint8_t)payload.at(7) << 16u) | (((uint64_t)(uint8_t)payload.at(6)) << 8u) | ((uint8_t)payload.at(5));
			uint64_t manufacturer = (((uint64_t)(uint8_t)payload.at(4) & 0x07u) << 8u) | (uint8_t)payload.at(3);
			uint64_t manufacturerEep = (manufacturer << 24u) | eep;

			uint8_t byte1 = payload.at(1);
			if((byte1 & 0x0Fu) != 0) return false; //Command 0 => teach-in request
			if(!(byte1 & 0x80u))
			{
				std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
				_pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.unsupportedUnidirectionalCommunication"));
				GD::out.printWarning("Warning: Could not teach-in device as it expects currently unsupported unidirectional communication.");
				return false;
			}
			bool responseExpected = !(byte1 & 0x40u);
			if((byte1 & 0x30u) == 0x10)
			{
				GD::out.printWarning("Warning: Could not teach-out device as the teach-out request is currently unsupported.");
				return false;
			}

			int32_t rfChannel = 0;
			if(!peerExists(packet->senderAddress(), eep) && !peerExists(packet->senderAddress(), manufacturerEep))
			{
				buildPeer(manufacturerEep, packet->senderAddress(), interfaceId, -1);
			}
			else
			{
				std::list<PMyPeer> peers = getPeer(packet->senderAddress());
				if(peers.empty()) return false;
				for(auto& peer : peers)
				{
					if(peer->getDeviceType() == eep || peer->getDeviceType() == manufacturerEep)
					{
						rfChannel = peer->getRfChannel(0);
						break;
					}
				}
			}

			if(responseExpected)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				PEnOceanPacket response(new EnOceanPacket((EnOceanPacket::Type)1, packet->getRorg(), physicalInterface->getBaseAddress() | rfChannel, packet->senderAddress()));
				std::vector<uint8_t> responsePayload;
				responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 8);
				responsePayload.at(1) = (responsePayload.at(1) & 0x80u) | 0x11u; // Command 1 => teach-in response
				response->setData(responsePayload);
				physicalInterface->sendEnoceanPacket(response);
			}
            return true;
		}
		else if(payload.size() >= 5 && packet->getRorg() == 0xA5 && (payload.at(4) & 0x88u) == 0x80) //4BS teach-in, variant 3; LRN type bit needs to be set and LRN bit unset (= LRN telegram)
		{
			uint64_t eep = ((uint32_t)(uint8_t)payload.at(0) << 16u) | (((uint32_t)(uint8_t)payload.at(1) >> 2u) << 8u) | (((uint8_t)payload.at(1) & 3u) << 5u) | (uint8_t)((uint8_t)payload.at(2) >> 3u);
            uint64_t manufacturer = (((uint32_t)(uint8_t)(payload.at(2) & 7u)) << 8u) | (uint8_t)payload.at(3);
            uint64_t manufacturerEep = (manufacturer << 24u) | eep;
            //In EEP version 3 we need the full bytes for the eep, so the following is deprecated
            uint64_t manufacturerEepOld = ((manufacturer & 0xFFu) << 24u) | ((manufacturer >> 9u) << 14u) | (((manufacturer >> 8u) & 1u) << 7u) | eep;
			std::string serial = getFreeSerialNumber(packet->senderAddress());

			if(!peerExists(packet->senderAddress(), eep) && !peerExists(packet->senderAddress(), manufacturerEep) && !peerExists(packet->senderAddress(), manufacturerEepOld))
			{
				int32_t rfChannel = getFreeRfChannel(interfaceId);
				if(rfChannel == -1)
				{
					std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
					_pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.noFreeRfChannels"));
					GD::out.printError("Error: Could not pair peer, because there are no free RF channels.");
					return false;
				}
				GD::out.printInfo("Info: Trying to pair peer with EEP " + BaseLib::HelperFunctions::getHexString(manufacturerEep) + ".");
				std::shared_ptr<EnOceanPeer> peer = createPeer(manufacturerEep, packet->senderAddress(), serial, false);
				if(!peer || !peer->getRpcDevice())
                {
                    GD::out.printInfo("Info: Trying to pair peer with EEP " + BaseLib::HelperFunctions::getHexString(eep) + ".");
                    peer = createPeer(manufacturerEepOld, packet->senderAddress(), serial, false);
                    if(!peer || !peer->getRpcDevice())
                    {
						std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
                        _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.unsupportedEep", std::list<std::string>{ BaseLib::HelperFunctions::getHexString(eep) }));
                        GD::out.printWarning("Warning: The EEP " + BaseLib::HelperFunctions::getHexString(manufacturerEepOld) + " is currently not supported.");
                        return false;
                    }
                }
				try
				{
					std::unique_lock<std::mutex> peersGuard(_peersMutex);
					if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
					peersGuard.unlock();
					peer->save(true, true, false);
					peer->initializeCentralConfig();
					peer->setPhysicalInterfaceId(interfaceId);
					if(peer->hasRfChannel(0)) peer->setRfChannel(0, rfChannel);
					peersGuard.lock();
					_peers[peer->getAddress()].push_back(peer);
					_peersById[peer->getID()] = peer;
					peersGuard.unlock();
				}
				catch(const std::exception& ex)
				{
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}

                if(peer->hasRfChannel(0))
                {
                    PEnOceanPacket response(new EnOceanPacket((EnOceanPacket::Type) 1, packet->getRorg(), physicalInterface->getBaseAddress() | peer->getRfChannel(0), 0xFFFFFFFF));
                    std::vector<uint8_t> responsePayload;
                    responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 5);
                    responsePayload.back() = 0xF0;
                    response->setData(responsePayload);
                    physicalInterface->sendEnoceanPacket(response);
                }

				PVariable deviceDescriptions(new Variable(VariableType::tArray));
				deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
				std::vector<uint64_t> newIds{ peer->getID() };
				raiseRPCNewDevices(newIds, deviceDescriptions);

				{
					auto pairingState = std::make_shared<PairingState>();
					pairingState->peerId = peer->getID();
					pairingState->state = "success";
					std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
					_newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
				}

				GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
			}
			else
			{
				uint32_t rfChannel = 0;
				std::list<PMyPeer> peers = getPeer(packet->senderAddress());
				if(peers.empty()) return false;
				for(auto& peer : peers)
				{
					if(peer->getDeviceType() == eep || peer->getDeviceType() == manufacturerEep || peer->getDeviceType() == manufacturerEepOld)
					{
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
		}
		else
        {
            {
                std::lock_guard<std::mutex> processedAddressesGuard(_processedAddressesMutex);
                if(_processedAddresses.find(packet->senderAddress()) != _processedAddresses.end()) return false;
                _processedAddresses.emplace(packet->senderAddress());
            }

		    //Try remote commissioning
		    if(!peerExists(packet->senderAddress()))
            {
                if(_remoteCommissioningDeviceAddress == 0 || _remoteCommissioningDeviceAddress == (unsigned)packet->senderAddress())
                {
                    GD::out.printInfo("Info: Pushing address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 8) + " to remote commissioning queue.");
                    _remoteCommissioningAddressQueue.push(std::make_pair(interfaceId, packet->senderAddress()));
                }
            }
        }
		return false;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void EnOceanCentral::savePeers(bool full)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
		{
			GD::out.printInfo("Info: Saving EnOcean peer " + std::to_string(i->second->getID()));
			i->second->save(full, full, full);
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanCentral::deletePeer(uint64_t id)
{
	try
	{
		std::shared_ptr<EnOceanPeer> peer(getPeer(id));
		if(!peer) return;
		peer->deleting = true;
		PVariable deviceAddresses(new Variable(VariableType::tArray));
		deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber())));

		PVariable deviceInfo(new Variable(VariableType::tStruct));
		deviceInfo->structValue->insert(StructElement("ID", PVariable(new Variable((int32_t)peer->getID()))));
		PVariable channels(new Variable(VariableType::tArray));
		deviceInfo->structValue->insert(StructElement("CHANNELS", channels));

		for(Functions::iterator i = peer->getRpcDevice()->functions.begin(); i != peer->getRpcDevice()->functions.end(); ++i)
		{
			deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber() + ":" + std::to_string(i->first))));
			channels->arrayValue->push_back(PVariable(new Variable(i->first)));
		}

		std::vector<uint64_t> deletedIds{ id };
		raiseRPCDeleteDevices(deletedIds, deviceAddresses, deviceInfo);

		if(peer->getRpcDevice()->addressSize == 25)
		{
			std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
			auto peerIterator = _wildcardPeers.find(peer->getAddress());
			if(peerIterator != _wildcardPeers.end())
			{
				for(std::list<PMyPeer>::iterator element = peerIterator->second.begin(); element != peerIterator->second.end(); ++element)
				{
					if((*element)->getID() == peer->getID())
					{
						peerIterator->second.erase(element);
						break;
					}
				}
				if(peerIterator->second.empty()) _wildcardPeers.erase(peerIterator);
			}
		}

        {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            if(_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
            if(_peersById.find(id) != _peersById.end()) _peersById.erase(id);
            auto peerIterator = _peers.find(peer->getAddress());
            if(peerIterator != _peers.end())
            {
                for(std::list<PMyPeer>::iterator element = peerIterator->second.begin(); element != peerIterator->second.end(); ++element)
                {
                    if((*element)->getID() == peer->getID())
                    {
                        peerIterator->second.erase(element);
                        break;
                    }
                }
                if(peerIterator->second.empty()) _peers.erase(peerIterator);
            }
        }

        int32_t i = 0;
        while(peer.use_count() > 1 && i < 600)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            i++;
        }
        if(i == 600) GD::out.printError("Error: Peer deletion took too long.");

        peer->deleteFromDatabase();

		GD::out.printMessage("Removed EnOcean peer " + std::to_string(peer->getID()));
	}
	catch(const std::exception& ex)
    {
		_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::string EnOceanCentral::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;
		std::vector<std::string> arguments;
		bool showHelp = false;
		if(BaseLib::HelperFunctions::checkCliCommand(command, "help", "h", "", 0, arguments, showHelp))
		{
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
			stringStream << "unselect (u)               Unselect this device" << std::endl;
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "pairing on", "pon", "", 0, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command enables pairing mode." << std::endl;
				stringStream << "Usage: pairing on [DURATION]" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  DURATION: Optional duration in seconds to stay in pairing mode." << std::endl;
				return stringStream.str();
			}

			int32_t duration = 60;
			if(!arguments.empty())
			{
				duration = BaseLib::Math::getNumber(arguments.at(0), false);
				if(duration < 5 || duration > 3600) return "Invalid duration. Duration has to be greater than 5 and less than 3600.\n";
			}

			setInstallMode(nullptr, true, duration, nullptr, false);
			stringStream << "Pairing mode enabled." << std::endl;
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "pairing off", "pof", "", 0, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command disables pairing mode." << std::endl;
				stringStream << "Usage: pairing off" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  There are no parameters." << std::endl;
				return stringStream.str();
			}

			setInstallMode(nullptr, false, -1, nullptr, false);
			stringStream << "Pairing mode disabled." << std::endl;
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers create", "pc", "", 3, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command creates a new peer." << std::endl;
				stringStream << "Usage: peers create INTERFACE TYPE ADDRESS" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  INTERFACE: The id of the interface to associate the new device to as defined in the familie's configuration file." << std::endl;
				stringStream << "  TYPE:      The 3 or 4 byte hexadecimal device type (for most devices the EEP number). Example: 0xF60201" << std::endl;
				stringStream << "  ADDRESS:   The 4 byte address/ID printed on the device. Example: 0x01952B7A" << std::endl;
				return stringStream.str();
			}

			std::string interfaceId = arguments.at(0);
			if(!GD::interfaces->hasInterface(interfaceId)) return "Unknown physical interface.\n";
			uint64_t deviceType = BaseLib::Math::getUnsignedNumber64(arguments.at(1), true);
			if(deviceType == 0) return "Invalid device type. Device type has to be provided in hexadecimal format.\n";
			uint32_t address = BaseLib::Math::getNumber(arguments.at(2), true);
			std::string serial = getFreeSerialNumber(address);

			if(peerExists(address, deviceType)) stringStream << "A peer with this address and EEP is already paired to this central." << std::endl;
			else
			{
				std::shared_ptr<EnOceanPeer> peer = createPeer(deviceType, address, serial, false);
				if(!peer || !peer->getRpcDevice()) return "Device type not supported.\n";
				try
				{
					if(peer->getRpcDevice()->addressSize == 25) peer->setAddress(address & 0xFFFFFF80u);
					_peersMutex.lock();
					if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
					_peersMutex.unlock();
					peer->save(true, true, false);
					peer->initializeCentralConfig();
					peer->setPhysicalInterfaceId(interfaceId);
					_peersMutex.lock();
					_peers[peer->getAddress()].push_back(peer);
					_peersById[peer->getID()] = peer;
					_peersMutex.unlock();
					if(peer->getRpcDevice()->addressSize == 25)
					{
						std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
						_wildcardPeers[peer->getAddress()].push_back(peer);
					}
				}
				catch(const std::exception& ex)
				{
					_peersMutex.unlock();
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}

				PVariable deviceDescriptions(new Variable(VariableType::tArray));
				deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
				std::vector<uint64_t> newIds{ peer->getID() };
				raiseRPCNewDevices(newIds, deviceDescriptions);

				{
					auto pairingState = std::make_shared<PairingState>();
					pairingState->peerId = peer->getID();
					pairingState->state = "success";
					std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
					_newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
				}

				GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
				stringStream << "Added peer " << std::to_string(peer->getID()) << " with address 0x" << BaseLib::HelperFunctions::getHexString(peer->getAddress(), 8) << " and serial number " << serial << "." << std::dec << std::endl;
			}
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers remove", "pr", "prm", 1, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command removes a peer." << std::endl;
				stringStream << "Usage: peers remove PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID: The id of the peer to remove. Example: 513" << std::endl;
				return stringStream.str();
			}

			uint64_t peerID = BaseLib::Math::getNumber(arguments.at(0), false);
			if(peerID == 0) return "Invalid id.\n";

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				stringStream << "Removing peer " << std::to_string(peerID) << std::endl;
				deletePeer(peerID);
			}
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers list", "pl", "ls", 0, arguments, showHelp))
		{
			try
			{
				if(showHelp)
				{
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

				if(arguments.size() >= 2)
				{
					 filterType = BaseLib::HelperFunctions::toLower(arguments.at(0));
					 filterValue = arguments.at(1);
					 if(filterType == "name") BaseLib::HelperFunctions::toLower(filterValue);
				}

				if(_peersById.empty())
				{
					stringStream << "No peers are paired to this central." << std::endl;
					return stringStream.str();
				}
				std::string bar(" │ ");
				const int32_t idWidth = 8;
				const int32_t nameWidth = 25;
				const int32_t serialWidth = 13;
				const int32_t addressWidth = 8;
				const int32_t typeWidth1 = 8;
				const int32_t typeWidth2 = 45;
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
					<< typeStringHeader
					<< std::endl;
				stringStream << "─────────┼───────────────────────────┼───────────────┼──────────┼──────────┼───────────────────────────────────────────────" << std::endl;
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << " " << bar
					<< std::setw(nameWidth) << " " << bar
					<< std::setw(serialWidth) << " " << bar
					<< std::setw(addressWidth) << " " << bar
					<< std::setw(typeWidth1) << " " << bar
					<< std::setw(typeWidth2)
					<< std::endl;
				_peersMutex.lock();
				for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
				{
					if(filterType == "id")
					{
						uint64_t id = BaseLib::Math::getNumber(filterValue, false);
						if(i->second->getID() != id) continue;
					}
					else if(filterType == "name")
					{
						std::string name = i->second->getName();
						if((signed)BaseLib::HelperFunctions::toLower(name).find(filterValue) == (signed)std::string::npos) continue;
					}
					else if(filterType == "serial")
					{
						if(i->second->getSerialNumber() != filterValue) continue;
					}
					else if(filterType == "address")
					{
						int32_t address = BaseLib::Math::getNumber(filterValue, true);
						if(i->second->getAddress() != address) continue;
					}
					else if(filterType == "type")
					{
						int32_t deviceType = BaseLib::Math::getNumber(filterValue, true);
						if((int32_t)i->second->getDeviceType() != deviceType) continue;
					}

					stringStream << std::setw(idWidth) << std::setfill(' ') << std::to_string(i->second->getID()) << bar;
					std::string name = i->second->getName();
					size_t nameSize = BaseLib::HelperFunctions::utf8StringSize(name);
					if(nameSize > (unsigned)nameWidth)
					{
						name = BaseLib::HelperFunctions::utf8Substring(name, 0, nameWidth - 3);
						name += "...";
					}
					else name.resize(nameWidth + (name.size() - nameSize), ' ');
					stringStream << name << bar
						<< std::setw(serialWidth) << i->second->getSerialNumber() << bar
						<< std::setw(addressWidth) << BaseLib::HelperFunctions::getHexString(i->second->getAddress(), 8) << bar
						<< std::setw(typeWidth1) << BaseLib::HelperFunctions::getHexString(i->second->getDeviceType(), 6) << bar;
					if(i->second->getRpcDevice())
					{
						PSupportedDevice type = i->second->getRpcDevice()->getType(i->second->getDeviceType(), i->second->getFirmwareVersion());
						std::string typeID;
						if(type) typeID = type->description;
						if(typeID.size() > (unsigned)typeWidth2)
						{
							typeID.resize(typeWidth2 - 3);
							typeID += "...";
						}
						else typeID.resize(typeWidth2, ' ');
						stringStream << typeID << bar;
					}
					else stringStream << std::setw(typeWidth2);
					stringStream << std::endl << std::dec;
				}
				_peersMutex.unlock();
				stringStream << "─────────┴───────────────────────────┴───────────────┴──────────┴──────────┴───────────────────────────────────────────────" << std::endl;

				return stringStream.str();
			}
			catch(const std::exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
		}
		else if(command.compare(0, 13, "peers setname") == 0 || command.compare(0, 2, "pn") == 0)
		{
			uint64_t peerID = 0;
			std::string name;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'n') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					else
					{
						peerID = BaseLib::Math::getNumber(element, false);
						if(peerID == 0) return "Invalid id.\n";
					}
				}
				else if(index == 2 + offset) name = element;
				else name += ' ' + element;
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command sets or changes the name of a peer to identify it more easily." << std::endl;
				stringStream << "Usage: peers setname PEERID NAME" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to set the name for. Example: 513" << std::endl;
				stringStream << "  NAME:\tThe name to set. Example: \"1st floor light switch\"." << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				std::shared_ptr<EnOceanPeer> peer = getPeer(peerID);
				peer->setName(name);
				stringStream << "Name set to \"" << name << "\"." << std::endl;
			}
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "interface setaddress", "ia", "", 2, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command sets the base address of an EnOcean interface. This can only be done 10 times!" << std::endl;
				stringStream << "Usage: interface setaddress INTERFACE ADDRESS" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  INTERFACE: The id of the interface to set the address for." << std::endl;
				stringStream << "  ADDRESS:   The new 4 byte address/ID starting with 0xFF the 7 least significant bits can't be set. Example: 0xFF422E80" << std::endl;
				return stringStream.str();
			}

			std::string interfaceId = arguments.at(0);
			if(!GD::interfaces->hasInterface(interfaceId)) return "Unknown physical interface.\n";
			uint32_t address = BaseLib::Math::getUnsignedNumber(arguments.at(1), true) & 0xFFFFFF80;

			int32_t result = GD::interfaces->getInterface(interfaceId)->setBaseAddress(address);

			if(result == -1) stringStream << "Error setting base address. See error log for more details." << std::endl;
			else stringStream << "Base address set to 0x" << BaseLib::HelperFunctions::getHexString(address) << ". Remaining changes: " << result << std::endl;

			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "process packet", "pp", "", 2, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command processes the passed packet as it were received from an EnOcean interface" << std::endl;
				stringStream << "Usage: process packet INTERFACE PACKET" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  INTERFACE: The id of the interface to set the address for." << std::endl;
				stringStream << "  ADDRESS:   The hex string of the packet to process." << std::endl;
				return stringStream.str();
			}

			std::string interfaceId = arguments.at(0);
			if(!GD::interfaces->hasInterface(interfaceId)) return "Unknown physical interface.\n";

			std::vector<uint8_t> rawPacket = _bl->hf.getUBinary(arguments.at(1));
			PEnOceanPacket packet = std::make_shared<EnOceanPacket>(rawPacket);
			if(packet->getType() == EnOceanPacket::Type::RADIO_ERP1 || packet->getType() == EnOceanPacket::Type::RADIO_ERP2)
			{
				if((packet->senderAddress() & 0xFFFFFF80) != GD::interfaces->getInterface(interfaceId)->getBaseAddress())
				{
					onPacketReceived(interfaceId, packet);
					stringStream << "Processed packet " << BaseLib::HelperFunctions::getHexString(packet->getBinary()) << std::endl;
				}
			}

			return stringStream.str();
		}
		else return "Unknown command.\n";
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return "Error executing command. See log file for more details.\n";
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::createPeer(uint64_t eep, int32_t address, std::string serialNumber, bool save)
{
	try
	{
	    auto rpcDevice = GD::family->getRpcDevices()->find(eep, 0x10, -1);
	    if(!rpcDevice) rpcDevice = GD::family->getRpcDevices()->find(eep & 0xFFFFFFu, 0x10, -1);
	    if(!rpcDevice) return std::shared_ptr<EnOceanPeer>();

		std::shared_ptr<EnOceanPeer> peer(new EnOceanPeer(_deviceId, this));
		peer->setDeviceType(eep);
		peer->setAddress(address);
		peer->setSerialNumber(serialNumber);
		peer->setRpcDevice(rpcDevice);
		if(!peer->getRpcDevice()) return std::shared_ptr<EnOceanPeer>();
		if(save) peer->save(true, true, false); //Save and create peerID
		return peer;
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::shared_ptr<EnOceanPeer>();
}

std::shared_ptr<EnOceanPeer> EnOceanCentral::buildPeer(uint64_t eep, int32_t address, const std::string& interfaceId, int32_t rfChannel)
{
    try
    {
        std::string serial = getFreeSerialNumber(address);
        if(rfChannel == -1) rfChannel = getFreeRfChannel(interfaceId);
        if(rfChannel == -1)
        {
            std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
            _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.noFreeRfChannels"));
            GD::out.printError("Error: Could not pair peer, because there are no free RF channels.");
            return std::shared_ptr<EnOceanPeer>();
        }
        GD::out.printInfo("Info: Trying to create peer with EEP " + BaseLib::HelperFunctions::getHexString(eep) + " or EEP " + BaseLib::HelperFunctions::getHexString(eep) + ".");
        std::shared_ptr<EnOceanPeer> peer = createPeer(eep, address, serial, false);
        if(!peer || !peer->getRpcDevice())
        {
            std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
            _pairingMessages.emplace_back(std::make_shared<PairingMessage>("l10n.enocean.pairing.unsupportedEep", std::list<std::string>{ BaseLib::HelperFunctions::getHexString(eep) }));
            GD::out.printWarning("Warning: The EEP " + BaseLib::HelperFunctions::getHexString(eep) + " is currently not supported.");
            return std::shared_ptr<EnOceanPeer>();
        }
        try
        {
            std::unique_lock<std::mutex> peersGuard(_peersMutex);
            if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
            peersGuard.unlock();
            peer->save(true, true, false);
            peer->initializeCentralConfig();
            peer->setPhysicalInterfaceId(interfaceId);
            peer->setRfChannel(0, rfChannel);
            peersGuard.lock();
            _peers[peer->getAddress()].push_back(peer);
            _peersById[peer->getID()] = peer;
            peersGuard.unlock();
        }
        catch(const std::exception& ex)
        {
            GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }

        PVariable deviceDescriptions(new Variable(VariableType::tArray));
        deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
        std::vector<uint64_t> newIds{ peer->getID() };
        raiseRPCNewDevices(newIds, deviceDescriptions);

        {
            auto pairingState = std::make_shared<PairingState>();
            pairingState->peerId = peer->getID();
            pairingState->state = "success";
            std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
            _newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
        }

        GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");

        return peer;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::shared_ptr<EnOceanPeer>();
}

PVariable EnOceanCentral::addLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannelIndex, uint64_t receiverID, int32_t receiverChannelIndex, std::string name, std::string description)
{
    try
    {
        if(senderID == 0) return Variable::createError(-2, "Sender id is not set.");
        if(receiverID == 0) return Variable::createError(-2, "Receiver is not set.");
        if(senderID == receiverID) return Variable::createError(-2, "Sender and receiver are the same device.");
        auto sender = getPeer(senderID);
        auto receiver = getPeer(receiverID);
        if(!sender) return Variable::createError(-2, "Sender device not found.");
        if(!receiver) return Variable::createError(-2, "Receiver device not found.");
        if(senderChannelIndex < 0) senderChannelIndex = 0;
        if(receiverChannelIndex < 0) receiverChannelIndex = 0;
        auto senderRpcDevice = sender->getRpcDevice();
        auto receiverRpcDevice = receiver->getRpcDevice();
        auto senderFunctionIterator = senderRpcDevice->functions.find(senderChannelIndex);
        if(senderFunctionIterator == senderRpcDevice->functions.end()) return Variable::createError(-2, "Sender channel not found.");
        auto receiverFunctionIterator = receiverRpcDevice->functions.find(receiverChannelIndex);
        if(receiverFunctionIterator == receiverRpcDevice->functions.end()) return Variable::createError(-2, "Receiver channel not found.");
        auto senderFunction = senderFunctionIterator->second;
        auto receiverFunction = receiverFunctionIterator->second;
        if(senderFunction->linkSenderFunctionTypes.empty() || receiverFunction->linkReceiverFunctionTypes.empty()) return Variable::createError(-6, "Link not supported.");
        bool validLink = false;
        for(auto& senderFunctionType : senderFunction->linkSenderFunctionTypes)
        {
            for(auto& receiverFunctionType : receiverFunction->linkReceiverFunctionTypes)
            {
                if(senderFunctionType == receiverFunctionType)
                {
                    validLink = true;
                    break;
                }
            }
            if(validLink) break;
        }
        if(!validLink) return Variable::createError(-6, "Link not supported.");

        bool supportsSetLinkTable = false;
        if(receiverRpcDevice->metadata)
        {
            auto metadataIterator = receiverRpcDevice->metadata->structValue->find("remoteManagementInfo");
            if(metadataIterator != receiverRpcDevice->metadata->structValue->end() && !metadataIterator->second->arrayValue->empty())
            {
                auto remoteManagementInfo = metadataIterator->second->arrayValue->at(0);
                auto infoIterator = remoteManagementInfo->structValue->find("features");
                if(infoIterator != metadataIterator->second->structValue->end() && !infoIterator->second->arrayValue->empty())
                {
                    auto features = infoIterator->second->arrayValue->at(0);
                    auto featureIterator = features->structValue->find("setLinkTable");
                    if(featureIterator != features->structValue->end() && featureIterator->second->booleanValue)
                    {
                        supportsSetLinkTable = true;
                    }
                    featureIterator = features->structValue->find("setInboundLinkTableSize");
                    if(featureIterator != features->structValue->end())
                    {
                        uint32_t inboundLinkTableSize = featureIterator->second->integerValue;
                        if(receiver->getLinkCount() + 1u > inboundLinkTableSize)
                        {
                            return Variable::createError(-3, "Can't link more devices. You need to unlink a device first.");
                        }
                    }
                }
            }
        }
        if(!supportsSetLinkTable)
        {
            return Variable::createError(-1, "Device does not support links.");
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

        if(!receiver->sendInboundLinkTable())
        {
            sender->removePeer(senderChannelIndex, receiverPeer->address, receiverChannelIndex);
            receiver->removePeer(receiverChannelIndex, senderPeer->address, senderChannelIndex);
            return Variable::createError(-4, "Error updating link table on device.");
        }

        raiseRPCUpdateDevice(sender->getID(), senderChannelIndex, sender->getSerialNumber() + ":" + std::to_string(senderChannelIndex), 1);
        raiseRPCUpdateDevice(receiver->getID(), receiverChannelIndex, receiver->getSerialNumber() + ":" + std::to_string(receiverChannelIndex), 1);

        return std::make_shared<Variable>(VariableType::tVoid);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::removeLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannelIndex, uint64_t receiverID, int32_t receiverChannelIndex)
{
    try
    {
        if(senderID == 0) return Variable::createError(-2, "Sender id is not set.");
        if(receiverID == 0) return Variable::createError(-2, "Receiver id is not set.");
        auto sender = getPeer(senderID);
        auto receiver = getPeer(receiverID);
        if(!sender) return Variable::createError(-2, "Sender device not found.");
        if(!receiver) return Variable::createError(-2, "Receiver device not found.");
        if(senderChannelIndex < 0) senderChannelIndex = 0;
        if(receiverChannelIndex < 0) receiverChannelIndex = 0;
        std::string senderSerialNumber = sender->getSerialNumber();
        std::string receiverSerialNumber = receiver->getSerialNumber();
        std::shared_ptr<HomegearDevice> senderRpcDevice = sender->getRpcDevice();
        std::shared_ptr<HomegearDevice> receiverRpcDevice = receiver->getRpcDevice();
        if(senderRpcDevice->functions.find(senderChannelIndex) == senderRpcDevice->functions.end()) return Variable::createError(-2, "Sender channel not found.");
        if(receiverRpcDevice->functions.find(receiverChannelIndex) == receiverRpcDevice->functions.end()) return Variable::createError(-2, "Receiver channel not found.");
        if(!sender->getPeer(senderChannelIndex, receiver->getAddress()) && !receiver->getPeer(receiverChannelIndex, sender->getAddress())) return Variable::createError(-6, "Devices are not paired to each other.");

        auto receiverPeer = sender->getPeer(senderChannelIndex, receiver->getAddress(), receiverChannelIndex);
        auto senderPeer = receiver->getPeer(receiverChannelIndex, sender->getAddress(), senderChannelIndex);

        sender->removePeer(senderChannelIndex, receiver->getAddress(), receiverChannelIndex);
        receiver->removePeer(receiverChannelIndex, sender->getAddress(), senderChannelIndex);

        if(!receiver->sendInboundLinkTable())
        {
            sender->addPeer(senderChannelIndex, receiverPeer);
            receiver->addPeer(receiverChannelIndex, senderPeer);
            return Variable::createError(-4, "Error updating link table on device.");
        }

        raiseRPCUpdateDevice(sender->getID(), senderChannelIndex, senderSerialNumber + ":" + std::to_string(senderChannelIndex), 1);
        raiseRPCUpdateDevice(receiver->getID(), receiverChannelIndex, receiverSerialNumber + ":" + std::to_string(receiverChannelIndex), 1);

        return std::make_shared<Variable>(VariableType::tVoid);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::createDevice(BaseLib::PRpcClientInfo clientInfo, int32_t deviceType, std::string serialNumber, int32_t address, int32_t firmwareVersion, std::string interfaceId)
{
	try
	{
		std::string serial = getFreeSerialNumber(address);
		if(peerExists(deviceType, address)) return Variable::createError(-5, "This peer is already paired to this central.");

		if(!interfaceId.empty() && !GD::interfaces->hasInterface(interfaceId)) return Variable::createError(-6, "Unknown physical interface.");
        if(interfaceId.empty())
        {
            if(GD::interfaces->count() > 1) return Variable::createError(-7, "Please specify the ID of the physical interface (= communication module) to use.");
        }

		std::shared_ptr<EnOceanPeer> peer = createPeer(deviceType, address, serial, false);
		if(!peer || !peer->getRpcDevice()) return Variable::createError(-6, "Unknown device type.");

		try
		{
			if(peer->getRpcDevice()->addressSize == 25) peer->setAddress(address & 0xFFFFFF80);
			peer->save(true, true, false);
			peer->initializeCentralConfig();
			peer->setPhysicalInterfaceId(interfaceId);
			_peersMutex.lock();
			_peers[peer->getAddress()].push_back(peer);
			_peersById[peer->getID()] = peer;
			_peersBySerial[peer->getSerialNumber()] = peer;
			_peersMutex.unlock();
			if(peer->getRpcDevice()->addressSize == 25)
			{
				std::lock_guard<std::mutex> wildcardPeersGuard(_wildcardPeersMutex);
				_wildcardPeers[peer->getAddress()].push_back(peer);
			}
		}
		catch(const std::exception& ex)
		{
			_peersMutex.unlock();
			GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
		}

		PVariable deviceDescriptions(new Variable(VariableType::tArray));
		deviceDescriptions->arrayValue = peer->getDeviceDescriptions(clientInfo, true, std::map<std::string, bool>());
		std::vector<uint64_t> newIds{ peer->getID() };
		raiseRPCNewDevices(newIds, deviceDescriptions);

		{
			auto pairingState = std::make_shared<PairingState>();
			pairingState->peerId = peer->getID();
			pairingState->state = "success";
			std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
			_newPeers[BaseLib::HelperFunctions::getTime()].emplace_back(std::move(pairingState));
		}

		GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");

		return std::make_shared<Variable>((uint32_t)peer->getID());
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::createDevice(BaseLib::PRpcClientInfo clientInfo, const std::string& code)
{
    try
    {
        if(code.empty()) return Variable::createError(-1, "Code is empty.");

        std::regex regex(R"([0-9A-F]{2}S[0-9A-F]{12}\+[0-9A-F]P[0-9A-F]{12}\+[0-9A-F]{2}Z[0-9A-F]{2}\+[0-9A-F]{2}Z[0-9A-F]{8})");
        if(!std::regex_match(code, regex)) return Variable::createError(-1, "Unknown code.");

        GD::out.printInfo("Info: OPUS BRiDGE QR code detected.");

        //OPUS BRiDGE QR code looks like this:
        // - Actuators:     30S0000050BFB53+1P004000000006+10Z00+11Z6FEC6172
        // - Motion sensor: 30S0000050E71BB+1P004000000009+10Z00+11Z535A4C2C+S---
        // - Remote:        30S00000035E6E4+1P00401000000A

        uint32_t address = BaseLib::Math::getUnsignedNumber(code.substr(7, 8), true);
        uint32_t securityCode = BaseLib::Math::getUnsignedNumber(code.substr(40, 8), true);

        auto interface = GD::interfaces->getDefaultInterface();
        if(interface)
        {
            auto eep = remoteManagementGetEep(interface, address, securityCode);
            if(eep == 0) return Variable::createError(-1, "Could not get EEP.");

            auto peerId = remoteCommissionPeer(interface, address, eep, securityCode, interface->getAddress());
            if(peerId != 0) return std::make_shared<BaseLib::Variable>(peerId);
        }

        return Variable::createError(-1, "Could not create peer.");
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags)
{
	try
	{
		if(serialNumber.empty()) return Variable::createError(-2, "Unknown device.");

        uint64_t peerId = 0;

        {
            std::shared_ptr<EnOceanPeer> peer = getPeer(serialNumber);
            if(!peer) return std::make_shared<Variable>(VariableType::tVoid);
            peerId = peer->getID();
        }

		return deleteDevice(clientInfo, peerId, flags);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, int32_t flags)
{
	try
	{
		if(peerId == 0) return Variable::createError(-2, "Unknown device.");

        {
            std::shared_ptr<EnOceanPeer> peer = getPeer(peerId);
            if(!peer) return std::make_shared<Variable>(VariableType::tVoid);
        }

		deletePeer(peerId);

		if(peerExists(peerId)) return Variable::createError(-1, "Error deleting peer. See log for more details.");

		return std::make_shared<Variable>(VariableType::tVoid);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::getPairingState(BaseLib::PRpcClientInfo clientInfo)
{
    try
    {
		auto states = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

        states->structValue->emplace("pairingModeEnabled", std::make_shared<BaseLib::Variable>(_pairing));
        states->structValue->emplace("pairingModeEndTime", std::make_shared<BaseLib::Variable>(BaseLib::HelperFunctions::getTimeSeconds() + _timeLeftInPairingMode));

		{
			std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);

			auto pairingMessages = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
			pairingMessages->arrayValue->reserve(_pairingMessages.size());
			for(auto& message : _pairingMessages)
			{
				auto pairingMessage = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
				pairingMessage->structValue->emplace("messageId", std::make_shared<BaseLib::Variable>(message->messageId));
				auto variables = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
				variables->arrayValue->reserve(message->variables.size());
				for(auto& variable : message->variables)
				{
					variables->arrayValue->emplace_back(std::make_shared<BaseLib::Variable>(variable));
				}
				pairingMessage->structValue->emplace("variables", variables);
				pairingMessages->arrayValue->push_back(pairingMessage);
			}
			states->structValue->emplace("general", std::move(pairingMessages));

			for(auto& element : _newPeers)
			{
				for(auto& peer : element.second)
				{
					auto peerState = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
					peerState->structValue->emplace("state", std::make_shared<BaseLib::Variable>(peer->state));
					peerState->structValue->emplace("messageId", std::make_shared<BaseLib::Variable>(peer->messageId));
					auto variables = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
					variables->arrayValue->reserve(peer->variables.size());
					for(auto& variable : peer->variables)
					{
						variables->arrayValue->emplace_back(std::make_shared<BaseLib::Variable>(variable));
					}
					peerState->structValue->emplace("variables", variables);
					states->structValue->emplace(std::to_string(peer->peerId), std::move(peerState));
				}
			}
		}

		return states;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::getSniffedDevices(BaseLib::PRpcClientInfo clientInfo)
{
	try
	{
		PVariable array(new Variable(VariableType::tArray));

		std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
		array->arrayValue->reserve(_sniffedPackets.size());
		for(auto peerPackets : _sniffedPackets)
		{
			PVariable info(new Variable(VariableType::tStruct));
			array->arrayValue->push_back(info);

			info->structValue->insert(StructElement("FAMILYID", std::make_shared<Variable>(MY_FAMILY_ID)));
			info->structValue->insert(StructElement("ADDRESS", std::make_shared<Variable>(peerPackets.first)));
			if(!peerPackets.second.empty()) info->structValue->insert(StructElement("RORG", std::make_shared<Variable>(peerPackets.second.back()->getRorg())));
			if(!peerPackets.second.empty()) info->structValue->insert(StructElement("RSSI", std::make_shared<Variable>(peerPackets.second.back()->getRssi())));

			PVariable packets(new Variable(VariableType::tArray));
			info->structValue->insert(StructElement("PACKETS", packets));

			for(const auto& packet : peerPackets.second)
			{
				PVariable packetInfo(new Variable(VariableType::tStruct));
				packetInfo->structValue->insert(StructElement("TIME_RECEIVED", std::make_shared<Variable>(packet->getTimeReceived() / 1000)));
				packetInfo->structValue->insert(StructElement("PACKET", std::make_shared<Variable>(BaseLib::HelperFunctions::getHexString(packet->getBinary()))));
				packets->arrayValue->push_back(packetInfo);
			}
		}
		return array;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

void EnOceanCentral::pairingModeTimer(int32_t duration, bool debugOutput)
{
	try
	{
		_pairing = true;
		if(debugOutput) GD::out.printInfo("Info: Pairing mode enabled.");
		_timeLeftInPairingMode = duration;
		int64_t startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		int64_t timePassed = 0;
		while(timePassed < ((int64_t)duration * 1000) && !_stopPairingModeThread)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - startTime;
			_timeLeftInPairingMode = duration - (timePassed / 1000);
            handleRemoteCommissioningQueue();
		}
		_timeLeftInPairingMode = 0;
		_pairing = false;
		if(debugOutput) GD::out.printInfo("Info: Pairing mode disabled.");
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanCentral::handleRemoteCommissioningQueue()
{
    try
    {
        std::lock_guard<std::mutex> pairingGuard(_pairingMutex);

        int32_t deviceAddress = 0;
        uint64_t eep = 0;

        std::shared_ptr<IEnOceanInterface> interface;
        if(!_pairingInterface.empty()) interface = GD::interfaces->getInterface(_pairingInterface);

        if(!_remoteCommissioningAddressQueue.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            deviceAddress = _remoteCommissioningAddressQueue.front().second;
            auto interfaceId = _remoteCommissioningAddressQueue.front().first;
            _remoteCommissioningAddressQueue.pop();

            if(!interface) interface = GD::interfaces->getInterface(interfaceId);
            if(!interface) interface = GD::interfaces->getDefaultInterface();

            eep = remoteManagementGetEep(interface, deviceAddress, _remoteCommissioningSecurityCode);
        }
        else if(_remoteCommissioningDeviceAddress != 0 && _remoteCommissioningEep != 0)
        {
            _stopPairingModeThread = true;
            deviceAddress = _remoteCommissioningDeviceAddress;
            eep = _remoteCommissioningEep;
        }
        else return;

        if(!interface) interface = GD::interfaces->getDefaultInterface();

        remoteCommissionPeer(interface, deviceAddress, eep, _remoteCommissioningSecurityCode, _remoteCommissioningGatewayAddress);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

uint64_t EnOceanCentral::remoteManagementGetEep(const std::shared_ptr<IEnOceanInterface>& interface, uint32_t deviceAddress, uint32_t securityCode)
{
    try
    {
        if(!interface) return 0;

        uint64_t eep = 0;

        if(securityCode != 0)
        {
            auto unlock = std::make_shared<Unlock>(deviceAddress, securityCode);
            interface->sendEnoceanPacket(unlock);
            interface->sendEnoceanPacket(unlock);

            auto queryStatus = std::make_shared<QueryStatusPacket>(deviceAddress);
            auto response = interface->sendAndReceivePacket(queryStatus, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse}});

            if(!response) return 0;
            auto queryStatusData = response->getData();

            bool codeSet = queryStatusData.at(4) & 0x80u;
            auto lastFunctionNumber = (uint16_t)((uint16_t)(queryStatusData.at(5) & 0x0Fu) << 8u) | queryStatusData.at(6);
            if(lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::unlock || (codeSet && queryStatusData.at(7) != (uint8_t)EnOceanPacket::QueryStatusReturnCode::ok))
            {
                GD::out.printWarning("Warning: Error unlocking device.");
                return 0;
            }
        }

        {
            auto queryId = std::make_shared<QueryIdPacket>(deviceAddress);
            auto response = interface->sendAndReceivePacket(queryId, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{0x06, 0x04},
                                                                                                                                                {0x07, 0x04}});
            if(!response)
            {
                if(securityCode != 0)
                {
                    auto lock = std::make_shared<Lock>(deviceAddress, securityCode);
                    interface->sendEnoceanPacket(lock);
                    interface->sendEnoceanPacket(lock);
                }
                return 0;
            }

            GD::out.printInfo("Info: Got query ID response.");
            auto queryIdData = response->getData();
            eep = ((uint64_t)response->getRemoteManagementManufacturer() << 24u) | (unsigned)((uint32_t)queryIdData.at(4) << 16u) | (unsigned)((uint16_t)(queryIdData.at(5) >> 2u) << 8u) | (((uint8_t)queryIdData.at(5) & 3u) << 5u) | (uint8_t)((uint8_t)queryIdData.at(6) >> 3u);
        }

        return eep;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return 0;
}

uint64_t EnOceanCentral::remoteCommissionPeer(const std::shared_ptr<IEnOceanInterface>& interface, uint32_t deviceAddress, uint64_t eep, uint32_t securityCode, uint32_t gatewayAddress)
{
    try
    {
        if(!interface) return 0;
        GD::out.printInfo("Info: Trying to pair device with address " + BaseLib::HelperFunctions::getHexString(deviceAddress, 8) + " and EEP " + BaseLib::HelperFunctions::getHexString(eep) + "...");

        if(peerExists((int32_t)deviceAddress, eep))
        {
            if(securityCode != 0)
            {
                auto lock = std::make_shared<Lock>(deviceAddress, securityCode);
                interface->sendEnoceanPacket(lock);
                interface->sendEnoceanPacket(lock);
            }

            GD::out.printInfo("Info: Peer is already paired to this central.");
            auto peer = getPeer((int32_t)deviceAddress);
            return !peer.empty() ? peer.front()->getID() : 0;
        }

        auto rpcDevice = GD::family->getRpcDevices()->find(eep, 0x10, -1);
        if(!rpcDevice) rpcDevice = GD::family->getRpcDevices()->find(eep & 0xFFFFFFu, 0x10, -1);
        if(!rpcDevice)
        {
            GD::out.printWarning("Warning: No device description found for EEP " + BaseLib::HelperFunctions::getHexString(eep) + " or EEP "  + BaseLib::HelperFunctions::getHexString(eep & 0xFFFFFFu) + ". Aborting pairing.");
            if(securityCode != 0)
            {
                auto lock = std::make_shared<Lock>(deviceAddress, securityCode);
                interface->sendEnoceanPacket(lock);
                interface->sendEnoceanPacket(lock);
            }
            return 0;
        }

        bool sendsAck = false;
        bool setLinkTableHasIndex = true;
        uint32_t setInboundLinkTableSize = 1u;
        uint32_t setOutboundLinkTableSize = 0u;
        uint32_t linkTableGatewayEep = 0xA53808u;
        bool hasSetRepeaterFunctions = false;

        if(rpcDevice->metadata)
        {
            auto metadataIterator = rpcDevice->metadata->structValue->find("remoteManagementInfo");
            if(metadataIterator != rpcDevice->metadata->structValue->end() && !metadataIterator->second->arrayValue->empty())
            {
                auto remoteManagementInfo = metadataIterator->second->arrayValue->at(0);
                auto infoIterator = remoteManagementInfo->structValue->find("features");
                if(infoIterator != metadataIterator->second->structValue->end() && !infoIterator->second->arrayValue->empty())
                {
                    auto features = infoIterator->second->arrayValue->at(0);
                    auto featureIterator = features->structValue->find("setLinkTable");
                    if(featureIterator == features->structValue->end() || !featureIterator->second->booleanValue)
                    {
                        GD::out.printWarning("Warning: EEP " + BaseLib::HelperFunctions::getHexString(eep) + " does not support \"Set Link Table\". Cannot pair.");
                        return 0;
                    }

                    featureIterator = features->structValue->find("sendsAck");
                    if(featureIterator != features->structValue->end()) sendsAck = featureIterator->second->booleanValue;

                    featureIterator = features->structValue->find("setLinkTableHasIndex");
                    if(featureIterator != features->structValue->end()) setLinkTableHasIndex = featureIterator->second->booleanValue;

                    featureIterator = features->structValue->find("setInboundLinkTableSize");
                    if(featureIterator != features->structValue->end()) setInboundLinkTableSize = featureIterator->second->integerValue;

                    featureIterator = features->structValue->find("setOutboundLinkTableSize");
                    if(featureIterator != features->structValue->end()) setOutboundLinkTableSize = featureIterator->second->integerValue;

                    featureIterator = features->structValue->find("linkTableGatewayEep");
                    if(featureIterator != features->structValue->end()) linkTableGatewayEep = BaseLib::Math::getUnsignedNumber(featureIterator->second->stringValue, true);

                    featureIterator = features->structValue->find("setRepeaterFunctions");
                    if(featureIterator != features->structValue->end()) hasSetRepeaterFunctions = featureIterator->second->booleanValue;
                }
            }
        }

        if(sendsAck) GD::out.printInfo("Info: EEP " + BaseLib::HelperFunctions::getHexString(eep) + " supports remote management ACKs.");
        else  GD::out.printInfo("Info: EEP " + BaseLib::HelperFunctions::getHexString(eep) + " does not support remote management ACKs. Using \"Query Status\" instead.");

        if(!setLinkTableHasIndex) GD::out.printInfo("Info: Using \"Set Link Table\" without index.");

        if(securityCode != 0)
        {
            auto unlock = std::make_shared<Unlock>(deviceAddress, securityCode);
            interface->sendEnoceanPacket(unlock);
            interface->sendEnoceanPacket(unlock);

            auto queryStatus = std::make_shared<QueryStatusPacket>(deviceAddress);
            auto response = interface->sendAndReceivePacket(queryStatus, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse}});

            if(!response) return 0;
            auto queryStatusData = response->getData();

            bool codeSet = queryStatusData.at(4) & 0x80u;
            auto lastFunctionNumber = (uint16_t)((uint16_t)(queryStatusData.at(5) & 0x0Fu) << 8u) | queryStatusData.at(6);
            if(lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::unlock || (codeSet && queryStatusData.at(7) != (uint8_t)EnOceanPacket::QueryStatusReturnCode::ok))
            {
                GD::out.printWarning("Warning: Error unlocking device.");
                return 0;
            }
        }

        bool pairingSuccessful = false;
        int32_t rfChannel = -1;

        //{{{ //Set inbound link table (pairing)
        {
            rfChannel = getFreeRfChannel(interface->getID());
            if(rfChannel == -1)
            {
                GD::out.printError("Error: Could not get free RF channel.");
                return 0;
            }

            std::vector<uint8_t> linkTable{};
            linkTable.reserve(9 * setInboundLinkTableSize);
            if(setLinkTableHasIndex) linkTable.push_back(0);
            if(gatewayAddress == 0) gatewayAddress = (uint32_t)GD::interfaces->getDefaultInterface()->getAddress();
            linkTable.push_back(gatewayAddress >> 24u);
            linkTable.push_back(gatewayAddress >> 16u);
            linkTable.push_back(gatewayAddress >> 8u);
            linkTable.push_back(gatewayAddress | (uint8_t)rfChannel);
            linkTable.push_back(linkTableGatewayEep >> 16u);
            linkTable.push_back(linkTableGatewayEep >> 8u);
            linkTable.push_back(linkTableGatewayEep);
            linkTable.push_back(0);
            for(uint32_t i = 1; i < setInboundLinkTableSize; i++)
            {
                if(setLinkTableHasIndex) linkTable.push_back(i);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0);
                linkTable.push_back(0);
                linkTable.push_back(0);
                linkTable.push_back(0);
            }
            auto setLinkTable = std::make_shared<SetLinkTable>(deviceAddress, true, linkTable);

            if(sendsAck)
            {
                auto response = interface->sendAndReceivePacket(setLinkTable, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
                pairingSuccessful = (bool)response;
            }
            else
            {
                //Remote commissioning ACK in response to query status is needed for Laights actuators
                interface->sendEnoceanPacket(setLinkTable);
                auto queryStatus = std::make_shared<QueryStatusPacket>(deviceAddress);
                auto response = interface->sendAndReceivePacket(queryStatus, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u,    (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse},
                                                                                                                                                        {(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
                if(response)
                {
                    auto responseData = response->getData();
                    if((response->getRemoteManagementFunction() == (uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse && responseData.size() >= 8 && responseData.at(5) == ((uint16_t)EnOceanPacket::RemoteManagementFunction::setLinkTable >> 8u) && responseData.at(6) == (uint8_t)EnOceanPacket::RemoteManagementFunction::setLinkTable && responseData.at(7) == 0) ||
                       response->getRemoteManagementFunction() == (uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck)
                    {
                        pairingSuccessful = true;
                    }
                }
            }
        }
        //}}}

        //{{{ Set outbound link table
        if(setOutboundLinkTableSize != 0)
        {
            std::vector<uint8_t> linkTable{};
            linkTable.reserve(9 * setOutboundLinkTableSize);
            for(uint32_t i = 0; i < setOutboundLinkTableSize; i++)
            {
                if(setLinkTableHasIndex) linkTable.push_back(i);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0xFFu);
                linkTable.push_back(0);
                linkTable.push_back(0);
                linkTable.push_back(0);
                linkTable.push_back(0);
            }
            auto setLinkTable = std::make_shared<SetLinkTable>(deviceAddress, false, linkTable);

            if(sendsAck)
            {
                auto response = interface->sendAndReceivePacket(setLinkTable, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
                pairingSuccessful = (bool)response;
            }
        }
        //}}}

        //{{{ Set repeater functions
        if(hasSetRepeaterFunctions)
        {
            auto setRepeaterFunctions = std::make_shared<SetRepeaterFunctions>(deviceAddress, 0, 1, 0);
            auto response = interface->sendAndReceivePacket(setRepeaterFunctions, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
        }
        //}}}

        if(securityCode != 0)
        {
            auto lock = std::make_shared<Lock>(deviceAddress, securityCode);
            interface->sendEnoceanPacket(lock);
            interface->sendEnoceanPacket(lock);
        }

        if(pairingSuccessful)
        {
            auto peer = buildPeer(eep, deviceAddress, interface->getID(), rfChannel);
            if(peer)
            {
                if(gatewayAddress != 0)
                {
                    peer->setGatewayAddress(gatewayAddress);
                }
                if(securityCode != 0)
                {
                    auto channelIterator = peer->configCentral.find(0);
                    if(channelIterator != peer->configCentral.end())
                    {
                        auto variableIterator = channelIterator->second.find("SECURITY_CODE");
                        if(variableIterator != channelIterator->second.end() && variableIterator->second.rpcParameter)
                        {
                            auto rpcSecurityCode = std::make_shared<BaseLib::Variable>(BaseLib::HelperFunctions::getHexString(securityCode, 8));
                            std::vector<uint8_t> parameterData;
                            variableIterator->second.rpcParameter->convertToPacket(rpcSecurityCode, variableIterator->second.mainRole(), parameterData);
                            variableIterator->second.setBinaryData(parameterData);
                            if(variableIterator->second.databaseId > 0) peer->saveParameter(variableIterator->second.databaseId, parameterData);
                            else peer->saveParameter(0, ParameterGroup::Type::Enum::config, channelIterator->first, variableIterator->first, parameterData);
                        }
                    }
                }

                if(!peer->updateConfiguration())
                {
                    GD::out.printError("Error: Could not read current device configuration.");
                }

                return peer->getID();
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return 0;
}

std::shared_ptr<Variable> EnOceanCentral::setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput)
{
	try
	{
		std::lock_guard<std::mutex> pairingModeGuard(_pairingModeThreadMutex);
		if(_disposing) return Variable::createError(-32500, "Central is disposing.");
		_stopPairingModeThread = true;
		_bl->threadManager.join(_pairingModeThread);
		_stopPairingModeThread = false;
        {
            std::lock_guard<std::mutex> processedAddressesGuard(_processedAddressesMutex);
            _processedAddresses.clear();
        }
        _remoteCommissioningDeviceAddress = 0;
        _remoteCommissioningGatewayAddress = 0;
        _remoteCommissioningSecurityCode = 0;

        if(metadata)
        {
            auto metadataIterator = metadata->structValue->find("interface");
            if(metadataIterator != metadata->structValue->end()) _pairingInterface = metadataIterator->second->stringValue;
            else _pairingInterface = "";

            metadataIterator = metadata->structValue->find("type");
            if(metadataIterator != metadata->structValue->end() && metadataIterator->second->stringValue == "remoteCommissioning")
            {
                metadataIterator = metadata->structValue->find("deviceAddress");
                if(metadataIterator != metadata->structValue->end()) _remoteCommissioningDeviceAddress = metadataIterator->second->integerValue;
                metadataIterator = metadata->structValue->find("gatewayAddress");
                if(metadataIterator != metadata->structValue->end()) _remoteCommissioningGatewayAddress = metadataIterator->second->integerValue;
                metadataIterator = metadata->structValue->find("eep");
                if(metadataIterator != metadata->structValue->end()) _remoteCommissioningEep = metadataIterator->second->integerValue64;
                metadataIterator = metadata->structValue->find("securityCode");
                if(metadataIterator != metadata->structValue->end()) _remoteCommissioningSecurityCode = metadataIterator->second->integerValue;
            }
        }
        else _pairingInterface = "";

		_timeLeftInPairingMode = 0;
		if(on && duration >= 5)
		{
			{
				std::lock_guard<std::mutex> newPeersGuard(_newPeersMutex);
				_newPeers.clear();
				_pairingMessages.clear();
			}

			_timeLeftInPairingMode = duration; //It's important to set it here, because the thread often doesn't completely initialize before getInstallMode requests _timeLeftInPairingMode
			_bl->threadManager.start(_pairingModeThread, true, &EnOceanCentral::pairingModeTimer, this, duration, debugOutput);
		}
		return std::make_shared<Variable>(VariableType::tVoid);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, std::string interfaceId)
{
	try
	{
		std::shared_ptr<EnOceanPeer> peer(getPeer(peerId));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		return peer->setInterface(clientInfo, interfaceId);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanCentral::startSniffing(BaseLib::PRpcClientInfo clientInfo)
{
	std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
	_sniffedPackets.clear();
	_sniff = true;
	return std::make_shared<Variable>();
}

PVariable EnOceanCentral::stopSniffing(BaseLib::PRpcClientInfo clientInfo)
{
	_sniff = false;
	return std::make_shared<Variable>();
}

}
