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
  virtual ~EnOcean();
  virtual void dispose();

  virtual bool hasPhysicalInterface() { return true; }
  virtual PVariable getPairingInfo();
 protected:
  virtual std::shared_ptr<BaseLib::Systems::ICentral> initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber);
  virtual void createCentral();
};

}

#endif
