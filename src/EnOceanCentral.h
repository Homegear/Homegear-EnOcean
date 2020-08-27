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
	~EnOceanCentral() override;
	void dispose(bool wait = true) override;

	std::string handleCliCommand(std::string command);
	virtual bool onPacketReceived(std::string& senderId, std::shared_ptr<BaseLib::Systems::Packet> packet);

	int32_t getFreeRfChannel(const std::string& interfaceId);

	uint64_t getPeerIdFromSerial(std::string& serialNumber) { std::shared_ptr<EnOceanPeer> peer = getPeer(serialNumber); if(peer) return peer->getID(); else return 0; }
	PMyPeer getPeer(uint64_t id);
	std::list<PMyPeer> getPeer(int32_t address);
	PMyPeer getPeer(std::string serialNumber);

	bool peerExists(uint64_t id);
	bool peerExists(std::string serialNumber);
	bool peerExists(int32_t address, uint64_t eep);

    PVariable addLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannel, uint64_t receiverID, int32_t receiverChannel, std::string name, std::string description) override;
	PVariable createDevice(BaseLib::PRpcClientInfo clientInfo, int32_t deviceType, std::string serialNumber, int32_t address, int32_t firmwareVersion, std::string interfaceId) override;
    PVariable createDevice(BaseLib::PRpcClientInfo clientInfo, const std::string& code) override;
	PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags) override;
	PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, int32_t flags) override;
	PVariable getSniffedDevices(BaseLib::PRpcClientInfo clientInfo) override;
    PVariable getPairingState(BaseLib::PRpcClientInfo clientInfo) override;
    PVariable removeLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannel, uint64_t receiverID, int32_t receiverChannel) override;
	PVariable setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput = true) override;
	PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, std::string interfaceId) override;
	PVariable startSniffing(BaseLib::PRpcClientInfo clientInfo) override;
	PVariable stopSniffing(BaseLib::PRpcClientInfo clientInfo) override;
protected:
	bool _sniff = false;
	std::mutex _sniffedPacketsMutex;
	std::map<int32_t, std::vector<PEnOceanPacket>> _sniffedPackets;

	std::map<int32_t, std::list<PMyPeer>> _peers;
	std::mutex _wildcardPeersMutex;
	std::map<int32_t, std::list<PMyPeer>> _wildcardPeers;

	//{{{ Pairing
    std::mutex _pairingMutex;
    std::string _pairingInterface;
	std::atomic_bool _stopPairingModeThread{false};
	std::mutex _pairingModeThreadMutex;
	std::thread _pairingModeThread;
	std::mutex _processedAddressesMutex;
	std::unordered_set<int32_t> _processedAddresses;
    std::atomic_bool _remoteCommissioning{false};
	std::atomic<uint32_t> _remoteCommissioningSecurityCode{0};
    std::atomic<uint32_t> _remoteCommissioningGatewayAddress{0};
	std::atomic<uint32_t> _remoteCommissioningDeviceAddress{0};
	std::atomic<uint64_t> _remoteCommissioningEep{0};
    std::atomic_bool _remoteCommissioningWaitForSignal{false};
	std::queue<std::pair<std::string, uint32_t>> _remoteCommissioningAddressQueue;
	//}}}

	std::atomic_bool _stopWorkerThread{false};
	std::thread _workerThread;

	std::string getFreeSerialNumber(int32_t address);
	void init();
	void worker();
	void loadPeers() override;
	void savePeers(bool full) override;
	void loadVariables() override {}
	void saveVariables() override {}
	std::shared_ptr<EnOceanPeer> createPeer(uint64_t eep, int32_t address, std::string serialNumber, bool save = true);
    std::shared_ptr<EnOceanPeer> buildPeer(uint64_t eep, int32_t address, const std::string& interfaceId, int32_t rfChannel);
	void deletePeer(uint64_t id);

	void pairingModeTimer(int32_t duration, bool debugOutput = true);
	void handleRemoteCommissioningQueue();
	uint64_t remoteCommissionPeer(const std::shared_ptr<IEnOceanInterface>& interface, uint32_t deviceAddress, uint64_t eep, uint32_t securityCode = 0, uint32_t gatewayAddress = 0);
	static uint64_t remoteManagementGetEep(const std::shared_ptr<IEnOceanInterface>& interface, uint32_t deviceAddress, uint32_t securityCode = 0);
	bool handlePairingRequest(std::string& interfaceId, PEnOceanPacket packet);
};

}

#endif
