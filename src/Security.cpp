/* Copyright 2013-2019 Homegear GmbH */

#include "Security.h"

#include "Gd.h"

namespace EnOcean {
Security::Security(BaseLib::SharedObjects *bl) : _bl(bl) {
  gcry_error_t result;
  if ((result = gcry_cipher_open(&_encryptHandle, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_ECB, GCRY_CIPHER_SECURE)) != GPG_ERR_NO_ERROR) {
    _encryptHandle = nullptr;
    Gd::out.printError("Error initializing cypher handle for encryption: " + BaseLib::Security::Gcrypt::getError(result));
    return;
  }
  if (!_encryptHandle) {
    Gd::out.printError("Error cypher handle for encryption is nullptr.");
    return;
  }
}

Security::~Security() {
  if (_encryptHandle) gcry_cipher_close(_encryptHandle);
  _encryptHandle = nullptr;
}

std::vector<uint8_t> Security::encryptRollingCode(const std::vector<uint8_t> &deviceAesKey, uint32_t rollingCode, int32_t rollingCodeSize) {
  try {
    std::vector<uint8_t> plain{0x34, 0x10, (uint8_t)0xde, (uint8_t)0x8f, 0x1a, (uint8_t)0xba, 0x3e, (uint8_t)0xff, (uint8_t)0x9f, 0x5a, 0x11, 0x71, 0x72, (uint8_t)0xea, (uint8_t)0xca, (uint8_t)0xbd};
    if (rollingCodeSize == 4) {
      plain[0] ^= (uint8_t)(rollingCode >> 24);
      plain[1] ^= (uint8_t)((rollingCode >> 16) & 0xFF);
      plain[2] ^= (uint8_t)((rollingCode >> 8) & 0xFF);
      plain[3] ^= (uint8_t)(rollingCode & 0xFF);
    } else if (rollingCodeSize == 3) {
      plain[0] ^= (uint8_t)(rollingCode >> 16);
      plain[1] ^= (uint8_t)((rollingCode >> 8) & 0xFF);
      plain[2] ^= (uint8_t)(rollingCode & 0xFF);
    } else {
      plain[0] ^= (uint8_t)(rollingCode >> 8);
      plain[1] ^= (uint8_t)(rollingCode & 0xFF);
    }

    std::vector<uint8_t> encryptedRollingCode(16);
    {
      int32_t result = 0;
      std::lock_guard<std::mutex> encryptGuard(_encryptMutex);
      if ((result = gcry_cipher_setkey(_encryptHandle, deviceAesKey.data(), deviceAesKey.size())) != GPG_ERR_NO_ERROR) {
        Gd::out.printError("Error: Could not set key for encryption: " + BaseLib::Security::Gcrypt::getError(result));
        return std::vector<uint8_t>();
      }

      if ((result = gcry_cipher_encrypt(_encryptHandle, encryptedRollingCode.data(), encryptedRollingCode.size(), plain.data(), plain.size())) != GPG_ERR_NO_ERROR) {
        Gd::out.printError("Error encrypting data: " + BaseLib::Security::Gcrypt::getError(result));
        return std::vector<uint8_t>();
      }
    }

    return encryptedRollingCode;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<uint8_t>();
}

bool Security::decrypt(const std::vector<uint8_t> &deviceAesKey, std::vector<uint8_t> &data, int32_t dataSize, uint32_t rollingCode, int32_t rollingCodeSize) {
  try {
    std::vector<uint8_t> encryptedRollingCode = encryptRollingCode(deviceAesKey, rollingCode, rollingCodeSize);
    if (encryptedRollingCode.empty()) return false;

    for (int32_t i = 1; i < dataSize && i - 1 < (signed)encryptedRollingCode.size(); i++) {
      data[i] ^= encryptedRollingCode[i - 1];
    }

    if (data[0] == 0x30 || data[0] == 0x31) data[0] = 0x32;

    return true;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

bool Security::checkCmacImplicitRlc(const std::vector<uint8_t> &deviceAesKey, const std::vector<uint8_t> &encryptedData, int32_t dataSize, uint32_t &rollingCode, int32_t rollingCodeSize, int32_t cmacSize) {
  try {
    if ((signed)encryptedData.size() < dataSize + cmacSize) return false;
    for (uint32_t currentRollingCode = rollingCode; currentRollingCode < rollingCode + 128; currentRollingCode++) {
      std::vector<uint8_t> cmacInPacket(encryptedData.begin() + dataSize, encryptedData.begin() + dataSize + cmacSize);
      std::vector<uint8_t> calculatedCmac = getCmac(deviceAesKey, encryptedData, dataSize, currentRollingCode, rollingCodeSize, cmacSize);
      if (cmacInPacket.empty() || calculatedCmac.empty()) return false;

      if (cmacInPacket.size() == calculatedCmac.size() && std::equal(cmacInPacket.begin(), cmacInPacket.end(), calculatedCmac.begin())) {
        rollingCode = currentRollingCode;
        return true;
      }
    }

    return false;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

bool Security::checkCmacExplicitRlc(const std::vector<uint8_t> &deviceAesKey, const std::vector<uint8_t> &encryptedData, uint32_t lastRollingCode, uint32_t &newRollingCode, int32_t dataSize, int32_t rollingCodeSize, int32_t cmacSize) {
  try {
    if ((signed)encryptedData.size() < dataSize + rollingCodeSize + cmacSize) return false;
    uint32_t rollingCode = 0;
    if (rollingCodeSize == 4) rollingCode = (((uint32_t)encryptedData.at(dataSize)) << 24) | (((uint32_t)encryptedData.at(dataSize + 1)) << 16) | (((uint32_t)encryptedData.at(dataSize + 2)) << 8) | encryptedData.at(dataSize + 3);
    else if (rollingCodeSize == 3) rollingCode = (((uint32_t)encryptedData.at(dataSize)) << 16) | (((uint32_t)encryptedData.at(dataSize + 1)) << 8) | encryptedData.at(dataSize + 2);
    else if (rollingCodeSize == 2) rollingCode = (((uint32_t)encryptedData.at(dataSize)) << 8) | encryptedData.at(dataSize + 1);
    else return false;

    if (lastRollingCode >= rollingCode) return false;
    newRollingCode = rollingCode;

    std::vector<uint8_t> cmacInPacket(encryptedData.begin() + dataSize + rollingCodeSize, encryptedData.begin() + dataSize + rollingCodeSize + cmacSize);
    std::vector<uint8_t> calculatedCmac = getCmac(deviceAesKey, encryptedData, dataSize, rollingCode, rollingCodeSize, cmacSize);
    if (cmacInPacket.empty() || calculatedCmac.empty()) return false;

    return cmacInPacket.size() == calculatedCmac.size() && std::equal(cmacInPacket.begin(), cmacInPacket.end(), calculatedCmac.begin());
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return false;
}

std::vector<uint8_t> Security::getCmac(const std::vector<uint8_t> &deviceAesKey, const std::vector<uint8_t> &encryptedData, int32_t dataSize, uint32_t rollingCode, int32_t rollingCodeSize, int32_t cmacSize) {
  try {
    std::vector<uint8_t> plain;
    plain.reserve(16);
    plain.insert(plain.end(), encryptedData.begin(), encryptedData.begin() + dataSize);
    if (rollingCodeSize == 4) {
      plain.push_back((uint8_t)(rollingCode >> 24));
      plain.push_back((uint8_t)((rollingCode >> 16) & 0xFF));
      plain.push_back((uint8_t)((rollingCode >> 8) & 0xFF));
      plain.push_back((uint8_t)(rollingCode & 0xFF));
    } else if (rollingCodeSize == 3) {
      plain.push_back((uint8_t)(rollingCode >> 16));
      plain.push_back((uint8_t)((rollingCode >> 8) & 0xFF));
      plain.push_back((uint8_t)(rollingCode & 0xFF));
    } else if (rollingCodeSize == 2) {
      plain.push_back((uint8_t)(rollingCode >> 8));
      plain.push_back((uint8_t)(rollingCode & 0xFF));
    } else {
      return std::vector<uint8_t>();
    }
    bool aligned = (plain.size() % 16 == 0);
    if ((plain.size() % 16) != 0) plain.push_back(0x80);
    while ((plain.size() % 16) != 0) plain.push_back(0);

    std::vector<uint8_t> subkeyAligned = getSubkey(deviceAesKey, true);
    std::vector<uint8_t> subkeyNonAligned = getSubkey(deviceAesKey, false);
    if (subkeyAligned.size() != 16 || subkeyNonAligned.size() != 16) return std::vector<uint8_t>();

    std::lock_guard<std::mutex> encryptGuard(_encryptMutex);
    int32_t result = 0;
    if ((result = gcry_cipher_setkey(_encryptHandle, deviceAesKey.data(), deviceAesKey.size())) != GPG_ERR_NO_ERROR) {
      Gd::out.printError("Error: Could not set key for encryption: " + BaseLib::Security::Gcrypt::getError(result));
      return std::vector<uint8_t>();
    }

    std::vector<uint8_t> cmac(16);
    uint32_t currentBlock = 0;
    uint32_t blocks = plain.size() / 16;
    for (uint32_t i = 0; i < plain.size(); i++) {
      if (!aligned && currentBlock == blocks - 1) plain[i] ^= subkeyNonAligned[i % 16];
      else plain[i] ^= subkeyAligned[i % 16];

      if ((i % 16) == 15) {
        if ((result = gcry_cipher_encrypt(_encryptHandle, cmac.data(), cmac.size(), plain.data() + (currentBlock * 16), 16)) != GPG_ERR_NO_ERROR) {
          Gd::out.printError("Error encrypting data: " + BaseLib::Security::Gcrypt::getError(result));
          return std::vector<uint8_t>();
        }
        currentBlock++;
      }
    }

    cmac.resize(cmacSize);
    return cmac;
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<uint8_t>();
}

void Security::leftShiftVector(std::vector<uint8_t> &data) {
  try {
    bool carry1 = false;
    bool carry2 = false;
    for (int32_t i = data.size() - 1; i >= 0; i--) {
      carry1 = (data[i] & 0x80) == 0x80;
      data[i] = data[i] << 1;
      if (carry2) data[i] |= 1;
      carry2 = carry1;
    }
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::vector<uint8_t> Security::getSubkey(const std::vector<uint8_t> &deviceAesKey, bool aligned) {
  try {
    std::vector<uint8_t> subkey(16);
    int32_t result = 0;

    {
      std::lock_guard<std::mutex> encryptGuard(_encryptMutex);
      if ((result = gcry_cipher_setkey(_encryptHandle, deviceAesKey.data(), deviceAesKey.size())) != GPG_ERR_NO_ERROR) {
        Gd::out.printError("Error: Could not set key for encryption: " + BaseLib::Security::Gcrypt::getError(result));
        return std::vector<uint8_t>();
      }

      if ((result = gcry_cipher_encrypt(_encryptHandle, subkey.data(), subkey.size(), (void *)_subkeyInput, 16)) != GPG_ERR_NO_ERROR) {
        Gd::out.printError("Error encrypting data: " + BaseLib::Security::Gcrypt::getError(result));
        return std::vector<uint8_t>();
      }
    }

    bool useConstRb = (subkey[0] & 0x80);
    leftShiftVector(subkey);
    if (useConstRb) subkey[15] ^= (uint8_t)0x87;

    if (aligned) return subkey; //K1

    useConstRb = (subkey[0] & 0x80);
    leftShiftVector(subkey);
    if (useConstRb) subkey[15] ^= (uint8_t)0x87;

    return subkey; //K2
  }
  catch (const std::exception &ex) {
    Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<uint8_t>();
}

}
