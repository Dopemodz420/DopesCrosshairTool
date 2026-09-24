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
    std::string ver, u, notes; bool mand=false;
    if(!ExtractJsonString(data,"version",ver)){
        if(err) *err="Missing version in JSON";
        return false;
    }
    ExtractJsonString(data,"url",u);
    ExtractJsonString(data,"notes",notes);
    ExtractJsonBool(data,"mandatory",mand);
    out.version=Trim(ver);
    out.url=Trim(u);
    out.notes=Trim(notes);
    out.mandatory=mand;
    return true;
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
