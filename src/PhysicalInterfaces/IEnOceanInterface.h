/* Copyright 2013-2019 Homegear GmbH */

#ifndef IENOCEANINTERFACE_H_
#define IENOCEANINTERFACE_H_

#include <homegear-base/BaseLib.h>
#include <queue>
#include "../EnOceanPacket.h"

namespace EnOcean {

class IEnOceanInterface : public BaseLib::Systems::IPhysicalInterface {
 public:
  enum class EnOceanRequestFilterType {
    senderAddress,
    remoteManagementFunction
  };

  explicit IEnOceanInterface(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
  ~IEnOceanInterface() override;

  int32_t getAddress() override { return _baseAddress; }
  uint32_t getBaseAddress() const { return _baseAddress; }
  int32_t getRssi(int32_t address, bool wildcardPeer);
  void decrementRssi(uint32_t address, bool wildcardPeer);
  virtual int32_t setBaseAddress(uint32_t value) { return -1; };

  void startListening() override {}
  void stopListening() override {}

  void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet) override { throw BaseLib::Exception("Not implemented."); }

  virtual bool sendEnoceanPacket(const PEnOceanPacket &packet);

  PEnOceanPacket sendAndReceivePacket(const PEnOceanPacket &packet,
                                      uint32_t retries = 0,
                                      EnOceanRequestFilterType filterType = EnOceanRequestFilterType::senderAddress,
                                      const std::vector<std::vector<uint8_t>> &filterData = std::vector<std::vector<uint8_t>>());

  PEnOceanPacket sendAndReceivePacket(const std::vector<PEnOceanPacket> &packets,
                                      uint32_t retries = 0,
                                      EnOceanRequestFilterType filterType = EnOceanRequestFilterType::senderAddress,
                                      const std::vector<std::vector<uint8_t>> &filterData = std::vector<std::vector<uint8_t>>());
 protected:
  struct SerialRequest {
    std::mutex mutex;
    std::condition_variable conditionVariable;
    bool mutexReady = false;
    std::vector<uint8_t> response;
  };

  struct EnOceanRequest {
    EnOceanRequestFilterType filterType = EnOceanRequestFilterType::senderAddress;

    std::vector<std::vector<uint8_t>> filterData;

    std::mutex mutex;
    std::condition_variable conditionVariable;
    bool mutexReady = false;
    PEnOceanPacket response;
  };

  const uint8_t _crc8Table[256] = {
      0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
      0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
      0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
      0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
      0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
      0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
      0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
      0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
      0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
      0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
      0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
      0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
      0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
      0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
      0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
      0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
      0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
      0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
      0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
      0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
      0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
      0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
      0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
      0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
      0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
      0x76, 0x71, 0x78, 0x7f, 0x6A, 0x6d, 0x64, 0x63,
      0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
      0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
      0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
      0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8D, 0x84, 0x83,
      0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
      0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
  };

  struct DeviceInfo {
    //std::queue<int64_t> packetReceivedTimes;
    int32_t rssi = 0;
  };

  std::map<uint8_t, std::string> _responseStatusCodes;

  BaseLib::SharedObjects *_bl = nullptr;
  BaseLib::Output _out;
  std::atomic<uint32_t> _baseAddress{0};

  std::mutex _sendPacketMutex;
  std::mutex _getResponseMutex;

  std::mutex _serialRequestsMutex;
  std::unordered_map<uint32_t, std::shared_ptr<SerialRequest>> _serialRequests;

  std::mutex _enoceanRequestsMutex;
  std::unordered_map<uint8_t, std::shared_ptr<EnOceanRequest>> _enoceanRequests;

  std::mutex _rssiMutex;
  std::unordered_map<uint32_t, DeviceInfo> _wildcardRssi;
  std::unordered_map<uint32_t, DeviceInfo> _rssi;

  std::mutex _rawSendMutex;
  uint64_t _lastRawPacketSent = 0;

  void getResponse(uint8_t packetType, std::vector<uint8_t> &requestPacket, std::vector<uint8_t> &responsePacket);
  bool checkForSerialRequest(const std::vector<uint8_t> &packet);
  bool checkForEnOceanRequest(PEnOceanPacket &packet);
  virtual void rawSend(std::vector<uint8_t> &packet);
  void addCrc8(std::vector<uint8_t> &packet);

  void raisePacketReceived(std::shared_ptr<BaseLib::Systems::Packet> packet) override;
};

}

#endif
