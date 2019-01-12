/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef MYFAMILY_H_
#define MYFAMILY_H_

#include <homegear-base/BaseLib.h>

using namespace BaseLib;

namespace MyFamily
{
class MyCentral;

class MyFamily : public BaseLib::Systems::DeviceFamily
{
public:
	MyFamily(BaseLib::SharedObjects* bl, BaseLib::Systems::IFamilyEventSink* eventHandler);
	virtual ~MyFamily();
	virtual void dispose();

	virtual bool hasPhysicalInterface() { return true; }
	virtual PVariable getPairingInfo();
protected:
	virtual std::shared_ptr<BaseLib::Systems::ICentral> initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber);
	virtual void createCentral();
};

}

#endif
