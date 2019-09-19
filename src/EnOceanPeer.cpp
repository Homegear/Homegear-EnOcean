/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPeer.h"

#include "GD.h"
#include "EnOceanPacket.h"
#include "EnOceanCentral.h"

#include <iomanip>

namespace EnOcean
{
std::shared_ptr<BaseLib::Systems::ICentral> EnOceanPeer::getCentral()
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
	return std::shared_ptr<BaseLib::Systems::ICentral>();
}

EnOceanPeer::EnOceanPeer(uint32_t parentID, IPeerEventSink* eventHandler) : BaseLib::Systems::Peer(GD::bl, parentID, eventHandler)
{
	init();
}

EnOceanPeer::EnOceanPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler) : BaseLib::Systems::Peer(GD::bl, id, address, serialNumber, parentID, eventHandler)
{
	init();
}

EnOceanPeer::~EnOceanPeer()
{
	try
	{
		dispose();
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanPeer::init()
{
	try
	{
        _blindSignalDuration = -1;
        _blindStateResetTime = -1;
        _blindUp = false;
        _lastBlindPositionUpdate = 0;
        _lastRpcBlindPositionUpdate = 0;
        _blindCurrentTargetPosition = 0;
        _blindCurrentSignalDuration = 0;
        _blindPosition = 0;
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanPeer::dispose()
{
	if(_disposing) return;
	Peer::dispose();
}

void EnOceanPeer::worker()
{
	try
	{
		//{{{ Resends
		{
			std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
			std::set<std::string> elementsToErase;
			if(!_rpcRequests.empty())
            {
                for(auto request : _rpcRequests)
                {
                    if(request.second->maxResends == 0) continue; //Synchronous
                    if(BaseLib::HelperFunctions::getTime() - request.second->lastResend < request.second->resendTimeout) continue;
                    if(request.second->resends == request.second->maxResends)
                    {
                        serviceMessages->setUnreach(true, false);
                        elementsToErase.emplace(request.first);
                        continue;
                    }

                    setBestInterface();
                    _physicalInterface->sendPacket(request.second->packet);
                    request.second->lastResend = BaseLib::HelperFunctions::getTime();
                    request.second->resends++;
                }
                for(auto& element : elementsToErase)
                {
                    _rpcRequests.erase(element);
                }

                if(_blindStateResetTime != -1)
                {
                    //Correct blind state reset time
                    _blindStateResetTime = BaseLib::HelperFunctions::getTime() + _blindCurrentSignalDuration + (_blindCurrentTargetPosition == 0 || _blindCurrentTargetPosition == 10000 ? 5000 : 0);
                    _lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
                    return;
                }
            }
		}
		// }}}

		if(_blindStateResetTime != -1)
		{
			if(_blindUp) _blindPosition -= (BaseLib::HelperFunctions::getTime() - _lastBlindPositionUpdate) * 10000 / _blindSignalDuration;
			else _blindPosition += (BaseLib::HelperFunctions::getTime() - _lastBlindPositionUpdate) * 10000 / _blindSignalDuration;
			_lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
			if(_blindPosition < 0) _blindPosition = 0;
			else if(_blindPosition > 10000) _blindPosition = 10000;
			bool updatePosition = false;
			if(BaseLib::HelperFunctions::getTime() >= _blindStateResetTime)
			{
                _blindStateResetTime = -1;
				setValue(BaseLib::PRpcClientInfo(), 1, _blindUp ? "UP" : "DOWN", std::make_shared<BaseLib::Variable>(false), false);
				updatePosition = true;
			}
			if(BaseLib::HelperFunctions::getTime() - _lastRpcBlindPositionUpdate >= 5000)
			{
				_lastRpcBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
				updatePosition = true;
			}

			if(updatePosition) updateBlindPosition();
		}
		if(!serviceMessages->getUnreach()) serviceMessages->checkUnreach(_rpcDevice->timeout, getLastPacketReceived());
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanPeer::updateBlindSpeed()
{
    try
    {
        auto channelIterator = valuesCentral.find(1);
        if(channelIterator != valuesCentral.end())
        {
            auto parameterIterator = channelIterator->second.find("CURRENT_SPEED");
            if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
            {
                BaseLib::PVariable blindSpeed = std::make_shared<BaseLib::Variable>(100.0 / (double)(_blindSignalDuration / 1000));
                if(_blindUp) blindSpeed->floatValue *= -1.0;

                std::vector<uint8_t> parameterData;
                parameterIterator->second.rpcParameter->convertToPacket(blindSpeed, parameterData);
                parameterIterator->second.setBinaryData(parameterData);
                if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
                else saveParameter(0, ParameterGroup::Type::Enum::variables, 1, "CURRENT_SPEED", parameterData);
                if(_bl->debugLevel >= 4) GD::out.printInfo("Info: CURRENT_SPEED of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(1) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

                std::shared_ptr<std::vector<std::string>> valueKeys = std::make_shared<std::vector<std::string>>();
                valueKeys->push_back("CURRENT_SPEED");
                std::shared_ptr<std::vector<PVariable>> values = std::make_shared<std::vector<PVariable>>();
                values->push_back(blindSpeed);
                std::string eventSource = "device-" + std::to_string(_peerID);
                std::string address(_serialNumber + ":" + std::to_string(1));
                raiseEvent(eventSource, _peerID, 1, valueKeys, values);
                raiseRPCEvent(eventSource, _peerID, 1, address, valueKeys, values);
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::updateBlindPosition()
{
    try
    {
        auto channelIterator = valuesCentral.find(1);
        if(channelIterator != valuesCentral.end())
        {
            auto parameterIterator = channelIterator->second.find("CURRENT_POSITION");
            if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
            {
                BaseLib::PVariable blindPosition = std::make_shared<BaseLib::Variable>(_blindPosition / 100);

                std::vector<uint8_t> parameterData;
                parameterIterator->second.rpcParameter->convertToPacket(blindPosition, parameterData);
                parameterIterator->second.setBinaryData(parameterData);
                if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
                else saveParameter(0, ParameterGroup::Type::Enum::variables, 1, "CURRENT_POSITION", parameterData);
                if(_bl->debugLevel >= 4) GD::out.printInfo("Info: CURRENT_POSITION of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(1) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

                std::shared_ptr<std::vector<std::string>> valueKeys = std::make_shared<std::vector<std::string>>();
                valueKeys->push_back("CURRENT_POSITION");
                std::shared_ptr<std::vector<PVariable>> values = std::make_shared<std::vector<PVariable>>();
                values->push_back(blindPosition);
				std::string eventSource = "device-" + std::to_string(_peerID);
				std::string address(_serialNumber + ":" + std::to_string(1));
                raiseEvent(eventSource, _peerID, 1, valueKeys, values);
                raiseRPCEvent(eventSource, _peerID, 1, address, valueKeys, values);
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::homegearStarted()
{
	try
	{
		Peer::homegearStarted();
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanPeer::homegearShuttingDown()
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
}

std::string EnOceanPeer::handleCliCommand(std::string command)
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
    return "Error executing command. See log file for more details.\n";
}

std::string EnOceanPeer::printConfig()
{
	try
	{
		std::ostringstream stringStream;
		stringStream << "MASTER" << std::endl;
		stringStream << "{" << std::endl;
		for(std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator i = configCentral.begin(); i != configCentral.end(); ++i)
		{
			stringStream << "\t" << "Channel: " << std::dec << i->first << std::endl;
			stringStream << "\t{" << std::endl;
			for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = i->second.begin(); j != i->second.end(); ++j)
			{
				stringStream << "\t\t[" << j->first << "]: ";
				if(!j->second.rpcParameter) stringStream << "(No RPC parameter) ";
				std::vector<uint8_t> parameterData = j->second.getBinaryData();
				for(std::vector<uint8_t>::const_iterator k = parameterData.begin(); k != parameterData.end(); ++k)
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
		for(std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator i = valuesCentral.begin(); i != valuesCentral.end(); ++i)
		{
			stringStream << "\t" << "Channel: " << std::dec << i->first << std::endl;
			stringStream << "\t{" << std::endl;
			for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = i->second.begin(); j != i->second.end(); ++j)
			{
				stringStream << "\t\t[" << j->first << "]: ";
				if(!j->second.rpcParameter) stringStream << "(No RPC parameter) ";
				std::vector<uint8_t> parameterData = j->second.getBinaryData();
				for(std::vector<uint8_t>::const_iterator k = parameterData.begin(); k != parameterData.end(); ++k)
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
    return "";
}

std::string EnOceanPeer::getPhysicalInterfaceId()
{
	if(_physicalInterfaceId.empty()) setPhysicalInterfaceId(GD::interfaces->getDefaultInterface()->getID());
	return _physicalInterfaceId;
}

void EnOceanPeer::setPhysicalInterfaceId(std::string id)
{
	if(id.empty() || GD::interfaces->hasInterface(id))
	{
		_physicalInterfaceId = id;
		setPhysicalInterface(id.empty() ? GD::interfaces->getDefaultInterface() : GD::interfaces->getInterface(_physicalInterfaceId));
		saveVariable(19, _physicalInterfaceId);
	}
	else
	{
		setPhysicalInterface(GD::interfaces->getDefaultInterface());
		saveVariable(19, _physicalInterfaceId);
	}
}

void EnOceanPeer::setPhysicalInterface(std::shared_ptr<IEnOceanInterface> interface)
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
}

void EnOceanPeer::setBestInterface()
{
    try
    {
        if(_physicalInterface->isOpen()) return; //Only change interface, when the current one is unavailable. If it is available it is switched in onPacketReceived of myCentral.
        std::string settingName = "roaming";
        auto roamingSetting = GD::family->getFamilySetting(settingName);
        if(roamingSetting && !roamingSetting->integerValue) return;
        std::shared_ptr<IEnOceanInterface> bestInterface = GD::interfaces->getDefaultInterface()->isOpen() ? GD::interfaces->getDefaultInterface() : std::shared_ptr<IEnOceanInterface>();
        auto interfaces = GD::interfaces->getInterfaces();
        for(auto& interface : interfaces)
        {
            if(interface->getBaseAddress() != _physicalInterface->getBaseAddress() || !interface->isOpen()) continue;
            if(!bestInterface)
            {
                bestInterface = interface;
                continue;
            }
            if(interface->getRssi(_address, isWildcardPeer()) > bestInterface->getRssi(_address, isWildcardPeer())) bestInterface = interface;
        }
        if(bestInterface) setPhysicalInterfaceId(bestInterface->getID());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows)
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
				if(!_physicalInterfaceId.empty() && GD::interfaces->hasInterface(_physicalInterfaceId)) setPhysicalInterface(GD::interfaces->getInterface(_physicalInterfaceId));
				break;
			case 20:
				_rollingCode = row->second.at(3)->intValue;
				break;
			case 21:
				_aesKey.clear();
				_aesKey.insert(_aesKey.end(), row->second.at(5)->binaryValue->begin(), row->second.at(5)->binaryValue->end());
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
		if(!_physicalInterface) _physicalInterface = GD::interfaces->getDefaultInterface();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::saveVariables()
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
}

bool EnOceanPeer::load(BaseLib::Systems::ICentral* central)
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
			std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator.second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator.second.end() && parameterIterator->second.rpcParameter)
			{
				if(channelIterator.first == 0) _globalRfChannel = true;
				std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
				setRfChannel(channelIterator.first, parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue);
			}
		}

		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = configCentral.find(0);
		if(channelIterator != configCentral.end())
		{
			std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("ENCRYPTION");
			if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
			{
				std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
				_forceEncryption = parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->booleanValue;
			}
		}

		if(_deviceType == 0x01A53807)
		{
			channelIterator = valuesCentral.find(1);
			if(channelIterator != configCentral.end())
			{
				auto parameterIterator = channelIterator->second.find("CURRENT_POSITION");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
					_blindPosition = parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue * 100;
				}
			}
		}

		return true;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void EnOceanPeer::initializeCentralConfig()
{
	try
	{
		Peer::initializeCentralConfig();

		for(auto channelIterator : valuesCentral)
		{
			std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator.second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator.second.end() && parameterIterator->second.rpcParameter)
			{
				if(channelIterator.first == 0) _globalRfChannel = true;
				std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
				setRfChannel(channelIterator.first, parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue);
			}
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanPeer::setRssiDevice(uint8_t rssi)
{
	try
	{
		if(_disposing || rssi == 0) return;
		uint32_t time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		if(time - _lastRssiDevice > 10)
		{
			_lastRssiDevice = time;

			std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(0);
			if(channelIterator == valuesCentral.end()) return;
			std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RSSI_DEVICE");
			if(parameterIterator == channelIterator->second.end()) return;

			BaseLib::Systems::RpcConfigurationParameter& parameter = parameterIterator->second;
			std::vector<uint8_t> parameterData{ rssi };
			parameter.setBinaryData(parameterData);

			std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>({std::string("RSSI_DEVICE")}));
			std::shared_ptr<std::vector<PVariable>> rpcValues(new std::vector<PVariable>());
			rpcValues->push_back(parameter.rpcParameter->convertFromPacket(parameterData));

			std::string eventSource = "device-" + std::to_string(_peerID);
			std::string address = _serialNumber + ":0";
			raiseEvent(eventSource, _peerID, 0, valueKeys, rpcValues);
			raiseRPCEvent(eventSource, _peerID, 0, address, valueKeys, rpcValues);
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

bool EnOceanPeer::hasRfChannel(int32_t channel)
{
    try
    {
        auto channelIterator = valuesCentral.find(channel);
        if(channelIterator != valuesCentral.end())
        {
            auto parameterIterator = channelIterator->second.find("RF_CHANNEL");
            if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter) return true;
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

int32_t EnOceanPeer::getRfChannel(int32_t channel)
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
    return 0;
}

std::vector<int32_t> EnOceanPeer::getRfChannels()
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
    return std::vector<int32_t>();
}

void EnOceanPeer::setRfChannel(int32_t channel, int32_t rfChannel)
{
	try
	{
		if(rfChannel < 0 || rfChannel > 127) return;
		BaseLib::PVariable value(new BaseLib::Variable(rfChannel));
		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
		if(channelIterator != valuesCentral.end())
		{
			std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
			{
				std::vector<uint8_t> parameterData;
				parameterIterator->second.rpcParameter->convertToPacket(value, parameterData);
				parameterIterator->second.setBinaryData(parameterData);
				if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
				else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, "RF_CHANNEL", parameterData);

				{
					std::lock_guard<std::mutex> rfChannelsGuard(_rfChannelsMutex);
					_rfChannels[channel] = parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue;
				}

				if(_bl->debugLevel >= 4 && !GD::bl->booting) GD::out.printInfo("Info: RF_CHANNEL of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");
			}
			else GD::out.printError("Error: Parameter RF_CHANNEL not found.");
		}
		else GD::out.printError("Error: Parameter RF_CHANNEL not found.");
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::getValuesFromPacket(PEnOceanPacket packet, std::vector<FrameValues>& frameValues)
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
			std::vector<uint8_t> erpPacket = packet->getData();
			if(erpPacket.empty()) break;
			uint32_t erpPacketBitSize = erpPacket.size() * 8;
			int32_t channelIndex = frame->channelIndex;
			int32_t channel = -1;
			if(channelIndex >= 0 && channelIndex < (signed)erpPacket.size()) channel = erpPacket.at(channelIndex);
			if(channel > -1 && frame->channelSize < 8.0) channel &= (0xFF >> (8 - std::lround(frame->channelSize)));
			channel += frame->channelIndexOffset;
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
}

void EnOceanPeer::packetReceived(PEnOceanPacket& packet)
{
	try
	{
		if(_disposing || !packet || !_rpcDevice) return;
		if(!isWildcardPeer() && packet->senderAddress() != _address) return;
		else if(isWildcardPeer() && (signed)(packet->senderAddress() & 0xFFFFFF80) != _address) return;
		std::shared_ptr<EnOceanCentral> central = std::dynamic_pointer_cast<EnOceanCentral>(getCentral());
		if(!central) return;
		setLastPacketReceived();
        if(_lastPacket && BaseLib::HelperFunctions::getTime() - _lastPacket->getTimeReceived() < 1000 && _lastPacket->getBinary() == packet->getBinary()) return;
		setRssiDevice(packet->getRssi() * -1);
		serviceMessages->endUnreach();

		if(packet->getRorg() == 0x35) // Encryption teach-in
		{
			std::vector<uint8_t> data = packet->getData();
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
			std::vector<uint8_t> data = packet->getData();
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

                GD::out.printInfo("Decrypted packet: " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));

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

		if(frameValues.empty())
		{
            PRpcRequest rpcRequest;
            {
                std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
                auto rpcRequestIterator = _rpcRequests.find("ANY");
                if(rpcRequestIterator != _rpcRequests.end()) rpcRequest = rpcRequestIterator->second;
            }
			if(rpcRequest)
			{
				std::unique_lock<std::mutex> conditionVariableGuard(rpcRequest->conditionVariableMutex);
				conditionVariableGuard.unlock();
				if(rpcRequest->wait) rpcRequest->conditionVariable.notify_all();
				else
                {
                    std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
                    _rpcRequests.erase("ANY");
                }
			}
		}

		//Loop through all matching frames
		for(std::vector<FrameValues>::iterator a = frameValues.begin(); a != frameValues.end(); ++a)
		{
			PPacket frame;
			if(!a->frameID.empty()) frame = _rpcDevice->packetsById.at(a->frameID);
			if(!frame) continue;

			{
                PRpcRequest rpcRequest;
                {
                    std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
                    auto rpcRequestIterator = _rpcRequests.find(a->frameID);
                    if(rpcRequestIterator != _rpcRequests.end()) rpcRequest = rpcRequestIterator->second;
                }
				if(rpcRequest)
				{
					std::unique_lock<std::mutex> conditionVariableGuard(rpcRequest->conditionVariableMutex);
					conditionVariableGuard.unlock();
					if(rpcRequest->wait) rpcRequest->conditionVariable.notify_all();
					else
                    {
                        std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
                        _rpcRequests.erase(a->frameID);
                    }
				}
				else
				{
                    {
                        std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
                        auto rpcRequestIterator = _rpcRequests.find("ANY");
                        if(rpcRequestIterator != _rpcRequests.end()) rpcRequest = rpcRequestIterator->second;
                    }
					if(rpcRequest)
					{
						std::unique_lock<std::mutex> conditionVariableGuard(rpcRequest->conditionVariableMutex);
						conditionVariableGuard.unlock();
						if(rpcRequest->wait) rpcRequest->conditionVariable.notify_all();
						else
                        {
                            std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
                            _rpcRequests.erase("ANY");
                        }
					}
				}
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

					BaseLib::Systems::RpcConfigurationParameter& parameter = valuesCentral[*j][i->first];
					parameter.setBinaryData(i->second.value);
					if(parameter.databaseId > 0) saveParameter(parameter.databaseId, i->second.value);
					else saveParameter(0, ParameterGroup::Type::Enum::variables, *j, i->first, i->second.value);
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
								serviceMessages->set(i->first, parameter.rpcParameter->convertFromPacket(i->second.value, true)->booleanValue);
							}
						}

						valueKeys[*j]->push_back(i->first);
						rpcValues[*j]->push_back(parameter.rpcParameter->convertFromPacket(i->second.value, true));
					}
				}
			}

            if(!frame->responses.empty())
            {
                if(getRfChannel(0) == -1) GD::out.printError("Error: RF_CHANNEL is not set. Please pair the device.");
                else
                {
                    PPacket responseFrame;
                    for(auto& response : frame->responses)
                    {
                        if(response->conditionOperator == BaseLib::DeviceDescription::DevicePacketResponse::ConditionOperator::Enum::none)
                        {
                            auto packetIterator = _rpcDevice->packetsById.find(response->responseId);
                            if(packetIterator == _rpcDevice->packetsById.end())
                            {
                                GD::out.printError("Error: Response packet with ID \"" + response->responseId + "\" not found.");
                                continue;
                            }
                            responseFrame = packetIterator->second;
                            break;
                        }
                        else
                        {
                            if(response->conditionParameterId.empty() || response->conditionChannel == -1)
                            {
                                GD::out.printError("Error: conditionParameterId or conditionChannel are unset.");
                                continue;
                            }

                            auto channelIterator = valuesCentral.find(response->conditionChannel);
                            if(channelIterator != valuesCentral.end())
                            {
                                std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(response->conditionParameterId);
                                if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
                                {
                                    std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
                                    if(response->checkCondition(parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue))
                                    {
                                        auto packetIterator = _rpcDevice->packetsById.find(response->responseId);
                                        if(packetIterator == _rpcDevice->packetsById.end())
                                        {
                                            GD::out.printError("Error: Response packet with ID \"" + response->responseId + "\" not found.");
                                            continue;
                                        }
                                        responseFrame = packetIterator->second;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if(responseFrame)
                    {
                        if(responseFrame->subtype == -1) responseFrame->subtype = 1;
                        PEnOceanPacket packet(new EnOceanPacket((EnOceanPacket::Type)responseFrame->subtype, (uint8_t)responseFrame->type, _physicalInterface->getBaseAddress() | getRfChannel(0), _address));

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
                            int32_t channel = (*i)->parameterChannel;
                            if(channel == -1) channel = responseFrame->channel;
                            for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = valuesCentral[channel].begin(); j != valuesCentral[channel].end(); ++j)
                            {
                                //Only compare id. Till now looking for value_id was not necessary.
                                if((*i)->parameterId == j->second.rpcParameter->physical->groupId)
                                {
                                    std::vector<uint8_t> data = j->second.getBinaryData();
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
			for(std::map<uint32_t, std::shared_ptr<std::vector<std::string>>>::iterator j = valueKeys.begin(); j != valueKeys.end(); ++j)
			{
				if(j->second->empty()) continue;
                std::string eventSource = "device-" + std::to_string(_peerID);
                std::string address(_serialNumber + ":" + std::to_string(j->first));
                raiseEvent(eventSource, _peerID, j->first, j->second, rpcValues.at(j->first));
                raiseRPCEvent(eventSource, _peerID, j->first, address, j->second, rpcValues.at(j->first));
			}
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

PParameterGroup EnOceanPeer::getParameterSet(int32_t channel, ParameterGroup::Type::Enum type)
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
	return PParameterGroup();
}

bool EnOceanPeer::getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
	try
	{
		if(channel == 1)
		{
			if(parameter->id == "PEER_ID")
			{
				std::vector<uint8_t> parameterData;
				parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), parameterData);
				valuesCentral[channel][parameter->id].setBinaryData(parameterData);
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

bool EnOceanPeer::getParamsetHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
	try
	{
		if(channel == 1)
		{
			if(parameter->id == "PEER_ID")
			{
				std::vector<uint8_t> parameterData;
				parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), parameterData);
				valuesCentral[channel][parameter->id].setBinaryData(parameterData);
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

PVariable EnOceanPeer::getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields)
{
	try
	{
		PVariable info(Peer::getDeviceInfo(clientInfo, fields));
		if(info->errorStruct) return info;

		if(fields.empty() || fields.find("INTERFACE") != fields.end()) info->structValue->insert(StructElement("INTERFACE", PVariable(new Variable(_physicalInterface->getID()))));

		return info;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return PVariable();
}

PVariable EnOceanPeer::putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing)
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

        auto central = getCentral();
        if(!central) return Variable::createError(-32500, "Could not get central.");

		if(type == ParameterGroup::Type::Enum::config)
		{
			bool parameterChanged = false;
			for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
			{
				if(i->first.empty() || !i->second) continue;
				if(configCentral[channel].find(i->first) == configCentral[channel].end()) continue;
				BaseLib::Systems::RpcConfigurationParameter& parameter = configCentral[channel][i->first];
				if(!parameter.rpcParameter) continue;
				if(parameter.rpcParameter->password && i->second->stringValue.empty()) continue; //Don't safe password if empty
				std::vector<uint8_t> parameterData;
				parameter.rpcParameter->convertToPacket(i->second, parameterData);
				parameter.setBinaryData(parameterData);
				if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
				else saveParameter(0, ParameterGroup::Type::Enum::config, channel, i->first, parameterData);

				if(channel == 0 && i->first == "ENCRYPTION" && i->second->booleanValue != _forceEncryption) _forceEncryption = i->second->booleanValue;

				parameterChanged = true;
				GD::out.printInfo("Info: Parameter " + i->first + " of peer " + std::to_string(_peerID) + " and channel " + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");
			}

			if(parameterChanged) raiseRPCUpdateDevice(_peerID, channel, _serialNumber + ":" + std::to_string(channel), 0);
		}
		else if(type == ParameterGroup::Type::Enum::variables)
		{
			for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
			{
				if(i->first.empty() || !i->second) continue;

                if(checkAcls && !clientInfo->acls->checkVariableWriteAccess(central->getPeer(_peerID), channel, i->first)) continue;

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
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanPeer::setInterface(BaseLib::PRpcClientInfo clientInfo, std::string interfaceId)
{
	try
	{
		if(!interfaceId.empty() && !GD::interfaces->hasInterface(interfaceId))
		{
			return Variable::createError(-5, "Unknown physical interface.");
		}
		setPhysicalInterfaceId(interfaceId);
		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

void EnOceanPeer::sendPacket(PEnOceanPacket packet, std::string responseId, int32_t delay, bool wait)
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
				std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RESENDS_WHEN_NO_ACK");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
					resends = parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue;
					if(resends < 0) resends = 0;
					else if(resends > 12) resends = 12;
				}
				parameterIterator = channelIterator->second.find("RESEND_TIMEOUT");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
					resendTimeout = parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue;
					if(resendTimeout < 10) resendTimeout = 10;
					else if(resendTimeout > 10000) resendTimeout = 10000;
				}
			}
            setBestInterface();
			if(resends == 0) _physicalInterface->sendPacket(packet);
			else
			{
				PRpcRequest rpcRequest = std::make_shared<RpcRequest>();
				rpcRequest->responseId = responseId;
				rpcRequest->wait = wait;
				if(!wait)
				{
					rpcRequest->packet = packet;
					rpcRequest->resends = 1;
					rpcRequest->maxResends = resends; //Also used as an identifier for asynchronous requests
					rpcRequest->resendTimeout = resendTimeout;
					rpcRequest->lastResend = BaseLib::HelperFunctions::getTime();
				}
				{
					std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
					auto requestIterator = _rpcRequests.find(rpcRequest->responseId);
					if(requestIterator != _rpcRequests.end())
					{
						requestIterator->second->abort = true;
						_rpcRequests.erase(requestIterator);
					}
					_rpcRequests.emplace(rpcRequest->responseId, rpcRequest);
				}
				if(wait)
				{
					std::unique_lock<std::mutex> conditionVariableGuard(rpcRequest->conditionVariableMutex);
					for(int32_t i = 0; i < resends + 1; i++)
					{
						_physicalInterface->sendPacket(packet);
						if(rpcRequest->conditionVariable.wait_for(conditionVariableGuard, std::chrono::milliseconds(resendTimeout)) == std::cv_status::no_timeout || rpcRequest->abort) break;
						if(i == resends) serviceMessages->setUnreach(true, false);
					}
                    conditionVariableGuard.unlock();
					{
						std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
						_rpcRequests.erase(rpcRequest->responseId);
					}
				}
				else _physicalInterface->sendPacket(packet);
			}
		}
		else _physicalInterface->sendPacket(packet);
		if(delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

PVariable EnOceanPeer::setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(!value) return Variable::createError(-32500, "value is nullptr.");
		Peer::setValue(clientInfo, channel, valueKey, value, wait); //Ignore result, otherwise setHomegerValue might not be executed
		std::shared_ptr<EnOceanCentral> central = std::dynamic_pointer_cast<EnOceanCentral>(getCentral());
		if(!central) return Variable::createError(-32500, "Could not get central object.");;
		if(valueKey.empty()) return Variable::createError(-5, "Value key is empty.");
		if(channel == 0 && serviceMessages->set(valueKey, value->booleanValue)) return PVariable(new Variable(VariableType::tVoid));
		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
		if(channelIterator == valuesCentral.end()) return Variable::createError(-2, "Unknown channel.");
		std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(valueKey);
		if(parameterIterator == valuesCentral[channel].end()) return Variable::createError(-5, "Unknown parameter.");
		PParameter rpcParameter = parameterIterator->second.rpcParameter;
		if(!rpcParameter) return Variable::createError(-5, "Unknown parameter.");
		BaseLib::Systems::RpcConfigurationParameter& parameter = valuesCentral[channel][valueKey];
		std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>());
		std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable>());

		if(rpcParameter->physical->operationType == IPhysical::OperationType::Enum::store)
		{
			std::vector<uint8_t> parameterData;
			rpcParameter->convertToPacket(value, parameterData);
			parameter.setBinaryData(parameterData);
			if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
			else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);

            if(rpcParameter->readable)
            {
                valueKeys->push_back(valueKey);
                values->push_back(rpcParameter->convertFromPacket(parameterData, true));
            }

			if(!valueKeys->empty())
			{
				std::string address(_serialNumber + ":" + std::to_string(channel));
                std::string eventSource = clientInfo ? clientInfo->initInterfaceId : "device-" + std::to_string(_peerID);
				raiseEvent(eventSource, _peerID, channel, valueKeys, values);
				raiseRPCEvent(eventSource, _peerID, channel, address, valueKeys, values);
			}
			return std::make_shared<Variable>(VariableType::tVoid);
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

		std::vector<uint8_t> parameterData;
		rpcParameter->convertToPacket(value, parameterData);
		parameter.setBinaryData(parameterData);
		if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
		else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);
		if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + valueKey + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

        if(rpcParameter->readable)
        {
            valueKeys->push_back(valueKey);
            values->push_back(rpcParameter->convertFromPacket(parameterData, true));
        }

		if(valueKey == "PAIRING")
		{
			if(value->integerValue == -1 && getRfChannel(_globalRfChannel ? 0 : channel) != -1) value->integerValue = getRfChannel(_globalRfChannel ? 0 : channel);
			if(value->integerValue == -1) value->integerValue = central->getFreeRfChannel(_physicalInterfaceId);
			if(value->integerValue == -1)
			{
				GD::out.printError("Error: There is no free channel to pair a new device. You need to either reuse a channel or install another communication module.");
				return Variable::createError(-7, "There is no free channel to pair a new device. You need to either reuse a channel or install another communication module.");
			}

			std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(_globalRfChannel ? 0 : channel);
			if(channelIterator != valuesCentral.end())
			{
				std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RF_CHANNEL");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					parameterIterator->second.rpcParameter->convertToPacket(value, parameterData);
					parameterIterator->second.setBinaryData(parameterData);
					if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
					else saveParameter(0, ParameterGroup::Type::Enum::variables, _globalRfChannel ? 0 : channel, "RF_CHANNEL", parameterData);
					setRfChannel(_globalRfChannel ? 0 : channel, parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue);
					if(_bl->debugLevel >= 4) GD::out.printInfo("Info: RF_CHANNEL of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");
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
						channelIterator = valuesCentral.find(1);
						if(channelIterator != valuesCentral.end())
						{
							std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(valueKey == "UP" ? "DOWN" : "UP");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								BaseLib::PVariable falseValue = std::make_shared<BaseLib::Variable>(false);
								parameterIterator->second.rpcParameter->convertToPacket(falseValue, parameterData);
								parameterIterator->second.setBinaryData(parameterData);
								if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
								else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey == "UP" ? "DOWN" : "UP", parameterData);
								valueKeys->push_back(valueKey == "UP" ? "DOWN" : "UP");
								values->push_back(falseValue);
							}

							PEnOceanPacket packet(new EnOceanPacket((EnOceanPacket::Type)1, (uint8_t)0xF6, _physicalInterface->getBaseAddress() | getRfChannel(_globalRfChannel ? 0 : channel), _address));
							std::vector<uint8_t> data{ 0 };
							packet->setPosition(8, 8, data);
							sendPacket(packet, "ANY", 0, wait);
						}

						channelIterator = configCentral.find(0);
						if(channelIterator != configCentral.end())
						{
							std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("SIGNAL_DURATION");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								_blindCurrentTargetPosition = valueKey == "DOWN" ? 10000 : 0;
								int32_t positionDifference = _blindCurrentTargetPosition - _blindPosition;
                                parameterData = parameterIterator->second.getBinaryData();
                                _blindSignalDuration = parameterIterator->second.rpcParameter->convertFromPacket(parameterData)->integerValue * 1000;
                                if(positionDifference != 0) _blindCurrentSignalDuration = _blindSignalDuration / (10000 / std::abs(positionDifference));
                                else _blindCurrentSignalDuration = 0;
                                _blindStateResetTime = BaseLib::HelperFunctions::getTime() + _blindCurrentSignalDuration + (_blindCurrentTargetPosition == 0 || _blindCurrentTargetPosition == 10000 ? 5000 : 0);
                                _lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
                                _blindUp = valueKey == "UP";
                                updateBlindSpeed();
							}
						}
					}
					else
					{
						// Set the opposite value to "false", too
						if(_blindStateResetTime != -1)
                        {
                            _blindStateResetTime = -1;
                            updateBlindPosition();
                        }
						channelIterator = valuesCentral.find(1);
						if(channelIterator != valuesCentral.end())
						{
							std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(valueKey == "UP" ? "DOWN" : "UP");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								BaseLib::PVariable falseValue = std::make_shared<BaseLib::Variable>(false);
								parameterIterator->second.rpcParameter->convertToPacket(falseValue, parameterData);
								parameterIterator->second.setBinaryData(parameterData);
								if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
								else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey == "UP" ? "DOWN" : "UP", parameterData);
								valueKeys->push_back(valueKey == "UP" ? "DOWN" : "UP");
								values->push_back(falseValue);
							}
						}
					}
				}
				else if(valueKey == "LEVEL")
				{
					if(value->integerValue > 10) value->integerValue = 10;
					else if(value->integerValue < 0) value->integerValue = 0;

					int32_t newPosition = value->integerValue * 1000;
					if(newPosition != _blindPosition)
					{
						int32_t positionDifference = newPosition - _blindPosition; //Can't be 0
                        setValue(clientInfo, channel, positionDifference > 0 ? "UP" : "DOWN", std::make_shared<BaseLib::Variable>(false), false);

                        channelIterator = configCentral.find(0);
                        if(channelIterator != configCentral.end())
                        {
                            auto parameterIterator2 = channelIterator->second.find("SIGNAL_DURATION");
                            if(parameterIterator2 != channelIterator->second.end() && parameterIterator2->second.rpcParameter)
                            {
                                parameterData = parameterIterator2->second.getBinaryData();
                                _blindSignalDuration = parameterIterator2->second.rpcParameter->convertFromPacket(parameterData)->integerValue * 1000;
                                _blindCurrentTargetPosition = _blindPosition + positionDifference;
                                _blindCurrentSignalDuration = _blindSignalDuration / (10000 / std::abs(positionDifference));
                                _blindStateResetTime = BaseLib::HelperFunctions::getTime() + _blindCurrentSignalDuration + (newPosition == 0 || newPosition == 10000 ? 5000 : 0);
                                _lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
                                _blindUp = positionDifference < 0;
                                updateBlindSpeed();

                                PEnOceanPacket packet(new EnOceanPacket((EnOceanPacket::Type)1, (uint8_t)0xF6, _physicalInterface->getBaseAddress() | getRfChannel(_globalRfChannel ? 0 : channel), _address));
                                std::vector<uint8_t> data{ _blindUp ? (uint8_t)0x30 : (uint8_t)0x10 };
                                packet->setPosition(8, 8, data);
                                sendPacket(packet, "ANY", 0, wait);

                                channelIterator = valuesCentral.find(1);
                                if(channelIterator != valuesCentral.end())
                                {
                                    parameterIterator2 = channelIterator->second.find(_blindUp ? "UP" : "DOWN");
                                    if(parameterIterator2 != channelIterator->second.end() && parameterIterator2->second.rpcParameter)
                                    {
                                        BaseLib::PVariable trueValue = std::make_shared<BaseLib::Variable>(true);
                                        parameterIterator2->second.rpcParameter->convertToPacket(trueValue, parameterData);
                                        parameterIterator2->second.setBinaryData(parameterData);
                                        if(parameterIterator2->second.databaseId > 0) saveParameter(parameterIterator2->second.databaseId, parameterData);
                                        else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, _blindUp ? "UP" : "DOWN", parameterData);
                                        valueKeys->push_back(_blindUp ? "UP" : "DOWN");
                                        values->push_back(trueValue);
                                    }
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
			PEnOceanPacket packet(new EnOceanPacket((EnOceanPacket::Type)frame->subtype, (uint8_t)frame->type, _physicalInterface->getBaseAddress() | getRfChannel(_globalRfChannel ? 0 : channel), _address));

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
					std::vector<uint8_t> data = valuesCentral[channel][valueKey].getBinaryData();
					packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
				}
				//Search for all other parameters
				else
				{
					bool paramFound = false;
                    int32_t currentChannel = (*i)->parameterChannel;
                    if(currentChannel == -1) currentChannel = channel;
					for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = valuesCentral[currentChannel].begin(); j != valuesCentral[currentChannel].end(); ++j)
					{
						//Only compare id. Till now looking for value_id was not necessary.
						if((*i)->parameterId == j->second.rpcParameter->physical->groupId)
						{
							std::vector<uint8_t> data = j->second.getBinaryData();
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
					std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator resetParameterIterator = channelIterator->second.find(*j);
					if(resetParameterIterator == channelIterator->second.end()) continue;
					PVariable logicalDefaultValue = resetParameterIterator->second.rpcParameter->logical->getDefaultValue();
					std::vector<uint8_t> defaultValue;
					resetParameterIterator->second.rpcParameter->convertToPacket(logicalDefaultValue, defaultValue);
					if(!resetParameterIterator->second.equals(defaultValue))
					{
						resetParameterIterator->second.setBinaryData(defaultValue);
						if(resetParameterIterator->second.databaseId > 0) saveParameter(resetParameterIterator->second.databaseId, defaultValue);
						else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, *j, defaultValue);
						GD::out.printInfo( "Info: Parameter \"" + *j + "\" was reset to " + BaseLib::HelperFunctions::getHexString(defaultValue) + ". Peer: " + std::to_string(_peerID) + " Serial number: " + _serialNumber + " Frame: " + frame->id);
						if(rpcParameter->readable)
						{
							valueKeys->push_back(*j);
							values->push_back(logicalDefaultValue);
						}
					}
				}
			}

			sendPacket(packet, setRequest->responseId, setRequest->delay, wait);
		}

		if(!valueKeys->empty())
		{
			std::string address(_serialNumber + ":" + std::to_string(channel));
            std::string eventSource = clientInfo ? clientInfo->initInterfaceId : "device-" + std::to_string(_peerID);
			raiseEvent(eventSource, _peerID, channel, valueKeys, values);
			raiseRPCEvent(eventSource, _peerID, channel, address, valueKeys, values);
		}

		return std::make_shared<Variable>(VariableType::tVoid);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error. See error log for more details.");
}

}