/* Copyright 2013-2016 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "Security.h"

#include "GD.h"

namespace MyFamily
{
Security::Security(BaseLib::Obj* bl) : _bl(bl)
{
	gcry_error_t result;
	if((result = gcry_cipher_open(&_encryptHandle, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_ECB, GCRY_CIPHER_SECURE)) != GPG_ERR_NO_ERROR)
	{
		_encryptHandle = nullptr;
		GD::out.printError("Error initializing cypher handle for encryption: " + BaseLib::Security::Gcrypt::getError(result));
		return;
	}
	if(!_encryptHandle)
	{
		GD::out.printError("Error cypher handle for encryption is nullptr.");
		return;
	}
}

Security::~Security()
{
	if(_encryptHandle) gcry_cipher_close(_encryptHandle);
	_encryptHandle = nullptr;
}

std::vector<char> Security::encryptRollingCode(const std::vector<char>& deviceAesKey, int32_t rollingCode, int32_t rollingCodeSize)
{
	try
	{
		std::vector<char> plain{ 0x34, 0x10, (char)(uint8_t)0xde, (char)(uint8_t)0x8f, 0x1a, (char)(uint8_t)0xba, 0x3e, (char)(uint8_t)0xff, (char)(uint8_t)0x9f, 0x5a, 0x11, 0x71, 0x72, (char)(uint8_t)0xea, (char)(uint8_t)0xca, (char)(uint8_t)0xbd };
		if(rollingCodeSize == 3)
		{
			plain[0] ^= (char)(uint8_t)(rollingCode >> 16);
			plain[1] ^= (char)(uint8_t)((rollingCode >> 8) & 0xFF);
			plain[2] ^= (char)(uint8_t)(rollingCode & 0xFF);
		}
		else
		{
			plain[0] ^= (char)(uint8_t)(rollingCode >> 8);
			plain[1] ^= (char)(uint8_t)(rollingCode & 0xFF);
		}

		std::vector<char> encryptedRollingCode(16);
		{
			int32_t result = 0;
			std::lock_guard<std::mutex> encryptGuard(_encryptMutex);
			if((result = gcry_cipher_setkey(_encryptHandle, &deviceAesKey.at(0), deviceAesKey.size())) != GPG_ERR_NO_ERROR)
			{
				GD::out.printError("Error: Could not set key for encryption: " + BaseLib::Security::Gcrypt::getError(result));
				return std::vector<char>();
			}

			if((result = gcry_cipher_encrypt(_encryptHandle, &encryptedRollingCode.at(0), encryptedRollingCode.size(), &plain.at(0), plain.size())) != GPG_ERR_NO_ERROR)
			{
				GD::out.printError("Error encrypting data: " + BaseLib::Security::Gcrypt::getError(result));
				return std::vector<char>();
			}
		}

		return encryptedRollingCode;
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
    return std::vector<char>();
}

bool Security::decrypt(const std::vector<char>& deviceAesKey, std::vector<char>& data, int32_t dataSize, int32_t rollingCode, int32_t rollingCodeSize)
{
	try
	{
		std::vector<char> encryptedRollingCode = encryptRollingCode(deviceAesKey, rollingCode, rollingCodeSize);
		if(encryptedRollingCode.empty()) return false;

		for(int32_t i = 1; i < dataSize && i - 1 < (signed)encryptedRollingCode.size(); i++)
		{
			data[i] ^= encryptedRollingCode[i - 1];
		}

		if(data[0] == 0x30 || data[0] == 0x31) data[0] = 0x32;

		return true;
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
    return false;
}

bool Security::checkCmac(const std::vector<char>& deviceAesKey, const std::vector<char>& encryptedData, int32_t dataSize, int32_t& rollingCode, int32_t rollingCodeSize, int32_t cmacSize)
{
	try
	{
		if((signed)encryptedData.size() < dataSize + cmacSize) return false;
		for(int32_t currentRollingCode = rollingCode; currentRollingCode < rollingCode + 128; currentRollingCode++)
		{
			std::vector<char> cmacInPacket(&encryptedData.at(dataSize), &encryptedData.at(dataSize) + cmacSize);
			std::vector<char> calculatedCmac = getCmac(deviceAesKey, encryptedData, dataSize, currentRollingCode, rollingCodeSize, cmacSize);
			if(cmacInPacket.empty() || calculatedCmac.empty()) return false;

			if(cmacInPacket.size() == calculatedCmac.size() && std::equal(cmacInPacket.begin(), cmacInPacket.end(), calculatedCmac.begin()))
			{
				rollingCode = currentRollingCode;
				return true;
			}
		}

		return false;
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
    return false;
}

std::vector<char> Security::getCmac(const std::vector<char>& deviceAesKey, const std::vector<char>& encryptedData, int32_t dataSize, int32_t rollingCode, int32_t rollingCodeSize, int32_t cmacSize)
{
	try
	{
		std::vector<char> plain;
		plain.reserve(16);
		plain.insert(plain.end(), encryptedData.begin(), encryptedData.begin() + dataSize);
		if(rollingCodeSize == 3)
		{
			plain.push_back((char)(uint8_t)(rollingCode >> 16));
			plain.push_back((char)(uint8_t)((rollingCode >> 8) & 0xFF));
			plain.push_back((char)(uint8_t)(rollingCode & 0xFF));
		}
		else
		{
			plain.push_back((char)(uint8_t)(rollingCode >> 8));
			plain.push_back((char)(uint8_t)(rollingCode & 0xFF));
		}
		bool greaterThan15Bytes = plain.size() > 15;
		if(plain.size() < 16) plain.push_back(0x80);
		while(plain.size() < 16) plain.push_back(0);

		std::vector<char> subkey = getSubkey(deviceAesKey, greaterThan15Bytes);
		if(subkey.size() != 16) return std::vector<char>();

		for(int32_t i = 0; i < 16; i++)
		{
			plain[i] ^= subkey[i];
		}

		std::vector<char> cmac(16);
		{
			int32_t result = 0;
			std::lock_guard<std::mutex> encryptGuard(_encryptMutex);
			if((result = gcry_cipher_setkey(_encryptHandle, &deviceAesKey.at(0), deviceAesKey.size())) != GPG_ERR_NO_ERROR)
			{
				GD::out.printError("Error: Could not set key for encryption: " + BaseLib::Security::Gcrypt::getError(result));
				return std::vector<char>();
			}

			if((result = gcry_cipher_encrypt(_encryptHandle, &cmac.at(0), cmac.size(), &plain.at(0), plain.size())) != GPG_ERR_NO_ERROR)
			{
				GD::out.printError("Error encrypting data: " + BaseLib::Security::Gcrypt::getError(result));
				return std::vector<char>();
			}
		}

		cmac.resize(cmacSize);
		return cmac;
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
    return std::vector<char>();
}

void Security::leftShiftVector(std::vector<char>& data)
{
	try
	{
		bool carry1 = false;
		bool carry2 = false;
		for(int32_t i = data.size() - 1; i >= 0; i--)
		{
			carry1 = (data[i] & 0x80) == 0x80;
			data[i] = data[i] << 1;
			if(carry2) data[i] |= 1;
			carry2 = carry1;
		}
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

std::vector<char> Security::getSubkey(const std::vector<char>& deviceAesKey, bool sizeGreater15Bytes)
{
	try
	{
		std::vector<char> subkey(16);
		int32_t result = 0;

		{
			std::lock_guard<std::mutex> encryptGuard(_encryptMutex);
			if((result = gcry_cipher_setkey(_encryptHandle, &deviceAesKey.at(0), deviceAesKey.size())) != GPG_ERR_NO_ERROR)
			{
				GD::out.printError("Error: Could not set key for encryption: " + BaseLib::Security::Gcrypt::getError(result));
				return std::vector<char>();
			}

			if((result = gcry_cipher_encrypt(_encryptHandle, &subkey.at(0), subkey.size(), (void*)_subkeyInput, 16)) != GPG_ERR_NO_ERROR)
			{
				GD::out.printError("Error encrypting data: " + BaseLib::Security::Gcrypt::getError(result));
				return std::vector<char>();
			}
		}

		leftShiftVector(subkey);
		if(subkey[0] != 0 && subkey[0] != 1) subkey[15] ^= (char)(uint8_t)0x87;

		if(sizeGreater15Bytes) return subkey; //K1

		leftShiftVector(subkey);
		if(subkey[0] != 0 && subkey[0] != 1) subkey[15] ^= (char)(uint8_t)0x87;

		return subkey; //K2
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
    return std::vector<char>();
}

}
