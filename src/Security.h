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

        bool decrypt(const std::vector<char>& deviceAesKey, std::vector<char>& data, int32_t dataSize, int32_t rollingCode, int32_t rollingCodeSize);
        bool checkCmac(const std::vector<char>& deviceAesKey, const std::vector<char>& encryptedData, int32_t dataSize, int32_t& rollingCode, int32_t rollingCodeSize, int32_t cmacSize);
        std::vector<char> getCmac(const std::vector<char>& deviceAesKey, const std::vector<char>& encryptedData, int32_t dataSize, int32_t rollingCode, int32_t rollingCodeSize, int32_t cmacSize);
    protected:
        BaseLib::SharedObjects* _bl = nullptr;

        const char _subkeyInput[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        std::mutex _encryptMutex;
		gcry_cipher_hd_t _encryptHandle = nullptr;

		std::vector<char> encryptRollingCode(const std::vector<char>& deviceAesKey, int32_t rollingCode, int32_t rollingCodeSize);
		std::vector<char> getSubkey(const std::vector<char>& deviceAesKey, bool sizeGreater15Bytes);
		void leftShiftVector(std::vector<char>& data);
};

typedef std::shared_ptr<Security> PSecurity;

}
#endif
