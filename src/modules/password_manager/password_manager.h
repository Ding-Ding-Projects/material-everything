#pragma once
// Material Everything - Password Manager module
// Encrypted local password vault. Plaintext passwords never touch disk.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace me::password_manager {

struct VaultEntry {
    std::string id;
    std::string title;
    std::string username;
    std::string password;   // plaintext only in memory
    std::string url;
    std::string notes;
};

struct GeneratorOptions {
    int length = 16;
    bool lowercase = true;
    bool uppercase = true;
    bool digits = true;
    bool symbols = true;
};

class PasswordManager {
public:
    explicit PasswordManager(std::string vaultPath = {});

    bool createVault(const std::string& masterPassword);
    bool unlock(const std::string& masterPassword);
    bool isUnlocked() const;
    void lock();

    std::optional<std::string> addEntry(const VaultEntry& entry);
    bool updateEntry(const std::string& id, const VaultEntry& entry);
    bool removeEntry(const std::string& id);
    std::optional<VaultEntry> getEntry(const std::string& id) const;
    std::vector<VaultEntry> listEntries() const;

    std::vector<VaultEntry> search(const std::string& query) const;

    static std::string generatePassword(const GeneratorOptions& opts);
    bool copyToClipboard(const std::string& id, int timeoutMs = 30000);

private:
    struct EncryptedVault {
        std::vector<uint8_t> salt;
        std::vector<uint8_t> iv;
        std::vector<uint8_t> ciphertext;
        std::vector<uint8_t> tag;
    };

    bool saveVault();
    bool loadVaultFile();
    std::vector<uint8_t> deriveKey(const std::string& password,
                                   const std::vector<uint8_t>& salt) const;
    std::string serializeEntries() const;
    bool deserializeEntries(const std::string& json);
    static std::string generateId();

    std::string vaultPath_;
    std::vector<VaultEntry> entries_;
    std::vector<uint8_t> masterKey_;
    std::vector<uint8_t> storedSalt_;
    std::optional<EncryptedVault> storedVault_;
    bool unlocked_ = false;
};

}  // namespace me::password_manager
