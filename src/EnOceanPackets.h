/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H
#define HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H

#include "EnOceanPacket.h"

namespace EnOcean
{

class Lock : public EnOceanPacket
{
public:
    Lock(int32_t destinationAddress, uint32_t securityCode);
};

class PingPacket : public EnOceanPacket
{
public:
    PingPacket(int32_t destinationAddress);
};

class QueryIdPacket : public EnOceanPacket
{
public:
    QueryIdPacket(int32_t destinationAddress);
};

class QueryStatusPacket : public EnOceanPacket
{
public:
    QueryStatusPacket(int32_t destinationAddress);
};

class SetLinkTable : public EnOceanPacket
{
public:
    SetLinkTable(int32_t destinationAddress, bool inbound, const std::vector<uint8_t>& table);
};

class SetRepeaterFunctions : public EnOceanPacket
{
public:
    SetRepeaterFunctions(int32_t destinationAddress, uint8_t function, uint8_t level, uint8_t filterStructure);
};

class Unlock : public EnOceanPacket
{
public:
    Unlock(int32_t destinationAddress, uint32_t securityCode);
};

}

#endif //HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H
