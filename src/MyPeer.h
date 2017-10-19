/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef MYPEER_H_
#define MYPEER_H_

#include "PhysicalInterfaces/IEnOceanInterface.h"
#include "MyPacket.h"
#include "Security.h"
#include <homegear-base/BaseLib.h>

using namespace BaseLib;
using namespace BaseLib::DeviceDescription;

namespace MyFamily
{
class MyCentral;

class MyPeer : public BaseLib::Systems::Peer, public BaseLib::Rpc::IWebserverEventSink
{
public:
	MyPeer(uint32_t parentID, IPeerEventSink* eventHandler);
	MyPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler);
	virtual ~MyPeer();
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

	int32_t getRfChannel(int32_t channel);
	std::vector<int32_t> getRfChannels();
	void setRfChannel(int32_t channel, int32_t value);

	void worker();
	virtual std::string handleCliCommand(std::string command);
	void packetReceived(PMyPacket& packet);

	virtual bool load(BaseLib::Systems::ICentral* central);
    virtual void savePeers() {}
    virtual void initializeCentralConfig();

	virtual int32_t getChannelGroupedWith(int32_t channel) { return -1; }
	virtual int32_t getNewFirmwareVersion() { return 0; }
	virtual std::string getFirmwareVersionString(int32_t firmwareVersion) { return "1.0"; }
    virtual bool firmwareUpdateAvailable() { return false; }

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
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool onlyPushing = false);
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
		PMyPacket packet;
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
	std::vector<char> _aesKey;
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

	bool _forceEncryption = false;
	PSecurity _security;
	std::vector<char> _aesKeyPart1;

	// {{{ Variables for getting RPC responses to requests
		std::mutex _rpcRequestsMutex;
		std::unordered_map<std::string, PRpcRequest> _rpcRequests;
	// }}}

	// {{{ Variables for blinds
		int32_t _blindSignalDuration = -1;
		int64_t _blindStateResetTime = -1;
		bool _blindUp = false;
		int64_t _lastBlindPositionUpdate = 0;
		int64_t _lastRpcBlindPositionUpdate = 0;
		int32_t _blindPosition = 0;
	// }}}

	virtual void loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows);
    virtual void saveVariables();

    void setRollingCode(int32_t value) { _rollingCode = value; saveVariable(20, value); }
    void setAesKey(std::vector<char>& value) { _aesKey = value; saveVariable(21, value); }
    void setEncryptionType(int32_t value) { _encryptionType = value; saveVariable(22, value); }
    void setCmacSize(int32_t value) { _cmacSize = value; saveVariable(23, value); }
    void setRollingCodeInTx(bool value) { _rollingCodeInTx = value; saveVariable(24, value); }
    void setRollingCodeSize(int32_t value) { _rollingCodeSize = value; saveVariable(25, value); }
    virtual void setPhysicalInterface(std::shared_ptr<IEnOceanInterface> interface);

    void setRssiDevice(uint8_t rssi);

	virtual std::shared_ptr<BaseLib::Systems::ICentral> getCentral();

	void getValuesFromPacket(PMyPacket packet, std::vector<FrameValues>& frameValue);

	virtual PParameterGroup getParameterSet(int32_t channel, ParameterGroup::Type::Enum type);

	void sendPacket(PMyPacket packet, std::string responseId, int32_t delay, bool wait);

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

typedef std::shared_ptr<MyPeer> PMyPeer;

}

#endif
