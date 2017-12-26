/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#include "IEnOceanInterface.h"
#include "../GD.h"
#include "../MyPacket.h"

namespace MyFamily
{

IEnOceanInterface::IEnOceanInterface(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(GD::bl, GD::family->getFamily(), settings)
{
	_bl = GD::bl;

	if(settings->listenThreadPriority == -1)
	{
		settings->listenThreadPriority = 0;
		settings->listenThreadPolicy = SCHED_OTHER;
	}

	_responseStatusCodes[0] = "RET_OK";
	_responseStatusCodes[1] = "RET_ERROR";
	_responseStatusCodes[2] = "RET_NOT_SUPPORTED";
	_responseStatusCodes[3] = "RET_WRONG_PARAM";
	_responseStatusCodes[4] = "RET_OPERATION_DENIED";
	_responseStatusCodes[5] = "RET_LOCK_SET - Duty cycle limit reached.";
	_responseStatusCodes[6] = "RET_BUFFER_TOO_SMALL";
	_responseStatusCodes[7] = "RET_NO_FREE_BUFFER";
}

IEnOceanInterface::~IEnOceanInterface()
{

}

void IEnOceanInterface::getResponse(uint8_t packetType, std::vector<uint8_t>& requestPacket, std::vector<uint8_t>& responsePacket)
{
	try
    {
        if(_stopped) return;
        responsePacket.clear();

        std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
        std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
        std::shared_ptr<Request> request(new Request());
        std::unique_lock<std::mutex> requestsGuard(_requestsMutex);
        _requests[packetType] = request;
        requestsGuard.unlock();
        std::unique_lock<std::mutex> lock(request->mutex);

        try
        {
            GD::out.printInfo("Info: Sending packet " + BaseLib::HelperFunctions::getHexString(requestPacket));
            rawSend(requestPacket);
        }
        catch(BaseLib::SocketOperationException ex)
        {
            _out.printError("Error sending packet: " + ex.what());
            return;
        }

        if(!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(10000), [&] { return request->mutexReady; }))
        {
            _out.printError("Error: No response received to packet: " + BaseLib::HelperFunctions::getHexString(requestPacket));
        }
        responsePacket = request->response;

        requestsGuard.lock();
        _requests.erase(packetType);
        requestsGuard.unlock();
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void IEnOceanInterface::addCrc8(std::vector<uint8_t>& packet)
{
	try
	{
		if(packet.size() < 6) return;

		uint8_t crc8 = 0;
		for(int32_t i = 1; i < 5; i++)
		{
			crc8 = _crc8Table[crc8 ^ (uint8_t)packet[i]];
		}
		packet[5] = crc8;

		crc8 = 0;
		for(uint32_t i = 6; i < packet.size() - 1; i++)
		{
			crc8 = _crc8Table[crc8 ^ (uint8_t)packet[i]];
		}
		packet.back() = crc8;
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void IEnOceanInterface::raisePacketReceived(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
    try
    {
        PMyPacket myPacket(std::dynamic_pointer_cast<MyPacket>(packet));
        if(!myPacket) return;

        if(myPacket->senderAddress() != (int32_t)_baseAddress)
        {
            std::lock_guard<std::mutex> rssiGuard(_rssiMutex);
            if(_rssi.size() > 10000 || _wildcardRssi.size() > 10000)
            {
                _out.printWarning("Warning: More than 10000 RSSI values are stored. Clearing them...");
                _rssi.clear();
                _wildcardRssi.clear();
            }

            auto rssiIterator = _rssi.find(myPacket->senderAddress());
            if(rssiIterator == _rssi.end()) rssiIterator = _rssi.emplace(myPacket->senderAddress(), DeviceInfo()).first;
            rssiIterator->second.rssi = myPacket->getRssi();
            rssiIterator->second.packetReceivedTimes.push(myPacket->timeReceived());
            while(rssiIterator->second.packetReceivedTimes.size() > 5) rssiIterator->second.packetReceivedTimes.pop();

            rssiIterator = _wildcardRssi.find(myPacket->senderAddress());
            if(rssiIterator == _wildcardRssi.end()) rssiIterator = _wildcardRssi.emplace(myPacket->senderAddress() & 0xFFFFFF80, DeviceInfo()).first;
            rssiIterator->second.rssi = myPacket->getRssi();
            rssiIterator->second.packetReceivedTimes.push(myPacket->timeReceived());
            while(rssiIterator->second.packetReceivedTimes.size() > 5) rssiIterator->second.packetReceivedTimes.pop();
        }

        BaseLib::Systems::IPhysicalInterface::raisePacketReceived(packet);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

int32_t IEnOceanInterface::getRssi(int32_t address, bool wildcardPeer)
{
    try
    {
        std::lock_guard<std::mutex> rssiGuard(_rssiMutex);
        if(wildcardPeer)
        {
            auto rssiIterator = _wildcardRssi.find(address & 0xFFFFFF80);
            if(rssiIterator != _wildcardRssi.end()) return rssiIterator->second.rssi;
        }
        else
        {
            auto rssiIterator = _rssi.find(address);
            if(rssiIterator != _rssi.end()) return rssiIterator->second.rssi;
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return 0;
}
}
