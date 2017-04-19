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

#include "MyPeer.h"

#include "GD.h"
#include "MyPacket.h"
#include "MyCentral.h"

namespace MyFamily
{
std::shared_ptr<BaseLib::Systems::ICentral> MyPeer::getCentral()
{
	try
	{
		if(_central) return _central;
		_central = GD::family->getCentral();
		return _central;
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
	return std::shared_ptr<BaseLib::Systems::ICentral>();
}

MyPeer::MyPeer(uint32_t parentID, IPeerEventSink* eventHandler) : BaseLib::Systems::Peer(GD::bl, parentID, eventHandler)
{
	init();
}

MyPeer::MyPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler) : BaseLib::Systems::Peer(GD::bl, id, address, serialNumber, parentID, eventHandler)
{
	init();
}

MyPeer::~MyPeer()
{
	try
	{
		dispose();
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

void MyPeer::init()
{
	try
	{
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

void MyPeer::dispose()
{
	if(_disposing) return;
	Peer::dispose();
}

void MyPeer::worker()
{
	try
	{
		if(_blindStateResetTime != -1)
		{
			if(_blindUp) _blindPosition += (BaseLib::HelperFunctions::getTime() - _lastBlindPositionUpdate) * 10000 / _blindSignalDuration;
			else _blindPosition -= (BaseLib::HelperFunctions::getTime() - _lastBlindPositionUpdate) * 10000 / _blindSignalDuration;
			_lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
			if(_blindPosition < 0) _blindPosition = 0;
			else if(_blindPosition > 10000) _blindPosition = 10000;
			bool updatePosition = false;
			if(BaseLib::HelperFunctions::getTime() >= _blindStateResetTime)
			{
				setValue(BaseLib::PRpcClientInfo(), 1, _blindUp ? "UP" : "DOWN", std::make_shared<BaseLib::Variable>(false), false);
				updatePosition = true;
			}
			if(BaseLib::HelperFunctions::getTime() - _lastRpcBlindPositionUpdate >= 1000)
			{
				_lastRpcBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
				updatePosition = true;
			}

			if(updatePosition)
			{
				auto channelIterator = valuesCentral.find(1);
				if(channelIterator != valuesCentral.end())
				{
					auto parameterIterator = channelIterator->second.find("CURRENT_POSITION");
					if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
					{
						BaseLib::PVariable blindPosition = std::make_shared<BaseLib::Variable>(_blindPosition / 100);

						parameterIterator->second.rpcParameter->convertToPacket(blindPosition, parameterIterator->second.data);
						if(parameterIterator->second.databaseID > 0) saveParameter(parameterIterator->second.databaseID, parameterIterator->second.data);
						else saveParameter(0, ParameterGroup::Type::Enum::variables, 1, "CURRENT_POSITION", parameterIterator->second.data);
						if(_bl->debugLevel >= 4) GD::out.printInfo("Info: CURRENT_POSITION of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(1) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterIterator->second.data) + ".");

						std::shared_ptr<std::vector<std::string>> valueKeys = std::make_shared<std::vector<std::string>>();
						valueKeys->push_back("CURRENT_POSITION");
						std::shared_ptr<std::vector<PVariable>> values = std::make_shared<std::vector<PVariable>>();
						values->push_back(blindPosition);
						raiseEvent(_peerID, 1, valueKeys, values);
						raiseRPCEvent(_peerID, 1, _serialNumber + ":" + std::to_string(1), valueKeys, values);
					}
				}
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

void MyPeer::homegearStarted()
{
	try
	{
		Peer::homegearStarted();
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

void MyPeer::homegearShuttingDown()
{
	try
	{
		_shuttingDown = true;
		Peer::homegearShuttingDown();
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

std::string MyPeer::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;

		if(command == "help")
		{
			stringStream << "List of commands:" << std::endl << std::endl;
			stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
			stringStream << "unselect\t\tUnselect this peer" << std::endl;
			stringStream << "channel count\t\tPrint the number of channels of this peer" << std::endl;
			stringStream << "config print\t\tPrints all configuration parameters and their values" << std::endl;
			return stringStream.str();
		}
		if(command.compare(0, 13, "channel count") == 0)
		{
			std::stringstream stream(command);
			std::string element;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 2)
				{
					index++;
					continue;
				}
				else if(index == 2)
				{
					if(element == "help")
					{
						stringStream << "Description: This command prints this peer's number of channels." << std::endl;
						stringStream << "Usage: channel count" << std::endl << std::endl;
						stringStream << "Parameters:" << std::endl;
						stringStream << "  There are no parameters." << std::endl;
						return stringStream.str();
					}
				}
				index++;
			}

			stringStream << "Peer has " << _rpcDevice->functions.size() << " channels." << std::endl;
			return stringStream.str();
		}
		else if(command.compare(0, 12, "config print") == 0)
		{
			std::stringstream stream(command);
			std::string element;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 2)
				{
					index++;
					continue;
				}
				else if(index == 2)
				{
					if(element == "help")
					{
						stringStream << "Description: This command prints all configuration parameters of this peer. The values are in BidCoS packet format." << std::endl;
						stringStream << "Usage: config print" << std::endl << std::endl;
						stringStream << "Parameters:" << std::endl;
						stringStream << "  There are no parameters." << std::endl;
						return stringStream.str();
					}
				}
				index++;
			}

			return printConfig();
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

std::string MyPeer::printConfig()
{
	try
	{
		std::ostringstream stringStream;
		stringStream << "MASTER" << std::endl;
		stringStream << "{" << std::endl;
		for(std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::const_iterator i = configCentral.begin(); i != configCentral.end(); ++i)
		{
			stringStream << "\t" << "Channel: " << std::dec << i->first << std::endl;
			stringStream << "\t{" << std::endl;
			for(std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
			{
				stringStream << "\t\t[" << j->first << "]: ";
				if(!j->second.rpcParameter) stringStream << "(No RPC parameter) ";
				for(std::vector<uint8_t>::const_iterator k = j->second.data.begin(); k != j->second.data.end(); ++k)
				{
					stringStream << std::hex << std::setfill('0') << std::setw(2) << (int32_t)*k << " ";
				}
				stringStream << std::endl;
			}
			stringStream << "\t}" << std::endl;
		}
		stringStream << "}" << std::endl << std::endl;

		stringStream << "VALUES" << std::endl;
		stringStream << "{" << std::endl;
		for(std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::const_iterator i = valuesCentral.begin(); i != valuesCentral.end(); ++i)
		{
			stringStream << "\t" << "Channel: " << std::dec << i->first << std::endl;
			stringStream << "\t{" << std::endl;
			for(std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
			{
				stringStream << "\t\t[" << j->first << "]: ";
				if(!j->second.rpcParameter) stringStream << "(No RPC parameter) ";
				for(std::vector<uint8_t>::const_iterator k = j->second.data.begin(); k != j->second.data.end(); ++k)
				{
					stringStream << std::hex << std::setfill('0') << std::setw(2) << (int32_t)*k << " ";
				}
				stringStream << std::endl;
			}
			stringStream << "\t}" << std::endl;
		}
		stringStream << "}" << std::endl << std::endl;

		return stringStream.str();
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
    return "";
}

std::string MyPeer::getPhysicalInterfaceId()
{
	if(_physicalInterfaceId.empty()) setPhysicalInterfaceId(GD::defaultPhysicalInterface->getID());
	return _physicalInterfaceId;
}

void MyPeer::setPhysicalInterfaceId(std::string id)
{
	if(id.empty() || (GD::physicalInterfaces.find(id) != GD::physicalInterfaces.end() && GD::physicalInterfaces.at(id)))
	{
		_physicalInterfaceId = id;
		setPhysicalInterface(id.empty() ? GD::defaultPhysicalInterface : GD::physicalInterfaces.at(_physicalInterfaceId));
		saveVariable(19, _physicalInterfaceId);
	}
	else
	{
		setPhysicalInterface(GD::defaultPhysicalInterface);
		saveVariable(19, _physicalInterfaceId);
	}
}

void MyPeer::setPhysicalInterface(std::shared_ptr<IEnOceanInterface> interface)
{
	try
	{
		if(!interface) return;
		_physicalInterface = interface;
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

void MyPeer::loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows)
{
	try
	{
		if(!rows) rows = _bl->db->getPeerVariables(_peerID);
		Peer::loadVariables(central, rows);

		_rpcDevice = GD::family->getRpcDevices()->find(_deviceType, _firmwareVersion, -1);
		if(!_rpcDevice) return;

		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			switch(row->second.at(2)->intValue)
			{
			case 19:
				_physicalInterfaceId = row->second.at(4)->textValue;
				if(!_physicalInterfaceId.empty() && GD::physicalInterfaces.find(_physicalInterfaceId) != GD::physicalInterfaces.end()) setPhysicalInterface(GD::physicalInterfaces.at(_physicalInterfaceId));
				break;
			case 20:
				_rollingCode = row->second.at(3)->intValue;
				break;
			case 21:
				_aesKey = *row->second.at(5)->binaryValue;
				break;
			case 22:
				_encryptionType = row->second.at(3)->intValue;
				break;
			case 23:
				_cmacSize = row->second.at(3)->intValue;
				break;
			case 24:
				_rollingCodeInTx = (bool)row->second.at(3)->intValue;
				break;
			case 25:
				_rollingCodeSize = row->second.at(3)->intValue;
				break;
			}
		}
		if(!_physicalInterface) _physicalInterface = GD::defaultPhysicalInterface;
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

void MyPeer::saveVariables()
{
	try
	{
		if(_peerID == 0) return;
		Peer::saveVariables();
		saveVariable(19, _physicalInterfaceId);
		saveVariable(20, _rollingCode);
		saveVariable(21, _aesKey);
		saveVariable(22, _encryptionType);
		saveVariable(23, _cmacSize);
		saveVariable(24, (int32_t)_rollingCodeInTx);
		saveVariable(25, _rollingCodeSize);
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

bool MyPeer::load(BaseLib::Systems::ICentral* central)
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows;
		loadVariables(central, rows);
		if(!_rpcDevice)
		{
			GD::out.printError("Error loading peer " + std::to_string(_peerID) + ": Device type not found: 0x" + BaseLib::HelperFunctions::getHexString(_deviceType) + " Firmware version: " + std::to_string(_firmwareVersion));
			return false;
		}

		initializeTypeString();
		std::string entry;
		loadConfig();
		initializeCentralConfig();

		serviceMessages.reset(new BaseLib::Systems::ServiceMessages(_bl, _peerID, _serialNumber, this));
		serviceMessages->load();

		for(auto channelIterator : valuesCentral)
		{
			std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator.second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator.second.end() && parameterIterator->second.rpcParameter)
			{
				if(channelIterator.first == 0) _globalRfChannel = true;
				setRfChannel(channelIterator.first, parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue);
			}
		}

		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::iterator channelIterator = configCentral.find(0);
		if(channelIterator != configCentral.end())
		{
			std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("ENCRYPTION");
			if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
			{
				_forceEncryption = parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->booleanValue;
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

void MyPeer::initializeCentralConfig()
{
	try
	{
		Peer::initializeCentralConfig();

		for(auto channelIterator : valuesCentral)
		{
			std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator.second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator.second.end() && parameterIterator->second.rpcParameter)
			{
				if(channelIterator.first == 0) _globalRfChannel = true;
				setRfChannel(channelIterator.first, parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue);
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

void MyPeer::setRssiDevice(uint8_t rssi)
{
	try
	{
		if(_disposing || rssi == 0) return;
		uint32_t time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		if(time - _lastRssiDevice > 10)
		{
			_lastRssiDevice = time;

			std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::iterator channelIterator = valuesCentral.find(0);
			if(channelIterator == valuesCentral.end()) return;
			std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RSSI_DEVICE");
			if(parameterIterator == channelIterator->second.end()) return;

			BaseLib::Systems::RPCConfigurationParameter& parameter = parameterIterator->second;
			parameter.data.at(0) = rssi;

			std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>({std::string("RSSI_DEVICE")}));
			std::shared_ptr<std::vector<PVariable>> rpcValues(new std::vector<PVariable>());
			rpcValues->push_back(parameter.rpcParameter->convertFromPacket(parameter.data));

			raiseRPCEvent(_peerID, 0, _serialNumber + ":0", valueKeys, rpcValues);
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

int32_t MyPeer::getRfChannel(int32_t channel)
{
	try
	{
		std::lock_guard<std::mutex> rfChannelsGuard(_rfChannelsMutex);
		return _rfChannels[channel];
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
    return 0;
}

std::vector<int32_t> MyPeer::getRfChannels()
{
	try
	{
		std::vector<int32_t> channels;
		std::lock_guard<std::mutex> rfChannelsGuard(_rfChannelsMutex);
		for(auto element : _rfChannels)
		{
			if(element.second != -1) channels.push_back(element.second);
		}
		return channels;
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
    return std::vector<int32_t>();
}

void MyPeer::setRfChannel(int32_t channel, int32_t rfChannel)
{
	try
	{
		if(rfChannel < 0 || rfChannel > 127) return;
		BaseLib::PVariable value(new BaseLib::Variable(rfChannel));
		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
		if(channelIterator != valuesCentral.end())
		{
			std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
			{
				parameterIterator->second.rpcParameter->convertToPacket(value, parameterIterator->second.data);
				if(parameterIterator->second.databaseID > 0) saveParameter(parameterIterator->second.databaseID, parameterIterator->second.data);
				else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, "RF_CHANNEL", parameterIterator->second.data);

				{
					std::lock_guard<std::mutex> rfChannelsGuard(_rfChannelsMutex);
					_rfChannels[channel] = parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue;
				}

				if(_bl->debugLevel >= 4) GD::out.printInfo("Info: RF_CHANNEL of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterIterator->second.data) + ".");
			}
			else GD::out.printError("Error: Parameter RF_CHANNEL not found.");
		}
		else GD::out.printError("Error: Parameter RF_CHANNEL not found.");
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

void MyPeer::getValuesFromPacket(PMyPacket packet, std::vector<FrameValues>& frameValues)
{
	try
	{
		if(!_rpcDevice) return;
		//equal_range returns all elements with "0" or an unknown element as argument
		if(_rpcDevice->packetsByMessageType.find(packet->getRorg()) == _rpcDevice->packetsByMessageType.end()) return;
		std::pair<PacketsByMessageType::iterator, PacketsByMessageType::iterator> range = _rpcDevice->packetsByMessageType.equal_range((uint32_t)packet->getRorg());
		if(range.first == _rpcDevice->packetsByMessageType.end()) return;
		PacketsByMessageType::iterator i = range.first;
		do
		{
			FrameValues currentFrameValues;
			PPacket frame(i->second);
			if(!frame) continue;
			std::vector<char> erpPacket = packet->getData();
			if(erpPacket.empty()) break;
			uint32_t erpPacketBitSize = erpPacket.size() * 8;
			int32_t channelIndex = frame->channelIndex;
			int32_t channel = -1;
			if(channelIndex >= 0 && channelIndex < (signed)erpPacket.size()) channel = erpPacket.at(channelIndex);
			if(channel > -1 && frame->channelSize < 1.0) channel &= (0xFF >> (8 - std::lround(frame->channelSize * 10) % 10));
			if(frame->channel > -1) channel = frame->channel;
			if(channel == -1) continue;
			currentFrameValues.frameID = frame->id;
			bool abort = false;

			for(BinaryPayloads::iterator j = frame->binaryPayloads.begin(); j != frame->binaryPayloads.end(); ++j)
			{
				std::vector<uint8_t> data;
				if((*j)->bitSize > 0 && (*j)->bitIndex > 0)
				{
					if((*j)->bitIndex >= erpPacketBitSize) continue;
					data = packet->getPosition((*j)->bitIndex, (*j)->bitSize);

					if((*j)->constValueInteger > -1)
					{
						int32_t intValue = 0;
						_bl->hf.memcpyBigEndian(intValue, data);
						if(intValue != (*j)->constValueInteger)
						{
							abort = true;
							break;
						}
						else if((*j)->parameterId.empty()) continue;
					}
				}
				else if((*j)->constValueInteger > -1)
				{
					_bl->hf.memcpyBigEndian(data, (*j)->constValueInteger);
				}
				else continue;

				//Check for low battery
				if((*j)->parameterId == "LOWBAT")
				{
					if(data.size() > 0 && data.at(0))
					{
						serviceMessages->set("LOWBAT", true);
						if(_bl->debugLevel >= 4) GD::out.printInfo("Info: LOWBAT of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + " was set to \"true\".");
					}
					else serviceMessages->set("LOWBAT", false);
				}

				for(std::vector<PParameter>::iterator k = frame->associatedVariables.begin(); k != frame->associatedVariables.end(); ++k)
				{
					if((*k)->physical->groupId != (*j)->parameterId) continue;
					currentFrameValues.parameterSetType = (*k)->parent()->type();
					bool setValues = false;
					if(currentFrameValues.paramsetChannels.empty()) //Fill paramsetChannels
					{
						int32_t startChannel = (channel < 0) ? 0 : channel;
						int32_t endChannel;
						//When fixedChannel is -2 (means '*') cycle through all channels
						if(frame->channel == -2)
						{
							startChannel = 0;
							endChannel = _rpcDevice->functions.rbegin()->first;
						}
						else endChannel = startChannel;
						for(int32_t l = startChannel; l <= endChannel; l++)
						{
							Functions::iterator functionIterator = _rpcDevice->functions.find(l);
							if(functionIterator == _rpcDevice->functions.end()) continue;
							PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(currentFrameValues.parameterSetType);
							if(!parameterGroup || parameterGroup->parameters.find((*k)->id) == parameterGroup->parameters.end()) continue;
							currentFrameValues.paramsetChannels.push_back(l);
							currentFrameValues.values[(*k)->id].channels.push_back(l);
							setValues = true;
						}
					}
					else //Use paramsetChannels
					{
						for(std::list<uint32_t>::const_iterator l = currentFrameValues.paramsetChannels.begin(); l != currentFrameValues.paramsetChannels.end(); ++l)
						{
							Functions::iterator functionIterator = _rpcDevice->functions.find(*l);
							if(functionIterator == _rpcDevice->functions.end()) continue;
							PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(currentFrameValues.parameterSetType);
							if(!parameterGroup || parameterGroup->parameters.find((*k)->id) == parameterGroup->parameters.end()) continue;
							currentFrameValues.values[(*k)->id].channels.push_back(*l);
							setValues = true;
						}
					}
					if(setValues) currentFrameValues.values[(*k)->id].value = data;
				}
			}
			if(abort) continue;
			if(!currentFrameValues.values.empty()) frameValues.push_back(currentFrameValues);
		} while(++i != range.second && i != _rpcDevice->packetsByMessageType.end());
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

void MyPeer::packetReceived(PMyPacket& packet)
{
	try
	{
		if(_disposing || !packet || !_rpcDevice) return;
		if(_rpcDevice->addressSize != 25 && packet->senderAddress() != _address) return;
		else if(_rpcDevice->addressSize == 25 && (signed)(packet->senderAddress() & 0xFFFFFF80) != _address) return;
		std::shared_ptr<MyCentral> central = std::dynamic_pointer_cast<MyCentral>(getCentral());
		if(!central) return;
		setLastPacketReceived();
		setRssiDevice(packet->getRssi() * -1);
		serviceMessages->endUnreach();

		if(packet->getRorg() == 0x35) // Encryption teach-in
		{
			std::vector<char> data = packet->getData();
			if(data.size() == 15 && (data[1] & 0xC0) == 0) // First packet
			{
				if((data[1] & 0x30) != 0x20) // IDX === 0 only => Number of packets
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet as it specifies an unrecognized number of packets.");
					return;
				}
				if((data[1] & 0x08) == 0x08)
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet, because RLC and key are encrypted with a pre-shared key. That is currenctly not supported.");
					return;
				}
				if((data[1] & 0x04) == 0 && (data[1] & 0x03) != 0)
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet, because bidrectional teach-in procedure was requested. That is currenctly not supported.");
					return;
				}
				if((data[2] & 0x07) != 0x03)
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet, because an encryption type other than VAES was requested. That is currenctly not supported.");
					return;
				}
				if((data[2] & 0x18) != 0x08 && (data[2] & 0x18) != 0x10)
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet, because encryption has no CMAC. That is currenctly not supported.");
					return;
				}
				if((data[2] & 0x20) == 0x20)
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet, because the Rolling Code is transmitted in data telegram. That is currenctly not supported.");
					return;
				}
				if((data[2] & 0xC0) != 0x40)
				{
					GD::out.printWarning("Warning: Can't process first encryption teach-in packet, because encryption has no Rolling Code or Rolling Code size is 3 bytes. That is currenctly not supported.");
					return;
				}
				setEncryptionType(data[2] & 0x07);
				setCmacSize((data[2] & 0x18) == 0x08 ? 3 : 4);
				setRollingCodeInTx((data[2] & 0x20) == 0x20);
				setRollingCodeSize((data[2] & 0xC0) == 0x40 ? 2 : 3);
				setRollingCode((((int32_t)(uint8_t)data[3]) << 8) | (uint8_t)data[4]);
				_aesKeyPart1.clear();
				_aesKeyPart1.reserve(16);
				for(int32_t i = 5; i <= 9; i++)
				{
					_aesKeyPart1.push_back(data[i]);
				}
			}
			else if(data.size() == 0x12 && (data[1] & 0xC0) == 0x40) // Second packet
			{
				if(_aesKeyPart1.empty())
				{
					GD::out.printWarning("Warning: Second encryption packet received but no data from first packet is available.");
					return;
				}
				for(int32_t i = 2; i <= 12; i++)
				{
					_aesKeyPart1.push_back(data[i]);
				}
				setAesKey(_aesKeyPart1);
				GD::out.printInfo("Info: Encryption was setup successfully. Encryption type: " + std::to_string(_encryptionType) + ", CMAC size: " + std::to_string(_cmacSize) + " bytes, Rolling Code size: " + std::to_string(_rollingCodeSize) + " bytes, Rolling Code: 0x" + BaseLib::HelperFunctions::getHexString(_rollingCode) + ", Key: " + BaseLib::HelperFunctions::getHexString(_aesKey));
			}
			else GD::out.printWarning("Warning: Can't process encryption teach-in packet as it has an unrecognized size or index.");
			return;
		}
		else if(packet->getRorg() == 0x30)
		{
			if(_aesKey.empty() || _rollingCode == -1)
			{
				GD::out.printError("Error: Encrypted packet received, but Homegear never received the encryption teach-in packets. Plaise activate \"encryption teach-in\" on your device.");
				return;
			}
			// Create object here to avoid unnecessary allocation of secure memory
			if(!_security) _security.reset(new Security(GD::bl));
			std::vector<char> data = packet->getData();
			if(_security->checkCmac(_aesKey, data, packet->getDataSize() - _cmacSize - 5, _rollingCode, _rollingCodeSize, _cmacSize))
			{
				if(_bl->debugLevel >= 5) GD::out.printDebug("Debug: CMAC verified.");
				if(!_security->decrypt(_aesKey, data, packet->getDataSize() - _cmacSize - 5, _rollingCode, _rollingCodeSize))
				{
					GD::out.printError("Error: Decryption of packet failed.");
					return;
				}
				packet->setData(data);
				setRollingCode(_rollingCode + 1);

				if(!_forceEncryption) GD::out.printWarning("Warning: Encrypted packet received for peer " + std::to_string(_peerID) + " but unencrypted packet still will be accepted. Please set the configuration parameter \"ENCRYPTION\" to \"true\" to enforce encryption and ignore unencrypted packets.");
			}
			else
			{
				GD::out.printError("Error: Secure packet verification failed. If your device is still working, this might be an attack. If your device is not working please send an encryption teach-in packet to Homegear to resync the encryption.");
				return;
			}
		}
		else if(packet->getRorg() == 0x31)
		{
			GD::out.printWarning("Warning: Encrypted packets containing the RORG are currently not supported.");
			return;
		}
		else if(packet->getRorg() == 0x32)
		{
			GD::out.printWarning("Warning: Ignoring unencrypted secure telegram.");
			return;
		}
		else if(_forceEncryption)
		{
			GD::out.printError("Error: Unencrypted packet received for peer " + std::to_string(_peerID) + ", but encryption is enforced. Ignoring packet.");
			return;
		}

		std::vector<FrameValues> frameValues;
		getValuesFromPacket(packet, frameValues);
		std::map<uint32_t, std::shared_ptr<std::vector<std::string>>> valueKeys;
		std::map<uint32_t, std::shared_ptr<std::vector<PVariable>>> rpcValues;
		//Loop through all matching frames
		for(std::vector<FrameValues>::iterator a = frameValues.begin(); a != frameValues.end(); ++a)
		{
			PPacket frame;
			if(!a->frameID.empty()) frame = _rpcDevice->packetsById.at(a->frameID);
			if(!frame) continue;

			{
				std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
				auto rpcRequestIterator = _rpcRequests.find(a->frameID);
				if(rpcRequestIterator != _rpcRequests.end()) rpcRequestIterator->second->conditionVariable.notify_all();
			}

			for(std::map<std::string, FrameValue>::iterator i = a->values.begin(); i != a->values.end(); ++i)
			{
				for(std::list<uint32_t>::const_iterator j = a->paramsetChannels.begin(); j != a->paramsetChannels.end(); ++j)
				{
					if(std::find(i->second.channels.begin(), i->second.channels.end(), *j) == i->second.channels.end()) continue;
					if(!valueKeys[*j] || !rpcValues[*j])
					{
						valueKeys[*j].reset(new std::vector<std::string>());
						rpcValues[*j].reset(new std::vector<PVariable>());
					}

					BaseLib::Systems::RPCConfigurationParameter& parameter = valuesCentral[*j][i->first];
					if(parameter.data.size() == i->second.value.size() && std::equal(parameter.data.begin(), parameter.data.end(), i->second.value.begin())) continue;
					parameter.data = i->second.value;
					if(parameter.databaseID > 0) saveParameter(parameter.databaseID, parameter.data);
					else saveParameter(0, ParameterGroup::Type::Enum::variables, *j, i->first, parameter.data);
					if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + i->first + " on channel " + std::to_string(*j) + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber  + " was set to 0x" + BaseLib::HelperFunctions::getHexString(i->second.value) + ".");

					if(parameter.rpcParameter)
					{
						//Process service messages
						if(parameter.rpcParameter->service && !i->second.value.empty())
						{
							if(parameter.rpcParameter->logical->type == ILogical::Type::Enum::tEnum)
							{
								serviceMessages->set(i->first, i->second.value.at(0), *j);
							}
							else if(parameter.rpcParameter->logical->type == ILogical::Type::Enum::tBoolean)
							{
								serviceMessages->set(i->first, (bool)i->second.value.at(0));
							}
						}

						valueKeys[*j]->push_back(i->first);
						rpcValues[*j]->push_back(parameter.rpcParameter->convertFromPacket(i->second.value, true));
					}
				}
			}

			if(!frame->responseTypeId.empty())
			{
				if(getRfChannel(0) == -1) GD::out.printError("Error: RF_CHANNEL is not set. Please pair the device.");
				else
				{
					PacketsById::iterator packetIterator = _rpcDevice->packetsById.find(frame->responseTypeId);
					if(packetIterator == _rpcDevice->packetsById.end()) GD::out.printError("Error: Response packet with ID \"" + frame->responseTypeId + "\" not found.");
					else
					{
						PPacket responseFrame = packetIterator->second;
						if(responseFrame->subtype == -1) responseFrame->subtype = 1;
						PMyPacket packet(new MyPacket((MyPacket::Type)responseFrame->subtype, (uint8_t)responseFrame->type, _physicalInterface->getBaseAddress() | getRfChannel(0), _address));

						for(BinaryPayloads::iterator i = responseFrame->binaryPayloads.begin(); i != responseFrame->binaryPayloads.end(); ++i)
						{
							if((*i)->constValueInteger > -1)
							{
								std::vector<uint8_t> data;
								_bl->hf.memcpyBigEndian(data, (*i)->constValueInteger);
								packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
								continue;
							}
							bool paramFound = false;
							for(std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator j = valuesCentral[responseFrame->channel].begin(); j != valuesCentral[responseFrame->channel].end(); ++j)
							{
								//Only compare id. Till now looking for value_id was not necessary.
								if((*i)->parameterId == j->second.rpcParameter->physical->groupId)
								{
									std::vector<uint8_t> data = j->second.data;
									packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
									paramFound = true;
									break;
								}
							}
							if(!paramFound) GD::out.printError("Error constructing packet. param \"" + (*i)->parameterId + "\" not found. Peer: " + std::to_string(_peerID) + " Serial number: " + _serialNumber + " Frame: " + responseFrame->id);
						}

						_physicalInterface->sendPacket(packet);
					}
				}
			}
		}

		//if(!rpcValues.empty() && !resendPacket)
		if(!rpcValues.empty())
		{
			for(std::map<uint32_t, std::shared_ptr<std::vector<std::string>>>::const_iterator j = valueKeys.begin(); j != valueKeys.end(); ++j)
			{
				if(j->second->empty()) continue;
				std::string address(_serialNumber + ":" + std::to_string(j->first));
				raiseEvent(_peerID, j->first, j->second, rpcValues.at(j->first));
				raiseRPCEvent(_peerID, j->first, address, j->second, rpcValues.at(j->first));
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

PParameterGroup MyPeer::getParameterSet(int32_t channel, ParameterGroup::Type::Enum type)
{
	try
	{
		PFunction rpcChannel = _rpcDevice->functions.at(channel);
		if(type == ParameterGroup::Type::Enum::variables) return rpcChannel->variables;
		else if(type == ParameterGroup::Type::Enum::config) return rpcChannel->configParameters;
		else if(type == ParameterGroup::Type::Enum::link) return rpcChannel->linkParameters;
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
	return PParameterGroup();
}

bool MyPeer::getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
	try
	{
		if(channel == 1)
		{
			if(parameter->id == "PEER_ID") parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), valuesCentral[channel][parameter->id].data);
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
    return false;
}

bool MyPeer::getParamsetHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
	try
	{
		if(channel == 1)
		{
			if(parameter->id == "PEER_ID") parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), valuesCentral[channel][parameter->id].data);
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
    return false;
}

PVariable MyPeer::putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool onlyPushing)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(channel < 0) channel = 0;
		if(remoteChannel < 0) remoteChannel = 0;
		Functions::iterator functionIterator = _rpcDevice->functions.find(channel);
		if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel.");
		if(type == ParameterGroup::Type::none) type = ParameterGroup::Type::link;
		PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
		if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set.");
		if(variables->structValue->empty()) return PVariable(new Variable(VariableType::tVoid));

		if(type == ParameterGroup::Type::Enum::config)
		{
			bool parameterChanged = false;
			for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
			{
				if(i->first.empty() || !i->second) continue;
				if(configCentral[channel].find(i->first) == configCentral[channel].end()) continue;
				BaseLib::Systems::RPCConfigurationParameter& parameter = configCentral[channel][i->first];
				if(!parameter.rpcParameter) continue;
				if(parameter.rpcParameter->password && i->second->stringValue.empty()) continue; //Don't safe password if empty
				parameter.rpcParameter->convertToPacket(i->second, parameter.data);
				if(parameter.databaseID > 0) saveParameter(parameter.databaseID, parameter.data);
				else saveParameter(0, ParameterGroup::Type::Enum::config, channel, i->first, parameter.data);

				if(channel == 0 && i->first == "ENCRYPTION" && i->second->booleanValue != _forceEncryption) _forceEncryption = i->second->booleanValue;

				parameterChanged = true;
				GD::out.printInfo("Info: Parameter " + i->first + " of peer " + std::to_string(_peerID) + " and channel " + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameter.data) + ".");
			}

			if(parameterChanged) raiseRPCUpdateDevice(_peerID, channel, _serialNumber + ":" + std::to_string(channel), 0);
		}
		else if(type == ParameterGroup::Type::Enum::variables)
		{
			for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
			{
				if(i->first.empty() || !i->second) continue;
				setValue(clientInfo, channel, i->first, i->second, false);
			}
		}
		else
		{
			return Variable::createError(-3, "Parameter set type is not supported.");
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

PVariable MyPeer::setInterface(BaseLib::PRpcClientInfo clientInfo, std::string interfaceId)
{
	try
	{
		if(!interfaceId.empty() && GD::physicalInterfaces.find(interfaceId) == GD::physicalInterfaces.end())
		{
			return Variable::createError(-5, "Unknown physical interface.");
		}
		std::shared_ptr<IEnOceanInterface> interface(GD::physicalInterfaces.at(interfaceId));
		setPhysicalInterfaceId(interfaceId);
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

void MyPeer::sendPacket(PMyPacket packet, std::string responseId, int32_t delay)
{
	try
	{
		if(!responseId.empty())
		{
			int32_t resends = 0;
			int32_t resendTimeout = 500;
			auto channelIterator = configCentral.find(0);
			if(channelIterator != configCentral.end())
			{
				std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RESENDS_WHEN_NO_ACK");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					resends = parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue;
					if(resends < 0) resends = 0;
					else if(resends > 12) resends = 12;
				}
				parameterIterator = channelIterator->second.find("RESEND_TIMEOUT");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					resendTimeout = parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue;
					if(resends < 10) resends = 10;
					else if(resends > 10000) resends = 10000;
				}
			}
			if(resends == 0) _physicalInterface->sendPacket(packet);
			else
			{
				PRpcRequest rpcRequest = std::make_shared<RpcRequest>();
				rpcRequest->responseId = responseId;
				{
					std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
					auto requestIterator = _rpcRequests.find(rpcRequest->responseId);
					if(requestIterator != _rpcRequests.end()) requestIterator->second->abort = true;
					_rpcRequests.emplace(rpcRequest->responseId, rpcRequest);
				}
				for(int32_t i = 0; i < resends + 1; i++)
				{
					std::unique_lock<std::mutex> conditionVariableGuard(rpcRequest->conditionVariableMutex);
					_physicalInterface->sendPacket(packet);
					if(rpcRequest->conditionVariable.wait_for(conditionVariableGuard, std::chrono::milliseconds(resendTimeout)) == std::cv_status::no_timeout || rpcRequest->abort) break;
				}
				{
					std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
					_rpcRequests.erase(rpcRequest->responseId);
				}
			}
		}
		else _physicalInterface->sendPacket(packet);
		if(delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
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

PVariable MyPeer::setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(!value) return Variable::createError(-32500, "value is nullptr.");
		Peer::setValue(clientInfo, channel, valueKey, value, wait); //Ignore result, otherwise setHomegerValue might not be executed
		std::shared_ptr<MyCentral> central = std::dynamic_pointer_cast<MyCentral>(getCentral());
		if(!central) return Variable::createError(-32500, "Could not get central object.");;
		if(valueKey.empty()) return Variable::createError(-5, "Value key is empty.");
		if(channel == 0 && serviceMessages->set(valueKey, value->booleanValue)) return PVariable(new Variable(VariableType::tVoid));
		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
		if(channelIterator == valuesCentral.end()) return Variable::createError(-2, "Unknown channel.");
		std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(valueKey);
		if(parameterIterator == valuesCentral[channel].end()) return Variable::createError(-5, "Unknown parameter.");
		PParameter rpcParameter = parameterIterator->second.rpcParameter;
		if(!rpcParameter) return Variable::createError(-5, "Unknown parameter.");
		BaseLib::Systems::RPCConfigurationParameter& parameter = valuesCentral[channel][valueKey];
		std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>());
		std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable>());
		if(rpcParameter->readable)
		{
			valueKeys->push_back(valueKey);
			values->push_back(value);
		}
		if(rpcParameter->physical->operationType == IPhysical::OperationType::Enum::store)
		{
			rpcParameter->convertToPacket(value, parameter.data);
			if(parameter.databaseID > 0) saveParameter(parameter.databaseID, parameter.data);
			else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameter.data);
			if(!valueKeys->empty())
			{
				raiseEvent(_peerID, channel, valueKeys, values);
				raiseRPCEvent(_peerID, channel, _serialNumber + ":" + std::to_string(channel), valueKeys, values);
			}
			return PVariable(new Variable(VariableType::tVoid));
		}
		else if(rpcParameter->physical->operationType != IPhysical::OperationType::Enum::command) return Variable::createError(-6, "Parameter is not settable.");
		if(rpcParameter->setPackets.empty() && !rpcParameter->writeable) return Variable::createError(-6, "parameter is read only");
		std::vector<std::shared_ptr<Parameter::Packet>> setRequests;
		if(!rpcParameter->setPackets.empty())
		{
			for(std::vector<std::shared_ptr<Parameter::Packet>>::iterator i = rpcParameter->setPackets.begin(); i != rpcParameter->setPackets.end(); ++i)
			{
				if((*i)->conditionOperator != Parameter::Packet::ConditionOperator::none)
				{
					int32_t intValue = value->integerValue;
					if(parameter.rpcParameter->logical->type == BaseLib::DeviceDescription::ILogical::Type::Enum::tBoolean) intValue = value->booleanValue;
					if(!(*i)->checkCondition(intValue)) continue;
				}
				setRequests.push_back(*i);
			}
		}

		rpcParameter->convertToPacket(value, parameter.data);
		if(parameter.databaseID > 0) saveParameter(parameter.databaseID, parameter.data);
		else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameter.data);
		if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + valueKey + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameter.data) + ".");

		if(valueKey == "PAIRING")
		{
			if(value->integerValue == -1 && getRfChannel(_globalRfChannel ? 0 : channel) != -1) value->integerValue = getRfChannel(_globalRfChannel ? 0 : channel);
			if(value->integerValue == -1) value->integerValue = central->getFreeRfChannel(_physicalInterfaceId);
			if(value->integerValue == -1)
			{
				GD::out.printError("Error: There is no free channel to pair a new device. You need to either reuse a channel or install another communication module.");
				return Variable::createError(-7, "There is no free channel to pair a new device. You need to either reuse a channel or install another communication module.");
			}

			std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>>::iterator channelIterator = valuesCentral.find(_globalRfChannel ? 0 : channel);
			if(channelIterator != valuesCentral.end())
			{
				std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RF_CHANNEL");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					parameterIterator->second.rpcParameter->convertToPacket(value, parameterIterator->second.data);
					if(parameterIterator->second.databaseID > 0) saveParameter(parameterIterator->second.databaseID, parameterIterator->second.data);
					else saveParameter(0, ParameterGroup::Type::Enum::variables, _globalRfChannel ? 0 : channel, "RF_CHANNEL", parameterIterator->second.data);
					setRfChannel(_globalRfChannel ? 0 : channel, parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue);
					if(_bl->debugLevel >= 4) GD::out.printInfo("Info: RF_CHANNEL of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterIterator->second.data) + ".");
					valueKeys->push_back("RF_CHANNEL");
					values->push_back(value);
				}
				else return Variable::createError(-5, "Parameter RF_CHANNEL not found.");
			}
			else return Variable::createError(-5, "Parameter RF_CHANNEL not found.");
		}
		// {{{ Blinds
			else if(_deviceType == 0x01A53807)
			{
				if(valueKey == "UP" || valueKey == "DOWN")
				{
					if(value->booleanValue)
					{
						setValue(clientInfo, channel, valueKey == "UP" ? "DOWN" : "UP", std::make_shared<BaseLib::Variable>(false), false);

						channelIterator = configCentral.find(0);
						if(channelIterator != configCentral.end())
						{
							std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("SIGNAL_DURATION");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								int32_t newPosition = valueKey == "UP" ? 10000 : 0;
								int32_t positionDifference = newPosition - _blindPosition;
								_blindSignalDuration = parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue * 1000;
								int32_t blindCurrentSignalDuration = _blindSignalDuration / (10000 / std::abs(positionDifference));
								_blindStateResetTime = BaseLib::HelperFunctions::getTime() + blindCurrentSignalDuration + (newPosition == 0 || newPosition == 10000 ? 5000 : 0);
								_lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
								_blindUp = valueKey == "UP";
							}
						}
					}
					else _blindStateResetTime = -1;
				}
				else if(valueKey == "LEVEL")
				{
					if(value->integerValue > 10) value->integerValue = 10;
					else if(value->integerValue < 0) value->integerValue = 0;
					setValue(clientInfo, channel, valueKey == "UP" ? "DOWN" : "UP", std::make_shared<BaseLib::Variable>(false), false);

					int32_t newPosition = value->integerValue * 1000;
					if(newPosition != _blindPosition)
					{
						int32_t positionDifference = newPosition - _blindPosition;

						if(positionDifference != 0)
						{
							channelIterator = configCentral.find(0);
							if(channelIterator != configCentral.end())
							{
								std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("SIGNAL_DURATION");
								if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
								{
									_blindSignalDuration = parameterIterator->second.rpcParameter->convertFromPacket(parameterIterator->second.data)->integerValue * 1000;
									int32_t blindCurrentSignalDuration = _blindSignalDuration / (10000 / std::abs(positionDifference));
									_blindStateResetTime = BaseLib::HelperFunctions::getTime() + blindCurrentSignalDuration + (newPosition == 0 || newPosition == 10000 ? 5000 : 0);
									_lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
									_blindUp = positionDifference > 0;

									PMyPacket packet(new MyPacket((MyPacket::Type)1, (uint8_t)0xF6, _physicalInterface->getBaseAddress() | getRfChannel(_globalRfChannel ? 0 : channel), _address));
									std::vector<uint8_t> data{ _blindUp ? (uint8_t)0x30 : (uint8_t)0x10 };
									packet->setPosition(8, 8, data);
									sendPacket(packet, "STATE_INFO", 0);
								}
							}
						}
					}
				}
			}
		// }}}

		if(getRfChannel(_globalRfChannel ? 0 : channel) == -1) return Variable::createError(-5, "RF_CHANNEL is not set. Please pair the device.");

		for(std::shared_ptr<Parameter::Packet> setRequest : setRequests)
		{
			PacketsById::iterator packetIterator = _rpcDevice->packetsById.find(setRequest->id);
			if(packetIterator == _rpcDevice->packetsById.end()) return Variable::createError(-6, "No frame was found for parameter " + valueKey);
			PPacket frame = packetIterator->second;

			if(frame->subtype == -1) frame->subtype = 1;
			PMyPacket packet(new MyPacket((MyPacket::Type)frame->subtype, (uint8_t)frame->type, _physicalInterface->getBaseAddress() | getRfChannel(_globalRfChannel ? 0 : channel), _address));

			for(BinaryPayloads::iterator i = frame->binaryPayloads.begin(); i != frame->binaryPayloads.end(); ++i)
			{
				if((*i)->constValueInteger > -1)
				{
					std::vector<uint8_t> data;
					_bl->hf.memcpyBigEndian(data, (*i)->constValueInteger);
					packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
					continue;
				}
				//We can't just search for param, because it is ambiguous (see for example LEVEL for HM-CC-TC.
				if((*i)->parameterId == rpcParameter->physical->groupId)
				{
					std::vector<uint8_t> data = valuesCentral[channel][valueKey].data;
					packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
				}
				//Search for all other parameters
				else
				{
					bool paramFound = false;
					for(std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator j = valuesCentral[channel].begin(); j != valuesCentral[channel].end(); ++j)
					{
						//Only compare id. Till now looking for value_id was not necessary.
						if((*i)->parameterId == j->second.rpcParameter->physical->groupId)
						{
							std::vector<uint8_t> data = j->second.data;
							packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
							paramFound = true;
							break;
						}
					}
					if(!paramFound) GD::out.printError("Error constructing packet. param \"" + (*i)->parameterId + "\" not found. Peer: " + std::to_string(_peerID) + " Serial number: " + _serialNumber + " Frame: " + frame->id);
				}
			}

			if(!setRequest->autoReset.empty())
			{
				for(std::vector<std::string>::iterator j = setRequest->autoReset.begin(); j != setRequest->autoReset.end(); ++j)
				{
					std::unordered_map<std::string, BaseLib::Systems::RPCConfigurationParameter>::iterator resetParameterIterator = channelIterator->second.find(*j);
					if(resetParameterIterator == channelIterator->second.end()) continue;
					PVariable logicalDefaultValue = resetParameterIterator->second.rpcParameter->logical->getDefaultValue();
					std::vector<uint8_t> defaultValue;
					resetParameterIterator->second.rpcParameter->convertToPacket(logicalDefaultValue, defaultValue);
					if(defaultValue != resetParameterIterator->second.data)
					{
						resetParameterIterator->second.data = defaultValue;
						if(resetParameterIterator->second.databaseID > 0) saveParameter(resetParameterIterator->second.databaseID, resetParameterIterator->second.data);
						else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, *j, resetParameterIterator->second.data);
						GD::out.printInfo( "Info: Parameter \"" + *j + "\" was reset to " + BaseLib::HelperFunctions::getHexString(defaultValue) + ". Peer: " + std::to_string(_peerID) + " Serial number: " + _serialNumber + " Frame: " + frame->id);
						if(rpcParameter->readable)
						{
							valueKeys->push_back(*j);
							values->push_back(logicalDefaultValue);
						}
					}
				}
			}

			sendPacket(packet, setRequest->responseId, setRequest->delay);
		}

		if(!valueKeys->empty())
		{
			raiseEvent(_peerID, channel, valueKeys, values);
			raiseRPCEvent(_peerID, channel, _serialNumber + ":" + std::to_string(channel), valueKeys, values);
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
    return Variable::createError(-32500, "Unknown application error. See error log for more details.");
}

}
