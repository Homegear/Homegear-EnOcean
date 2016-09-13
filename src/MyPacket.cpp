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

#include "MyPacket.h"

#include "GD.h"

namespace MyFamily
{
MyPacket::MyPacket()
{
}

MyPacket::MyPacket(std::vector<char>& espPacket) : _packet(espPacket)
{
	_timeReceived = BaseLib::HelperFunctions::getTime();
	if(espPacket.size() < 6) return;
	uint32_t dataSize = (espPacket[1] << 8) | espPacket[2];
	uint32_t optionalSize = espPacket[3];
	uint32_t fullSize = dataSize + optionalSize;
	if(espPacket.size() != fullSize + 7 || fullSize == 0)
	{
		GD::out.printWarning("Warning: Tried to import packet with wrong size information: " + BaseLib::HelperFunctions::getHexString(espPacket));
		return;
	}
	_type = (Type)espPacket[4];
	_data.insert(_data.end(), espPacket.begin() + 6, espPacket.begin() + 6 + dataSize);
	_optionalData.insert(_optionalData.end(), espPacket.begin() + 6 + dataSize, espPacket.begin() + 6 + dataSize + optionalSize);

	if(_type == Type::RADIO_ERP1 || _type == Type::RADIO_ERP2)
	{
		if(!_data.empty()) _rorg = (uint8_t)_data[0];
		if(_data.size() >= 6) _senderAddress = (((int32_t)(uint8_t)_data[_data.size() - 5]) << 24) | (((int32_t)(uint8_t)_data[_data.size() - 4]) << 16) | (((int32_t)(uint8_t)_data[_data.size() - 3]) << 8) | ((int32_t)(uint8_t)_data[_data.size() - 2]);
		//Destination address is unset for RADIO_ERP2
		if(_optionalData.size() >= 5) _destinationAddress = (((int32_t)(uint8_t)_optionalData[1]) << 24) | (((int32_t)(uint8_t)_optionalData[2]) << 16) | (((int32_t)(uint8_t)_optionalData[3]) << 8) | (int32_t)(uint8_t)_optionalData[4];
		if(_optionalData.size() >= 2) _rssi = _type == Type::RADIO_ERP1 ? -((int32_t)_optionalData[_optionalData.size() - 2]) : -((int32_t)_optionalData.back());
	}
}

MyPacket::MyPacket(Type type, uint8_t rorg, int32_t senderAddress) : _type(type), _rorg(rorg)
{
	_senderAddress = senderAddress;
	_appendAddressAndStatus = true;
	_data.reserve(20);
	_data.push_back((char)rorg);
	if(type == Type::RADIO_ERP1) _optionalData = std::vector<char>{ 3, (char)(uint8_t)0xFF, (char)(uint8_t)0xFF, (char)(uint8_t)0xFF, (char)(uint8_t)0xFF, 0, 0 };
	else if(type == Type::RADIO_ERP2) _optionalData = std::vector<char>{ 3, 0 };
}

MyPacket::~MyPacket()
{
	_packet.clear();
	_data.clear();
	_optionalData.clear();
}

std::vector<char> MyPacket::getBinary()
{
	try
	{
		if(!_packet.empty()) return _packet;
		if(_appendAddressAndStatus)
		{
			_data.push_back((char)(uint8_t)(_senderAddress >> 24));
			_data.push_back((char)(uint8_t)((_senderAddress >> 16) & 0xFF));
			_data.push_back((char)(uint8_t)((_senderAddress >> 8) & 0xFF));
			_data.push_back((char)(uint8_t)(_senderAddress & 0xFF));
			_data.push_back(0);
		}
		if(_data.empty() && _optionalData.empty()) return std::vector<char>();
		_packet.reserve(7 + _data.size() + _optionalData.size());
		_packet.push_back(0x55);
		_packet.push_back((char)(uint8_t)(_data.size() >> 8));
		_packet.push_back((char)(uint8_t)(_data.size() & 0xFF));
		_packet.push_back((char)(uint8_t)_optionalData.size());
		_packet.push_back((char)_type);
		_packet.push_back(0);
		_packet.insert(_packet.end(), _data.begin(), _data.end());
		_packet.insert(_packet.end(), _optionalData.begin(), _optionalData.end());
		_packet.push_back(0);
		return _packet;
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
    return std::vector<char>();
}

void MyPacket::setPosition(double index, double size, std::vector<uint8_t>& value)
{
	try
	{
		if(size < 0)
		{
			GD::out.printError("Error: Negative size not allowed.");
			return;
		}
		double byteIndex = std::floor(index);
		if(byteIndex != index || size < 0.8) //0.8 == 8 Bits
		{
			if(value.empty()) value.push_back(0);
			int32_t intByteIndex = byteIndex;
			if(size > 1.0)
			{
				GD::out.printError("Error: Can't set partial byte index > 1.");
				return;
			}
			while((signed)_data.size() - 1 < intByteIndex)
			{
				_data.push_back(0);
			}
			_data.at(intByteIndex) |= value.at(value.size() - 1) << (std::lround(index * 10) % 10);
		}
		else
		{
			uint32_t intByteIndex = byteIndex;
			uint32_t bytes = (uint32_t)std::ceil(size);
			while(_data.size() < intByteIndex + bytes)
			{
				_data.push_back(0);
			}
			if(value.empty()) return;
			uint32_t bitSize = std::lround(size * 10) % 10;
			if(bitSize > 8) bitSize = 8;
			if(bytes == 0) bytes = 1; //size is 0 - assume 1
			//if(bytes > value.size()) bytes = value.size();
			if(bytes <= value.size())
			{
				_data.at(intByteIndex) |= value.at(0) & _bitmask[bitSize];
				for(uint32_t i = 1; i < bytes; i++)
				{
					_data.at(intByteIndex + i) |= value.at(i);
				}
			}
			else
			{
				uint32_t missingBytes = bytes - value.size();
				for(uint32_t i = 0; i < value.size(); i++)
				{
					_data.at(intByteIndex + missingBytes + i) |= value.at(i);
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

std::vector<uint8_t> MyPacket::getPosition(double index, double size, int32_t mask)
{
	std::vector<uint8_t> result;
	try
	{
		if(size < 0)
		{
			GD::out.printError("Error: Negative size not allowed.");
			result.push_back(0);
			return result;
		}
		if(index < 0)
		{
			GD::out.printError("Error: Packet index < 0 requested.");
			result.push_back(0);
			return result;
		}
		double byteIndex = std::floor(index);
		int32_t intByteIndex = byteIndex;
		if(byteIndex >= _data.size())
		{
			result.push_back(0);
			return result;
		}
		if(byteIndex != index || size < 0.8) //0.8 == 8 Bits
		{
			if(size > 1)
			{
				GD::out.printError("Error: Partial byte index > 1 requested.");
				result.push_back(0);
				return result;
			}
			//The round is necessary, because for example (uint32_t)(0.2 * 10) is 1
			uint32_t bitSize = std::lround(size * 10);
			if(bitSize > 8) bitSize = 8;
			result.push_back((_data.at(intByteIndex) >> (std::lround(index * 10) % 10)) & _bitmask[bitSize]);
		}
		else
		{
			uint32_t bytes = (uint32_t)std::ceil(size);
			uint32_t bitSize = std::lround(size * 10) % 10;
			if(bitSize > 8) bitSize = 8;
			if(bytes == 0) bytes = 1; //size is 0 - assume 1
			uint8_t currentByte = _data.at(intByteIndex) & _bitmask[bitSize];
			if(mask != -1 && bytes <= 4) currentByte &= (mask >> ((bytes - 1) * 8));
			result.push_back(currentByte);
			for(uint32_t i = 1; i < bytes; i++)
			{
				if((intByteIndex + i) >= _data.size()) result.push_back(0);
				else
				{
					currentByte = _data.at(intByteIndex + i);
					if(mask != -1 && bytes <= 4) currentByte &= (mask >> ((bytes - i - 1) * 8));
					result.push_back(currentByte);
				}
			}
		}
		if(result.empty()) result.push_back(0);
		return result;
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
    result.push_back(0);
    return result;
}

}
