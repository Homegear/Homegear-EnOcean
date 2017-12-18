/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#include "../GD.h"
#include "HomegearGateway.h"

namespace MyFamily
{

HomegearGateway::HomegearGateway(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IEnOceanInterface(settings)
{
    _settings = settings;
    _out.init(GD::bl);
    _out.setPrefix(GD::out.getPrefix() + "EnOcean Homegear Gateway \"" + settings->id + "\": ");

    signal(SIGPIPE, SIG_IGN);

    _waitForResponse = false;

    _binaryRpc.reset(new BaseLib::Rpc::BinaryRpc(_bl));
    _rpcEncoder.reset(new BaseLib::Rpc::RpcEncoder(_bl, true, true));
    _rpcDecoder.reset(new BaseLib::Rpc::RpcDecoder(_bl, false, false));
}

HomegearGateway::~HomegearGateway()
{
    stopListening();
    _bl->threadManager.join(_initThread);
}

void HomegearGateway::startListening()
{
    try
    {
        stopListening();

        if(_settings->host.empty() || _settings->port.empty() || _settings->caFile.empty() || _settings->certFile.empty() || _settings->keyFile.empty())
        {
            _out.printError("Error: Configuration of Homegear Gateway is incomplete. Please correct it in \"enocean.conf\".");
            return;
        }

        _tcpSocket.reset(new BaseLib::TcpSocket(_bl, _settings->host, _settings->port, true, _settings->caFile, true, _settings->certFile, _settings->keyFile));
        _tcpSocket->setConnectionRetries(1);
        _tcpSocket->setReadTimeout(5000000);
        _tcpSocket->setWriteTimeout(5000000);
        _stopCallbackThread = false;
        if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &HomegearGateway::listen, this);
        else _bl->threadManager.start(_listenThread, true, &HomegearGateway::listen, this);
        IPhysicalInterface::startListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HomegearGateway::stopListening()
{
    try
    {
        _stopCallbackThread = true;
        if(_tcpSocket) _tcpSocket->close();
        _bl->threadManager.join(_listenThread);
        _stopped = true;
        _tcpSocket.reset();
        IPhysicalInterface::stopListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

int32_t HomegearGateway::setBaseAddress(uint32_t value)
{
    try
    {
        if(!_tcpSocket->connected())
        {
            _out.printError("Error: Could not set base address. Not connected to gateway.");
            return -1;
        }

        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->reserve(2);
        parameters->push_back(std::make_shared<BaseLib::Variable>(MY_FAMILY_ID));
        parameters->push_back(std::make_shared<BaseLib::Variable>((int64_t)value));

        auto result = invoke("setBaseAddress", parameters);
        if(result->errorStruct)
        {
            _out.printError(result->structValue->at("faultString")->stringValue);
        }
        else _out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(value, 8) + ". Remaining changes: " + std::to_string(result->integerValue64));

        return result->integerValue64;
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return -1;
}

void HomegearGateway::init()
{
    try
    {
        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->push_back(std::make_shared<BaseLib::Variable>(MY_FAMILY_ID));

        auto result = invoke("getBaseAddress", parameters);
        if(result->errorStruct)
        {
            _out.printError(result->structValue->at("faultString")->stringValue);
        }
        else
        {
            _baseAddress = (uint32_t)(int32_t)result->integerValue64;
            _out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ".");
        }
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HomegearGateway::listen()
{
    try
    {
        try
        {
            _tcpSocket->open();
            if(_tcpSocket->connected())
            {
                _out.printInfo("Info: Successfully connected.");
                _stopped = false;
                _bl->threadManager.start(_initThread, true, &HomegearGateway::init, this);
            }
        }
        catch(BaseLib::Exception& ex)
        {
            _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }
        catch(const std::exception& ex)
        {
            _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }
        catch(...)
        {
            _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
        }

        std::vector<char> buffer(1024);
        while(!_stopCallbackThread)
        {
            try
            {
                if(_stopped || !_tcpSocket->connected())
                {
                    if(_stopCallbackThread) return;
                    if(_stopped) _out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
                    _tcpSocket->close();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    _tcpSocket->open();
                    if(_tcpSocket->connected())
                    {
                        _out.printInfo("Info: Successfully connected.");
                        _stopped = false;
                        _bl->threadManager.start(_initThread, true, &HomegearGateway::init, this);
                    }
                    continue;
                }

                int32_t bytesRead = 0;
                try
                {
                    bytesRead = _tcpSocket->proofread(buffer.data(), buffer.size());
                }
                catch(BaseLib::SocketTimeOutException& ex)
                {
                    continue;
                }
                if(bytesRead <= 0) continue;
                if(bytesRead > 1024) bytesRead = 1024;

                if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: TCP packet received: " + BaseLib::HelperFunctions::getHexString(buffer.data(), bytesRead));

                _binaryRpc->process(buffer.data(), bytesRead);
                if(_binaryRpc->isFinished())
                {
                    if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request)
                    {
                        std::string method;
                        BaseLib::PArray parameters = _rpcDecoder->decodeRequest(_binaryRpc->getData(), method);

                        if(method == "packetReceived" && parameters && parameters->size() == 2 && parameters->at(0)->integerValue64 == MY_FAMILY_ID && !parameters->at(1)->binaryValue.empty())
                        {
                            processPacket(parameters->at(1)->binaryValue);
                        }

                        BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();
                        std::vector<char> data;
                        _rpcEncoder->encodeResponse(response, data);
                        _tcpSocket->proofwrite(data);
                    }
                    else if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::response && _waitForResponse)
                    {
                        std::unique_lock<std::mutex> requestLock(_requestMutex);
                        _rpcResponse = _rpcDecoder->decodeResponse(_binaryRpc->getData());
                        requestLock.unlock();
                        _requestConditionVariable.notify_all();
                    }
                    _binaryRpc->reset();
                }
            }
            catch(BaseLib::Exception& ex)
            {
                _stopped = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(const std::exception& ex)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(...)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
            }
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HomegearGateway::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
    try
    {
        std::shared_ptr<MyPacket> myPacket(std::dynamic_pointer_cast<MyPacket>(packet));
        if(!myPacket) return;

        if(_stopped || !_tcpSocket->connected())
        {
            _out.printInfo("Info: Waiting two seconds, because wre are not connected.");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            if(_stopped || !_tcpSocket->connected())
            {
                _out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(myPacket->getBinary()) + ", because init is not complete.");
                return;
            }
        }

        std::vector<uint8_t> data = std::move(myPacket->getBinary());
        addCrc8(data);
        std::vector<uint8_t> response;
        getResponse(0x02, data, response);
        if(response.size() != 8 || (response.size() >= 7 && response[6] != 0))
        {
            if(response.size() >= 7 && response[6] != 0)
            {
                std::map<uint8_t, std::string>::iterator statusIterator = _responseStatusCodes.find(response[6]);
                if(statusIterator != _responseStatusCodes.end()) _out.printError("Error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\": " + statusIterator->second);
                else _out.printError("Unknown error (" + std::to_string(response[6]) + ") sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
            }
            else _out.printError("Unknown error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
            return;
        }
        _lastPacketSent = BaseLib::HelperFunctions::getTime();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HomegearGateway::rawSend(std::vector<uint8_t>& packet)
{
    try
    {
        if(!_tcpSocket || !_tcpSocket->connected()) return;

        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->reserve(2);
        parameters->push_back(std::make_shared<BaseLib::Variable>(MY_FAMILY_ID));
        parameters->push_back(std::make_shared<BaseLib::Variable>(packet));

        auto result = invoke("sendPacket", parameters);
        if(result->errorStruct)
        {
            _out.printError("Error sending packet " + BaseLib::HelperFunctions::getHexString(packet) + ": " + result->structValue->at("faultString")->stringValue);
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

PVariable HomegearGateway::invoke(std::string methodName, PArray& parameters)
{
    try
    {
        std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

        std::unique_lock<std::mutex> requestLock(_requestMutex);
        _rpcResponse.reset();
        _waitForResponse = true;

        std::vector<char> encodedPacket;
        _rpcEncoder->encodeRequest(methodName, parameters, encodedPacket);

        int32_t i = 0;
        for(i = 0; i < 5; i++)
        {
            try
            {
                _tcpSocket->proofwrite(encodedPacket);
                break;
            }
            catch(BaseLib::SocketOperationException& ex)
            {
                _out.printError("Error: " + ex.what());
                if(i == 5) return BaseLib::Variable::createError(-32500, ex.what());
                _tcpSocket->open();
            }
        }

        i = 0;
        while(!_requestConditionVariable.wait_for(requestLock, std::chrono::milliseconds(1000), [&]
        {
            i++;
            return _rpcResponse || _stopped || i == 10;
        }));
        _waitForResponse = false;
        if(i == 10 || !_rpcResponse) return BaseLib::Variable::createError(-32500, "No RPC response received.");

        return _rpcResponse;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void HomegearGateway::processPacket(std::vector<uint8_t>& data)
{
    try
    {
        if(data.size() < 5)
        {
            _out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
            return;
        }

        uint8_t packetType = data[4];
        std::unique_lock<std::mutex> requestsGuard(_requestsMutex);
        std::map<uint8_t, std::shared_ptr<Request>>::iterator requestIterator = _requests.find(packetType);
        if(requestIterator != _requests.end())
        {
            std::shared_ptr<Request> request = requestIterator->second;
            requestsGuard.unlock();
            request->response = data;
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->mutexReady = true;
            }
            request->conditionVariable.notify_one();
            return;
        }
        else requestsGuard.unlock();

        PMyPacket packet(new MyPacket(data));
        if(packet->getType() == MyPacket::Type::RADIO_ERP1 || packet->getType() == MyPacket::Type::RADIO_ERP2)
        {
            if((packet->senderAddress() & 0xFFFFFF80) == _baseAddress) _out.printInfo("Info: Ignoring packet from myself: " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));
            else raisePacketReceived(packet);
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

}