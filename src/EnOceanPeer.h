/* Copyright 2013-2019 Homegear GmbH */

#ifndef MYPEER_H_
#define MYPEER_H_

#include "PhysicalInterfaces/IEnOceanInterface.h"
#include "EnOceanPacket.h"
#include "Security.h"
#include <homegear-base/BaseLib.h>

using namespace BaseLib;
using namespace BaseLib::DeviceDescription;

namespace EnOcean
{
class EnOceanCentral;

class EnOceanPeer : public BaseLib::Systems::Peer, public BaseLib::Rpc::IWebserverEventSink
{
public:
	EnOceanPeer(uint32_t parentID, IPeerEventSink* eventHandler);
	EnOceanPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler);
	~EnOceanPeer() override;
	void init();
	void dispose() override;

	//Features
	bool wireless() override { return true; }
	//End features

	//{{{ In table variables
	std::string getPhysicalInterfaceId();
	void setPhysicalInterfaceId(std::string);
	uint32_t getGatewayAddress();
	void setGatewayAddress(uint32_t value);
	//}}}

	std::shared_ptr<IEnOceanInterface>& getPhysicalInterface() { return _physicalInterface; }

    bool hasRfChannel(int32_t channel);
	int32_t getRfChannel(int32_t channel);
	std::vector<int32_t> getRfChannels();
	void setRfChannel(int32_t channel, int32_t value);

	void worker();
	std::string handleCliCommand(std::string command) override;
	void packetReceived(PEnOceanPacket& packet);

	bool load(BaseLib::Systems::ICentral* central) override;
    void serializePeers(std::vector<uint8_t>& encodedData);
    void unserializePeers(std::shared_ptr<std::vector<char>> serializedData);
    void savePeers() override;
    void initializeCentralConfig() override;

    void addPeer(int32_t channel, std::shared_ptr<BaseLib::Systems::BasicPeer> peer);
    void removePeer(int32_t channel, int32_t address, int32_t remoteChannel);
    uint32_t getLinkCount();
	int32_t getChannelGroupedWith(int32_t channel) override { return -1; }
	int32_t getNewFirmwareVersion() override { return 0; }
	std::string getFirmwareVersionString(int32_t firmwareVersion) override { return "1.0"; }
    bool firmwareUpdateAvailable() override { return false; }

    bool isWildcardPeer() { return _rpcDevice->addressSize == 25; }

    std::string printConfig();

    /**
	 * {@inheritDoc}
	 */
    void homegearStarted() override;

    /**
	 * {@inheritDoc}
	 */
    void homegearShuttingDown() override;

    bool updateConfiguration();
    bool sendInboundLinkTable();

	//RPC methods
    PVariable forceConfigUpdate(PRpcClientInfo clientInfo) override;
    PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields) override;
	PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing) override;
	PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, std::string interfaceId) override;
	PVariable setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait) override;
    //End RPC methods
protected:
	class FrameValue
	{
	public:
		std::list<uint32_t> channels;
		std::vector<uint8_t> value;
	};

	class FrameValues
	{
	public:
		std::string frameID;
		std::list<uint32_t> paramsetChannels;
		ParameterGroup::Type::Enum parameterSetType;
		std::map<std::string, FrameValue> values;
	};

	class RpcRequest
	{
	public:
		std::atomic_bool abort;
		std::mutex conditionVariableMutex;
		std::condition_variable conditionVariable;
		std::string responseId;

		std::atomic_bool wait;
		PEnOceanPacket packet;
		uint32_t maxResends = 0;
		uint32_t resends = 0;
		uint32_t resendTimeout = 0;
		int64_t lastResend = 0;

		RpcRequest() : abort(false), wait(true) {}
	};
	typedef std::shared_ptr<RpcRequest> PRpcRequest;

	//In table variables:
	std::string _physicalInterfaceId;
	int32_t _rollingCode = -1;
	std::vector<uint8_t> _aesKey;
	int32_t _encryptionType = -1;
	int32_t _cmacSize = -1;
	bool _rollingCodeInTx = false;
	int32_t _rollingCodeSize = -1;
	uint32_t _gatewayAddress = 0;
	//End

	std::shared_ptr<IEnOceanInterface> _physicalInterface;
	uint32_t _lastRssiDevice = 0;
	bool _globalRfChannel = false;
	std::mutex _rfChannelsMutex;
	std::unordered_map<int32_t, int32_t> _rfChannels;

	PEnOceanPacket _lastPacket;

	bool _forceEncryption = false;
	PSecurity _security;
	std::vector<uint8_t> _aesKeyPart1;

	// {{{ Variables for getting RPC responses to requests
		std::mutex _rpcRequestsMutex;
		std::unordered_map<std::string, PRpcRequest> _rpcRequests;
	// }}}

	// {{{ Variables for blinds
		std::atomic<int32_t> _blindSignalDuration;
        std::atomic<int64_t> _blindStateResetTime;
		std::atomic_bool _blindUp;
		std::atomic<int64_t> _lastBlindPositionUpdate;
        std::atomic<int64_t> _lastRpcBlindPositionUpdate;
        std::atomic<int64_t> _blindCurrentTargetPosition;
        std::atomic<int64_t> _blindCurrentSignalDuration;
        std::atomic<int32_t> _blindPosition;
	// }}}

	void loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows) override;
    void saveVariables() override;

    void setRollingCode(int32_t value) { _rollingCode = value; saveVariable(20, value); }
    void setAesKey(std::vector<uint8_t>& value) { _aesKey = value; saveVariable(21, value); }
    void setEncryptionType(int32_t value) { _encryptionType = value; saveVariable(22, value); }
    void setCmacSize(int32_t value) { _cmacSize = value; saveVariable(23, value); }
    void setRollingCodeInTx(bool value) { _rollingCodeInTx = value; saveVariable(24, value); }
    void setRollingCodeSize(int32_t value) { _rollingCodeSize = value; saveVariable(25, value); }
    virtual void setPhysicalInterface(std::shared_ptr<IEnOceanInterface> interface);
    void setBestInterface();

    void setRssiDevice(uint8_t rssi);

	std::shared_ptr<BaseLib::Systems::ICentral> getCentral() override;

	bool remoteManagementUnlock();
	void remoteManagementLock();

	void getValuesFromPacket(PEnOceanPacket packet, std::vector<FrameValues>& frameValue);

	PParameterGroup getParameterSet(int32_t channel, ParameterGroup::Type::Enum type) override;

	void sendPacket(const PEnOceanPacket& packet, const std::string& responseId, int32_t delay, bool wait);

    void updateBlindSpeed();

    void updateBlindPosition();

	// {{{ Hooks
		/**
		 * {@inheritDoc}
		 */
		bool getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters) override;

		/**
		 * {@inheritDoc}
		 */
		bool getParamsetHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters) override;
	// }}}
};

typedef std::shared_ptr<EnOceanPeer> PMyPeer;

}

#endif
