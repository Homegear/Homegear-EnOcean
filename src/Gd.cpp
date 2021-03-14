/* Copyright 2013-2019 Homegear GmbH */

#include "Gd.h"

namespace EnOcean {
BaseLib::SharedObjects *Gd::bl = nullptr;
EnOcean *Gd::family = nullptr;
std::shared_ptr<Interfaces> Gd::interfaces;
BaseLib::Output Gd::out;
}
