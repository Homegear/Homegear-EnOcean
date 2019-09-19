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
	virtual ~EnOceanPeer();
	void init();
	void dispose();

	//Features
	virtual bool wireless() { return true; }
	//End features

	//{{{ In table variables
	std::string getPhysicalInterfaceId();
	void setPhysicalInterfaceId(std::string);
	//}}}

	std::shared_ptr<IEnOceanInterface>& getPhysicalInterface() { return _physicalInterface; }

    bool hasRfChannel(int32_t channel);
	int32_t getRfChannel(int32_t channel);
	std::vector<int32_t> getRfChannels();
	void setRfChannel(int32_t channel, int32_t value);

	void worker();
	virtual std::string handleCliCommand(std::string command);
	void packetReceived(PEnOceanPacket& packet);

	virtual bool load(BaseLib::Systems::ICentral* central);
    virtual void savePeers() {}
    virtual void initializeCentralConfig();

	virtual int32_t getChannelGroupedWith(int32_t channel) { return -1; }
	virtual int32_t getNewFirmwareVersion() { return 0; }
	virtual std::string getFirmwareVersionString(int32_t firmwareVersion) { return "1.0"; }
    virtual bool firmwareUpdateAvailable() { return false; }

    bool isWildcardPeer() { return _rpcDevice->addressSize == 25; }

    std::string printConfig();

    /**
	 * {@inheritDoc}
	 */
    virtual void homegearStarted();

    /**
	 * {@inheritDoc}
	 */
    virtual void homegearShuttingDown();

	//RPC methods
    virtual PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields);
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing = false);
	PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, std::string interfaceId);
	virtual PVariable setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait);
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
	//End

	bool _shuttingDown = false;
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

	virtual void loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows);
    virtual void saveVariables();

    void setRollingCode(int32_t value) { _rollingCode = value; saveVariable(20, value); }
    void setAesKey(std::vector<uint8_t>& value) { _aesKey = value; saveVariable(21, value); }
    void setEncryptionType(int32_t value) { _encryptionType = value; saveVariable(22, value); }
    void setCmacSize(int32_t value) { _cmacSize = value; saveVariable(23, value); }
    void setRollingCodeInTx(bool value) { _rollingCodeInTx = value; saveVariable(24, value); }
    void setRollingCodeSize(int32_t value) { _rollingCodeSize = value; saveVariable(25, value); }
    virtual void setPhysicalInterface(std::shared_ptr<IEnOceanInterface> interface);
    void setBestInterface();

    void setRssiDevice(uint8_t rssi);

	virtual std::shared_ptr<BaseLib::Systems::ICentral> getCentral();

	void getValuesFromPacket(PEnOceanPacket packet, std::vector<FrameValues>& frameValue);

	virtual PParameterGroup getParameterSet(int32_t channel, ParameterGroup::Type::Enum type);

	void sendPacket(PEnOceanPacket packet, std::string responseId, int32_t delay, bool wait);

    void updateBlindSpeed();

    void updateBlindPosition();

	// {{{ Hooks
		/**
		 * {@inheritDoc}
		 */
		virtual bool getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters);

		/**
		 * {@inheritDoc}
		 */
		virtual bool getParamsetHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters);
	// }}}
};

typedef std::shared_ptr<EnOceanPeer> PMyPeer;

}

#endif