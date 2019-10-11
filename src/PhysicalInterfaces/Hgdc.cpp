/* Copyright 2013-2019 Homegear GmbH */

#include "../GD.h"
#include "Hgdc.h"

namespace EnOcean
{

Hgdc::Hgdc(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IEnOceanInterface(settings)
{
    _settings = settings;
    _out.init(GD::bl);
    _out.setPrefix(GD::out.getPrefix() + "EnOcean HGDC \"" + settings->id + "\": ");

    signal(SIGPIPE, SIG_IGN);

    _stopped = true;
}

Hgdc::~Hgdc()
{
    stopListening();
    _bl->threadManager.join(_initThread);
}

void Hgdc::startListening()
{
    try
    {
        GD::bl->hgdc->unregisterPacketReceivedEventHandler(_packetReceivedEventHandlerId);
        _packetReceivedEventHandlerId = GD::bl->hgdc->registerPacketReceivedEventHandler(MY_FAMILY_ID, std::function<void(int64_t, const std::string&, const std::vector<uint8_t>&)>(std::bind(&Hgdc::processPacket, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
        IPhysicalInterface::startListening();

        _stopped = false;
        init();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Hgdc::stopListening()
{
    try
    {
        _stopped = true;
        IPhysicalInterface::stopListening();
        GD::bl->hgdc->unregisterPacketReceivedEventHandler(_packetReceivedEventHandlerId);
        _packetReceivedEventHandlerId = -1;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Hgdc::init()
{
    try
    {
        _initComplete = false;
        std::vector<uint8_t> response;
        for(int32_t i = 0; i < 10; i++)
        {
            std::vector<uint8_t> data{ 0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00 };
            addCrc8(data);
            getResponse(0x02, data, response);
            if(response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0)
            {
                if(i < 9) continue;
                _out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(response));
                _stopped = true;
                return;
            }
            _baseAddress = ((int32_t)(uint8_t)response[7] << 24) | ((int32_t)(uint8_t)response[8] << 16) | ((int32_t)(uint8_t)response[9] << 8) | (uint8_t)response[10];
            break;
        }

        _out.printInfo("Info: Init complete. Base address is 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ". Remaining changes: " + std::to_string(response[11]));

        _initComplete = true;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

int32_t Hgdc::setBaseAddress(uint32_t value)
{
    try
    {
        if(!_initComplete)
        {
            _out.printError("Error: Could not set base address. Init is not complete.");
            return -1;
        }
        if((value & 0xFF000000) != 0xFF000000)
        {
            _out.printError("Error: Could not set base address. Address must start with 0xFF.");
            return -1;
        }

        std::vector<uint8_t> response;

        {
            // Set address - only possible 10 times, Must start with "0xFF"
            std::vector<uint8_t> data{ 0x55, 0x00, 0x05, 0x00, 0x05, 0x00, 0x07, (uint8_t)(value >> 24), (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF), 0x00 };
            addCrc8(data);
            getResponse(0x02, data, response);
            if(response.size() != 8 || response[1] != 0 || response[2] != 1 || response[3] != 0 || response[4] != 2 || response[6] != 0)
            {
                _out.printError("Error setting address on device: " + BaseLib::HelperFunctions::getHexString(response));
                _stopped = true;
                return -1;
            }
        }

        for(int32_t i = 0; i < 10; i++)
        {
            std::vector<uint8_t> data{ 0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00 };
            addCrc8(data);
            getResponse(0x02, data, response);
            if(response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0)
            {
                if(i < 9) continue;
                _out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(data));
                _stopped = true;
                return -1;
            }
            _baseAddress = ((int32_t)(uint8_t)response[7] << 24) | ((int32_t)(uint8_t)response[8] << 16) | ((int32_t)(uint8_t)response[9] << 8) | (uint8_t)response[10];
            break;
        }

        _out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ". Remaining changes: " + std::to_string(response[11]));

        return response[11];
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return -1;
}

void Hgdc::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
    try
    {
        std::shared_ptr<EnOceanPacket> myPacket(std::dynamic_pointer_cast<EnOceanPacket>(packet));
        if(!myPacket) return;

        std::vector<uint8_t> data = std::move(myPacket->getBinary());
        addCrc8(data);
        std::vector<uint8_t> response;
        getResponse(0x02, data, response);
        if(response.size() != 8 || (response.size() >= 7 && response[6] != 0))
        {
            if(response.size() >= 7 && response[6] != 0)
            {
                std::map<uint8_t, std::string>::iterator statusIterator = _responseStatusCodes.find(response[6]);
                if(statusIterator != _responseStatusCodes.end()) _out.printError("Error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\": " + statusIterator->second);
                else _out.printError("Unknown error (" + std::to_string(response[6]) + ") sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
            }
            else _out.printError("Unknown error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
            return;
        }
        _lastPacketSent = BaseLib::HelperFunctions::getTime();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Hgdc::rawSend(std::vector<uint8_t>& packet)
{
    try
    {
        if(!GD::bl->hgdc->sendPacket(_settings->serialNumber, packet))
        {
            _out.printError("Error sending packet " + BaseLib::HelperFunctions::getHexString(packet) + ".");
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Hgdc::processPacket(int64_t familyId, const std::string& serialNumber, const std::vector<uint8_t>& data)
{
    try
    {
        if(serialNumber != _settings->serialNumber) return;

        if(data.size() < 6)
        {
            _out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
            return;
        }

        {
            uint8_t crc8 = 0;
            for(int32_t i = 1; i < 5; i++)
            {
                crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
            }
            if(crc8 != data[5])
            {
                _out.printError("Error: CRC (0x" + BaseLib::HelperFunctions::getHexString(crc8, 2) + ") failed for header: " + BaseLib::HelperFunctions::getHexString(data));
                return;
            }

            crc8 = 0;
            for(uint32_t i = 6; i < data.size() - 1; i++)
            {
                crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
            }
            if(crc8 != data.back())
            {
                _out.printError("Error: CRC failed for packet: " + BaseLib::HelperFunctions::getHexString(data));
                return;
            }
        }

        _lastPacketReceived = BaseLib::HelperFunctions::getTime();

        uint8_t packetType = data[4];
        std::unique_lock<std::mutex> requestsGuard(_requestsMutex);
        auto requestIterator = _requests.find(packetType);
        if(requestIterator != _requests.end())
        {
            auto request = requestIterator->second;
            requestsGuard.unlock();
            request->response = data;
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->mutexReady = true;
            }
            request->conditionVariable.notify_one();
            return;
        }
        else requestsGuard.unlock();

        auto packet = std::make_shared<EnOceanPacket>(data);
        if(packet->getType() == EnOceanPacket::Type::RADIO_ERP1 || packet->getType() == EnOceanPacket::Type::RADIO_ERP2)
        {
            if((packet->senderAddress() & 0xFFFFFF80) == _baseAddress) _out.printInfo("Info: Ignoring packet from myself: " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));
            else raisePacketReceived(packet);
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

}