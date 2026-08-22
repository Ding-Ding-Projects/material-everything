#include "password_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

// Windows: clipboard via Win32; AES-256-GCM + PBKDF2 via OpenSSL EVP API.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace me::password_manager {
namespace {

constexpr size_t kSaltSize = 32;
constexpr size_t kIvSize = 12;
constexpr size_t kTagSize = 16;
constexpr int kPbkdf2Iterations = 310000;

std::string defaultVaultPath() {
#ifdef _WIN32
    char buf[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH)) {
        return std::string(buf) + "\\MaterialEverything\\vault.mev";
    }
#endif
    return "vault.mev";
}

void secureZero(std::vector<uint8_t>& v) {
    volatile uint8_t* p = v.data();
    for (size_t i = 0; i < v.size(); ++i) p[i] = 0;
}

// Minimal JSON escaping for entry serialization.
std::string jsonEscape(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<uint8_t>(c) < 0x20)
                    out << "\\u" << std::hex << static_cast<int>(c);
                else
                    out << c;
        }
    }
    return out.str();
}

}  // namespace

PasswordManager::PasswordManager(std::string vaultPath)
    : vaultPath_(std::move(vaultPath)) {
    if (vaultPath_.empty()) vaultPath_ = defaultVaultPath();
}

bool PasswordManager::createVault(const std::string& masterPassword) {
    storedSalt_.resize(kSaltSize);
    RAND_bytes(storedSalt_.data(), kSaltSize);
    masterKey_ = deriveKey(masterPassword, storedSalt_);

    // Serialize empty entries and store encrypted blob so unlock can verify.
    unlocked_ = true;
    bool ok = saveVault();
    if (!ok) { unlocked_ = false; secureZero(masterKey_); }
    return ok;
}

bool PasswordManager::unlock(const std::string& masterPassword) {
    if (!loadVaultFile()) return false;
    auto candidate = deriveKey(masterPassword, storedSalt_);
    // Verify by decrypting with the candidate key.
    const auto& sv = *storedVault_;
    std::vector<uint8_t> plaintext(sv.ciphertext.size());
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    bool ok = ctx != nullptr &&
              EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) == 1 &&
              EVP_DecryptInit_ex(ctx, nullptr, nullptr, candidate.data(), sv.iv.data()) == 1;
    int len = 0, total = 0;
    if (ok && !sv.ciphertext.empty()) {
        ok = EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                               sv.ciphertext.data(), (int)sv.ciphertext.size()) == 1;
        total += len;
    } else if (!ok) {
        // Empty ciphertext case handled below.
    }
    if (ok) {
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagSize,
                            const_cast<uint8_t*>(sv.tag.data()));
        ok = EVP_DecryptFinal_ex(ctx, plaintext.data() + total, &len) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        secureZero(candidate);
        return false;
    }
    secureZero(candidate);
    masterKey_ = deriveKey(masterPassword, storedSalt_);
    deserializeEntries(reinterpret_cast<const char*>(plaintext.data()));
    unlocked_ = true;
    return true;
}

bool PasswordManager::isUnlocked() const { return unlocked_; }

void PasswordManager::lock() {
    secureZero(masterKey_);
    entries_.clear();
    unlocked_ = false;
}

std::optional<std::string> PasswordManager::addEntry(const VaultEntry& entry) {
    if (!unlocked_) return std::nullopt;
    VaultEntry e = entry;
    e.id = generateId();
    entries_.push_back(e);
    if (!saveVault()) { entries_.pop_back(); return std::nullopt; }
    return e.id;
}

bool PasswordManager::updateEntry(const std::string& id, const VaultEntry& entry) {
    if (!unlocked_) return false;
    for (auto& e : entries_) {
        if (e.id == id) {
            VaultEntry backup = e;
            entry.id = id;  // preserve identity
            e = entry;
            e.id = id;
            if (!saveVault()) { e = backup; return false; }
            return true;
        }
    }
    return false;
}

bool PasswordManager::removeEntry(const std::string& id) {
    if (!unlocked_) return false;
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const VaultEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    VaultEntry removed = *it;
    entries_.erase(it);
    if (!saveVault()) { entries_.push_back(removed); return false; }
    return true;
}

std::optional<VaultEntry> PasswordManager::getEntry(const std::string& id) const {
    for (const auto& e : entries_)
        if (e.id == id) return e;
    return std::nullopt;
}

std::vector<VaultEntry> PasswordManager::listEntries() const { return entries_; }

std::vector<VaultEntry> PasswordManager::search(const std::string& query) const {
    if (query.empty()) return entries_;
    std::string q;
    q.reserve(query.size());
    for (char c : query) q += (char)tolower((uint8_t)c);

    std::vector<VaultEntry> results;
    for (const auto& e : entries_) {
        auto contains = [&](const std::string& field) {
            std::string f;
            f.reserve(field.size());
            for (char c : field) f += (char)tolower((uint8_t)c);
            return f.find(q) != std::string::npos;
        };
        if (contains(e.title) || contains(e.username) || contains(e.url))
            results.push_back(e);
    }
    return results;
}

std::string PasswordManager::generatePassword(const GeneratorOptions& opts) {
    std::string pool;
    if (opts.lowercase) pool += "abcdefghijklmnopqrstuvwxyz";
    if (opts.uppercase) pool += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (opts.digits) pool += "0123456789";
    if (opts.symbols) pool += "!@#$%^&*-_=+[]{}|;:,.<>?/";
    if (pool.empty() || opts.length < 4) return "";

    std::string result;
    result.reserve(opts.length);
    for (int i = 0; i < opts.length; ++i) {
        uint8_t b;
        RAND_bytes(&b, 1);
        result += pool[b % pool.size()];
    }
    return result;
}

bool PasswordManager::copyToClipboard(const std::string& id, int timeoutMs) {
    auto entry = getEntry(id);
    if (!entry || !unlocked_) return false;

#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, entry->password.size() + 1);
    if (!hg) { CloseClipboard(); return false; }
    memcpy(GlobalLock(hg), entry->password.c_str(), entry->password.size() + 1);
    GlobalUnlock(hg);
    SetClipboardData(CF_TEXT, hg);
    CloseClipboard();

    // Auto-clear timer.
    std::thread([timeoutMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        if (OpenClipboard(nullptr)) {
            HANDLE data = GetClipboardData(CF_TEXT);
            if (data) {
                char* text = (char*)GlobalLock(data);
                if (text) {
                    SecureZeroMemory(text, strlen(text));
                    GlobalUnlock(data);
                }
            }
            EmptyClipboard();
            CloseClipboard();
        }
    }).detach();
    return true;
#else
    return false;
#endif
}

bool PasswordManager::saveVault() {
    std::string json = serializeEntries();
    EncryptedVault sv;
    sv.salt = storedSalt_;
    sv.iv.resize(kIvSize);
    RAND_bytes(sv.iv.data(), kIvSize);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    sv.ciphertext.resize(json.size());
    sv.tag.resize(kTagSize);
    int len = 0, total = 0;
    bool ok = ctx != nullptr &&
              EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) == 1 &&
              EVP_EncryptInit_ex(ctx, nullptr, nullptr, masterKey_.data(), sv.iv.data()) == 1;
    if (ok && !json.empty()) {
        ok = EVP_EncryptUpdate(ctx, sv.ciphertext.data(), &len,
                               reinterpret_cast<const uint8_t*>(json.data()), (int)json.size()) == 1;
        total = len;
    }
    if (ok) {
        EVP_EncryptFinal_ex(ctx, sv.ciphertext.data() + total, &len);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagSize, sv.tag.data());
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return false;

    // Binary format: salt(32) | iv(12) | tag(16) | ciphertext(rest).
    std::ofstream f(vaultPath_, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(sv.salt.data()), sv.salt.size());
    f.write(reinterpret_cast<const char*>(sv.iv.data()), sv.iv.size());
    f.write(reinterpret_cast<const char*>(sv.tag.data()), sv.tag.size());
    f.write(reinterpret_cast<const char*>(sv.ciphertext.data()), sv.ciphertext.size());
    storedVault_ = sv;
    return true;
}

bool PasswordManager::loadVaultFile() {
    std::ifstream f(vaultPath_, std::ios::binary);
    if (!f) return false;
    EncryptedVault sv;
    sv.salt.resize(kSaltSize);
    sv.iv.resize(kIvSize);
    sv.tag.resize(kTagSize);
    f.read(reinterpret_cast<char*>(sv.salt.data()), kSaltSize);
    f.read(reinterpret_cast<char*>(sv.iv.data()), kIvSize);
    f.read(reinterpret_cast<char*>(sv.tag.data()), kTagSize);
    if (!f.good()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string rest = ss.str();
    sv.ciphertext.assign(rest.begin(), rest.end());
    storedVault_ = std::move(sv);
    storedSalt_ = storedVault_->salt;
    return true;
}

std::vector<uint8_t> PasswordManager::deriveKey(
    const std::string& password, const std::vector<uint8_t>& salt) const {
    std::vector<uint8_t> key(32);
    PKCS5_PBKDF2_HMAC(password.data(), (int)password.size(),
                      salt.data(), salt.size(),
                      kPbkdf2Iterations, EVP_sha256(), key.size(), key.data());
    return key;
}

std::string PasswordManager::serializeEntries() const {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (i > 0) out << ",";
        out << "{\"id\":\"" << jsonEscape(entries_[i].id)
            << "\",\"title\":\"" << jsonEscape(entries_[i].title)
            << "\",\"username\":\"" << jsonEscape(entries_[i].username)
            << "\",\"password\":\"" << jsonEscape(entries_[i].password)
            << "\",\"url\":\"" << jsonEscape(entries_[i].url)
            << "\",\"notes\":\"" << jsonEscape(entries_[i].notes)
            << "\"}";
    }
    out << "]";
    return out.str();
}

bool PasswordManager::deserializeEntries(const std::string& json) {
    // Simple JSON array parser for the known flat schema.
    entries_.clear();
    size_t pos = 0;
    auto skipWs = [&]() { while (pos < json.size() && isspace((uint8_t)json[pos])) ++pos; };
    auto parseString = [&]() -> std::string {
        if (pos >= json.size() || json[pos] != '"') return "";
        ++pos;
        std::string s;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                }
            } else {
                s += json[pos];
            }
            ++pos;
        }
        if (pos < json.size()) ++pos;  // closing quote
        return s;
    };

    skipWs();
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;
    skipWs();
    while (pos < json.size() && json[pos] != ']') {
        skipWs();
        if (pos >= json.size() || json[pos] != '{') return false;
        ++pos;
        VaultEntry e;
        skipWs();
        while (pos < json.size() && json[pos] != '}') {
            skipWs();
            std::string key = parseString();
            skipWs();
            if (pos < json.size() && json[pos] == ':') ++pos;
            skipWs();
            std::string val = parseString();
            if (key == "id") e.id = val;
            else if (key == "title") e.title = val;
            else if (key == "username") e.username = val;
            else if (key == "password") e.password = val;
            else if (key == "url") e.url = val;
            else if (key == "notes") e.notes = val;
            skipWs();
            if (pos < json.size() && json[pos] == ',') { ++pos; skipWs(); }
        }
        if (pos < json.size()) ++pos;  // closing brace
        skipWs();
        if (pos < json.size() && json[pos] == ',') { ++pos; skipWs(); }
        entries_.push_back(std::move(e));
    }
    return true;
}

std::string PasswordManager::generateId() {
    uint8_t bytes[16];
    RAND_bytes(bytes, sizeof(bytes));
    char hex[33];
    for (int i = 0; i < 16; ++i)
        snprintf(hex + i * 2, 3, "%02x", bytes[i]);
    return std::string(hex, 32);
}

}  // namespace me::password_manager
