/* Copyright 2013-2019 Homegear GmbH */

#ifndef MYCENTRAL_H_
#define MYCENTRAL_H_

#include "EnOceanPeer.h"
#include "EnOceanPacket.h"
#include <homegear-base/BaseLib.h>

#include <memory>
#include <mutex>
#include <string>

namespace EnOcean
{

class EnOceanCentral : public BaseLib::Systems::ICentral
{
public:
	EnOceanCentral(ICentralEventSink* eventHandler);
	EnOceanCentral(uint32_t deviceType, std::string serialNumber, ICentralEventSink* eventHandler);
	virtual ~EnOceanCentral();
	virtual void dispose(bool wait = true);

	std::string handleCliCommand(std::string command);
	virtual bool onPacketReceived(std::string& senderId, std::shared_ptr<BaseLib::Systems::Packet> packet);

	int32_t getFreeRfChannel(std::string& interfaceId);

	uint64_t getPeerIdFromSerial(std::string& serialNumber) { std::shared_ptr<EnOceanPeer> peer = getPeer(serialNumber); if(peer) return peer->getID(); else return 0; }
	PMyPeer getPeer(uint64_t id);
	std::list<PMyPeer> getPeer(int32_t address);
	PMyPeer getPeer(std::string serialNumber);

	bool peerExists(uint64_t id);
	bool peerExists(std::string serialNumber);
	bool peerExists(int32_t address, int32_t eep);

	virtual PVariable createDevice(BaseLib::PRpcClientInfo clientInfo, int32_t deviceType, std::string serialNumber, int32_t address, int32_t firmwareVersion, std::string interfaceId);
	virtual PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags);
	virtual PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, int32_t flags);
	virtual PVariable getSniffedDevices(BaseLib::PRpcClientInfo clientInfo);
    virtual PVariable getPairingState(BaseLib::PRpcClientInfo clientInfo);
	virtual PVariable setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput = true);
	virtual PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, std::string interfaceId);
	virtual PVariable startSniffing(BaseLib::PRpcClientInfo clientInfo);
	virtual PVariable stopSniffing(BaseLib::PRpcClientInfo clientInfo);
protected:
	bool _sniff = false;
	std::mutex _sniffedPacketsMutex;
	std::map<int32_t, std::vector<PMyPacket>> _sniffedPackets;

	std::map<int32_t, std::list<PMyPeer>> _peers;
	std::mutex _wildcardPeersMutex;
	std::map<int32_t, std::list<PMyPeer>> _wildcardPeers;
    std::mutex _pairingMutex;
    std::string _pairingInterface;
	std::atomic_bool _stopPairingModeThread;
	std::mutex _pairingModeThreadMutex;
	std::thread _pairingModeThread;

	std::atomic_bool _stopWorkerThread;
	std::thread _workerThread;

	std::string getFreeSerialNumber(int32_t address);
	virtual void init();
	virtual void worker();
	virtual void loadPeers();
	virtual void savePeers(bool full);
	virtual void loadVariables() {}
	virtual void saveVariables() {}
	std::shared_ptr<EnOceanPeer> createPeer(uint32_t deviceType, int32_t address, std::string serialNumber, bool save = true);
	void deletePeer(uint64_t id);

	void pairingModeTimer(int32_t duration, bool debugOutput = true);
	bool handlePairingRequest(std::string& interfaceId, PMyPacket packet);
};

}

#endif
