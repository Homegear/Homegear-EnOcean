/* Copyright 2013-2019 Homegear GmbH */

#include "RemanFeatures.h"
#include "Gd.h"

namespace EnOcean {

PRemanFeatures RemanFeatureParser::parse(const PHomegearDevice &rpcDevice) {
  try {
    auto features = std::make_shared<RemanFeatures>();
    if (rpcDevice->metadata) {
      auto metadataIterator = rpcDevice->metadata->structValue->find("remoteManagementInfo");
      if (metadataIterator != rpcDevice->metadata->structValue->end() && !metadataIterator->second->arrayValue->empty()) {
        auto remoteManagementInfo = metadataIterator->second->arrayValue->at(0);
        auto infoIterator = remoteManagementInfo->structValue->find("features");
        if (infoIterator != metadataIterator->second->structValue->end() && !infoIterator->second->arrayValue->empty()) {
          auto featureStruct = infoIterator->second->arrayValue->at(0);

          auto featureIterator = featureStruct->structValue->find("maxDataLength");
          if (featureIterator != featureStruct->structValue->end()) features->kMaxDataLength = featureIterator->second->integerValue;

          featureIterator = featureStruct->structValue->find("addressedRemanPackets");
          if (featureIterator != featureStruct->structValue->end()) features->kAddressedRemanPackets = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("forceEncryption");
          if (featureIterator != featureStruct->structValue->end()) features->kForceEncryption = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("slf");
          if (featureIterator != featureStruct->structValue->end()) features->kSlf = featureIterator->second->integerValue;

          featureIterator = featureStruct->structValue->find("recomVersion");
          if (featureIterator != featureStruct->structValue->end()) features->kRecomVersion = (featureIterator->second->stringValue == "v1.1" ? 0x11 : 0x13);

          featureIterator = featureStruct->structValue->find("inboundLinkTableSize");
          if (featureIterator != featureStruct->structValue->end()) features->kInboundLinkTableSize = featureIterator->second->integerValue;

          featureIterator = featureStruct->structValue->find("outboundLinkTableSize");
          if (featureIterator != featureStruct->structValue->end()) features->kSetOutboundLinkTableSize = featureIterator->second->integerValue;

          featureIterator = featureStruct->structValue->find("linkTableGatewayEep");
          if (featureIterator != featureStruct->structValue->end()) features->kLinkTableGatewayEep = BaseLib::Math::getUnsignedNumber(featureIterator->second->stringValue, true);

          featureIterator = featureStruct->structValue->find("deviceConfigurationSize");
          if (featureIterator != featureStruct->structValue->end()) features->kDeviceConfigurationSize = featureIterator->second->integerValue;

          featureIterator = featureStruct->structValue->find("unlock");
          if (featureIterator != featureStruct->structValue->end()) features->kUnlock = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("lock");
          if (featureIterator != featureStruct->structValue->end()) features->kLock = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setCode");
          if (featureIterator != featureStruct->structValue->end()) features->kSetCode = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("queryId");
          if (featureIterator != featureStruct->structValue->end()) features->kQueryId = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("action");
          if (featureIterator != featureStruct->structValue->end()) features->kAction = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("ping");
          if (featureIterator != featureStruct->structValue->end()) features->kPing = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("queryFunction");
          if (featureIterator != featureStruct->structValue->end()) features->kQueryFunction = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("queryStatus");
          if (featureIterator != featureStruct->structValue->end()) features->kQueryStatus = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("startSession");
          if (featureIterator != featureStruct->structValue->end()) features->kStartSession = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("closeSession");
          if (featureIterator != featureStruct->structValue->end()) features->kCloseSession = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("rpcRemoteLearn");
          if (featureIterator != featureStruct->structValue->end()) features->kRpcRemoteLearn = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("rpcRemoteFlashWrite");
          if (featureIterator != featureStruct->structValue->end()) features->kRpcRemoteFlashWrite = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("rpcRemoteFlashRead");
          if (featureIterator != featureStruct->structValue->end()) features->kRpcRemoteFlashRead = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("rpcSmartAckRead");
          if (featureIterator != featureStruct->structValue->end()) features->kRpcSmartAckRead = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("rpcSmartAckWrite");
          if (featureIterator != featureStruct->structValue->end()) features->kRpcSmartAckWrite = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getLinkTableMetadata");
          if (featureIterator != featureStruct->structValue->end()) features->kGetLinkTableMetadata = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getLinkTable");
          if (featureIterator != featureStruct->structValue->end()) features->kGetLinkTable = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setLinkTable");
          if (featureIterator != featureStruct->structValue->end()) features->kSetLinkTable = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getLinkTableGpEntry");
          if (featureIterator != featureStruct->structValue->end()) features->kGetLinkTableGpEntry = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setLinkTableGpEntry");
          if (featureIterator != featureStruct->structValue->end()) features->kSetLinkTableGpEntry = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getSecurityProfile");
          if (featureIterator != featureStruct->structValue->end()) features->kGetSecurityProfile = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setSecurityProfile");
          if (featureIterator != featureStruct->structValue->end()) features->kSetSecurityProfile = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("remoteSetLearnMode");
          if (featureIterator != featureStruct->structValue->end()) features->kRemoteSetLearnMode = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("triggerOutboundRemoteTeachRequest");
          if (featureIterator != featureStruct->structValue->end()) features->kTriggerOutboundRemoteTeachRequest = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("resetToDefaults");
          if (featureIterator != featureStruct->structValue->end()) features->kResetToDefaults = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("radioLinkTestControl");
          if (featureIterator != featureStruct->structValue->end()) features->kRadioLinkTestControl = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("applyChanges");
          if (featureIterator != featureStruct->structValue->end()) features->kApplyChanges = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getProductId");
          if (featureIterator != featureStruct->structValue->end()) features->kGetProductId = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getDeviceConfiguration");
          if (featureIterator != featureStruct->structValue->end()) features->kGetDeviceConfiguration = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setDeviceConfiguration");
          if (featureIterator != featureStruct->structValue->end()) features->kSetDeviceConfiguration = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getLinkBasedConfiguration");
          if (featureIterator != featureStruct->structValue->end()) features->kGetLinkBasedConfiguration = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setLinkBasedConfiguration");
          if (featureIterator != featureStruct->structValue->end()) features->kSetLinkBasedConfiguration = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getDeviceSecurityInfo");
          if (featureIterator != featureStruct->structValue->end()) features->kGetDeviceSecurityInfo = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setDeviceSecurityInfo");
          if (featureIterator != featureStruct->structValue->end()) features->kSetDeviceSecurityInfo = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("getRepeaterFunctions");
          if (featureIterator != featureStruct->structValue->end()) features->kGetRepeaterFunctions = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setRepeaterFunctions");
          if (featureIterator != featureStruct->structValue->end()) features->kSetRepeaterFunctions = featureIterator->second->booleanValue;

          featureIterator = featureStruct->structValue->find("setRepeaterFilter");
          if (featureIterator != featureStruct->structValue->end()) features->kSetRepeaterFilter = featureIterator->second->booleanValue;
        }
      }
    }

    return features;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return PRemanFeatures();
}

}