/* Copyright 2013-2019 Homegear GmbH */

#ifndef USB300_H_
#define USB300_H_

#include "../EnOceanPacket.h"
#include "IEnOceanInterface.h"
#include <homegear-base/BaseLib.h>

namespace EnOcean {

class Usb300 : public IEnOceanInterface {
 public:
  explicit Usb300(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
  ~Usb300() override;

  void startListening() override;
  void stopListening() override;
  void setup(int32_t userID, int32_t groupID, bool setPermissions) override;

  int32_t setBaseAddress(uint32_t value) override;
  DutyCycleInfo getDutyCycleInfo() override;

  bool isOpen() override { return _serial && _serial->isOpen() && !_stopped; }

  bool sendEnoceanPacket(const PEnOceanPacket &packet) override;
 protected:
  std::unique_ptr<BaseLib::SerialReaderWriter> _serial;
  std::atomic_bool _initComplete;
  std::thread _initThread;

  void init();
  void reconnect();
  void listen();
  void rawSend(std::vector<uint8_t> &packet) override;
  void processPacket(std::vector<uint8_t> &data);
};

}

#endif
