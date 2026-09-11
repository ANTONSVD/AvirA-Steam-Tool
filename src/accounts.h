#pragma once
#include "types.h"
#include <string>
#include <vector>

class AccountStore {
public:
    void Load(const std::string& path);
    void Save(const std::string& path);
    bool AddOrUpdate(const Cred& cred, AccStatus st);
    void SetBan(const std::string& user, const std::string& ban, int days);
    void SetSteamId(const std::string& user, const std::string& steamid);
    bool Remove(const std::string& user);
    void Clear();
    Account* Find(const std::string& user);
    std::vector<Account>& Items() { return m_items; }
    int CountValid() const;
    int CountGuard() const;

private:
    std::vector<Account> m_items;
};

std::vector<Cred> ParseCombos(const std::string& text);
void ExportHits(const std::string& path, const std::vector<Account>& items);
