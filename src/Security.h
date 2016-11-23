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
