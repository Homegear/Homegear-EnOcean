/* Copyright 2013-2019 Homegear GmbH */

#include "EnOceanPeer.h"

#include "GD.h"
#include "EnOceanPacket.h"
#include "EnOceanCentral.h"
#include "EnOceanPackets.h"

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
        _blindTransitionTime = -1;
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
                    _physicalInterface->sendEnoceanPacket(request.second->packet);
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
			if(_blindUp) _blindPosition -= (BaseLib::HelperFunctions::getTime() - _lastBlindPositionUpdate) * 10000 / _blindTransitionTime;
			else _blindPosition += (BaseLib::HelperFunctions::getTime() - _lastBlindPositionUpdate) * 10000 / _blindTransitionTime;
			_lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
			if(_blindPosition < 0) _blindPosition = 0;
			else if(_blindPosition > 10000) _blindPosition = 10000;
			bool updatePosition = false;
			if(BaseLib::HelperFunctions::getTime() >= _blindStateResetTime)
			{
                _blindStateResetTime = -1;
				if(_deviceType == 0x01A53807) setValue(BaseLib::PRpcClientInfo(), 1, _blindUp ? "UP" : "DOWN", std::make_shared<BaseLib::Variable>(false), false);
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
                BaseLib::PVariable blindSpeed = std::make_shared<BaseLib::Variable>(100.0 / (double)(_blindTransitionTime / 1000));
                if(_blindUp) blindSpeed->floatValue *= -1.0;

                std::vector<uint8_t> parameterData;
                parameterIterator->second.rpcParameter->convertToPacket(blindSpeed, parameterIterator->second.mainRole(), parameterData);
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
                parameterIterator->second.rpcParameter->convertToPacket(blindPosition, parameterIterator->second.mainRole(), parameterData);
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

uint32_t EnOceanPeer::getGatewayAddress()
{
    return _gatewayAddress;
}

void EnOceanPeer::setGatewayAddress(uint32_t value)
{
    _gatewayAddress = value;
    saveVariable(26, (int32_t)_gatewayAddress);
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
            case 12:
                unserializePeers(*row->second.at(5)->binaryValue);
                break;
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
            case 26:
                _gatewayAddress = row->second.at(3)->intValue;
                break;
            case 27:
			    loadUpdatedParameters(*row->second.at(5)->binaryValue);
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

void EnOceanPeer::loadUpdatedParameters(const std::vector<char>& encodedData)
{
    {
        std::lock_guard<std::mutex> updatedParametersGuard(_updatedParametersMutex);
        BaseLib::Rpc::RpcDecoder rpcDecoder;
        auto updatedParameters = rpcDecoder.decodeResponse(encodedData);
        for(auto& element : *updatedParameters->structValue)
        {
            if(element.second->type != BaseLib::VariableType::tBinary) continue;
            _updatedParameters.emplace(BaseLib::Math::getUnsignedNumber(element.first), element.second->binaryValue);
        }
        if(!_updatedParameters.empty()) _remoteManagementQueueSetDeviceConfiguration = true;
    }
}

void EnOceanPeer::saveVariables()
{
	try
	{
		if(_peerID == 0) return;
		Peer::saveVariables();
        savePeers(); //12
		saveVariable(19, _physicalInterfaceId);
		saveVariable(20, _rollingCode);
		saveVariable(21, _aesKey);
		saveVariable(22, _encryptionType);
		saveVariable(23, _cmacSize);
		saveVariable(24, (int32_t)_rollingCodeInTx);
		saveVariable(25, _rollingCodeSize);
        saveVariable(26, (int32_t)_gatewayAddress);
        saveUpdatedParameters(); //27
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::saveUpdatedParameters()
{
    std::lock_guard<std::mutex> updatedParametersGuard(_updatedParametersMutex);
    auto updatedParameters = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
    for(auto& element : _updatedParameters)
    {
        updatedParameters->structValue->emplace(std::to_string(element.first), std::make_shared<BaseLib::Variable>(element.second));
    }
    BaseLib::Rpc::RpcEncoder rpcEncoder;
    std::vector<uint8_t> encodedData;
    rpcEncoder.encodeResponse(updatedParameters, encodedData);
    saveVariable(27, encodedData);
}

void EnOceanPeer::savePeers()
{
    try
    {
        std::vector<uint8_t> serializedData;
        serializePeers(serializedData);
        saveVariable(12, serializedData);
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
				setRfChannel(channelIterator.first, parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue);
			}
		}

		std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = configCentral.find(0);
		if(channelIterator != configCentral.end())
		{
			std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("ENCRYPTION");
			if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
			{
				std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
				_forceEncryption = parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->booleanValue;
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
					_blindPosition = parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue * 100;
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
				setRfChannel(channelIterator.first, parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue);
			}
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void EnOceanPeer::addPeer(int32_t channel, std::shared_ptr<BaseLib::Systems::BasicPeer> peer)
{
    try
    {
        if(_rpcDevice->functions.find(channel) == _rpcDevice->functions.end()) return;

        {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            try
            {
                for(auto linkPeer = _peers[channel].begin(); linkPeer != _peers[channel].end(); ++linkPeer)
                {
                    if((*linkPeer)->address == peer->address && (*linkPeer)->channel == peer->channel)
                    {
                        _peers[channel].erase(linkPeer);
                        break;
                    }
                }
                _peers[channel].push_back(peer);
            }
            catch(const std::exception& ex)
            {
                GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }
        savePeers();
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::removePeer(int32_t channel, int32_t address, int32_t remoteChannel)
{
    try
    {
        std::unique_lock<std::mutex> peersGuard(_peersMutex);
        for(auto linkPeer = _peers[channel].begin(); linkPeer != _peers[channel].end(); ++linkPeer)
        {
            if((*linkPeer)->address == address && (*linkPeer)->channel == remoteChannel)
            {
                _peers[channel].erase(linkPeer);
                peersGuard.unlock();
                savePeers();
                return;
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::serializePeers(std::vector<uint8_t>& encodedData)
{
    try
    {
        std::lock_guard<std::mutex> peersGuard(_peersMutex);
        BaseLib::BinaryEncoder encoder;
        encoder.encodeInteger(encodedData, 0);
        encoder.encodeInteger(encodedData, _peers.size());
        for(std::unordered_map<int32_t, std::vector<std::shared_ptr<BaseLib::Systems::BasicPeer>>>::const_iterator i = _peers.begin(); i != _peers.end(); ++i)
        {
            encoder.encodeInteger(encodedData, i->first);
            encoder.encodeInteger(encodedData, i->second.size());
            for(std::vector<std::shared_ptr<BaseLib::Systems::BasicPeer>>::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                if(!*j) continue;
                encoder.encodeBoolean(encodedData, (*j)->isSender);
                encoder.encodeInteger(encodedData, (*j)->id);
                encoder.encodeInteger(encodedData, (*j)->address);
                encoder.encodeInteger(encodedData, (*j)->channel);
                encoder.encodeString(encodedData, (*j)->serialNumber);
                encoder.encodeBoolean(encodedData, (*j)->isVirtual);
                encoder.encodeString(encodedData, (*j)->linkName);
                encoder.encodeString(encodedData, (*j)->linkDescription);
                encoder.encodeInteger(encodedData, (*j)->data.size());
                encodedData.insert(encodedData.end(), (*j)->data.begin(), (*j)->data.end());
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOceanPeer::unserializePeers(const std::vector<char>& serializedData)
{
    try
    {
        std::lock_guard<std::mutex> peersGuard(_peersMutex);
        BaseLib::BinaryDecoder decoder;
        uint32_t position = 0;
        BaseLib::BinaryDecoder::decodeInteger(serializedData, position); //version
        uint32_t peersSize = 0;
        peersSize = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
        for(uint32_t i = 0; i < peersSize; i++)
        {
            uint32_t channel = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
            uint32_t peerCount = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
            for(uint32_t j = 0; j < peerCount; j++)
            {
                std::shared_ptr<BaseLib::Systems::BasicPeer> basicPeer(new BaseLib::Systems::BasicPeer());
                basicPeer->isSender = BaseLib::BinaryDecoder::decodeBoolean(serializedData, position);
                basicPeer->id = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
                basicPeer->address = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
                basicPeer->channel = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
                basicPeer->serialNumber = decoder.decodeString(serializedData, position);
                basicPeer->isVirtual = BaseLib::BinaryDecoder::decodeBoolean(serializedData, position);
                _peers[channel].push_back(basicPeer);
                basicPeer->linkName = decoder.decodeString(serializedData, position);
                basicPeer->linkDescription = decoder.decodeString(serializedData, position);
                uint32_t dataSize = BaseLib::BinaryDecoder::decodeInteger(serializedData, position);
                if(position + dataSize <= serializedData.size()) basicPeer->data.insert(basicPeer->data.end(), serializedData.begin() + position, serializedData.begin() + position + dataSize);
                position += dataSize;
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

uint32_t EnOceanPeer::getLinkCount()
{
    std::lock_guard<std::mutex> peersGuard(_peersMutex);
    uint32_t count = 0;
    for(auto& channel : _peers)
    {
        count += channel.second.size();
    }
    return count;
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

			auto channelIterator = valuesCentral.find(0);
			if(channelIterator == valuesCentral.end()) return;
			auto parameterIterator = channelIterator->second.find("RSSI_DEVICE");
			if(parameterIterator == channelIterator->second.end()) return;

			BaseLib::Systems::RpcConfigurationParameter& parameter = parameterIterator->second;
			std::vector<uint8_t> parameterData{ rssi };
			parameter.setBinaryData(parameterData);

			std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>({std::string("RSSI_DEVICE")}));
			std::shared_ptr<std::vector<PVariable>> rpcValues(new std::vector<PVariable>());
			rpcValues->push_back(parameter.rpcParameter->convertFromPacket(parameterData, parameter.mainRole(), false));

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
		auto channelIterator = valuesCentral.find(channel);
		if(channelIterator != valuesCentral.end())
		{
			auto parameterIterator = channelIterator->second.find("RF_CHANNEL");
			if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
			{
				std::vector<uint8_t> parameterData;
				parameterIterator->second.rpcParameter->convertToPacket(value, parameterIterator->second.mainRole(), parameterData);
				parameterIterator->second.setBinaryData(parameterData);
				if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
				else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, "RF_CHANNEL", parameterData);

				{
					std::lock_guard<std::mutex> rfChannelsGuard(_rfChannelsMutex);
					_rfChannels[channel] = parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue;
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
		auto i = range.first;
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
			if(channel > -1 && frame->channelSize < 8.0) channel &= (0xFFu >> (unsigned)(8u - std::lround(frame->channelSize)));
			channel += frame->channelIndexOffset;
			if(frame->channel > -1) channel = frame->channel;
			if(channel == -1) continue;
			currentFrameValues.frameID = frame->id;
			bool abort = false;

			for(auto j = frame->binaryPayloads.begin(); j != frame->binaryPayloads.end(); ++j)
			{
				std::vector<uint8_t> data;
				if((*j)->bitSize > 0 && (*j)->bitIndex > 0)
				{
					if((*j)->bitIndex >= erpPacketBitSize) continue;
					data = packet->getPosition((*j)->bitIndex, (*j)->bitSize);

					if((*j)->constValueInteger > -1)
					{
						int32_t intValue = 0;
						BaseLib::HelperFunctions::memcpyBigEndian(intValue, data);
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
					BaseLib::HelperFunctions::memcpyBigEndian(data, (*j)->constValueInteger);
				}
				else continue;

                for(auto k = frame->associatedVariables.begin(); k != frame->associatedVariables.end(); ++k)
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
							auto functionIterator = _rpcDevice->functions.find(l);
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
							auto functionIterator = _rpcDevice->functions.find(*l);
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
				GD::out.printError("Error: Encrypted packet received, but Homegear never received the encryption teach-in packets. Please activate \"encryption teach-in\" on your device.");
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

				if(!_forceEncryption) GD::out.printWarning("Warning: Encrypted packet received from peer " + std::to_string(_peerID) + " but unencrypted packet will still be accepted. Please set the configuration parameter \"ENCRYPTION\" to \"true\" to enforce encryption and ignore unencrypted packets.");
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
			GD::out.printError("Error: Unencrypted packet received from peer " + std::to_string(_peerID) + ", but encryption is enforced. Ignoring packet.");
			return;
		}

		if(packet->getRorg() == 0xD0)
        {
		    GD::out.printInfo("Info: Signal packet received from peer " + std::to_string(_peerID));
		    if(_remoteManagementQueueSetDeviceConfiguration)
            {
                GD::out.printInfo("Sending configuration changes.");

                {
                    std::lock_guard<std::mutex> updatedParametersGuard(_updatedParametersMutex);
                    if(setDeviceConfiguration(_updatedParameters))
                    {
                        _updatedParameters.clear();
                    }
                }

                saveUpdatedParameters();
            }
            else serviceMessages->setConfigPending(false);
            if(_remoteManagementQueueGetDeviceConfiguration)
            {
                GD::out.printInfo("Requesting configuration changes.");
                getDeviceConfiguration();
            }
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

                    //{{{ Blinds
                    if((_deviceType & 0xFFFFFFu) == 0xD20502)
                    {
                        if(i->first == "CURRENT_POSITION")
                        {
                            if(parameter.rpcParameter->convertFromPacket(i->second.value, parameter.mainRole(), true)->integerValue == 127) continue;

                            _blindStateResetTime = -1;
                        }
                    }
                    //}}}

					parameter.setBinaryData(i->second.value);
					if(parameter.databaseId > 0) saveParameter(parameter.databaseId, i->second.value);
					else saveParameter(0, ParameterGroup::Type::Enum::variables, *j, i->first, i->second.value);
					if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + i->first + " on channel " + std::to_string(*j) + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber  + " was set to 0x" + BaseLib::HelperFunctions::getHexString(i->second.value) + " (Frame ID: " + a->frameID + ").");

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
								serviceMessages->set(i->first, parameter.rpcParameter->convertFromPacket(i->second.value, parameter.mainRole(), true)->booleanValue);
							}
						}

						valueKeys[*j]->push_back(i->first);
						rpcValues[*j]->push_back(parameter.rpcParameter->convertFromPacket(i->second.value, parameter.mainRole(), true));
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
                                    if(response->checkCondition(parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue))
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

                        _physicalInterface->sendEnoceanPacket(packet);
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
				auto& rpcConfigurationParameter = valuesCentral[channel][parameter->id];
				parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), rpcConfigurationParameter.mainRole(), parameterData);
                rpcConfigurationParameter.setBinaryData(parameterData);
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
                auto& rpcConfigurationParameter = valuesCentral[channel][parameter->id];
				parameter->convertToPacket(std::make_shared<Variable>((int32_t)_peerID), rpcConfigurationParameter.mainRole(), parameterData);
                rpcConfigurationParameter.setBinaryData(parameterData);
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void EnOceanPeer::queueSetDeviceConfiguration(const std::map<uint32_t, std::vector<uint8_t>>& updatedParameters)
{
    try
    {
        if(_rpcDevice->receiveModes & BaseLib::DeviceDescription::HomegearDevice::ReceiveModes::Enum::wakeUp2)
        {
            serviceMessages->setConfigPending(true);
            _remoteManagementQueueSetDeviceConfiguration = true;

            {
                std::lock_guard<std::mutex> updatedParametersGuard(_updatedParametersMutex);
                for(auto& element : updatedParameters)
                {
                    _updatedParameters.erase(element.first);
                    _updatedParameters.emplace(element);
                }
            }

            saveUpdatedParameters();
        }
        else setDeviceConfiguration(updatedParameters);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

bool EnOceanPeer::setDeviceConfiguration(const std::map<uint32_t, std::vector<uint8_t>>& updatedParameters)
{
    try
    {
        remoteManagementUnlock();

        bool result = true;

        auto setDeviceConfiguration = std::make_shared<SetDeviceConfiguration>(_address, updatedParameters);
        setBestInterface();
        auto response = _physicalInterface->sendAndReceivePacket(setDeviceConfiguration, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
        if(!response)
        {
            result = false;
            GD::out.printError("Error: Could not set device configuration on device.");
        }

        remoteManagementLock();

        if(result)
        {
            serviceMessages->setConfigPending(false);
            _remoteManagementQueueSetDeviceConfiguration = false;
        }
        return result;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void EnOceanPeer::queueGetDeviceConfiguration()
{
    try
    {
        if(_rpcDevice->receiveModes & BaseLib::DeviceDescription::HomegearDevice::ReceiveModes::Enum::wakeUp2)
        {
            _remoteManagementQueueGetDeviceConfiguration = true;
        }
        else getDeviceConfiguration();
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

bool EnOceanPeer::getDeviceConfiguration()
{
    try
    {
        uint32_t deviceConfigurationSize = 0xFF;

        if(_rpcDevice->metadata)
        {
            auto metadataIterator = _rpcDevice->metadata->structValue->find("remoteManagementInfo");
            if(metadataIterator != _rpcDevice->metadata->structValue->end() && !metadataIterator->second->arrayValue->empty())
            {
                auto remoteManagementInfo = metadataIterator->second->arrayValue->at(0);
                auto infoIterator = remoteManagementInfo->structValue->find("features");
                if(infoIterator != metadataIterator->second->structValue->end() && !infoIterator->second->arrayValue->empty())
                {
                    auto features = infoIterator->second->arrayValue->at(0);
                    auto featureIterator = features->structValue->find("deviceConfigurationSize");
                    if(featureIterator != features->structValue->end())
                    {
                        deviceConfigurationSize = featureIterator->second->integerValue;
                    }

                    featureIterator = features->structValue->find("getDeviceConfiguration");
                    if(featureIterator != features->structValue->end() && !featureIterator->second->booleanValue)
                    {
                        GD::out.printInfo("Info: Device does not support getDeviceConfiguration.");
                        return true;
                    }
                }
            }
        }

        remoteManagementUnlock();

        auto getDeviceConfiguration = std::make_shared<GetDeviceConfiguration>(_address, 0, deviceConfigurationSize, 0xFF);
        setBestInterface();
        auto response = _physicalInterface->sendAndReceivePacket(getDeviceConfiguration, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::getDeviceConfigurationResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::getDeviceConfigurationResponse}});
        if(!response)
        {
            GD::out.printError("Error: Could not set device configuration on device.");
        }

        remoteManagementLock();

        if(!response) return false;

        auto rawConfig = response->getData();
        std::unordered_map<uint32_t, std::vector<uint8_t>> config;
        uint32_t pos = 32;
        while(pos < rawConfig.size() * 8)
        {
            auto index = BitReaderWriter::getPosition16(rawConfig, pos, 16);
            pos += 16;
            auto length = ((uint32_t)BitReaderWriter::getPosition8(rawConfig, pos, 8)) * 8;
            pos += 8;
            auto data = BitReaderWriter::getPosition(rawConfig, pos, length);
            pos += length;
            config.emplace(index, data);
        }

        bool configChanged = false;
        auto channelIterator = configCentral.find(0);
        if(channelIterator != configCentral.end())
        {
            for(auto& variableIterator : channelIterator->second)
            {
                if(!variableIterator.second.rpcParameter) continue;
                if(variableIterator.second.rpcParameter->physical->type != IPhysical::Type::tInteger || variableIterator.second.rpcParameter->physical->bitSize <= 0) continue;

                auto configIterator = config.find(variableIterator.second.rpcParameter->physical->memoryIndex);
                if(configIterator != config.end() && (variableIterator.second.getBinaryDataSize() != configIterator->second.size() || !std::equal(configIterator->second.begin(), configIterator->second.end(), variableIterator.second.getBinaryData().begin())))
                {
                    variableIterator.second.setBinaryData(configIterator->second);
                    if(variableIterator.second.databaseId > 0) saveParameter(variableIterator.second.databaseId, configIterator->second);
                    else saveParameter(0, ParameterGroup::Type::Enum::config, channelIterator->first, variableIterator.first, configIterator->second);
                    configChanged = true;
                    GD::out.printInfo("Info: Parameter " + variableIterator.first + " of peer " + std::to_string(_peerID) + " and channel " + std::to_string(channelIterator->first) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(configIterator->second) + ".");
                }
            }
        }

        if(configChanged) raiseRPCUpdateDevice(_peerID, 0, _serialNumber, 0);

        _remoteManagementQueueGetDeviceConfiguration = false;

        return true;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

bool EnOceanPeer::sendInboundLinkTable()
{
    try
    {
        uint32_t setInboundLinkTableSize = 1;
        bool setLinkTableHasIndex = true;
        uint32_t linkTableGatewayEep = 0xA53808u;

        if(_rpcDevice->metadata)
        {
            auto metadataIterator = _rpcDevice->metadata->structValue->find("remoteManagementInfo");
            if(metadataIterator != _rpcDevice->metadata->structValue->end() && !metadataIterator->second->arrayValue->empty())
            {
                auto remoteManagementInfo = metadataIterator->second->arrayValue->at(0);
                auto infoIterator = remoteManagementInfo->structValue->find("features");
                if(infoIterator != metadataIterator->second->structValue->end() && !infoIterator->second->arrayValue->empty())
                {
                    auto features = infoIterator->second->arrayValue->at(0);
                    auto featureIterator = features->structValue->find("setInboundLinkTableSize");
                    if(featureIterator != features->structValue->end())
                    {
                        setInboundLinkTableSize = featureIterator->second->integerValue;
                    }

                    featureIterator = features->structValue->find("setLinkTableHasIndex");
                    if(featureIterator != features->structValue->end()) setLinkTableHasIndex = featureIterator->second->booleanValue;

                    featureIterator = features->structValue->find("linkTableGatewayEep");
                    if(featureIterator != features->structValue->end()) linkTableGatewayEep = BaseLib::Math::getUnsignedNumber(featureIterator->second->stringValue, true);
                }
            }
        }

        remoteManagementUnlock();

        std::vector<uint8_t> linkTable{};
        linkTable.reserve(9 * setInboundLinkTableSize);
        if(setLinkTableHasIndex) linkTable.push_back(0);
        auto gatewayAddress = (_gatewayAddress != 0) ? _gatewayAddress : _physicalInterface->getAddress();
        linkTable.push_back(gatewayAddress >> 24u);
        linkTable.push_back(gatewayAddress >> 16u);
        linkTable.push_back(gatewayAddress >> 8u);
        linkTable.push_back(gatewayAddress | (uint8_t)getRfChannel(0));
        linkTable.push_back(linkTableGatewayEep >> 16u);
        linkTable.push_back(linkTableGatewayEep >> 8u);
        linkTable.push_back(linkTableGatewayEep);
        linkTable.push_back(0);

        {
            std::lock_guard<std::mutex> peersGuard(_peersMutex);
            uint32_t index = 1;
            for(auto& channel : _peers)
            {
                uint64_t outputSelectorBitField = 0;
                uint32_t outputSelectorMemoryIndex = 0;
                uint32_t outputSelectorBitSize = 0;
                bool outputSelectorValue = false;

                {
                    auto functionIterator = _rpcDevice->functions.find(channel.first);
                    if(functionIterator != _rpcDevice->functions.end())
                    {
                        auto attributeIterator = functionIterator->second->linkReceiverAttributes.find("outputSelectorMemoryIndex");
                        if(attributeIterator != functionIterator->second->linkReceiverAttributes.end())
                        {
                            outputSelectorMemoryIndex = (uint32_t)attributeIterator->second->integerValue;

                            attributeIterator = functionIterator->second->linkReceiverAttributes.find("outputSelectorBitSize");
                            if(attributeIterator != functionIterator->second->linkReceiverAttributes.end())
                            {
                                outputSelectorBitSize = (uint32_t)attributeIterator->second->integerValue;
                            }
                        }

                        attributeIterator = functionIterator->second->linkReceiverAttributes.find("outputSelectorValue");
                        if(attributeIterator != functionIterator->second->linkReceiverAttributes.end())
                        {
                            outputSelectorValue = (bool)attributeIterator->second->integerValue;
                        }
                    }
                }

                for(auto& peer : channel.second)
                {
                    if(!peer->isSender) continue;
                    auto senderPeer = _central->getPeer(peer->id);
                    if(!senderPeer) continue;
                    uint64_t eep = senderPeer->getDeviceType();
                    uint8_t senderChannel = 0;
                    auto functionIterator = senderPeer->getRpcDevice()->functions.find(peer->channel);
                    if(functionIterator != senderPeer->getRpcDevice()->functions.end())
                    {
                        auto senderChannelIterator = functionIterator->second->linkSenderAttributes.find("channel");
                        if(senderChannelIterator != functionIterator->second->linkSenderAttributes.end())
                        {
                            senderChannel = (uint8_t)senderChannelIterator->second->integerValue;
                        }
                    }
                    if(outputSelectorBitSize != 0 && outputSelectorValue)
                    {
                        outputSelectorBitField |= (1u << index);
                    }

                    if(setLinkTableHasIndex) linkTable.push_back(index);
                    linkTable.push_back((uint32_t)peer->address >> 24u);
                    linkTable.push_back((uint32_t)peer->address >> 16u);
                    linkTable.push_back((uint32_t)peer->address >> 8u);
                    linkTable.push_back((uint32_t)peer->address);
                    linkTable.push_back(eep >> 16u);
                    linkTable.push_back(eep >> 8u);
                    linkTable.push_back(eep);
                    linkTable.push_back(senderChannel);
                    index++;
                }

                //{{{ Set device configuration
                std::map<uint32_t, std::vector<uint8_t>> selectorData;
                std::vector<uint8_t> outputSelectorRawData;
                BaseLib::HelperFunctions::memcpyBigEndian(outputSelectorRawData, (int64_t)outputSelectorBitField);
                uint32_t byteSize = outputSelectorBitSize / 8;
                if(outputSelectorRawData.size() > byteSize)
                {
                    outputSelectorRawData.erase(outputSelectorRawData.begin(), outputSelectorRawData.begin() + (outputSelectorRawData.size() - byteSize));
                }
                else if(outputSelectorRawData.size() < byteSize)
                {
                    std::vector<uint8_t> fill(byteSize - outputSelectorRawData.size(), 0);
                    outputSelectorRawData.insert(outputSelectorRawData.begin(), fill.begin(), fill.end());
                }
                selectorData.emplace(outputSelectorMemoryIndex, outputSelectorRawData);
                auto setDeviceConfiguration = std::make_shared<SetDeviceConfiguration>(_address, selectorData);
                setBestInterface();
                auto response = _physicalInterface->sendAndReceivePacket(setDeviceConfiguration, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
                if(!response)
                {
                    GD::out.printError("Error: Could not set device configuration on device.");
                    return false;
                }
                //}}}
            }
            for(uint32_t i = index; i < setInboundLinkTableSize; i++)
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
        }

        auto setLinkTable = std::make_shared<SetLinkTable>(_address, true, linkTable);

        auto response = _physicalInterface->sendAndReceivePacket(setLinkTable, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
        if(!response)
        {
            GD::out.printError("Error: Could not set device configuration on device.");
        }

        remoteManagementLock();

        return (bool)response;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

PVariable EnOceanPeer::forceConfigUpdate(PRpcClientInfo clientInfo)
{
    try
    {
        queueGetDeviceConfiguration();

        return std::make_shared<BaseLib::Variable>();
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanPeer::getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields)
{
	try
	{
		PVariable info(Peer::getDeviceInfo(clientInfo, fields));
		if(info->errorStruct) return info;

		if(fields.empty() || fields.find("INTERFACE") != fields.end()) info->structValue->insert(StructElement("INTERFACE", std::make_shared<Variable>(_physicalInterface->getID())));

		return info;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable EnOceanPeer::putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(channel < 0) channel = 0;
		if(remoteChannel < 0) remoteChannel = 0;
		auto functionIterator = _rpcDevice->functions.find(channel);
		if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel.");
		if(type == ParameterGroup::Type::none) type = ParameterGroup::Type::link;
		PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
		if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set.");
		if(variables->structValue->empty()) return std::make_shared<Variable>(VariableType::tVoid);

        auto central = getCentral();
        if(!central) return Variable::createError(-32500, "Could not get central.");

		if(type == ParameterGroup::Type::Enum::config)
		{
			bool parameterChanged = false;
			bool setRepeaterLevel = false;
			uint8_t repeaterLevel = 0;

			std::map<uint32_t, std::vector<uint8_t>> updatedParameters;
			for(auto& variable : *variables->structValue)
			{
				if(variable.first.empty() || !variable.second) continue;
				if(configCentral[channel].find(variable.first) == configCentral[channel].end()) continue;
				BaseLib::Systems::RpcConfigurationParameter& parameter = configCentral[channel][variable.first];
				if(!parameter.rpcParameter) continue;
				if(parameter.rpcParameter->password && variable.second->stringValue.empty()) continue; //Don't safe password if empty
				std::vector<uint8_t> parameterData;
				parameter.rpcParameter->convertToPacket(variable.second, parameter.mainRole(), parameterData);
				parameter.setBinaryData(parameterData);
				if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
				else saveParameter(0, ParameterGroup::Type::Enum::config, channel, variable.first, parameterData);

				if(channel == 0 && variable.first == "ENCRYPTION" && variable.second->booleanValue != _forceEncryption) _forceEncryption = variable.second->booleanValue;
                if(channel == 0 && variable.first == "REPEATER_LEVEL")
                {
                    setRepeaterLevel = true;
                    repeaterLevel = parameter.rpcParameter->convertFromPacket(parameterData, parameter.mainRole(), false)->integerValue;
                }

				parameterChanged = true;
				GD::out.printInfo("Info: Parameter " + variable.first + " of peer " + std::to_string(_peerID) + " and channel " + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

				if(!parameter.rpcParameter->linkedParameter.empty())
                {
                    BaseLib::Systems::RpcConfigurationParameter& linkedParameter = configCentral[channel][parameter.rpcParameter->linkedParameter];
                    if(linkedParameter.rpcParameter)
                    {
                        std::vector<uint8_t> linkedParameterData;
                        linkedParameter.rpcParameter->convertToPacket(variable.second, parameter.mainRole(), linkedParameterData);
                        linkedParameter.setBinaryData(linkedParameterData);
                        if(linkedParameter.databaseId > 0) saveParameter(linkedParameter.databaseId, linkedParameterData);
                        else saveParameter(0, ParameterGroup::Type::Enum::config, channel, variable.first, linkedParameterData);
                        GD::out.printInfo("Info: Parameter " + linkedParameter.rpcParameter->id + " of peer " + std::to_string(_peerID) + " and channel " + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(linkedParameterData) + ".");

                        if(linkedParameter.rpcParameter->physical->type == IPhysical::Type::tInteger &&
                                linkedParameter.rpcParameter->physical->bitSize > 0 &&
                                !((unsigned)linkedParameter.rpcParameter->physical->bitSize & 7u))
                        {
                            uint32_t byteSize = linkedParameter.rpcParameter->physical->bitSize / 8;
                            if(linkedParameterData.size() > byteSize)
                            {
                                linkedParameterData.erase(linkedParameterData.begin(), linkedParameterData.begin() + (linkedParameterData.size() - byteSize));
                            }
                            else if(linkedParameterData.size() < byteSize)
                            {
                                std::vector<uint8_t> fill(byteSize - linkedParameterData.size(), 0);
                                linkedParameterData.insert(linkedParameterData.begin(), fill.begin(), fill.end());
                            }
                            updatedParameters.emplace((uint32_t)linkedParameter.rpcParameter->physical->memoryIndex, linkedParameterData);
                        }
                    }
                }

				if(parameter.rpcParameter->physical->type != IPhysical::Type::tInteger || parameter.rpcParameter->physical->bitSize <= 0) continue;

				if((unsigned)parameter.rpcParameter->physical->bitSize & 7u)
                {
				    GD::out.printInfo("Info: Configuration parameters are only supported in multiples of 8 bits.");
				    continue;
                }

                uint32_t byteSize = parameter.rpcParameter->physical->bitSize / 8;
                if(parameterData.size() > byteSize)
                {
                    parameterData.erase(parameterData.begin(), parameterData.begin() + (parameterData.size() - byteSize));
                }
                else if(parameterData.size() < byteSize)
                {
                    std::vector<uint8_t> fill(byteSize - parameterData.size(), 0);
                    parameterData.insert(parameterData.begin(), fill.begin(), fill.end());
                }
                updatedParameters.emplace((uint32_t)parameter.rpcParameter->physical->memoryIndex, parameterData);
			}

            //{{{ Set repeater level
            if(setRepeaterLevel)
            {
                remoteManagementUnlock();

                uint8_t function = (repeaterLevel > 0) ? 1 : 0;
                uint8_t level = (repeaterLevel == 0) ? 1 : repeaterLevel;
                auto setRepeaterFunctions = std::make_shared<SetRepeaterFunctions>(_address, function, level, 0);
                setBestInterface();
                auto response = _physicalInterface->sendAndReceivePacket(setRepeaterFunctions, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::remoteCommissioningAck}});
                if(!response)
                {
                    GD::out.printError("Error: Could not set repeater level on device.");
                }

                remoteManagementLock();
            }
            //}}}

            //{{{ Build and send packet
			if(!updatedParameters.empty())
            {
                queueSetDeviceConfiguration(updatedParameters);
            }
            //}}}

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
		return std::make_shared<Variable>(VariableType::tVoid);
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
		return std::make_shared<Variable>(VariableType::tVoid);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

void EnOceanPeer::sendPacket(const PEnOceanPacket& packet, const std::string& responseId, int32_t delay, bool wait)
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
				auto parameterIterator = channelIterator->second.find("RESENDS_WHEN_NO_ACK");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
					resends = parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue;
					if(resends < 0) resends = 0;
					else if(resends > 12) resends = 12;
				}
				parameterIterator = channelIterator->second.find("RESEND_TIMEOUT");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					std::vector<uint8_t> parameterData = parameterIterator->second.getBinaryData();
					resendTimeout = parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue;
					if(resendTimeout < 10) resendTimeout = 10;
					else if(resendTimeout > 10000) resendTimeout = 10000;
				}
			}
            setBestInterface();
			if(resends == 0) _physicalInterface->sendEnoceanPacket(packet);
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
						_physicalInterface->sendEnoceanPacket(packet);
						if(rpcRequest->conditionVariable.wait_for(conditionVariableGuard, std::chrono::milliseconds(resendTimeout)) == std::cv_status::no_timeout || rpcRequest->abort) break;
						if(i == resends) serviceMessages->setUnreach(true, false);
					}
                    conditionVariableGuard.unlock();
					{
						std::lock_guard<std::mutex> requestsGuard(_rpcRequestsMutex);
						_rpcRequests.erase(rpcRequest->responseId);
					}
				}
				else _physicalInterface->sendEnoceanPacket(packet);
			}
		}
		else
        {
            setBestInterface();
            _physicalInterface->sendEnoceanPacket(packet);
        }
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
		if(channel == 0 && serviceMessages->set(valueKey, value->booleanValue)) return std::make_shared<Variable>(VariableType::tVoid);
		auto channelIterator = valuesCentral.find(channel);
		if(channelIterator == valuesCentral.end()) return Variable::createError(-2, "Unknown channel.");
		auto parameterIterator = channelIterator->second.find(valueKey);
		if(parameterIterator == valuesCentral[channel].end()) return Variable::createError(-5, "Unknown parameter.");
		PParameter rpcParameter = parameterIterator->second.rpcParameter;
		if(!rpcParameter) return Variable::createError(-5, "Unknown parameter.");
		BaseLib::Systems::RpcConfigurationParameter& parameter = valuesCentral[channel][valueKey];
		std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>());
		std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable>());

		if(rpcParameter->physical->operationType == IPhysical::OperationType::Enum::store)
		{
			std::vector<uint8_t> parameterData;
			rpcParameter->convertToPacket(value, parameter.mainRole(), parameterData);
			parameter.setBinaryData(parameterData);
			if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
			else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);

            if(rpcParameter->readable)
            {
                valueKeys->push_back(valueKey);
                values->push_back(rpcParameter->convertFromPacket(parameterData, parameter.mainRole(), true));
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
			for(auto i = rpcParameter->setPackets.begin(); i != rpcParameter->setPackets.end(); ++i)
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
		rpcParameter->convertToPacket(value, parameter.mainRole(), parameterData);
		parameter.setBinaryData(parameterData);
		if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
		else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);
		if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + valueKey + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

        if(rpcParameter->readable)
        {
            valueKeys->push_back(valueKey);
            values->push_back(rpcParameter->convertFromPacket(parameterData, parameter.mainRole(), true));
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

			auto channelIterator = valuesCentral.find(_globalRfChannel ? 0 : channel);
			if(channelIterator != valuesCentral.end())
			{
				auto parameterIterator = channelIterator->second.find("RF_CHANNEL");
				if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
				{
					parameterIterator->second.rpcParameter->convertToPacket(value, parameterIterator->second.mainRole(), parameterData);
					parameterIterator->second.setBinaryData(parameterData);
					if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
					else saveParameter(0, ParameterGroup::Type::Enum::variables, _globalRfChannel ? 0 : channel, "RF_CHANNEL", parameterData);
					setRfChannel(_globalRfChannel ? 0 : channel, parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue);
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
							auto parameterIterator = channelIterator->second.find(valueKey == "UP" ? "DOWN" : "UP");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								BaseLib::PVariable falseValue = std::make_shared<BaseLib::Variable>(false);
								parameterIterator->second.rpcParameter->convertToPacket(falseValue,  parameterIterator->second.mainRole(), parameterData);
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
							auto parameterIterator = channelIterator->second.find("SIGNAL_DURATION");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								_blindCurrentTargetPosition = valueKey == "DOWN" ? 10000 : 0;
								int32_t positionDifference = _blindCurrentTargetPosition - _blindPosition;
                                parameterData = parameterIterator->second.getBinaryData();
                                _blindTransitionTime = parameterIterator->second.rpcParameter->convertFromPacket(parameterData, parameterIterator->second.mainRole(), false)->integerValue * 1000;
                                if(positionDifference != 0) _blindCurrentSignalDuration = _blindTransitionTime / (10000 / std::abs(positionDifference));
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
							auto parameterIterator = channelIterator->second.find(valueKey == "UP" ? "DOWN" : "UP");
							if(parameterIterator != channelIterator->second.end() && parameterIterator->second.rpcParameter)
							{
								BaseLib::PVariable falseValue = std::make_shared<BaseLib::Variable>(false);
								parameterIterator->second.rpcParameter->convertToPacket(falseValue, parameterIterator->second.mainRole(), parameterData);
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
                                _blindTransitionTime = parameterIterator2->second.rpcParameter->convertFromPacket(parameterData, parameterIterator2->second.mainRole(), false)->integerValue * 1000;
                                _blindCurrentTargetPosition = _blindPosition + positionDifference;
                                _blindCurrentSignalDuration = _blindTransitionTime / (10000 / std::abs(positionDifference));
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
                                        parameterIterator2->second.rpcParameter->convertToPacket(trueValue, parameterIterator2->second.mainRole(), parameterData);
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
            else if((_deviceType & 0xFFFFFFu) == 0xD20502)
            {
                if(valueKey == "POSITION" && value->integerValue != 127)
                {
                    if(value->integerValue > 100) value->integerValue = 100;
                    else if(value->integerValue < 0) value->integerValue = 0;

                    int32_t newPosition = value->integerValue * 100;
                    if(newPosition != _blindPosition)
                    {
                        int32_t positionDifference = newPosition - _blindPosition; //Can't be 0

                        channelIterator = configCentral.find(0);
                        if(channelIterator != configCentral.end())
                        {
                            auto parameterIterator2 = channelIterator->second.find("TRANSITION_TIME");
                            if(parameterIterator2 != channelIterator->second.end() && parameterIterator2->second.rpcParameter)
                            {
                                parameterData = parameterIterator2->second.getBinaryData();
                                _blindTransitionTime = parameterIterator2->second.rpcParameter->convertFromPacket(parameterData, parameterIterator2->second.mainRole(), false)->integerValue;
                                _blindCurrentTargetPosition = _blindPosition + positionDifference;
                                _blindCurrentSignalDuration = _blindTransitionTime / (10000 / std::abs(positionDifference));
                                _blindStateResetTime = BaseLib::HelperFunctions::getTime() + _blindCurrentSignalDuration + (newPosition == 0 || newPosition == 10000 ? 5000 : 0);
                                _lastBlindPositionUpdate = BaseLib::HelperFunctions::getTime();
                                _blindUp = positionDifference < 0;
                                updateBlindSpeed();
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
					BaseLib::HelperFunctions::memcpyBigEndian(data, (*i)->constValueInteger);
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
					resetParameterIterator->second.rpcParameter->convertToPacket(logicalDefaultValue, resetParameterIterator->second.mainRole(), defaultValue);
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

bool EnOceanPeer::remoteManagementUnlock()
{
    try
    {
        uint32_t securityCode = 0;

        auto channelIterator = configCentral.find(0);
        if(channelIterator != configCentral.end())
        {
            auto variableIterator = channelIterator->second.find("SECURITY_CODE");
            if(variableIterator != channelIterator->second.end() && variableIterator->second.rpcParameter)
            {
                securityCode = BaseLib::Math::getUnsignedNumber(variableIterator->second.rpcParameter->convertFromPacket(variableIterator->second.getBinaryData(), variableIterator->second.mainRole(), false)->stringValue, true);
            }
        }

        if(securityCode != 0)
        {
            setBestInterface();
            auto unlock = std::make_shared<Unlock>(_address, securityCode);
            _physicalInterface->sendEnoceanPacket(unlock);
            _physicalInterface->sendEnoceanPacket(unlock);

            auto queryStatus = std::make_shared<QueryStatusPacket>(_address);
            auto response = _physicalInterface->sendAndReceivePacket(queryStatus, 2, IEnOceanInterface::EnOceanRequestFilterType::remoteManagementFunction, {{(uint16_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse >> 8u, (uint8_t)EnOceanPacket::RemoteManagementResponse::queryStatusResponse}});

            if(!response) return false;
            auto queryStatusData = response->getData();

            bool codeSet = queryStatusData.at(4) & 0x80u;
            auto lastFunctionNumber = (uint16_t)((uint16_t)(queryStatusData.at(5) & 0x0Fu) << 8u) | queryStatusData.at(6);
            //Some devices return "query status" as function number here (i. e. OPUS 563.052).
            if((lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::unlock && lastFunctionNumber != (uint16_t)EnOceanPacket::RemoteManagementFunction::queryStatus) || (codeSet && queryStatusData.at(7) != (uint8_t)EnOceanPacket::QueryStatusReturnCode::ok))
            {
                GD::out.printWarning("Warning: Error unlocking device.");
                return false;
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

void EnOceanPeer::remoteManagementLock()
{
    try
    {
        uint32_t securityCode = 0;

        auto channelIterator = configCentral.find(0);
        if(channelIterator != configCentral.end())
        {
            auto variableIterator = channelIterator->second.find("SECURITY_CODE");
            if(variableIterator != channelIterator->second.end() && variableIterator->second.rpcParameter)
            {
                securityCode = BaseLib::Math::getUnsignedNumber(variableIterator->second.rpcParameter->convertFromPacket(variableIterator->second.getBinaryData(), variableIterator->second.mainRole(), false)->stringValue, true);
            }
        }

        if(securityCode != 0)
        {
            auto lock = std::make_shared<Lock>(_address, securityCode);
            _physicalInterface->sendEnoceanPacket(lock);
            _physicalInterface->sendEnoceanPacket(lock);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

}
