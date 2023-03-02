/* Copyright 2013-2019 Homegear GmbH */

#include "../Gd.h"
#include "HomegearGateway.h"

namespace EnOcean {

HomegearGateway::HomegearGateway(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IEnOceanInterface(settings) {
  _settings = settings;
  _out.init(Gd::bl);
  _out.setPrefix(Gd::out.getPrefix() + "EnOcean Homegear Gateway \"" + settings->id + "\": ");

  signal(SIGPIPE, SIG_IGN);

  _stopped = true;
  _waitForResponse = false;

  _binaryRpc.reset(new BaseLib::Rpc::BinaryRpc(_bl));
  _rpcEncoder.reset(new BaseLib::Rpc::RpcEncoder(_bl, true, true));
  _rpcDecoder.reset(new BaseLib::Rpc::RpcDecoder(_bl, false, false));
}

HomegearGateway::~HomegearGateway() {
  stopListening();
  _bl->threadManager.join(_initThread);
}

void HomegearGateway::startListening() {
  try {
    stopListening();

    if (_settings->host.empty() || _settings->port.empty() || _settings->caFile.empty() || _settings->certFile.empty() || _settings->keyFile.empty()) {
      _out.printError("Error: Configuration of Homegear Gateway is incomplete. Please correct it in \"enocean.conf\".");
      return;
    }

    C1Net::TcpSocketInfo tcp_socket_info;
    tcp_socket_info.read_timeout = 5000;
    tcp_socket_info.write_timeout = 5000;

    C1Net::TcpSocketHostInfo tcp_socket_host_info;
    tcp_socket_host_info.host = _settings->host;
    tcp_socket_host_info.port = BaseLib::Math::getUnsignedNumber(_settings->port);
    tcp_socket_host_info.tls = true;
    tcp_socket_host_info.verify_certificate = true;
    tcp_socket_host_info.ca_file = _settings->caFile;
    tcp_socket_host_info.client_cert_file = _settings->certFile;
    tcp_socket_host_info.client_key_file = _settings->keyFile;
    tcp_socket_host_info.connection_retries = 1;
    tcp_socket_host_info.verify_custom_hostname = true;
    tcp_socket_host_info.custom_hostname = _settings->id;

    _tcpSocket = std::make_unique<C1Net::TcpSocket>(tcp_socket_info, tcp_socket_host_info);

    _stopCallbackThread = false;
    if (_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &HomegearGateway::listen, this);
    else _bl->threadManager.start(_listenThread, true, &HomegearGateway::listen, this);
    IPhysicalInterface::startListening();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void HomegearGateway::stopListening() {
  try {
    _stopCallbackThread = true;
    if (_tcpSocket) _tcpSocket->Shutdown();
    _bl->threadManager.join(_listenThread);
    _stopped = true;
    _tcpSocket.reset();
    IPhysicalInterface::stopListening();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

int32_t HomegearGateway::setBaseAddress(uint32_t value) {
  try {
    if (!_tcpSocket->Connected()) {
      _out.printError("Error: Could not set base address. Not connected to gateway.");
      return -1;
    }

    Gd::out.printInfo("Info: Changing base address to: " + BaseLib::HelperFunctions::getHexString(value));

    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(2);
    parameters->push_back(std::make_shared<BaseLib::Variable>(MY_FAMILY_ID));
    parameters->push_back(std::make_shared<BaseLib::Variable>((int64_t)value));

    auto result = invoke("setBaseAddress", parameters);
    if (result->errorStruct) {
      _out.printError(result->structValue->at("faultString")->stringValue);
    } else _out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(value, 8) + ". Remaining changes: " + std::to_string(result->integerValue64));

    return result->integerValue64;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return -1;
}

IEnOceanInterface::DutyCycleInfo HomegearGateway::getDutyCycleInfo() {
  try {
    if (!_tcpSocket->Connected()) {
      _out.printError("Error: Could not set base address. Not connected to gateway.");
      return DutyCycleInfo();
    }

    Gd::out.printError("Error: getDutyCycleInfo is not supported yet.");
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return DutyCycleInfo();
}

void HomegearGateway::init() {
  try {
    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->push_back(std::make_shared<BaseLib::Variable>(MY_FAMILY_ID));

    auto result = invoke("getBaseAddress", parameters);
    if (result->errorStruct) {
      _out.printError(result->structValue->at("faultString")->stringValue);
    } else {
      _baseAddress = (uint32_t)(int32_t)result->integerValue64;
      _out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ".");
    }

    auto roamingSetting = Gd::family->getFamilySetting("forcebaseid");
    if (roamingSetting) {
      uint32_t newBaseId = (uint32_t)roamingSetting->integerValue & 0xFFFFFF80;
      if (newBaseId >= 0xFF800000) {
        setBaseAddress(newBaseId);
      } else {
        Gd::out.printWarning(R"(Warning: Invalid base ID specified in setting "forceBaseId" in "enocean.conf".)");
      }
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void HomegearGateway::listen() {
  try {
    try {
      _tcpSocket->Open();
      if (_tcpSocket->Connected()) {
        _out.printInfo("Info: Successfully connected.");
        _stopped = false;
        _bl->threadManager.start(_initThread, true, &HomegearGateway::init, this);
      }
    }
    catch (const std::exception &ex) {
      _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }

    std::vector<char> buffer(1024);
    int32_t processedBytes = 0;
    bool more_data = false;
    while (!_stopCallbackThread) {
      try {
        if (_stopped || !_tcpSocket->Connected()) {
          if (_stopCallbackThread) return;
          if (_stopped) _out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
          _tcpSocket->Shutdown();
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          _tcpSocket->Open();
          if (_tcpSocket->Connected()) {
            _out.printInfo("Info: Successfully connected.");
            _stopped = false;
            _bl->threadManager.start(_initThread, true, &HomegearGateway::init, this);
          }
          continue;
        }

        int32_t bytesRead = 0;
        try {
          bytesRead = _tcpSocket->Read((uint8_t *)buffer.data(), buffer.size(), more_data);
        }
        catch (BaseLib::SocketTimeOutException &ex) {
          continue;
        }
        if (bytesRead <= 0) continue;
        if (bytesRead > 1024) bytesRead = 1024;

        if (Gd::bl->debugLevel >= 5) _out.printDebug("Debug: TCP packet received: " + BaseLib::HelperFunctions::getHexString(buffer.data(), bytesRead));

        processedBytes = 0;
        while (processedBytes < bytesRead) {
          try {
            processedBytes += _binaryRpc->process(buffer.data() + processedBytes, bytesRead - processedBytes);
            if (_binaryRpc->isFinished()) {
              if (_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request) {
                std::string method;
                BaseLib::PArray parameters = _rpcDecoder->decodeRequest(_binaryRpc->getData(), method);

                if (method == "packetReceived" && parameters && parameters->size() == 2 && parameters->at(0)->integerValue64 == MY_FAMILY_ID && !parameters->at(1)->binaryValue.empty()) {
                  processPacket(parameters->at(1)->binaryValue);
                }

                BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();
                std::vector<uint8_t> data;
                _rpcEncoder->encodeResponse(response, data);
                _tcpSocket->Send(data);
              } else if (_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::response && _waitForResponse) {
                std::unique_lock<std::mutex> requestLock(_requestMutex);
                _rpcResponse = _rpcDecoder->decodeResponse(_binaryRpc->getData());
                requestLock.unlock();
                _requestConditionVariable.notify_all();
              }
              _binaryRpc->reset();
            }
          }
          catch (BaseLib::Rpc::BinaryRpcException &ex) {
            _binaryRpc->reset();
            _out.printError("Error processing packet: " + std::string(ex.what()));
          }
        }
      }
      catch (const std::exception &ex) {
        _stopped = true;
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

bool HomegearGateway::sendEnoceanPacket(const std::vector<PEnOceanPacket> &packets) {
  try {
    if (!_tcpSocket || packets.empty() || !packets.at(0)) return false;

    if (_stopped || !_tcpSocket->Connected()) {
      _out.printInfo("Info: Waiting two seconds, because wre are not connected.");
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      if (_stopped || !_tcpSocket->Connected()) {
        _out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(packets.at(0)->getBinary()) + ", because init is not complete.");
        return false;
      }
    }

    uint32_t i = 0;
    for (auto &packet: packets) {
      i++;
      if (!packet) return false;

      std::vector<uint8_t> data = std::move(packet->getBinary());
      addCrc8(data);

      if (packet->getRorg() == 0xC5) {
        Gd::out.printInfo("Info: Sending packet " + std::to_string(i) + " of " + std::to_string(packets.size()) + " (REMAN function 0x" + BaseLib::HelperFunctions::getHexString(packet->getRemoteManagementFunction(), 3) + ") " + BaseLib::HelperFunctions::getHexString(data));
      } else {
        Gd::out.printInfo("Info: Sending packet " + std::to_string(i) + " of " + std::to_string(packets.size()) + ": " + BaseLib::HelperFunctions::getHexString(data));
      }

      std::vector<uint8_t> response;
      getResponse(0x02, data, response);
      if (response.size() != 8 || (response.size() >= 7 && response[6] != 0)) {
        if (response.size() >= 7 && response[6] != 0) {
          std::map<uint8_t, std::string>::iterator statusIterator = _responseStatusCodes.find(response[6]);
          if (statusIterator != _responseStatusCodes.end()) _out.printError("Error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\": " + statusIterator->second);
          else _out.printError("Unknown error (" + std::to_string(response[6]) + ") sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
        } else _out.printError("Unknown error sending packet \"" + BaseLib::HelperFunctions::getHexString(data) + "\".");
        return false;
      }
    }
    _lastPacketSent = BaseLib::HelperFunctions::getTime();
    return true;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

void HomegearGateway::rawSend(std::vector<uint8_t> &packet) {
  try {
    IEnOceanInterface::rawSend(packet);
    if (!_tcpSocket || !_tcpSocket->Connected()) return;

    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(2);
    parameters->push_back(std::make_shared<BaseLib::Variable>(MY_FAMILY_ID));
    parameters->push_back(std::make_shared<BaseLib::Variable>(packet));

    auto result = invoke("sendPacket", parameters);
    if (result->errorStruct) {
      _out.printError("Error sending packet " + BaseLib::HelperFunctions::getHexString(packet) + ": " + result->structValue->at("faultString")->stringValue);
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

PVariable HomegearGateway::invoke(std::string methodName, PArray &parameters) {
  try {
    std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

    std::unique_lock<std::mutex> requestLock(_requestMutex);
    _rpcResponse.reset();
    _waitForResponse = true;

    std::vector<uint8_t> encodedPacket;
    _rpcEncoder->encodeRequest(methodName, parameters, encodedPacket);

    int32_t i = 0;
    for (i = 0; i < 5; i++) {
      try {
        _tcpSocket->Send(encodedPacket);
        break;
      }
      catch (BaseLib::SocketOperationException &ex) {
        _out.printError("Error: " + std::string(ex.what()));
        if (i == 5) return BaseLib::Variable::createError(-32500, ex.what());
        _tcpSocket->Open();
      }
    }

    i = 0;
    while (!_requestConditionVariable.wait_for(requestLock, std::chrono::milliseconds(1000), [&] {
      i++;
      return _rpcResponse || _stopped || i == 10;
    }));
    _waitForResponse = false;
    if (i == 10 || !_rpcResponse) return BaseLib::Variable::createError(-32500, "No RPC response received.");

    return _rpcResponse;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void HomegearGateway::processPacket(std::vector<uint8_t> &data) {
  try {
    if (data.size() < 5) {
      _out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
      return;
    }

    _lastPacketReceived = BaseLib::HelperFunctions::getTime();

    if (checkForSerialRequest(data)) return;

    PEnOceanPacket packet(new EnOceanPacket(data));
    if (checkForEnOceanRequest(packet)) return;
    if (packet->getType() == EnOceanPacket::Type::RADIO_ERP1 || packet->getType() == EnOceanPacket::Type::RADIO_ERP2) {
      if ((packet->senderAddress() & 0xFFFFFF80) == _baseAddress) _out.printInfo("Info: Ignoring packet from myself: " + BaseLib::HelperFunctions::getHexString(packet->getBinary()));
      else raisePacketReceived(packet);
    } else {
      _out.printInfo("Info: Not processing packet: " + BaseLib::HelperFunctions::getHexString(data));
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

}