/* Copyright 2013-2016 Sathya Laufer
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
