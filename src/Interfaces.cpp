/* Copyright 2013-2019 Homegear GmbH */

#include "Interfaces.h"
#include "GD.h"
#include "PhysicalInterfaces/Usb300.h"
#include "PhysicalInterfaces/HomegearGateway.h"
#include "PhysicalInterfaces/Hgdc.h"

namespace EnOcean
{

Interfaces::Interfaces(BaseLib::SharedObjects* bl, std::map<std::string, Systems::PPhysicalInterfaceSettings> physicalInterfaceSettings) : Systems::PhysicalInterfaces(bl, GD::family->getFamily(), physicalInterfaceSettings)
{
	create();
}

Interfaces::~Interfaces()
{
    _physicalInterfaces.clear();
    _defaultPhysicalInterface.reset();
    _physicalInterfaceEventhandlers.clear();
}

void Interfaces::addEventHandlers(BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink* central)
{
    try
    {
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        _central = central;
        for(const auto& interface : _physicalInterfaces)
        {
            if(_physicalInterfaceEventhandlers.find(interface.first) != _physicalInterfaceEventhandlers.end()) continue;
            _physicalInterfaceEventhandlers[interface.first] = interface.second->addEventHandler(central);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Interfaces::removeEventHandlers()
{
    try
    {
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        for(const auto& interface : _physicalInterfaces)
        {
            auto physicalInterfaceEventhandler = _physicalInterfaceEventhandlers.find(interface.first);
            if(physicalInterfaceEventhandler == _physicalInterfaceEventhandlers.end()) continue;
            interface.second->removeEventHandler(physicalInterfaceEventhandler->second);
            _physicalInterfaceEventhandlers.erase(physicalInterfaceEventhandler);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Interfaces::create()
{
	try
	{
		for(auto i = _physicalInterfaceSettings.begin(); i != _physicalInterfaceSettings.end(); ++i)
		{
			std::shared_ptr<IEnOceanInterface> device;
			if(!i->second) continue;
			GD::out.printDebug("Debug: Creating physical device. Type defined in enocean.conf is: " + i->second->type);
			if(i->second->type == "usb300" || i->second->type == "tcm310") device.reset(new Usb300(i->second));
			else if(i->second->type == "homegeargateway") device.reset(new HomegearGateway(i->second));
			else GD::out.printError("Error: Unsupported physical device type: " + i->second->type);
			if(device)
			{
				if(_physicalInterfaces.find(i->second->id) != _physicalInterfaces.end()) GD::out.printError("Error: id used for two devices: " + i->second->id);
				_physicalInterfaces[i->second->id] = device;
				if(i->second->isDefault || !_defaultPhysicalInterface) _defaultPhysicalInterface = device;
			}
		}
        if(!_defaultPhysicalInterface) _defaultPhysicalInterface = std::make_shared<IEnOceanInterface>(std::make_shared<BaseLib::Systems::PhysicalInterfaceSettings>());
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
}

void Interfaces::startListening()
{
    try
    {
        {
            std::lock_guard<std::mutex> interfacesGuard(_physicalInterfacesMutex);
            if(GD::bl->hgdc)
            {
                _hgdcEventHandlerId = GD::bl->hgdc->registerModuleUpdateEventHandler(std::function<void(const BaseLib::PVariable&)>(std::bind(&Interfaces::hgdcModuleUpdate, this, std::placeholders::_1)));

                auto modules = GD::bl->hgdc->getModules(MY_FAMILY_ID);
                if(modules->errorStruct)
                {
                    GD::out.printError("Error getting HGDC modules: " + modules->structValue->at("faultString")->stringValue);
                }
                for(auto& module : *modules->arrayValue)
                {
                    std::shared_ptr<IEnOceanInterface> device;
                    GD::out.printDebug("Debug: Creating HGDC device.");
                    auto settings = std::make_shared<Systems::PhysicalInterfaceSettings>();
                    settings->type = "hgdc";
                    settings->id = module->structValue->at("serialNumber")->stringValue;
                    settings->serialNumber = settings->id;
                    device = std::make_shared<Hgdc>(settings);

                    if(_physicalInterfaces.find(settings->id) != _physicalInterfaces.end()) GD::out.printError("Error: id used for two devices: " + settings->id);
                    _physicalInterfaces[settings->id] = device;
                    if(settings->isDefault || !_defaultPhysicalInterface || _defaultPhysicalInterface->getID().empty()) _defaultPhysicalInterface = device;

                    if(_central)
                    {
                        if(_physicalInterfaceEventhandlers.find(settings->id) != _physicalInterfaceEventhandlers.end()) continue;
                        _physicalInterfaceEventhandlers[settings->id] = device->addEventHandler(_central);
                    }
                }
            }
        }

        PhysicalInterfaces::startListening();
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Interfaces::stopListening()
{
    try
    {
        if(GD::bl->hgdc)
        {
            GD::bl->hgdc->unregisterModuleUpdateEventHandler(_hgdcEventHandlerId);
        }

        GD::bl->threadManager.join(_modulesAddedThread);

        PhysicalInterfaces::stopListening();
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::vector<std::shared_ptr<IEnOceanInterface>> Interfaces::getInterfaces()
{
    std::vector<std::shared_ptr<IEnOceanInterface>> interfaces;
    try
    {
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        interfaces.reserve(_physicalInterfaces.size());
        for(auto interfaceBase : _physicalInterfaces)
        {
            std::shared_ptr<IEnOceanInterface> interface(std::dynamic_pointer_cast<IEnOceanInterface>(interfaceBase.second));
            if(!interface) continue;
            if(interface->isOpen()) interfaces.push_back(interface);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return interfaces;
}

std::shared_ptr<IEnOceanInterface> Interfaces::getDefaultInterface()
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    return _defaultPhysicalInterface;
}

bool Interfaces::hasInterface(const std::string& name)
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    auto interfaceBase = _physicalInterfaces.find(name);
    return interfaceBase != _physicalInterfaces.end();
}

std::shared_ptr<IEnOceanInterface> Interfaces::getInterface(const std::string& name)
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    auto interfaceBase = _physicalInterfaces.find(name);
    if(interfaceBase == _physicalInterfaces.end()) return _defaultPhysicalInterface;
    std::shared_ptr<IEnOceanInterface> interface(std::dynamic_pointer_cast<IEnOceanInterface>(interfaceBase->second));
    return interface;
}

void Interfaces::hgdcModuleUpdate(const BaseLib::PVariable& modules)
{
    try
    {
        auto addedModules = std::make_shared<std::list<std::shared_ptr<BaseLib::Systems::IPhysicalInterface>>>();

        {
            std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
            for(auto& module : *modules->structValue)
            {
                auto familyIdIterator = module.second->structValue->find("familyId");
                if(familyIdIterator == module.second->structValue->end() || familyIdIterator->second->integerValue64 != MY_FAMILY_ID) continue;

                auto removedIterator = module.second->structValue->find("removed");
                if(removedIterator != module.second->structValue->end())
                {
                    auto interface = _physicalInterfaces.find(module.first);
                    if(interface != _physicalInterfaces.end())
                    {
                        interface->second->stopListening();
                        continue;
                    }
                }

                auto restartedIterator = module.second->structValue->find("restarted");
                if(restartedIterator != module.second->structValue->end())
                {
                    auto interfaceBase = _physicalInterfaces.find(module.first);
                    if(interfaceBase != _physicalInterfaces.end())
                    {
                        std::shared_ptr<Hgdc> interface(std::dynamic_pointer_cast<Hgdc>(interfaceBase->second));
                        if(!interface) continue;
                        interface->init();
                        continue;
                    }
                }

                auto addedIterator = module.second->structValue->find("added");
                if(addedIterator != module.second->structValue->end())
                {
                    auto interfaceBase = _physicalInterfaces.find(module.first);
                    if(interfaceBase == _physicalInterfaces.end())
                    {
                        std::shared_ptr<IEnOceanInterface> device;
                        GD::out.printDebug("Debug: Creating HGDC device.");
                        auto settings = std::make_shared<Systems::PhysicalInterfaceSettings>();
                        settings->type = "hgdc";
                        settings->id = module.first;
                        settings->serialNumber = settings->id;
                        device = std::make_shared<Hgdc>(settings);

                        if(_physicalInterfaces.find(settings->id) != _physicalInterfaces.end()) GD::out.printError("Error: id used for two devices: " + settings->id);
                        _physicalInterfaces[settings->id] = device;
                        if(settings->isDefault || !_defaultPhysicalInterface || _defaultPhysicalInterface->getID().empty()) _defaultPhysicalInterface = device;

                        addedModules->push_back(device);
                    }
                }
            }
        }

        GD::bl->threadManager.start(_modulesAddedThread, true, &Interfaces::hgdcModulesAdded, this, addedModules);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Interfaces::hgdcModulesAdded(std::shared_ptr<std::list<std::shared_ptr<BaseLib::Systems::IPhysicalInterface>>> addedModules)
{
    try
    {
        for(auto& module : *addedModules)
        {
            if(_central)
            {
                if(_physicalInterfaceEventhandlers.find(module->getID()) != _physicalInterfaceEventhandlers.end()) continue;
                _physicalInterfaceEventhandlers[module->getID()] = module->addEventHandler(_central);
            }

            module->startListening();
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

}
