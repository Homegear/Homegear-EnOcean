/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef USB300_H_
#define USB300_H_

#include "../MyPacket.h"
#include "IEnOceanInterface.h"
#include <homegear-base/BaseLib.h>

namespace MyFamily
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
	bool _initComplete = false;
	std::thread _initThread;

	void init();
	void reconnect();
	void listen();
	virtual void rawSend(const std::vector<char>& packet);
	void processPacket(std::vector<char>& data);
};

}

#endif
