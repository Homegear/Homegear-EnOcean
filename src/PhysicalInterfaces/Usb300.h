/* Copyright 2013-2019 Homegear GmbH */

#ifndef USB300_H_
#define USB300_H_

#include "../EnOceanPacket.h"
#include "IEnOceanInterface.h"
#include <homegear-base/BaseLib.h>

namespace EnOcean
{

class Usb300 : public IEnOceanInterface
{
public:
	Usb300(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
	virtual ~Usb300();

	virtual void startListening();
	virtual void stopListening();
	virtual void setup(int32_t userID, int32_t groupID, bool setPermissions);

	virtual int32_t setBaseAddress(uint32_t value);

	virtual bool isOpen() { return _serial && _serial->isOpen() && !_stopped; }

	virtual void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet);
protected:
	std::unique_ptr<BaseLib::SerialReaderWriter> _serial;
    std::atomic_bool _initComplete;
	std::thread _initThread;

	void init();
	void reconnect();
	void listen();
	virtual void rawSend(std::vector<uint8_t>& packet);
	void processPacket(std::vector<uint8_t>& data);
};

}

#endif
