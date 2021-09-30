/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_ENOCEAN_HGDC_H
#define HOMEGEAR_ENOCEAN_HGDC_H

#include "../EnOceanPacket.h"
#include "IEnOceanInterface.h"
#include <homegear-base/BaseLib.h>

namespace EnOcean {

class Hgdc : public IEnOceanInterface {
 public:
  Hgdc(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings, const std::string &firmwareVersion);
  ~Hgdc() override;

  void startListening() override;
  void stopListening() override;
  void init();

  void reset() override;

  int32_t setBaseAddress(uint32_t value) override;
  DutyCycleInfo getDutyCycleInfo() override;

  bool isOpen() override { return !_stopped && _initComplete; }
  std::string getSerialNumber() override { return _settings->serialNumber; }
  std::string getFirmwareVersion() override { return _firmwareVersion; }

  bool sendEnoceanPacket(const PEnOceanPacket &packet) override;
 protected:
  int32_t _packetReceivedEventHandlerId = -1;
  std::atomic_bool _initComplete{false};
  std::thread _initThread;
  std::string _firmwareVersion;

  void rawSend(std::vector<uint8_t> &packet) override;
  void processPacket(int64_t familyId, const std::string &serialNumber, const std::vector<uint8_t> &data);
};

}

#endif //HOMEGEAR_ENOCEAN_HOMEGEARGATEWAY_H
