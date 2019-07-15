/* Copyright 2013-2019 Homegear GmbH */

#ifndef GD_H_
#define GD_H_

#define MY_FAMILY_ID 15
#define MY_FAMILY_NAME "EnOcean"

#include <homegear-base/BaseLib.h>
#include "EnOcean.h"
#include "PhysicalInterfaces/IEnOceanInterface.h"

namespace EnOcean
{

class GD
{
public:
	virtual ~GD();

	static BaseLib::SharedObjects* bl;
	static EnOcean* family;
	static std::map<std::string, std::shared_ptr<IEnOceanInterface>> physicalInterfaces;
	static std::shared_ptr<IEnOceanInterface> defaultPhysicalInterface;
	static BaseLib::Output out;
private:
	GD();
};

}

#endif /* GD_H_ */
