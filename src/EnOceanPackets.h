/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H
#define HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H

#include "EnOceanPacket.h"

namespace EnOcean {

class ApplyChanges : public EnOceanPacket {
 public:
  explicit ApplyChanges(uint32_t senderAddress, uint32_t destinationAddress, bool applyLinkTableChanges, bool applyConfigurationChanges);
};

class GetDeviceConfiguration : public EnOceanPacket {
 public:
  GetDeviceConfiguration(uint32_t senderAddress, uint32_t destinationAddress, uint16_t startIndex, uint16_t endIndex, uint8_t length);
};

class GetLinkTable : public EnOceanPacket {
 public:
  GetLinkTable(uint32_t sender_address, uint32_t destination_address, bool inbound, uint8_t start_index, uint8_t end_index);
};

class GetPathInfoThroughPing : public EnOceanPacket {
 public:
  GetPathInfoThroughPing(uint32_t senderAddress, uint32_t destinationAddress, uint32_t destinationPingDeviceId);
};

class Lock : public EnOceanPacket {
 public:
  Lock(uint32_t senderAddress, uint32_t destinationAddress, uint32_t securityCode);
};

class PingPacket : public EnOceanPacket {
 public:
  PingPacket(uint32_t sender_address, uint32_t destination_address);
};

class QueryIdPacket : public EnOceanPacket {
 public:
  QueryIdPacket(uint32_t senderAddress, uint32_t destinationAddress);
};

class QueryStatusPacket : public EnOceanPacket {
 public:
  QueryStatusPacket(uint32_t senderAddress, uint32_t destinationAddress);
};

class SetCode : public EnOceanPacket {
 public:
  SetCode(uint32_t senderAddress, uint32_t destinationAddress, uint32_t securityCode);
};

class SetDeviceConfiguration : public EnOceanPacket {
 public:
  SetDeviceConfiguration(uint32_t senderAddress, uint32_t destinationAddress, const std::map<uint32_t, std::vector<uint8_t>> &configuration);
};

class SetLinkTable : public EnOceanPacket {
 public:
  SetLinkTable(uint32_t senderAddress, uint32_t destinationAddress, bool inbound, const std::vector<uint8_t> &table);
};

class SetRepeaterFilter : public EnOceanPacket {
 public:
  SetRepeaterFilter(uint32_t sender_address, uint32_t destination_address, uint8_t filter_control, uint8_t filter_type, uint32_t filter_value);
};

class SetRepeaterFunctions : public EnOceanPacket {
 public:
  SetRepeaterFunctions(uint32_t senderAddress, uint32_t destinationAddress, uint8_t function, uint8_t level, uint8_t filterStructure);
};

class SetSecurityProfile : public EnOceanPacket {
 public:
  SetSecurityProfile(uint32_t senderAddress, uint32_t destinationAddress, bool recomVersion11, bool hasAddresses, bool outbound, uint8_t index, uint8_t slf, uint32_t rlc, const std::vector<uint8_t> &aesKey, uint32_t destinationId = 0, uint32_t sourceId = 0);
};

class Unlock : public EnOceanPacket {
 public:
  Unlock(uint32_t senderAddress, uint32_t destinationAddress, uint32_t securityCode);
};

}

#endif //HOMEGEAR_ENOCEAN_ENOCEANPACKETS_H
