#include "rsa.h"
#include "util.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

static bool HexToBytes(const std::string& hex, std::vector<unsigned char>& out) {
    out.clear();
    if (hex.size() % 2) return false;
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back((unsigned char)((hi << 4) | lo));
    }
    while (out.size() > 1 && out[0] == 0) out.erase(out.begin());
    return true;
}

std::string RsaEncryptPassword(const std::string& modHex, const std::string& expHex,
                               const std::string& password) {
    std::vector<unsigned char> mod, exp;
    if (!HexToBytes(modHex, mod) || !HexToBytes(expHex, exp)) return "";
    if (mod.empty() || exp.empty() || exp.size() > 255 || mod.size() < 8) return "";

    size_t blobSize = sizeof(BCRYPT_RSAKEY_BLOB) + exp.size() + mod.size();
    std::vector<unsigned char> blob(blobSize);
    BCRYPT_RSAKEY_BLOB* hdr = (BCRYPT_RSAKEY_BLOB*)blob.data();
    hdr->Magic = BCRYPT_RSAPUBLIC_MAGIC;
    hdr->BitLength = (ULONG)mod.size() * 8;
    hdr->cbPublicExp = (ULONG)exp.size();
    hdr->cbModulus = (ULONG)mod.size();
    hdr->cbPrime1 = 0;
    hdr->cbPrime2 = 0;
    memcpy(blob.data() + sizeof(BCRYPT_RSAKEY_BLOB), exp.data(), exp.size());
    memcpy(blob.data() + sizeof(BCRYPT_RSAKEY_BLOB) + exp.size(), mod.data(), mod.size());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::string result;

    do {
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM,
                                                    MS_PRIMITIVE_PROVIDER, 0)))
            break;
        if (!NT_SUCCESS(BCryptImportKeyPair(hAlg, nullptr, BCRYPT_RSAPUBLIC_BLOB, &hKey,
                                            blob.data(), (ULONG)blob.size(), 0)))
            break;
        ULONG cbOut = 0;
        if (!NT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)password.data(),
                                      (ULONG)password.size(), nullptr, nullptr, 0,
                                      nullptr, 0, &cbOut, BCRYPT_PAD_PKCS1)))
            break;
        std::vector<unsigned char> enc(cbOut);
        if (!NT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)password.data(),
                                      (ULONG)password.size(), nullptr, nullptr, 0,
                                      enc.data(), cbOut, &cbOut, BCRYPT_PAD_PKCS1)))
            break;
        enc.resize(cbOut);
        result = util::Base64Encode(enc.data(), enc.size());
    } while (false);

    if (hKey) BCryptDestroyKey(hKey);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}
