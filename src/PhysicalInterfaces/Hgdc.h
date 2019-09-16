/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_ENOCEAN_HGDC_H
#define HOMEGEAR_ENOCEAN_HGDC_H

#include "../EnOceanPacket.h"
#include "IEnOceanInterface.h"
#include <homegear-base/BaseLib.h>

namespace EnOcean
{

class Hgdc : public IEnOceanInterface
{
public:
    explicit Hgdc(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
    virtual ~Hgdc();

    virtual void startListening();
    virtual void stopListening();
    void init();

    virtual int32_t setBaseAddress(uint32_t value);

    virtual bool isOpen() { return !_stopped && _initComplete; }

    virtual void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet);
protected:
    int32_t _packetReceivedEventHandlerId = -1;
    int32_t _reconnectedEventHandlerId = -1;
    std::atomic_bool _initComplete;
    std::thread _initThread;

    virtual void rawSend(std::vector<uint8_t>& packet);
    void processPacket(int64_t familyId, const std::string& serialNumber, const std::vector<uint8_t>& data);
    void reconnected();
};

}

#endif //HOMEGEAR_ENOCEAN_HOMEGEARGATEWAY_H
