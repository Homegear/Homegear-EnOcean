/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#include "../GD.h"
#include "Usb300.h"

namespace MyFamily
{

Usb300::Usb300(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IEnOceanInterface(settings)
{
	_settings = settings;
	_out.init(GD::bl);
	_out.setPrefix(GD::out.getPrefix() + "EnOcean USB 300 \"" + settings->id + "\": ");

	signal(SIGPIPE, SIG_IGN);
}

Usb300::~Usb300()
{
	stopListening();
	GD::bl->threadManager.join(_initThread);
}

void Usb300::setup(int32_t userID, int32_t groupID, bool setPermissions)
{
    try
    {
    	if(setPermissions) setDevicePermission(userID, groupID);
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

void Usb300::startListening()
{
	try
	{
		stopListening();

		if(_settings->device.empty())
		{
			_out.printError("Error: No device defined for USB 300. Please specify it in \"enocean.conf\".");
			return;
		}

		_serial.reset(new BaseLib::SerialReaderWriter(_bl, _settings->device, 57600, 0, true, -1));
		_serial->openDevice(false, false, false);
		if(!_serial->isOpen())
		{
			_out.printError("Error: Could not open device.");
			return;
		}

		_stopCallbackThread = false;
		_stopped = false;
		int32_t result = 0;
		char byte = 0;
		while(result == 0)
		{
			//Clear buffer, otherwise the address response cannot be sent by the module if the buffer is full.
			result = _serial->readChar(byte, 100000);
		}
		if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Usb300::listen, this);
		else _bl->threadManager.start(_listenThread, true, &Usb300::listen, this);
		IPhysicalInterface::startListening();

		init();
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

void Usb300::stopListening()
{
	try
	{
		_stopCallbackThread = true;
		_bl->threadManager.join(_listenThread);
		_stopped = true;
		_initComplete = false;
		if(_serial) _serial->closeDevice();
		IPhysicalInterface::stopListening();
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

int32_t Usb300::setBaseAddress(uint32_t value)
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

		std::vector<char> response;

		{
			// Set address - only possible 10 times, Must start with "0xFF"
			std::vector<char> data{ 0x55, 0x00, 0x05, 0x00, 0x05, 0x00, 0x07, (char)(uint8_t)(value >> 24), (char)(uint8_t)((value >> 16) & 0xFF), (char)(uint8_t)((value >> 8) & 0xFF), (char)(uint8_t)(value & 0xFF), 0x00 };
			addCrc8(data);
			getResponse(0x02, data, response);
			if(response.size() != 8 || response[1] != 0 || response[2] != 1 || response[3] != 0 || response[4] != 2 || response[6] != 0)
			{
				_out.printError("Error setting address on device: " + BaseLib::HelperFunctions::getHexString(data));
				_stopped = true;
				return -1;
			}
		}

		for(int32_t i = 0; i < 10; i++)
		{
			std::vector<char> data{ 0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00 };
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
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return -1;
}

void Usb300::init()
{
	try
    {
		std::vector<char> response;
		for(int32_t i = 0; i < 10; i++)
		{
			std::vector<char> data{ 0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00 };
			addCrc8(data);
			getResponse(0x02, data, response);
			if(response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0)
			{
				if(i < 9) continue;
				_out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(data));
				_stopped = true;
				return;
			}
			_baseAddress = ((int32_t)(uint8_t)response[7] << 24) | ((int32_t)(uint8_t)response[8] << 16) | ((int32_t)(uint8_t)response[9] << 8) | (uint8_t)response[10];
			break;
		}

		_out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ". Remaining changes: " + std::to_string(response[11]));

		_initComplete = true;
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

void Usb300::reconnect()
{
	try
    {
		_serial->closeDevice();
		_initComplete = false;
		_serial->openDevice(false, false, false);
		if(!_serial->isOpen())
		{
			_out.printError("Error: Could not open device.");
			return;
		}
		_stopped = false;

		GD::bl->threadManager.join(_initThread);
		_bl->threadManager.start(_initThread, true, &Usb300::init, this);
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

void Usb300::listen()
{
    try
    {
    	std::vector<char> data;
    	data.reserve(100);
    	char byte = 0;
    	int32_t result = 0;
    	uint32_t size = 0;
    	uint8_t crc8 = 0;

        while(!_stopCallbackThread)
        {
        	try
        	{
				if(_stopped || !_serial || !_serial->isOpen())
				{
					if(_stopCallbackThread) return;
					if(_stopped) _out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
					_serial->closeDevice();
					std::this_thread::sleep_for(std::chrono::milliseconds(10000));
					reconnect();
					continue;
				}

				result = _serial->readChar(byte, 100000);
				if(result == -1)
				{
					_out.printError("Error reading from serial device.");
					_stopped = true;
					size = 0;
					data.clear();
					continue;
				}
				else if(result == 1)
				{
					size = 0;
					data.clear();
					continue;
				}

				if(data.empty() && byte != 0x55) continue;
				data.push_back(byte);

				if(size == 0 && data.size() == 6)
				{
					crc8 = 0;
					for(int32_t i = 1; i < 5; i++)
					{
						crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
					}
					if((char)crc8 != data[5])
					{
						_out.printError("Error: CRC (0x" + BaseLib::HelperFunctions::getHexString(crc8, 2) + ") failed for header: " + BaseLib::HelperFunctions::getHexString(data));
						size = 0;
						data.clear();
						continue;
					}
					size = ((data[1] << 8) | data[2]) + data[3];
					if(size == 0)
					{
						_out.printError("Error: Header has invalid size information: " + BaseLib::HelperFunctions::getHexString(data));
						size = 0;
						data.clear();
						continue;
					}
					size += 7;
				}
				if(size > 0 && data.size() == size)
				{
					crc8 = 0;
					for(uint32_t i = 6; i < data.size() - 1; i++)
					{
						crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
					}
					if((char)crc8 != data.back())
					{
						_out.printError("Error: CRC failed for packet: " + BaseLib::HelperFunctions::getHexString(data));
						size = 0;
						data.clear();
						continue;
					}

					processPacket(data);

					_lastPacketReceived = BaseLib::HelperFunctions::getTime();
					size = 0;
					data.clear();
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
}

void Usb300::processPacket(std::vector<char>& data)
{
	try
	{
		if(data.size() < 5)
		{
			_out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
			return;
		}

		uint8_t packetType = data[4];
		_requestsMutex.lock();
		std::map<uint8_t, std::shared_ptr<Request>>::iterator requestIterator = _requests.find(packetType);
		if(requestIterator != _requests.end())
		{
			std::shared_ptr<Request> request = requestIterator->second;
			_requestsMutex.unlock();
			request->response = data;
			{
				std::lock_guard<std::mutex> lock(request->mutex);
				request->mutexReady = true;
			}
			request->conditionVariable.notify_one();
			return;
		}
		else _requestsMutex.unlock();

		PMyPacket packet(new MyPacket(data));
		if(packet->getType() == MyPacket::Type::RADIO_ERP1 || packet->getType() == MyPacket::Type::RADIO_ERP2)
		{
			if((packet->senderAddress() & 0xFFFFFF80) == _baseAddress) _out.printInfo("Info: Ignoring packet from myself: " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));
			else raisePacketReceived(packet);
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
}

void Usb300::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		std::shared_ptr<MyPacket> myPacket(std::dynamic_pointer_cast<MyPacket>(packet));
		if(!myPacket) return;

		if(!_initComplete)
		{
			_out.printInfo("Info: Waiting one second, because init is not complete.");
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			if(!_initComplete)
			{
				_out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(myPacket->getBinary()) + ", because init is not complete.");
				return;
			}
		}

		std::vector<char> data = std::move(myPacket->getBinary());
		addCrc8(data);
		std::vector<char> response;
		getResponse(0x02, data, response);
		if(response.size() != 8 || (response.size() >= 7 && response[6] != 0))
		{
			if(response.size() >= 7 && response[6] != 0)
			{
				std::map<char, std::string>::iterator statusIterator = _responseStatusCodes.find(response[6]);
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
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Usb300::rawSend(const std::vector<char>& packet)
{
	try
	{
		if(!_serial || !_serial->isOpen()) return;
		_serial->writeData(packet);
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
