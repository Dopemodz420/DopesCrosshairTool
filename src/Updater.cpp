#include "Updater.h"
#include "Version.h"
#include <windows.h>
#include <wininet.h>
#include <sstream>
#include <vector>
#include <thread>
#include <algorithm>

#pragma comment(lib, "wininet.lib")

namespace dopes {

static std::vector<int> ParseSemver(const std::string& s){
    std::vector<int> r;
    std::stringstream ss(s);
    std::string tok;
    while(std::getline(ss,tok,'.')){
        // strip leading v and suffix after -
        if(!tok.empty() && (tok[0]=='v' || tok[0]=='V')) tok=tok.substr(1);
        auto dash=tok.find('-'); if(dash!=std::string::npos) tok=tok.substr(0,dash);
        try{ r.push_back(std::stoi(tok)); } catch(...){ r.push_back(0); }
    }
    while(r.size()<3) r.push_back(0);
    return r;
}
int CompareVersion(const std::string& a, const std::string& b){
    auto av=ParseSemver(a), bv=ParseSemver(b);
    for(size_t i=0;i<3;++i){
        if(av[i]<bv[i]) return -1;
        if(av[i]>bv[i]) return 1;
    }
    return 0;
}
bool IsNewer(const std::string& remote, const std::string& local){
    return CompareVersion(local, remote) < 0;
}

std::wstring DefaultUpdateFeedUrl(){
    return L"https://raw.githubusercontent.com/Dopemodz420/DopesCrosshairTool/main/version.json";
}

static std::string Trim(const std::string& s){
    size_t a=s.find_first_not_of(" \t\r\n");
    if(a==std::string::npos) return "";
    size_t b=s.find_last_not_of(" \t\r\n");
    return s.substr(a,b-a+1);
}
static bool ExtractJsonString(const std::string& json, const std::string& key, std::string& out){
    std::string pat="\""+key+"\"";
    size_t p=json.find(pat);
    if(p==std::string::npos) return false;
    p=json.find(':',p);
    if(p==std::string::npos) return false;
    size_t q1=json.find('"',p+1);
    if(q1==std::string::npos) return false;
    size_t q2=q1+1; std::string raw;
    while(q2 < json.size()){
        char c=json[q2];
        if(c=='\\' && q2+1 < json.size()){
            char n=json[q2+1];
            if(n=='"'){ raw.push_back('"'); q2+=2; continue; }
            if(n=='\\'){ raw.push_back('\\'); q2+=2; continue; }
            if(n=='n'){ raw.push_back('\n'); q2+=2; continue; }
            if(n=='t'){ raw.push_back('\t'); q2+=2; continue; }
            raw.push_back(n); q2+=2; continue;
        }
        if(c=='"') break;
        raw.push_back(c); ++q2;
    }
    if(q2>=json.size()) return false;
    out=raw;
    return true;
}
static bool ExtractJsonBool(const std::string& json, const std::string& key, bool& out){
    std::string pat="\""+key+"\"";
    size_t p=json.find(pat);
    if(p==std::string::npos) return false;
    p=json.find(':',p);
    if(p==std::string::npos) return false;
    size_t q=json.find_first_not_of(" \t\r\n",p+1);
    if(q==std::string::npos) return false;
    if(json.compare(q,4,"true")==0){ out=true; return true; }
    if(json.compare(q,5,"false")==0){ out=false; return true; }
    return false;
}

bool FetchRemoteVersion(const std::wstring& url, RemoteVersion& out, std::string* err){
    HINTERNET hNet=InternetOpenW(L"DopesCrosshairTool/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if(!hNet){ if(err) *err="InternetOpen failed"; return false; }
    DWORD timeout=5000;
    InternetSetOptionW(hNet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    HINTERNET hUrl=InternetOpenUrlW(hNet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_UI, 0);
    if(!hUrl){
        if(err) *err="InternetOpenUrl failed: "+std::to_string(GetLastError());
        InternetCloseHandle(hNet); return false;
    }
    std::string data;
    char buf[4096]; DWORD read=0;
    while(InternetReadFile(hUrl, buf, sizeof(buf), &read) && read>0){
        data.append(buf, read);
        if(data.size()>64*1024) break;
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
    if(data.empty()){ if(err) *err="Empty response"; return false; }
    if(data.find("<!DOCTYPE") != std::string::npos || data.find("<html") != std::string::npos){
        if(err) *err="Feed not published yet - push version.json to GitHub (raw.githubusercontent.com/Dopemodz420/DopesCrosshairTool/main/version.json)";
        return false;
    }
    // Parse json
    std::string ver, u, inst, notes; bool mand=false;
    if(!ExtractJsonString(data,"version",ver)){
        if(err) *err="Missing version in JSON";
        return false;
    }
    ExtractJsonString(data,"url",u);
    ExtractJsonString(data,"installer",inst);
    ExtractJsonString(data,"notes",notes);
    ExtractJsonBool(data,"mandatory",mand);
    out.version=Trim(ver);
    out.url=Trim(u);
    out.installer=Trim(inst);
    out.notes=Trim(notes);
    out.mandatory=mand;
    return true;
}

void DownloadFileAsync(const std::wstring& url, const std::wstring& destPath,
    std::function<void(bool ok, std::string err)> done,
    std::function<void(unsigned long long downloaded, unsigned long long total)> progress) {
    std::thread([url, destPath, done, progress] {
        HINTERNET hNet = InternetOpenW(L"DopesCrosshairTool-Updater/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hNet) { if (done) done(false, "InternetOpen failed"); return; }
        DWORD timeout = 15000;
        InternetSetOptionW(hNet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        HINTERNET hUrl = InternetOpenUrlW(hNet, url.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
        if (!hUrl) {
            std::string e = "Download failed: " + std::to_string(GetLastError());
            InternetCloseHandle(hNet);
            if (done) done(false, e);
            return;
        }
        // Try content length for progress
        unsigned long long total = 0;
        {
            wchar_t lenBuf[64]{}; DWORD lenSize = sizeof(lenBuf);
            if (HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lenSize, NULL)) {
                try { total = std::stoull(lenBuf); } catch (...) { total = 0; }
            }
        }
        HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            InternetCloseHandle(hUrl); InternetCloseHandle(hNet);
            if (done) done(false, "Cannot write temp file");
            return;
        }
        char buf[32768]; DWORD read = 0;
        unsigned long long downloaded = 0;
        bool netOk = true;
        std::string netErr;
        while (true) {
            BOOL r = InternetReadFile(hUrl, buf, sizeof(buf), &read);
            if (!r) { netOk = false; netErr = "Download interrupted: " + std::to_string(GetLastError()); break; }
            if (read == 0) break;
            DWORD written = 0;
            if (!WriteFile(hFile, buf, read, &written, nullptr) || written != read) {
                netOk = false; netErr = "Cannot write temp file (disk?)"; break;
            }
            downloaded += read;
            if (progress) progress(downloaded, total);
            if (downloaded > 512ULL * 1024 * 1024) { netOk = false; netErr = "File too large, aborted"; break; }
        }
        CloseHandle(hFile);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hNet);
        if (!netOk) { DeleteFileW(destPath.c_str()); if (done) done(false, netErr); return; }
        if (downloaded < 1024 * 1024) {
            // Likely an HTML error page, not the installer
            DeleteFileW(destPath.c_str());
            if (done) done(false, "Download too small — release asset not published yet");
            return;
        }
        if (done) done(true, "");
    }).detach();
}

void CheckForUpdateAsync(const std::wstring& feedUrl, std::function<void(bool, RemoteVersion, std::string)> cb){
    std::wstring url=feedUrl.empty()? DefaultUpdateFeedUrl() : feedUrl;
    std::thread([url,cb]{
        RemoteVersion remote;
        std::string err;
        bool ok=FetchRemoteVersion(url, remote, &err);
        bool has=false;
        if(ok) has=IsNewer(remote.version, kVersion);
        cb(has, remote, err);
    }).detach();
}

}
