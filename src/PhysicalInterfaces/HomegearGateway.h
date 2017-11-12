/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef HOMEGEAR_ENOCEAN_HOMEGEARGATEWAY_H
#define HOMEGEAR_ENOCEAN_HOMEGEARGATEWAY_H

#include "../MyPacket.h"
#include "IEnOceanInterface.h"
#include <homegear-base/BaseLib.h>

namespace MyFamily
{

class HomegearGateway : public IEnOceanInterface
{
public:
    HomegearGateway(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
    virtual ~HomegearGateway();

    virtual void startListening();
    virtual void stopListening();

    virtual int32_t setBaseAddress(uint32_t value);

    virtual bool isOpen() { return !_stopped; }

    virtual void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet);
protected:
    std::unique_ptr<BaseLib::TcpSocket> _tcpSocket;
    std::unique_ptr<BaseLib::Rpc::BinaryRpc> _binaryRpc;
    std::unique_ptr<BaseLib::Rpc::RpcEncoder> _rpcEncoder;
    std::unique_ptr<BaseLib::Rpc::RpcDecoder> _rpcDecoder;

    std::thread _initThread;
    std::mutex _invokeMutex;
    std::mutex _requestMutex;
    std::atomic_bool _waitForResponse;
    std::condition_variable _requestConditionVariable;
    BaseLib::PVariable _rpcResponse;

    void listen();
    virtual void rawSend(std::vector<uint8_t>& packet);
    PVariable invoke(std::string methodName, PArray& parameters);
    void processPacket(std::vector<uint8_t>& data);
    void init();
};

}

#endif //HOMEGEAR_ENOCEAN_HOMEGEARGATEWAY_H