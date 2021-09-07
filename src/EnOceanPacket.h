/* Copyright 2013-2019 Homegear GmbH */

#ifndef ENOCEANPACKET_H_
#define ENOCEANPACKET_H_

#include "Security.h"

#include <homegear-base/BaseLib.h>

namespace EnOcean {

class EnOceanPacket : public BaseLib::Systems::Packet {
 public:
  enum class Type : uint8_t {
    RESERVED = 0,
    RADIO_ERP1 = 1,
    RESPONSE = 2,
    RADIO_SUB_TEL = 3,
    EVENT = 4,
    COMMON_COMMAND = 5,
    SMART_ACK_COMMAND = 6,
    REMOTE_MAN_COMMAND = 7,
    RADIO_MESSAGE = 9,
    RADIO_ERP2 = 0xA,
    RADIO_802_15_4 = 0x10,
    COMMAND_2_4 = 0x11
  };

  enum class RemoteManagementFunction : uint16_t {
    unlock = 0x001,
    lock = 0x002,
    setCode = 0x003,
    queryId = 0x004,
    action = 0x005,
    ping = 0x006,
    queryFunction = 0x007,
    queryStatus = 0x008,
    startSession = 0x009,
    closeSession = 0x00A,
    rpcRemoteLearn = 0x201,
    rpcRemoteFlashWrite = 0x203,
    rpcRemoteFlashRead = 0x204,
    rpcSmartAckRead = 0x205,
    rpcSmartAckWrite = 0x206,
    getLinkTableMetadata = 0x210,
    getLinkTable = 0x211,
    setLinkTable = 0x212,
    getLinkTableGpEntry = 0x213,
    setLinkTableGpEntry = 0x214,
    getSecurityProfile = 0x215,
    setSecurityProfile = 0x216,
    remoteRpcLearnMode = 0x220,
    triggerOutboundRemoteTeachRequest = 0x221,
    resetToDefaults = 0x224,
    radioLinkTestControl = 0x225,
    applyChanges = 0x226,
    getProductId = 0x227,
    getDeviceConfiguration = 0x230,
    setDeviceConfiguration = 0x231,
    getLinkBasedConfiguration = 0x232,
    setLinkBasedConfiguration = 0x233,
    getDeviceSecurityInfo = 0x234,
    setDeviceSecurityInfo = 0x235,
    getRepeaterFunctions = 0x250,
    setRepeaterFunctions = 0x251,
    setRepeaterFilter = 0x252,
    getPathInfoThroughPing = 0x2A0
  };

  enum class RemoteManagementResponse : uint16_t {
    remoteCommissioningAck = 0x240,
    queryIdResponse = 0x604,
    queryIdResponseNew = 0x704,
    pingResponse = 0x606,
    queryStatusResponse = 0x608,
    getLinkTableMetadataResponse = 0x810,
    getLinkTableResponse = 0x811,
    getLinkTableGpEntryResponse = 0x813,
    getSecurityProfileResponse = 0x815,
    getDeviceConfigurationResponse = 0x830,
    getLinkBasedConfigurationResponse = 0x832,
    getDeviceSecurityInfoResponse = 0x834,
    getProductIdResponse = 0x827,
    getPathInfoThroughPingResponse = 0x8A0
  };

  enum class QueryStatusReturnCode : uint8_t {
    ok = 0x00,
    wrongTargetId = 0x01,
    wrongUnlockCode = 0x02,
    wrongEep = 0x03,
    wrongManufacturerId = 0x04,
    wrongDataSize = 0x05,
    noCodeSet = 0x06,
    notSent = 0x07,
    rpcFailed = 0x08,
    messageTimeout = 0x09,
    tooLongMessage = 0x0A,
    messagePartAlreadyReceived = 0x0B,
    messagePartNotReceived = 0x0C,
    addressOutOfRange = 0x0D,
    codeDataSizeExceeded = 0x0E,
    wrongData = 0x0F
  };

  enum class RepeatingStatus : uint8_t {
    kOriginal = 0,
    kRepeatedOnce = 1,
    kRepeatedTwice = 2,
    kRepeatingDisabled = 0x0F
  };

  EnOceanPacket();
  explicit EnOceanPacket(const std::vector<uint8_t> &espPacket);
  EnOceanPacket(Type type, uint8_t rorg, int32_t senderAddress, int32_t destinationAddress, const std::vector<uint8_t> &data = std::vector<uint8_t>());
  ~EnOceanPacket() override;

  int32_t senderAddress() { return _senderAddress; }
  int32_t destinationAddress() { return _destinationAddress; }
  Type getType() { return _type; }
  uint8_t getRorg() { return _rorg; }
  int32_t getRssi() { return _rssi; }
  uint8_t getStatus() { return _status; }
  RepeatingStatus getRepeatingStatus() { return _repeatingStatus; }
  uint16_t getRemoteManagementFunction() { return _remoteManagementFunction; }
  uint16_t getRemoteManagementManufacturer() { return _remoteManagementManufacturer; }
  std::vector<uint8_t> getData() { return _data; }
  void setData(const std::vector<uint8_t> &value, uint32_t offset = 0);
  int32_t getDataSize() { return _data.size(); }
  std::vector<uint8_t> getOptionalData() { return _optionalData; }
  std::vector<uint8_t> getBinary();

  std::vector<uint8_t> getPosition(uint32_t position, uint32_t size);
  void setPosition(uint32_t position, uint32_t size, const std::vector<uint8_t> &source);
 protected:
  bool _appendAddressAndStatus = false;
  std::vector<uint8_t> _packet;
  int32_t _senderAddress = 0;
  int32_t _destinationAddress = 0;
  Type _type = Type::RESERVED;
  int32_t _rssi = 0;
  uint8_t _rorg = 0;
  uint8_t _status = 0;
  RepeatingStatus _repeatingStatus = RepeatingStatus::kOriginal;
  uint16_t _remoteManagementFunction = 0;
  uint16_t _remoteManagementManufacturer = 0;
  std::vector<uint8_t> _data;
  std::vector<uint8_t> _optionalData;
};

typedef std::shared_ptr<EnOceanPacket> PEnOceanPacket;

}
#endif
