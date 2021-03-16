/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H
#define HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H

#include "EnOceanPacket.h"

namespace EnOcean {

class ApplyChanges : public EnOceanPacket {
 public:
  explicit ApplyChanges(int32_t destinationAddress, bool applyLinkTableChanges, bool applyConfigurationChanges);
};

class GetDeviceConfiguration : public EnOceanPacket {
 public:
  GetDeviceConfiguration(int32_t destinationAddress, uint16_t startIndex, uint16_t endIndex, uint8_t length);
};

class Lock : public EnOceanPacket {
 public:
  Lock(int32_t destinationAddress, uint32_t securityCode);
};

class PingPacket : public EnOceanPacket {
 public:
  PingPacket(int32_t destinationAddress);
};

class QueryIdPacket : public EnOceanPacket {
 public:
  QueryIdPacket(int32_t destinationAddress);
};

class QueryStatusPacket : public EnOceanPacket {
 public:
  QueryStatusPacket(int32_t destinationAddress);
};

class SetDeviceConfiguration : public EnOceanPacket {
 public:
  SetDeviceConfiguration(int32_t destinationAddress, const std::map<uint32_t, std::vector<uint8_t>> &configuration);
};

class SetLinkTable : public EnOceanPacket {
 public:
  SetLinkTable(int32_t destinationAddress, bool inbound, const std::vector<uint8_t> &table);
};

class SetRepeaterFunctions : public EnOceanPacket {
 public:
  SetRepeaterFunctions(int32_t destinationAddress, uint8_t function, uint8_t level, uint8_t filterStructure);
};

class SetSecurityProfile : public EnOceanPacket {
 public:
  SetSecurityProfile(int32_t destinationAddress, bool fitIn22Byte, bool outbound, uint8_t index, uint8_t slf, uint32_t rlc, const std::vector<uint8_t> &aesKey, uint32_t destinationId, uint32_t sourceId);
};

class Unlock : public EnOceanPacket {
 public:
  Unlock(int32_t destinationAddress, uint32_t securityCode);
};

}

#endif //HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H
