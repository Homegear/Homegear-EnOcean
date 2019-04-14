/* Copyright 2013-2019 Homegear GmbH */

#include "MyPacket.h"

#include "GD.h"

namespace MyFamily
{
MyPacket::MyPacket()
{
}

MyPacket::MyPacket(std::vector<uint8_t>& espPacket) : _packet(espPacket)
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

MyPacket::MyPacket(Type type, uint8_t rorg, int32_t senderAddress, int32_t destinationAddress) : _type(type), _rorg(rorg)
{
	_senderAddress = senderAddress;
    _destinationAddress = ((destinationAddress & 0xFFFFFF80) == (senderAddress & 0xFFFFFF80) ? 0xFFFFFFFF : destinationAddress);
	_appendAddressAndStatus = true;
	_data.reserve(20);
	_data.push_back(rorg);
	if(type == Type::RADIO_ERP1) _optionalData = std::vector<uint8_t>{ 3, (uint8_t)((_destinationAddress >> 24) & 0xFF), (uint8_t)((_destinationAddress >> 16) & 0xFF), (uint8_t)((_destinationAddress >> 8) & 0xFF), (uint8_t)(_destinationAddress & 0xFF), 0, 0 };
	else if(type == Type::RADIO_ERP2) _optionalData = std::vector<uint8_t>{ 3, (uint8_t)0xFF };
}

MyPacket::~MyPacket()
{
	_packet.clear();
	_data.clear();
	_optionalData.clear();
}

std::vector<uint8_t> MyPacket::getBinary()
{
	try
	{
		if(!_packet.empty()) return _packet;
		if(_appendAddressAndStatus)
		{
			_data.push_back((uint8_t)(_senderAddress >> 24));
			_data.push_back((uint8_t)((_senderAddress >> 16) & 0xFF));
			_data.push_back((uint8_t)((_senderAddress >> 8) & 0xFF));
			_data.push_back((uint8_t)(_senderAddress & 0xFF));
			_data.push_back(_rorg == 0xF6 ? 0x30 : 0);
		}
		if(_data.empty() && _optionalData.empty()) return std::vector<uint8_t>();
		_packet.reserve(7 + _data.size() + _optionalData.size());
		_packet.push_back(0x55);
		_packet.push_back((uint8_t)(_data.size() >> 8));
		_packet.push_back((uint8_t)(_data.size() & 0xFF));
		_packet.push_back((uint8_t)_optionalData.size());
		_packet.push_back((uint8_t)_type);
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
    return std::vector<uint8_t>();
}

std::vector<uint8_t> MyPacket::getPosition(uint32_t position, uint32_t size)
{
	try
	{
		return BaseLib::BitReaderWriter::getPosition(_data, position, size);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::vector<uint8_t>();
}

void MyPacket::setPosition(uint32_t position, uint32_t size, const std::vector<uint8_t>& source)
{
	try
	{
		BaseLib::BitReaderWriter::setPosition(position, size, _data, source);
	}
	catch(const std::exception& ex)
	{
		GD::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

}
