/* Copyright 2013-2019 Homegear GmbH */

#include "GD.h"

namespace EnOcean
{
	BaseLib::SharedObjects* GD::bl = nullptr;
	EnOcean* GD::family = nullptr;
	std::map<std::string, std::shared_ptr<IEnOceanInterface>> GD::physicalInterfaces;
	std::shared_ptr<IEnOceanInterface> GD::defaultPhysicalInterface;
	BaseLib::Output GD::out;
}
