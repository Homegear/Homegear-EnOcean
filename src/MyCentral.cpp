/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#include "MyCentral.h"
#include "GD.h"

namespace MyFamily {

MyCentral::MyCentral(ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, GD::bl, eventHandler)
{
	init();
}

MyCentral::MyCentral(uint32_t deviceID, std::string serialNumber, ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, GD::bl, deviceID, serialNumber, -1, eventHandler)
{
	init();
}

MyCentral::~MyCentral()
{
	dispose();
}

void MyCentral::dispose(bool wait)
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
		for(std::map<std::string, std::shared_ptr<IEnOceanInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); ++i)
		{
			//Just to make sure cycle through all physical devices. If event handler is not removed => segfault
			i->second->removeEventHandler(_physicalInterfaceEventhandlers[i->first]);
		}
	}
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MyCentral::init()
{
	try
	{
		if(_initialized) return; //Prevent running init two times
		_initialized = true;
		_pairing = false;
		_stopPairingModeThread = false;
		_stopWorkerThread = false;
		_timeLeftInPairingMode = 0;

		for(std::map<std::string, std::shared_ptr<IEnOceanInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); ++i)
		{
			_physicalInterfaceEventhandlers[i->first] = i->second->addEventHandler((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink*)this);
		}

		GD::bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &MyCentral::worker, this);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void MyCentral::worker()
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

				std::shared_ptr<MyPeer> peer;

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
							peer = std::dynamic_pointer_cast<MyPeer>(nextPeer->second);
						}
					}
				}

				if(peer && !peer->deleting) peer->worker();
				counter++;
			}
			catch(const std::exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MyCentral::loadPeers()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			int32_t peerID = row->second.at(0)->intValue;
			GD::out.printMessage("Loading EnOcean peer " + std::to_string(peerID));
			std::shared_ptr<MyPeer> peer(new MyPeer(peerID, row->second.at(2)->intValue, row->second.at(3)->textValue, _deviceId, this));
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
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<MyPeer> MyCentral::getPeer(uint64_t id)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		if(_peersById.find(id) != _peersById.end())
		{
			std::shared_ptr<MyPeer> peer(std::dynamic_pointer_cast<MyPeer>(_peersById.at(id)));
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::shared_ptr<MyPeer>();
}

std::list<PMyPeer> MyCentral::getPeer(int32_t address)
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
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::list<PMyPeer>();
}

std::shared_ptr<MyPeer> MyCentral::getPeer(std::string serialNumber)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		if(_peersBySerial.find(serialNumber) != _peersBySerial.end())
		{
			std::shared_ptr<MyPeer> peer(std::dynamic_pointer_cast<MyPeer>(_peersBySerial.at(serialNumber)));
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::shared_ptr<MyPeer>();
}

bool MyCentral::peerExists(uint64_t id)
{
	return ICentral::peerExists(id);
}

bool MyCentral::peerExists(std::string serialNumber)
{
	return ICentral::peerExists(serialNumber);
}

bool MyCentral::peerExists(int32_t address, int32_t eep)
{
	std::list<PMyPeer> peers = getPeer(address);
	for(auto& peer : peers)
	{
		if(peer->getDeviceType() == (uint32_t)eep) return true;
	}
	return false;
}

int32_t MyCentral::getFreeRfChannel(std::string& interfaceId)
{
	try
	{
		std::vector<std::shared_ptr<BaseLib::Systems::Peer>> peers = getPeers();
		std::set<int32_t> usedChannels;
		for(std::vector<std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = peers.begin(); i != peers.end(); ++i)
		{
			PMyPeer peer(std::dynamic_pointer_cast<MyPeer>(*i));
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
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return -1;
}

bool MyCentral::onPacketReceived(std::string& senderId, std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(_disposing) return false;
		PMyPacket myPacket(std::dynamic_pointer_cast<MyPacket>(packet));
		if(!myPacket) return false;

		if(_bl->debugLevel >= 4) std::cout << BaseLib::HelperFunctions::getTimeString(myPacket->timeReceived()) << " EnOcean packet received (" << senderId << std::string(", RSSI: ") + std::to_string(myPacket->getRssi()) + " dBm" << "): " << BaseLib::HelperFunctions::getHexString(myPacket->getBinary()) << " - Sender address: 0x" << BaseLib::HelperFunctions::getHexString(myPacket->senderAddress(), 8) << std::endl;

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
			if(_pairing) return handlePairingRequest(senderId, myPacket);
			return false;
		}

		bool result = false;
		bool unpaired = true;
		for(auto& peer : peers)
		{
			if(senderId != peer->getPhysicalInterfaceId()) continue;
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
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return false;
}

std::string MyCentral::getFreeSerialNumber(int32_t address)
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

bool MyCentral::handlePairingRequest(std::string& interfaceId, PMyPacket packet)
{
	try
	{
		auto physicalInterfaceIterator = GD::physicalInterfaces.find(interfaceId);
		if(physicalInterfaceIterator == GD::physicalInterfaces.end()) return false;
		std::shared_ptr<IEnOceanInterface> physicalInterface = physicalInterfaceIterator->second;
		if(!physicalInterface) return false;

		std::vector<char> payload = packet->getData();
		if(packet->getRorg() == 0xD4) //UTE
		{
			if(payload.size() < 8) return false;

			int32_t eep = ((int32_t)(uint8_t)payload.at(7) << 16) | (((int32_t)(uint8_t)payload.at(6)) << 8) | ((uint8_t)payload.at(5));
			std::string serial = getFreeSerialNumber(packet->senderAddress());

			uint8_t byte1 = payload.at(1);
			if((byte1 & 0x0F) != 0) return false; //Command 0 => teach-in request
			if(!(byte1 & 0x80))
			{
				GD::out.printWarning("Warning: Could not teach-in device as it expects currently unsupported unidirectional communication.");
				return false;
			}
			bool responseExpected = !(byte1 & 0x40);
			if((byte1 & 0x30) == 0x10)
			{
				GD::out.printWarning("Warning: Could not teach-out device as the teach-out request is currently unsupported.");
				return false;
			}

			int32_t rfChannel = 0;
			if(!peerExists(packet->senderAddress(), eep))
			{
				rfChannel = getFreeRfChannel(interfaceId);
				if(rfChannel == -1)
				{
					GD::out.printError("Error: Could not pair peer, because there are no free RF channels.");
					return false;
				}
				GD::out.printInfo("Info: Trying to pair peer with EEP " + BaseLib::HelperFunctions::getHexString(eep) + ". If nothing happens, the EEP is not yet supported.");
				std::shared_ptr<MyPeer> peer = createPeer(eep, packet->senderAddress(), serial, false);
				if(!peer || !peer->getRpcDevice()) return false;
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
				catch(BaseLib::Exception& ex)
				{
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}
				catch(...)
				{
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
				}

				PVariable deviceDescriptions(new Variable(VariableType::tArray));
				deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
				raiseRPCNewDevices(deviceDescriptions);
				GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
			}
			else
			{
				std::list<PMyPeer> peers = getPeer(packet->senderAddress());
				if(peers.empty()) return false;
				for(auto& peer : peers)
				{
					if(peer->getDeviceType() == (uint32_t)eep)
					{
						rfChannel = peer->getRfChannel(0);
						break;
					}
				}
			}

			if(responseExpected)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				PMyPacket response(new MyPacket((MyPacket::Type)1, packet->getRorg(), physicalInterface->getBaseAddress() | rfChannel, packet->senderAddress()));
				std::vector<char> responsePayload;
				responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 8);
				responsePayload.at(1) = (responsePayload.at(1) & 0x80) | 0x11; // Command 1 => teach-in response
				response->setData(responsePayload);
				physicalInterface->sendPacket(response);
			}
		}
		else if(packet->getRorg() == 0xA5) //4BS teach-in, variant 3
		{
			if(payload.size() < 4) return false;
			int32_t eep = ((int32_t)(uint8_t)payload.at(0) << 16) | (((int32_t)(uint8_t)payload.at(1) >> 2) << 8) | (((uint8_t)payload.at(1) & 3) << 5) | ((uint8_t)payload.at(2) >> 3);
			std::string serial = getFreeSerialNumber(packet->senderAddress());

			if(!peerExists(packet->senderAddress(), eep))
			{
				int32_t rfChannel = getFreeRfChannel(interfaceId);
				if(rfChannel == -1)
				{
					GD::out.printError("Error: Could not pair peer, because there are no free RF channels.");
					return false;
				}
				GD::out.printInfo("Info: Trying to pair peer with EEP " + BaseLib::HelperFunctions::getHexString(eep) + ". If nothing happens, the EEP is not yet supported.");
				std::shared_ptr<MyPeer> peer = createPeer(eep, packet->senderAddress(), serial, false);
				if(!peer || !peer->getRpcDevice()) return false;
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
				catch(BaseLib::Exception& ex)
				{
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}
				catch(...)
				{
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
				}

				PMyPacket response(new MyPacket((MyPacket::Type)1, packet->getRorg(), physicalInterface->getBaseAddress() | peer->getRfChannel(0), 0xFFFFFFFF));
				std::vector<char> responsePayload;
				responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 5);
				responsePayload.back() = 0xF0;
				response->setData(responsePayload);
				physicalInterface->sendPacket(response);

				PVariable deviceDescriptions(new Variable(VariableType::tArray));
				deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
				raiseRPCNewDevices(deviceDescriptions);
				GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");
			}
			else
			{
				int32_t rfChannel = 0;
				std::list<PMyPeer> peers = getPeer(packet->senderAddress());
				if(peers.empty()) return false;
				for(auto& peer : peers)
				{
					if(peer->getDeviceType() == (uint32_t)eep)
					{
						rfChannel = peer->getRfChannel(0);
						break;
					}
				}
				PMyPacket response(new MyPacket((MyPacket::Type)1, packet->getRorg(), physicalInterface->getBaseAddress() | rfChannel, 0xFFFFFFFF));
				std::vector<char> responsePayload;
				responsePayload.insert(responsePayload.end(), payload.begin(), payload.begin() + 5);
				responsePayload.back() = 0xF0;
				response->setData(responsePayload);
				physicalInterface->sendPacket(response);
			}
		}
		return true;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return false;
}

void MyCentral::savePeers(bool full)
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
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MyCentral::deletePeer(uint64_t id)
{
	try
	{
		std::shared_ptr<MyPeer> peer(getPeer(id));
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

		raiseRPCDeleteDevices(deviceAddresses, deviceInfo);
		peer->deleteFromDatabase();

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

		_peersMutex.lock();
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
		_peersMutex.unlock();
		GD::out.printMessage("Removed EnOcean peer " + std::to_string(peer->getID()));
	}
	catch(const std::exception& ex)
    {
		_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::string MyCentral::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;
		std::vector<std::string> arguments;
		bool showHelp = false;
		if(_currentPeer)
		{
			if(BaseLib::HelperFunctions::checkCliCommand(command, "unselect", "u", "", 0, arguments, showHelp))
			{
				_currentPeer.reset();
				return "Peer unselected.\n";
			}
			return _currentPeer->handleCliCommand(command);
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "help", "h", "", 0, arguments, showHelp))
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

			setInstallMode(nullptr, true, duration, false);
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

			setInstallMode(nullptr, false, -1, false);
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
			if(GD::physicalInterfaces.find(interfaceId) == GD::physicalInterfaces.end()) return "Unknown physical interface.\n";
			int32_t deviceType = BaseLib::Math::getNumber(arguments.at(1), true);
			if(deviceType == 0) return "Invalid device type. Device type has to be provided in hexadecimal format.\n";
			int32_t address = BaseLib::Math::getNumber(arguments.at(2), true);
			std::string serial = getFreeSerialNumber(address);

			if(peerExists(address, deviceType)) stringStream << "A peer with this address and EEP is already paired to this central." << std::endl;
			else
			{
				std::shared_ptr<MyPeer> peer = createPeer(deviceType, address, serial, false);
				if(!peer || !peer->getRpcDevice()) return "Device type not supported.\n";
				try
				{
					if(peer->getRpcDevice()->addressSize == 25) peer->setAddress(address & 0xFFFFFF80);
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
				catch(BaseLib::Exception& ex)
				{
					_peersMutex.unlock();
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}
				catch(...)
				{
					_peersMutex.unlock();
					GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
				}

				PVariable deviceDescriptions(new Variable(VariableType::tArray));
				deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
				raiseRPCNewDevices(deviceDescriptions);
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
				if(_currentPeer && _currentPeer->getID() == peerID) _currentPeer.reset();
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
						stringStream << std::setw(typeWidth2) << typeID;
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
			catch(BaseLib::Exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
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
				std::shared_ptr<MyPeer> peer = getPeer(peerID);
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
				stringStream << "Usage: peers create INTERFACE ADDRESS" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  INTERFACE: The id of the interface to set the address for." << std::endl;
				stringStream << "  ADDRESS:   The new 4 byte address/ID starting with 0xFF the 7 least significant bits can't be set. Example: 0xFF422E80" << std::endl;
				return stringStream.str();
			}

			std::string interfaceId = arguments.at(0);
			if(GD::physicalInterfaces.find(interfaceId) == GD::physicalInterfaces.end()) return "Unknown physical interface.\n";
			uint32_t address = BaseLib::Math::getUnsignedNumber(arguments.at(1), true) & 0xFFFFFF80;

			int32_t result = GD::physicalInterfaces.at(interfaceId)->setBaseAddress(address);

			if(result == -1) stringStream << "Error setting base address. See error log for more details." << std::endl;
			else stringStream << "Base address set to 0x" << BaseLib::HelperFunctions::getHexString(address) << ". Remaining changes: " << result << std::endl;

			return stringStream.str();
		}
		else if(command.compare(0, 12, "peers select") == 0 || command.compare(0, 2, "ps") == 0)
		{
			uint64_t id = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 's') ? 0 : 1;
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
					id = BaseLib::Math::getNumber(element, false);
					if(id == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command selects a peer." << std::endl;
				stringStream << "Usage: peers select PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to select. Example: 513" << std::endl;
				return stringStream.str();
			}

			_currentPeer = getPeer(id);
			if(!_currentPeer) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				stringStream << "Peer with id " << std::hex << std::to_string(id) << " and device type 0x" << _bl->hf.getHexString(_currentPeer->getDeviceType()) << " selected." << std::dec << std::endl;
				stringStream << "For information about the peer's commands type: \"help\"" << std::endl;
			}
			return stringStream.str();
		}
		else return "Unknown command.\n";
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return "Error executing command. See log file for more details.\n";
}

std::shared_ptr<MyPeer> MyCentral::createPeer(uint32_t deviceType, int32_t address, std::string serialNumber, bool save)
{
	try
	{
		std::shared_ptr<MyPeer> peer(new MyPeer(_deviceId, this));
		peer->setDeviceType(deviceType);
		peer->setAddress(address);
		peer->setSerialNumber(serialNumber);
		peer->setRpcDevice(GD::family->getRpcDevices()->find(deviceType, 0x10, -1));
		if(!peer->getRpcDevice()) return std::shared_ptr<MyPeer>();
		if(save) peer->save(true, true, false); //Save and create peerID
		return peer;
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::shared_ptr<MyPeer>();
}

PVariable MyCentral::createDevice(BaseLib::PRpcClientInfo clientInfo, int32_t deviceType, std::string serialNumber, int32_t address, int32_t firmwareVersion, std::string interfaceId)
{
	try
	{
		std::string serial = getFreeSerialNumber(address);
		if(peerExists(deviceType, address)) return Variable::createError(-5, "This peer is already paired to this central.");

		if(GD::physicalInterfaces.find(interfaceId) == GD::physicalInterfaces.end()) return Variable::createError(-6, "Unknown physical interface.");

		std::shared_ptr<MyPeer> peer = createPeer(deviceType, address, serial, false);
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
		catch(BaseLib::Exception& ex)
		{
			_peersMutex.unlock();
			GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
		}
		catch(...)
		{
			_peersMutex.unlock();
			GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
		}

		PVariable deviceDescriptions(new Variable(VariableType::tArray));
		deviceDescriptions->arrayValue = peer->getDeviceDescriptions(clientInfo, true, std::map<std::string, bool>());
		raiseRPCNewDevices(deviceDescriptions);
		GD::out.printMessage("Added peer " + std::to_string(peer->getID()) + ".");

		return PVariable(new Variable((uint32_t)peer->getID()));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags)
{
	try
	{
		if(serialNumber.empty()) return Variable::createError(-2, "Unknown device.");
		std::shared_ptr<MyPeer> peer = getPeer(serialNumber);
		if(!peer) return PVariable(new Variable(VariableType::tVoid));

		return deleteDevice(clientInfo, peer->getID(), flags);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t flags)
{
	try
	{
		if(peerID == 0) return Variable::createError(-2, "Unknown device.");
		std::shared_ptr<MyPeer> peer = getPeer(peerID);
		if(!peer) return PVariable(new Variable(VariableType::tVoid));
		uint64_t id = peer->getID();

		deletePeer(id);

		if(peerExists(id)) return Variable::createError(-1, "Error deleting peer. See log for more details.");

		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, uint64_t id, std::map<std::string, bool> fields)
{
	try
	{
		if(id > 0)
		{
			std::shared_ptr<MyPeer> peer(getPeer(id));
			if(!peer) return Variable::createError(-2, "Unknown device.");

			return peer->getDeviceInfo(clientInfo, fields);
		}
		else
		{
			PVariable array(new Variable(VariableType::tArray));

			std::vector<std::shared_ptr<MyPeer>> peers;
			//Copy all peers first, because listDevices takes very long and we don't want to lock _peersMutex too long
			_peersMutex.lock();
			for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
			{
				peers.push_back(std::dynamic_pointer_cast<MyPeer>(i->second));
			}
			_peersMutex.unlock();

			for(std::vector<std::shared_ptr<MyPeer>>::iterator i = peers.begin(); i != peers.end(); ++i)
			{
				//listDevices really needs a lot of resources, so wait a little bit after each device
				std::this_thread::sleep_for(std::chrono::milliseconds(3));
				PVariable info = (*i)->getDeviceInfo(clientInfo, fields);
				if(!info) continue;
				array->arrayValue->push_back(info);
			}

			return array;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::getSniffedDevices(BaseLib::PRpcClientInfo clientInfo)
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

			info->structValue->insert(StructElement("FAMILYID", PVariable(new Variable(MY_FAMILY_ID))));
			info->structValue->insert(StructElement("ADDRESS", PVariable(new Variable(peerPackets.first))));
			if(!peerPackets.second.empty()) info->structValue->insert(StructElement("RORG", PVariable(new Variable(peerPackets.second.back()->getRorg()))));
			if(!peerPackets.second.empty()) info->structValue->insert(StructElement("RSSI", PVariable(new Variable(peerPackets.second.back()->getRssi()))));

			PVariable packets(new Variable(VariableType::tArray));
			info->structValue->insert(StructElement("PACKETS", packets));

			for(auto packet : peerPackets.second)
			{
				PVariable packetInfo(new Variable(VariableType::tStruct));
				packetInfo->structValue->insert(StructElement("TIME_RECEIVED", PVariable(new Variable(packet->timeReceived() / 1000))));
				packetInfo->structValue->insert(StructElement("PACKET", PVariable(new Variable(BaseLib::HelperFunctions::getHexString(packet->getBinary())))));
				packets->arrayValue->push_back(packetInfo);
			}
		}
		return array;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::putParamset(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t channel, ParameterGroup::Type::Enum type, std::string remoteSerialNumber, int32_t remoteChannel, PVariable paramset)
{
	try
	{
		std::shared_ptr<MyPeer> peer(getPeer(serialNumber));
		uint64_t remoteID = 0;
		if(!remoteSerialNumber.empty())
		{
			std::shared_ptr<MyPeer> remotePeer(getPeer(remoteSerialNumber));
			if(!remotePeer) return Variable::createError(-3, "Remote peer is unknown.");
			remoteID = remotePeer->getID();
		}
		if(peer) return peer->putParamset(clientInfo, channel, type, remoteID, remoteChannel, paramset);
		return Variable::createError(-2, "Unknown device.");
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::putParamset(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable paramset)
{
	try
	{
		std::shared_ptr<MyPeer> peer(getPeer(peerID));
		if(peer) return peer->putParamset(clientInfo, channel, type, remoteID, remoteChannel, paramset);
		return Variable::createError(-2, "Unknown device.");
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

void MyCentral::pairingModeTimer(int32_t duration, bool debugOutput)
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
		}
		_timeLeftInPairingMode = 0;
		_pairing = false;
		if(debugOutput) GD::out.printInfo("Info: Pairing mode disabled.");
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<Variable> MyCentral::setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, bool debugOutput)
{
	try
	{
		std::lock_guard<std::mutex> pairingModeGuard(_pairingModeThreadMutex);
		if(_disposing) return Variable::createError(-32500, "Central is disposing.");
		_stopPairingModeThread = true;
		_bl->threadManager.join(_pairingModeThread);
		_stopPairingModeThread = false;
		_timeLeftInPairingMode = 0;
		if(on && duration >= 5)
		{
			_timeLeftInPairingMode = duration; //It's important to set it here, because the thread often doesn't completely initialize before getInstallMode requests _timeLeftInPairingMode
			_bl->threadManager.start(_pairingModeThread, true, &MyCentral::pairingModeTimer, this, duration, debugOutput);
		}
		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, std::string interfaceId)
{
	try
	{
		std::shared_ptr<MyPeer> peer(getPeer(peerId));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		return peer->setInterface(clientInfo, interfaceId);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::startSniffing(BaseLib::PRpcClientInfo clientInfo)
{
	std::lock_guard<std::mutex> sniffedPacketsGuard(_sniffedPacketsMutex);
	_sniffedPackets.clear();
	_sniff = true;
	return PVariable(new Variable());
}

PVariable MyCentral::stopSniffing(BaseLib::PRpcClientInfo clientInfo)
{
	_sniff = false;
	return PVariable(new Variable());
}

}
