/*
 * Copyright (C) 2013-2018 Homegear UG (haftungsbeschränkt). All rights reserved.
 *
 * This document is the property of Homegear UG (haftungsbeschränkt)
 * It is considered confidential and proprietary.
 *
 * This document may not be reproduced or transmitted in any form,
 * in whole or in part, without the express written permission of
 * Homegear UG (haftungsbeschränkt)
 */

#include "GD.h"
#include "Interfaces.h"
#include "MyFamily.h"
#include "MyCentral.h"

namespace MyFamily
{

MyFamily::MyFamily(BaseLib::SharedObjects* bl, BaseLib::Systems::IFamilyEventSink* eventHandler) : BaseLib::Systems::DeviceFamily(bl, eventHandler, MY_FAMILY_ID, MY_FAMILY_NAME)
{
	GD::bl = bl;
	GD::family = this;
	GD::out.init(bl);
	GD::out.setPrefix(std::string("Module ") + MY_FAMILY_NAME + ": ");
	GD::out.printDebug("Debug: Loading module...");
	_physicalInterfaces.reset(new Interfaces(bl, _settings->getPhysicalInterfaceSettings()));
}

MyFamily::~MyFamily()
{

}

void MyFamily::dispose()
{
	if(_disposed) return;
	DeviceFamily::dispose();

	_central.reset();
}

void MyFamily::createCentral()
{
	try
	{
		_central.reset(new MyCentral(0, "VEO0000001", this));
		GD::out.printMessage("Created central with id " + std::to_string(_central->getId()) + ".");
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<BaseLib::Systems::ICentral> MyFamily::initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber)
{
	return std::shared_ptr<MyCentral>(new MyCentral(deviceId, serialNumber, this));
}

PVariable MyFamily::getPairingInfo()
{
	try
	{
		if(!_central) return std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		PVariable info = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		//{{{ General
		info->structValue->emplace("searchInterfaces", std::make_shared<BaseLib::Variable>(false));
		//}}}

		//{{{ Pairing methods
		PVariable pairingMethods = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

			//{{{ createDevice
			PVariable createDeviceMetadata = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
			PVariable createDeviceMetadataInfo = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
			PVariable createDeviceFields = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
			createDeviceFields->arrayValue->reserve(3);
			createDeviceFields->arrayValue->push_back(std::make_shared<BaseLib::Variable>("deviceType"));
			createDeviceFields->arrayValue->push_back(std::make_shared<BaseLib::Variable>("address"));
			createDeviceFields->arrayValue->push_back(std::make_shared<BaseLib::Variable>("interfaceId"));
			createDeviceMetadataInfo->structValue->emplace("fields", createDeviceFields);
			createDeviceMetadata->structValue->emplace("metadataInfo", createDeviceMetadataInfo);

			pairingMethods->structValue->emplace("createDevice", createDeviceMetadata);
			//}}}

			//{{{ createDevice+
			PVariable createDevicePlusMetadata = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
			PVariable createDevicePlusMetadataInfo = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
			PVariable createDevicePlusFields = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tArray);
			createDevicePlusFields->arrayValue->reserve(3);
			createDevicePlusFields->arrayValue->push_back(std::make_shared<BaseLib::Variable>("deviceType"));
			createDevicePlusFields->arrayValue->push_back(std::make_shared<BaseLib::Variable>("address"));
			createDevicePlusFields->arrayValue->push_back(std::make_shared<BaseLib::Variable>("interfaceId"));
			createDevicePlusMetadataInfo->structValue->emplace("fields", createDevicePlusFields);
			createDevicePlusMetadata->structValue->emplace("metadataInfo", createDevicePlusMetadataInfo);

			pairingMethods->structValue->emplace("createDevice+", createDevicePlusMetadata);
			//}}}

			//{{{ setInstallMode
			PVariable setInstallModeMetadata = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
			PVariable setInstallModeMetadataInfo = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
			setInstallModeMetadataInfo->structValue->emplace("interfaceSelector", std::make_shared<BaseLib::Variable>(true));
			setInstallModeMetadata->structValue->emplace("metadataInfo", setInstallModeMetadataInfo);

			pairingMethods->structValue->emplace("setInstallMode", setInstallModeMetadata);
			//}}}

		info->structValue->emplace("pairingMethods", pairingMethods);
		//}}}

		//{{{ interfaces
		PVariable interfaces = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

            //{{{ Gateway
            PVariable interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("Homegear Gateway")));
            interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(true));

            PVariable field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.enocean.pairingInfo.id")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("id", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.enocean.pairingInfo.hostname")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("host", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.enocean.pairingInfo.password")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("password", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("2017")));
            interface->structValue->emplace("port", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("/etc/homegear/ca/cacert.pem")));
            interface->structValue->emplace("caFile", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("/etc/homegear/ca/certs/gateway-client.crt")));
            interface->structValue->emplace("certFile", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("/etc/homegear/ca/private/gateway-client.key")));
            interface->structValue->emplace("keyFile", field);

            interfaces->structValue->emplace("homegeargateway", interface);
            //}}}

            //{{{ TCM310
            interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("USB 300 / TCM310")));
            interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(false));

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.enocean.pairingInfo.id")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("id", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.enocean.pairingInfo.device")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("device", field);

            interfaces->structValue->emplace("tcm310", interface);
            //}}}

        info->structValue->emplace("interfaces", interfaces);
		//}}}

		return info;
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Variable::createError(-32500, "Unknown application error.");
}
}
