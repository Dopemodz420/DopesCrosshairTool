#pragma once
#include <string>
#include <functional>

namespace dopes {

struct RemoteVersion {
    std::string version = "0.0.0";
    std::string url;
    std::string notes;
    bool mandatory = false;
};

// Semver compare: returns -1 if a<b, 0 if equal, 1 if a>b
int CompareVersion(const std::string& a, const std::string& b);
bool IsNewer(const std::string& remote, const std::string& local);

// Fetch remote version.json from URL (WinInet). Returns true on success.
bool FetchRemoteVersion(const std::wstring& url, RemoteVersion& out, std::string* err=nullptr);

// Default feed URL (raw GitHub). Change to your repo raw URL after you create it.
std::wstring DefaultUpdateFeedUrl();

// Async check: calls cb(true, remote) if newer version available, else cb(false,{})
void CheckForUpdateAsync(const std::wstring& feedUrl, std::function<void(bool hasUpdate, RemoteVersion remote, std::string err)> cb);

}
