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

void IEnOceanInterface::getResponse(uint8_t packetType, const std::vector<char>& requestPacket, std::vector<char>& responsePacket)
{
	try
    {
		if(_stopped) return;
		responsePacket.clear();

		std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
		std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
		std::shared_ptr<Request> request(new Request());
		_requestsMutex.lock();
		_requests[packetType] = request;
		_requestsMutex.unlock();
		std::unique_lock<std::mutex> lock(request->mutex);

		try
		{
			_out.printInfo("Info: Sending packet " + BaseLib::HelperFunctions::getHexString(requestPacket));
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

		_requestsMutex.lock();
		_requests.erase(packetType);
		_requestsMutex.unlock();
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        _requestsMutex.unlock();
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        _requestsMutex.unlock();
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
        _requestsMutex.unlock();
    }
}

void IEnOceanInterface::addCrc8(std::vector<char>& packet)
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

}
