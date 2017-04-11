/* Copyright 2013-2017 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

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
