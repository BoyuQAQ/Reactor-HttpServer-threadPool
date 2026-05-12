#include "ApiSharepicEx.h"
#include "ApiCommon.h"
#include "../log/Logging.h"
#include "../mysql/DBPool.h"
#include "../redis/CachePool.h"
#include <jsoncpp/json/json.h>
#include <random>
#include <sstream>
#include <cstdlib>
#include <cstring>

std::string generateShareKey(int length = 6) {
    static const char ch[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(ch) - 2);
    
    std::string key;
    for (int i = 0; i < length; i++) {
        key += ch[dis(gen)];
    }
    return key;
}

std::string generateUrlMd5(const std::string &input) {
    std::hash<std::string> hasher;
    size_t h = hasher(input);
    
    std::stringstream ss;
    ss << std::hex << h;
    return ss.str();
}
 
int decodeSharepicJson(const std::string &str_json, std::string &user, std::string &token,
                     std::string &md5, std::string &filename)
{
    Json::Reader reader;
    Json::Value root;
    
    if (!reader.parse(str_json, root)) {
        return -1;
    }
    
    user = root["user"].asString();
    token = root["token"].asString();
    md5 = root["md5"].asString();
    filename = root["filename"].asString();
    
    return 0;
}

int ApiSharepicEx(std::string &url, std::string &post_data, std::string &str_json)
{
    std::string user, token, md5, filename;
    
    if (decodeSharepicJson(post_data, user, token, md5, filename) < 0) {
        Json::Value response;
        response["code"] = 1;
        response["message"] = "Invalid parameters";
        str_json = response.toStyledString();
        return 0;
    }
    
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("tuchuang_slave");
    
    Json::Value response;
    
    if (!pCacheConn) {
        response["code"] = 1;
        response["message"] = "Redis connection failed";
        str_json = response.toStyledString();
        return 0;
    }
    
    std::string user_key = user + "_token";
    if (!pCacheConn->isExists(user_key) || 
        pCacheConn->get(user_key) != token) {
        response["code"] = 4;
        response["message"] = "Token expired";
        str_json = response.toStyledString();
        pCacheManager->RelCacheConn(pCacheConn);
        return 0;
    }
    
    std::string share_key = generateShareKey(6);
    std::string url_md5 = generateUrlMd5(share_key + md5);
    
    char sql_cmd[2048] = {0};
    sprintf(sql_cmd, 
            "INSERT INTO share_picture_list (user, filemd5, file_name, urlmd5, key, pv) VALUES ('%s', '%s', '%s', '%s', '%s', 0)",
            user.c_str(), md5.c_str(), filename.c_str(), url_md5.c_str(), share_key.c_str());
    
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetDBConn("tuchuang_slave");
    
    if (pDBConn) {
        pDBConn->ExecuteCreate(sql_cmd);
        pDBManager->RelDBConn(pDBConn);
    }
    
    pCacheManager->RelCacheConn(pCacheConn);
    
    response["code"] = 0;
    // 动态 host 前缀，优先从环境变量 SHARE_HOST 获取，若无则回退到 localhost
    std::string host_prefix = "http://localhost";
    const char* envHost = std::getenv("SHARE_HOST");
    if (envHost && std::strlen(envHost) > 0) host_prefix = envHost;
    response["url"] = host_prefix + "/pic/" + url_md5 + "?key=" + share_key;
    response["key"] = share_key;
    str_json = response.toStyledString();
    return 0;
}
