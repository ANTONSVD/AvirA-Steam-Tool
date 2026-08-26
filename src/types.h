#pragma once
#include <string>

enum class AccStatus { None, Checking, Valid, Guard, Invalid, Error, RateLimited };

struct Cred {
    std::string user;
    std::string pass;
};

struct Account {
    std::string user;
    std::string pass;
    AccStatus status = AccStatus::None;
    long long addedAt = 0;
};

struct CheckOutcome {
    AccStatus status;
    std::string message;
};

inline const char* StatusLabel(AccStatus s) {
    switch (s) {
        case AccStatus::Valid: return "VALID";
        case AccStatus::Guard: return "2FA";
        case AccStatus::Invalid: return "BAD";
        case AccStatus::Checking: return "...";
        case AccStatus::RateLimited: return "RATE";
        case AccStatus::Error: return "ERR";
        default: return "-";
    }
}
