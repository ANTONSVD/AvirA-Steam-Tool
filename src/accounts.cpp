#include "accounts.h"
#include "util.h"
#include "steamtoken.h"

static std::string StatusTag(AccStatus s) {
    switch (s) {
        case AccStatus::Valid: return "valid";
        case AccStatus::Guard: return "guard";
        default: return "none";
    }
}

static AccStatus TagStatus(const std::string& t) {
    if (t == "valid") return AccStatus::Valid;
    if (t == "guard") return AccStatus::Guard;
    return AccStatus::None;
}

void AccountStore::Load(const std::string& path) {
    std::string data;
    if (!util::ReadTextFile(path, data)) return;
    long long base = util::NowMs() / 1000;
    long long order = 0;
    for (auto& line : util::SplitLines(data)) {
        std::string l = util::Trim(line);
        if (l.empty() || l[0] == '#') continue;
        size_t t1 = l.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = l.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        Account a;
        a.user = l.substr(0, t1);
        a.status = TagStatus(l.substr(t1 + 1, t2 - t1 - 1));
        size_t t3 = l.find('\t', t2 + 1);
        if (t3 != std::string::npos) {
            a.pass = l.substr(t2 + 1, t3 - t2 - 1);
            size_t cur = t3 + 1;
            while (cur < l.size()) {
                size_t tn = l.find('\t', cur);
                std::string field = tn == std::string::npos
                                        ? l.substr(cur)
                                        : l.substr(cur, tn - cur);
                if (field.rfind("sid:", 0) == 0) {
                    a.steamid = field.substr(4);
                } else if (field.rfind("tok:", 0) == 0) {
                    a.token = field.substr(4);
                } else if (a.ban.empty()) {
                    a.ban = field;
                } else {
                    a.banDays = atoi(field.c_str());
                }
                if (tn == std::string::npos) break;
                cur = tn + 1;
            }
        } else {
            a.pass = l.substr(t2 + 1);
        }
        a.addedAt = base - order;
        order++;
        if (a.user.empty()) continue;
        for (auto& e : m_items)
            if (e.user == a.user) {
                e.status = a.status;
                e.pass = a.pass;
                e.token = a.token;
                e.ban = a.ban;
                e.banDays = a.banDays;
                e.steamid = a.steamid;
                goto next;
            }
        m_items.push_back(a);
    next:;
    }
}

void AccountStore::Save(const std::string& path) {
    std::string out = "# user\tstatus\tpassword[...tab...ban[...tab...days][...tab...sid:steamid][...tab...tok:token]\n";
    for (auto& a : m_items) {
        out += a.user + "\t" + StatusTag(a.status) + "\t" + a.pass;
        if (!a.ban.empty()) out += "\t" + a.ban + "\t" + std::to_string(a.banDays);
        if (!a.steamid.empty()) out += "\tsid:" + a.steamid;
        if (!a.token.empty()) out += "\ttok:" + a.token;
        out += "\n";
    }
    util::WriteTextFile(path, out);
}

bool AccountStore::AddOrUpdate(const Cred& cred, AccStatus st) {
    for (auto& a : m_items) {
        if (a.user == cred.user) {
            if (st == AccStatus::Valid || st == AccStatus::Guard) {
                a.status = st;
                a.pass = cred.pass;
                a.token = cred.token;
                return true;
            }
            return false;
        }
    }
    if (st != AccStatus::Valid && st != AccStatus::Guard) return false;
    Account a;
    a.user = cred.user;
    a.pass = cred.pass;
    a.token = cred.token;
    a.status = st;
    a.addedAt = util::NowMs() / 1000;
    m_items.push_back(a);
    return true;
}

void AccountStore::SetBan(const std::string& user, const std::string& ban, int days) {
    for (auto& a : m_items) {
        if (a.user == user) {
            a.ban = ban;
            a.banDays = days;
            return;
        }
    }
}

void AccountStore::SetSteamId(const std::string& user, const std::string& steamid) {
    for (auto& a : m_items) {
        if (a.user == user) {
            if (!steamid.empty()) a.steamid = steamid;
            return;
        }
    }
}

void AccountStore::SetToken(const std::string& user, const std::string& token) {
    for (auto& a : m_items) {
        if (a.user == user) {
            if (!token.empty()) a.token = token;
            return;
        }
    }
}

bool AccountStore::Remove(const std::string& user) {
    for (size_t i = 0; i < m_items.size(); i++) {
        if (m_items[i].user == user) {
            m_items.erase(m_items.begin() + i);
            return true;
        }
    }
    return false;
}

void AccountStore::Clear() { m_items.clear(); }

Account* AccountStore::Find(const std::string& user) {
    for (auto& a : m_items)
        if (a.user == user) return &a;
    return nullptr;
}

int AccountStore::CountValid() const {
    int n = 0;
    for (auto& a : m_items) if (a.status == AccStatus::Valid) n++;
    return n;
}

int AccountStore::CountGuard() const {
    int n = 0;
    for (auto& a : m_items) if (a.status == AccStatus::Guard) n++;
    return n;
}

std::vector<Cred> ParseCombos(const std::string& text) {
    std::vector<Cred> out;
    for (auto& line : util::SplitLines(text)) {
        std::string l = util::Trim(line);
        if (l.empty() || l[0] == '#') continue;
        Cred c;
        size_t sep = l.find("----");
        if (sep != std::string::npos && sep > 0) {
            std::string right = util::Trim(l.substr(sep + 4));
            if (steamtoken::LooksLikeJwt(right)) {
                c.user = util::Trim(l.substr(0, sep));
                c.token = right;
                if (c.user.empty()) continue;
                bool dup = false;
                for (auto& e : out)
                    if (e.user == c.user) { dup = true; break; }
                if (!dup) out.push_back(c);
                continue;
            }
        }
        if (steamtoken::LooksLikeJwt(l)) {
            steamtoken::JwtClaims jc = steamtoken::ParseJwt(l);
            if (!jc.ok) continue;
            c.user = jc.sub;
            c.token = l;
            bool dup = false;
            for (auto& e : out)
                if (e.user == c.user) { dup = true; break; }
            if (!dup) out.push_back(c);
            continue;
        }
        size_t p1 = l.find(':');
        if (p1 == std::string::npos || p1 == 0) continue;
        size_t p2 = l.find(':', p1 + 1);
        c.user = util::Trim(l.substr(0, p1));
        c.pass = p2 == std::string::npos ? util::Trim(l.substr(p1 + 1))
                                         : util::Trim(l.substr(p1 + 1, p2 - p1 - 1));
        if (c.user.empty() || c.pass.empty()) continue;
        if (steamtoken::LooksLikeJwt(c.pass)) {
            c.token = c.pass;
            c.pass.clear();
        }
        bool dup = false;
        for (auto& e : out) if (e.user == c.user) { dup = true; break; }
        if (!dup) out.push_back(c);
    }
    return out;
}

static bool LineHasToken(const std::string& l) {
    size_t sep = l.find("----");
    if (sep != std::string::npos && sep > 0 &&
        steamtoken::LooksLikeJwt(util::Trim(l.substr(sep + 4))))
        return true;
    if (steamtoken::LooksLikeJwt(l)) return true;
    size_t p1 = l.find(':');
    if (p1 != std::string::npos && p1 > 0 &&
        steamtoken::LooksLikeJwt(util::Trim(l.substr(p1 + 1))))
        return true;
    return false;
}

std::string StripTokenLines(const std::string& text) {
    std::string out;
    for (auto& line : util::SplitLines(text)) {
        std::string l = util::Trim(line);
        if (l.empty() || l[0] == '#') {
            out += line + "\n";
            continue;
        }
        if (LineHasToken(l)) continue;
        out += line + "\n";
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    if (!out.empty()) out += "\n";
    return out;
}

void ExportHits(const std::string& path, const std::vector<Account>& items) {    std::string out;
    for (auto& a : items) {
        if (a.status == AccStatus::Valid || a.status == AccStatus::Guard)
            out += a.user + ":" + (!a.token.empty() ? a.token : a.pass) + "\n";
    }
    util::WriteTextFile(path, out);
}
