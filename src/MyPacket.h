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
        MyPacket(Type type, uint8_t rorg, int32_t senderAddress);
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
