/* Copyright 2013-2019 Homegear GmbH */

#include "GD.h"

namespace EnOcean
{
	BaseLib::SharedObjects* GD::bl = nullptr;
	EnOcean* GD::family = nullptr;
    std::shared_ptr<Interfaces> GD::interfaces;
	BaseLib::Output GD::out;
}
