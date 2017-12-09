/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef SECURITY_H_
#define SECURITY_H_

#include <homegear-base/BaseLib.h>

namespace MyFamily
{

class Security
{
    public:
        Security(BaseLib::SharedObjects* bl);
        virtual ~Security();

        bool decrypt(const std::vector<uint8_t>& deviceAesKey, std::vector<uint8_t>& data, int32_t dataSize, int32_t rollingCode, int32_t rollingCodeSize);
        bool checkCmac(const std::vector<uint8_t>& deviceAesKey, const std::vector<uint8_t>& encryptedData, int32_t dataSize, int32_t& rollingCode, int32_t rollingCodeSize, int32_t cmacSize);
        std::vector<uint8_t> getCmac(const std::vector<uint8_t>& deviceAesKey, const std::vector<uint8_t>& encryptedData, int32_t dataSize, int32_t rollingCode, int32_t rollingCodeSize, int32_t cmacSize);
    protected:
        BaseLib::SharedObjects* _bl = nullptr;

        const uint8_t _subkeyInput[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        std::mutex _encryptMutex;
		gcry_cipher_hd_t _encryptHandle = nullptr;

		std::vector<uint8_t> encryptRollingCode(const std::vector<uint8_t>& deviceAesKey, int32_t rollingCode, int32_t rollingCodeSize);
		std::vector<uint8_t> getSubkey(const std::vector<uint8_t>& deviceAesKey, bool sizeGreater15Bytes);
		void leftShiftVector(std::vector<uint8_t>& data);
};

typedef std::shared_ptr<Security> PSecurity;

}
#endif
