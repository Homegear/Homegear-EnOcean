/* Copyright 2013-2019 Homegear GmbH */

#ifndef MYFAMILY_H_
#define MYFAMILY_H_

#include <homegear-base/BaseLib.h>

using namespace BaseLib;

namespace EnOcean {
class EnOceanCentral;

class EnOcean : public BaseLib::Systems::DeviceFamily {
 public:
  EnOcean(BaseLib::SharedObjects *bl, BaseLib::Systems::IFamilyEventSink *eventHandler);
  ~EnOcean() override;
  void dispose() override;

  bool hasPhysicalInterface() override { return true; }
  PVariable getPairingInfo() override;
 protected:
  std::shared_ptr<BaseLib::Systems::ICentral> initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber) override;
  void createCentral() override;
};

}

#endif
