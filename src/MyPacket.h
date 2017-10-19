/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef MYPACKET_H_
#define MYPACKET_H_

#include <homegear-base/BaseLib.h>

namespace MyFamily
{

class MyPacket : public BaseLib::Systems::Packet
{
    public:
		enum class Type : uint8_t
		{
			RESERVED = 0,
			RADIO_ERP1 = 1,
			RESPONSE = 2,
			RADIO_SUB_TEL = 3,
			EVENT = 4,
			COMMON_COMMAND = 5,
			SMART_ACK_COMMAND = 6,
			REMOTE_MAN_COMMAND = 7,
			RADIO_MESSAGE = 9,
			RADIO_ERP2 = 0xA,
			RADIO_802_15_4 = 0x10,
			COMMAND_2_4 = 0x11
		};

        MyPacket();
        MyPacket(std::vector<char>& espPacket);
        MyPacket(Type type, uint8_t rorg, int32_t senderAddress, int32_t destinationAddress);
        virtual ~MyPacket();

        Type getType() { return _type; }
        uint8_t getRorg() { return _rorg; }
        int32_t getRssi() { return _rssi; }
        std::vector<char> getData() { return _data; }
        void setData(std::vector<char>& value) { _data = value; _packet.clear(); if(!_data.empty()) _rorg = (uint8_t)_data[0]; }
        int32_t getDataSize() { return _data.size(); }
        std::vector<char> getOptionalData() { return _optionalData; }
        std::vector<char> getBinary();

        std::vector<uint8_t> getPosition(uint32_t position, uint32_t size);
		void setPosition(uint32_t position, uint32_t size, const std::vector<uint8_t>& source);
    protected:
		bool _appendAddressAndStatus = false;
        std::vector<char> _packet;
        Type _type = Type::RESERVED;
        int32_t _rssi = 0;
        uint8_t _rorg = 0;
        std::vector<char> _data;
        std::vector<char> _optionalData;
};

typedef std::shared_ptr<MyPacket> PMyPacket;

}
#endif
