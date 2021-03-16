/* Copyright 2013-2019 Homegear GmbH */

#ifndef MYPEER_H_
#define MYPEER_H_

#include "PhysicalInterfaces/IEnOceanInterface.h"
#include "EnOceanPacket.h"
#include "Security.h"
#include "RemanFeatures.h"
#include <homegear-base/BaseLib.h>

using namespace BaseLib;
using namespace BaseLib::DeviceDescription;

namespace EnOcean {
class EnOceanCentral;

class EnOceanPeer : public BaseLib::Systems::Peer, public BaseLib::Rpc::IWebserverEventSink {
 public:
  EnOceanPeer(uint32_t parentID, IPeerEventSink *eventHandler);
  EnOceanPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink *eventHandler);
  ~EnOceanPeer() override;
  void init();
  void dispose() override;

  //Features
  bool wireless() override { return true; }
  //End features

  //{{{ In table variables
  std::string getPhysicalInterfaceId();
  void setPhysicalInterfaceId(std::string);
  uint32_t getGatewayAddress();
  void setGatewayAddress(uint32_t value);
  void setRollingCodeInbound(int32_t value) {
    _rollingCodeInbound = value;
    saveVariable(29, value);
  }
  void setRollingCodeOutbound(int32_t value) {
    _rollingCodeOutbound = value;
    saveVariable(20, value);
  }
  void setAesKeyInbound(const std::vector<uint8_t> &value) {
    _aesKeyInbound = value;
    saveVariable(28, _aesKeyInbound);
  }
  void setAesKeyOutbound(const std::vector<uint8_t> &value) {
    _aesKeyOutbound = value;
    saveVariable(21, _aesKeyOutbound);
  }
  void setEncryptionType(int32_t value) {
    _encryptionType = value;
    saveVariable(22, value);
  }
  void setCmacSize(int32_t value) {
    _cmacSize = value;
    saveVariable(23, value);
  }
  void setExplicitRollingCode(bool value) {
    _explicitRollingCode = value;
    saveVariable(24, value);
  }
  void setRollingCodeSize(int32_t value) {
    _rollingCodeSize = value;
    saveVariable(25, value);
  }
  //}}}

  std::shared_ptr<IEnOceanInterface> getPhysicalInterface();

  bool hasRfChannel(int32_t channel);
  int32_t getRfChannel(int32_t channel);
  std::vector<int32_t> getRfChannels();
  void setRfChannel(int32_t channel, int32_t value);
  PRemanFeatures getRemanFeatures();

  void worker();
  std::string handleCliCommand(std::string command) override;
  void packetReceived(PEnOceanPacket &packet);

  bool load(BaseLib::Systems::ICentral *central) override;
  void serializePeers(std::vector<uint8_t> &encodedData);
  void unserializePeers(const std::vector<char> &serializedData);
  void savePeers() override;
  void initializeCentralConfig() override;

  void addPeer(int32_t channel, std::shared_ptr<BaseLib::Systems::BasicPeer> peer);
  void removePeer(int32_t channel, int32_t address, int32_t remoteChannel);
  uint32_t getLinkCount();
  int32_t getChannelGroupedWith(int32_t channel) override { return -1; }
  int32_t getNewFirmwareVersion() override { return 0; }
  std::string getFirmwareVersionString(int32_t firmwareVersion) override { return "1.0"; }
  bool firmwareUpdateAvailable() override { return false; }

  bool isWildcardPeer() { return _rpcDevice->addressSize == 25; }

  std::string printConfig();

  /**
   * {@inheritDoc}
   */
  void homegearStarted() override;

  /**
   * {@inheritDoc}
   */
  void homegearShuttingDown() override;

  void queueSetDeviceConfiguration(const std::map<uint32_t, std::vector<uint8_t>> &updatedParameters);
  void queueGetDeviceConfiguration();
  bool getDeviceConfiguration();
  bool setDeviceConfiguration(const std::map<uint32_t, std::vector<uint8_t>> &updatedParameters);
  bool sendInboundLinkTable();
  bool sendPing();
  bool setRepeaterFunctions(uint8_t function, uint8_t level, uint8_t structure);

  //RPC methods
  PVariable forceConfigUpdate(PRpcClientInfo clientInfo) override;
  PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields) override;
  PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing) override;
  PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, std::string interfaceId) override;
  PVariable setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait) override;
  //End RPC methods
 protected:
  class FrameValue {
   public:
    std::list<uint32_t> channels;
    std::vector<uint8_t> value;
  };

  class FrameValues {
   public:
    std::string frameID;
    std::list<uint32_t> paramsetChannels;
    ParameterGroup::Type::Enum parameterSetType;
    std::map<std::string, FrameValue> values;
  };

  class RpcRequest {
   public:
    std::atomic_bool abort;
    std::mutex conditionVariableMutex;
    std::condition_variable conditionVariable;
    std::string responseId;

    std::atomic_bool wait;
    PEnOceanPacket packet;
    uint32_t maxResends = 0;
    uint32_t resends = 0;
    uint32_t resendTimeout = 0;
    int64_t lastResend = 0;

    RpcRequest() : abort(false), wait(true) {}
  };
  typedef std::shared_ptr<RpcRequest> PRpcRequest;

  //In table variables:
  std::string _physicalInterfaceId;
  std::atomic<uint32_t> _rollingCodeOutbound{0xFFFFFFFF};
  std::atomic<uint32_t> _rollingCodeInbound{0xFFFFFFFF};
  std::vector<uint8_t> _aesKeyInbound;
  std::vector<uint8_t> _aesKeyOutbound;
  int32_t _encryptionType = -1;
  int32_t _cmacSize = -1;
  bool _explicitRollingCode = false;
  int32_t _rollingCodeSize = -1;
  uint32_t _gatewayAddress = 0;
  //End

  uint32_t _lastRssiDevice = 0;
  bool _globalRfChannel = false;
  std::mutex _rfChannelsMutex;
  std::unordered_map<int32_t, int32_t> _rfChannels;
  PRemanFeatures _remanFeatures;

  PEnOceanPacket _lastPacket;

  bool _forceEncryption = false;
  PSecurity _security;
  std::vector<uint8_t> _aesKeyPart1;

  // {{{ Variables for getting RPC responses to requests
  std::mutex _rpcRequestsMutex;
  std::unordered_map<std::string, PRpcRequest> _rpcRequests;
  // }}}

  // {{{ Variables for keep alives / pinging
  std::atomic<int64_t> _lastPing{0};
  std::atomic<int64_t> _pingInterval = 0;
  // }}}

  // {{{ Variables for blinds
  std::atomic<int32_t> _blindTransitionTime{-1};
  std::atomic<int64_t> _blindStateResetTime{-1};
  std::atomic_bool _blindUp{false};
  std::atomic<int64_t> _lastBlindPositionUpdate{0};
  std::atomic<int64_t> _lastRpcBlindPositionUpdate{0};
  std::atomic<int64_t> _blindCurrentTargetPosition{0};
  std::atomic<int64_t> _blindCurrentTransitionTime{0};
  std::atomic<int32_t> _blindPosition{0};
  // }}}

  // {{{ Remote management variables
  std::mutex _updatedParametersMutex;
  std::map<uint32_t, std::vector<uint8_t>> _updatedParameters;
  std::atomic_bool _remoteManagementQueueGetDeviceConfiguration{false};
  std::atomic_bool _remoteManagementQueueSetDeviceConfiguration{false};
  // }}}

  void loadVariables(BaseLib::Systems::ICentral *central, std::shared_ptr<BaseLib::Database::DataTable> &rows) override;
  void saveVariables() override;
  void loadUpdatedParameters(const std::vector<char> &encodedData);
  void saveUpdatedParameters();

  void setBestInterface();

  void setRssiDevice(uint8_t rssi);

  std::shared_ptr<BaseLib::Systems::ICentral> getCentral() override;

  bool remoteManagementUnlock();
  void remoteManagementLock();
  void remoteManagementApplyChanges(bool applyLinkTableChanges = true, bool applyConfigurationChanges = true);

  void getValuesFromPacket(PEnOceanPacket packet, std::vector<FrameValues> &frameValue);

  PParameterGroup getParameterSet(int32_t channel, ParameterGroup::Type::Enum type) override;

  bool sendPacket(const PEnOceanPacket &packet, const std::string &responseId, int32_t delay, bool wait);

  void updateBlindSpeed();

  void updateBlindPosition();

  // {{{ Hooks
  /**
   * {@inheritDoc}
   */
  bool getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters) override;

  /**
   * {@inheritDoc}
   */
  bool getParamsetHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters) override;
  // }}}
};

typedef std::shared_ptr<EnOceanPeer> PMyPeer;

}

#endif
