/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_ENOCEAN_SRC_REMANFEATURES_H_
#define HOMEGEAR_ENOCEAN_SRC_REMANFEATURES_H_

#include <homegear-base/BaseLib.h>

namespace EnOcean {

struct RemanFeatures {
  /**
   * Max length of the data part of a telegram.
   */
  uint32_t kMaxDataLength = 1024;

  /**
   * Encode sender address in REMAN packet. For some devices this improves ACK times.
   */
  bool kAddressedRemanPackets = false;

  /**
   * When true, encryption is forced, i. e. the security table (function 0x216) is written.
   */
  bool kForceEncryption = false;

  /**
   * When true, a second base address with suffix 1 is placed into the link table. For this
   * address no security profile is created.
   */
  bool kUnencryptedUpdates = false;

  /**
   * The security level format as described in the security specification. 0xF3 is recommended.
   */
  uint8_t kSlf = 0xF3;

  /**
   * 0x11 for Version 1.1 or 0x13 for Version 1.3
   */
  uint32_t kRecomVersion = 0x13;

  /**
   * Maximum number of entries in the inbound link table.
   */
  uint32_t kInboundLinkTableSize = 0;

  /**
   * Maximum number of entries in the outbound link table.
   */
  uint32_t kOutboundLinkTableSize = 0;

  /**
   * In our opinion A53808 is the correct EEP here, but some devices require a different EEP.
   */
  uint32_t kLinkTableGatewayEep = 0xA53808;

  /**
   * Number of device configuration parameters.
   */
  uint32_t kDeviceConfigurationSize = 0;

  /**
   * Device can be used as meshing repeater.
   */
  bool kMeshingRepeater = false;

  /**
   * Device can be used as meshing endpoint.
   */
  bool kMeshingEndpoint = false;

  /**
   * Always use a repeater to reach this device.
   */
  bool kEnforceMeshing = false;

  /**
   * Device supports Homegear firmware updates.
   */
  bool kFirmwareUpdates = false;

  //{{{ Function support
  /**
   * 0x001
   */
  bool kUnlock = false;

  /**
   * 0x002
   */
  bool kLock = false;

  /**
   * 0x003
   */
  bool kSetCode = false;

  /**
   * 0x004
   */
  bool kQueryId = false;

  /**
   * 0x005
   */
  bool kAction = false;

  /**
   * 0x006
   */
  bool kPing = false;

  /**
   * 0x007
   */
  bool kQueryFunction = false;

  /**
   * 0x008
   */
  bool kQueryStatus = false;

  /**
   * 0x009
   */
  bool kStartSession = false;

  /**
   * 0x00A
   */
  bool kCloseSession = false;

  /**
   * 0x201
   */
  bool kRpcRemoteLearn = false;

  /**
   * 0x203
   */
  bool kRpcRemoteFlashWrite = false;

  /**
   * 0x204
   */
  bool kRpcRemoteFlashRead = false;

  /**
   * 0x205
   */
  bool kRpcSmartAckRead = false;

  /**
   * 0x206
   */
  bool kRpcSmartAckWrite = false;

  /**
   * 0x210
   */
  bool kGetLinkTableMetadata = false;

  /**
   * 0x211
   */
  bool kGetLinkTable = false;

  /**
   * 0x212
   */
  bool kSetLinkTable = false;

  /**
   * 0x213
   */
  bool kGetLinkTableGpEntry = false;

  /**
   * 0x214
   */
  bool kSetLinkTableGpEntry = false;

  /**
   * 0x215
   */
  bool kGetSecurityProfile = false;

  /**
   * 0x216
   */
  bool kSetSecurityProfile = false;

  /**
   * 0x220
   */
  bool kRemoteSetLearnMode = false;

  /**
   * 0x221
   */
  bool kTriggerOutboundRemoteTeachRequest = false;

  /**
   * 0x224
   */
  bool kResetToDefaults = false;

  /**
   * 0x225
   */
  bool kRadioLinkTestControl = false;

  /**
   * 0x226
   */
  bool kApplyChanges = false;

  /**
   * 0x227
   */
  bool kGetProductId = false;

  /**
   * 0x230
   */
  bool kGetDeviceConfiguration = false;

  /**
   * 0x231
   */
  bool kSetDeviceConfiguration = false;

  /**
   * 0x232
   */
  bool kGetLinkBasedConfiguration = false;

  /**
   * 0x233
   */
  bool kSetLinkBasedConfiguration = false;

  /**
   * 0x234
   */
  bool kGetDeviceSecurityInfo = false;

  /**
   * 0x235
   */
  bool kSetDeviceSecurityInfo = false;

  /**
   * 0x250
   */
  bool kGetRepeaterFunctions = false;

  /**
   * 0x251
   */
  bool kSetRepeaterFunctions = false;

  /**
   * 0x252
   */
  bool kSetRepeaterFilter = false;
  //}}}
};

typedef std::shared_ptr<RemanFeatures> PRemanFeatures;

class RemanFeatureParser {
 public:
  RemanFeatureParser() = delete;

  static PRemanFeatures parse(const PHomegearDevice &rpcDevice);
};

}

#endif //HOMEGEAR_ENOCEAN_SRC_REMANFEATURES_H_
