#include "ApiChunk.h"
#include "ApiCommon.h"
#include "../log/Logging.h"
#include "../mysql/DBPool.h"
#include "../redis/CachePool.h"
#include <jsoncpp/json/json.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <fdfs_client.h>

#define CHUNK_SIZE (10 * 1024 * 1024)  // 10MB per chunk
#define CHUNK_TEMP_DIR "/tmp/chunks"
#define CHUNK_KEY_PREFIX "chunk:"

class ChunkUploader {
public:
    std::string md5;
    std::string filename;
    std::string user;
    int64_t filesize;
    int chunk_count;
    std::string uploaded;  // comma separated uploaded chunk indices
    
    ChunkUploader() : filesize(0), chunk_count(0) {}
};

int decodeChunkInitJson(const std::string &str_json, std::string &user, std::string &token,
                        std::string &filename, std::string &md5, int64_t &filesize, int &chunk_count)
{
    Json::Reader reader;
    Json::Value root;
    
    if (!reader.parse(str_json, root)) {
        return -1;
    }
    
    user = root["user"].asString();
    token = root["token"].asString();
    filename = root["filename"].asString();
    md5 = root["md5"].asString();
    filesize = root["size"].asInt64();
    chunk_count = root["chunk_count"].asInt();
    
    return 0;
}

int decodeChunkUploadJson(const std::string &str_json, std::string &user, std::string &token,
                        std::string &md5, int &chunk_index)
{
    Json::Reader reader;
    Json::Value root;
    
    if (!reader.parse(str_json, root)) {
        return -1;
    }
    
    user = root["user"].asString();
    token = root["token"].asString();
    md5 = root["md5"].asString();
    chunk_index = root["index"].asInt();
    
    return 0;
}

int decodeChunkMergeJson(const std::string &str_json, std::string &user, std::string &token,
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

bool checkChunkComplete(const std::string &uploaded, int total_chunks)
{
    std::stringstream ss(uploaded);
    std::string token;
    int count = 0;
    
    while (std::getline(ss, token, ',')) {
        count++;
    }
    
    return count >= total_chunks;
}

int ApiChunk(std::string &url, std::string &post_data, std::string &str_json)
{
    std::string cmd;
    
    size_t cmd_pos = url.find("cmd=");
    if (cmd_pos != std::string::npos) {
        cmd = url.substr(cmd_pos + 4);
    }
    
    Json::Value response;
    
    if (cmd == "init") {
        std::string user, token, filename, md5;
        int64_t filesize;
        int chunk_count;
        
        if (decodeChunkInitJson(post_data, user, token, filename, md5, filesize, chunk_count) < 0) {
            response["code"] = 1;
            response["message"] = "Invalid parameters";
            str_json = response.toStyledString();
            return 0;
        }
        
        CacheManager *pCacheManager = CacheManager::getInstance();
        CacheConn *pCacheConn = pCacheManager->GetCacheConn("tuchuang_slave");
        if (!pCacheConn) {
            response["code"] = 1;
            response["message"] = "Redis connection failed";
            str_json = response.toStyledString();
            return 0;
        }
        
        std::string key = std::string(CHUNK_KEY_PREFIX) + md5;
        
        std::string user_key = user + "_token";
        if (!pCacheConn->isExists(user_key) || 
            pCacheConn->get(user_key) != token) {
            response["code"] = 4;
            response["message"] = "Token expired";
            str_json = response.toStyledString();
            pCacheManager->RelCacheConn(pCacheConn);
            return 0;
        }

        // 尝试从 Redis 读取已上传的分片信息，支持断点续传
        std::string redisVal = pCacheConn->get(key);
        std::string uploaded_str;
        if (!redisVal.empty()) {
            size_t pos = redisVal.find("uploaded=");
            if (pos != std::string::npos) {
                uploaded_str = redisVal.substr(pos + 9);
            }
        }

        // 如果有上传记录，返回已上传的分片列表
        if (!uploaded_str.empty()) {
            response["code"] = 0;
            response["message"] = "部分分片已存在";
            response["uploaded"] = uploaded_str;
            str_json = response.toStyledString();
            pCacheManager->RelCacheConn(pCacheConn);
            return 0;
        }

        // 继续原有流程：查询文件是否已存在，若不存在则创建分片工作区
        char sql_cmd[1024] = {0};
        sprintf(sql_cmd, "SELECT id FROM file_info WHERE md5 = '%s'", md5.c_str());
        
        CDBManager *pDBManager = CDBManager::getInstance();
        CDBConn *pDBConn = pDBManager->GetDBConn("tuchuang_slave");
        
        if (pDBConn) {
            CResultSet *pResultSet = pDBConn->ExecuteQuery(sql_cmd);
            if (pResultSet && pResultSet->Next()) {
                pDBManager->RelDBConn(pDBConn);
                pCacheManager->RelCacheConn(pCacheConn);
                response["code"] = 0;
                response["message"] = "File already exists";
                str_json = response.toStyledString();
                return 0;
            }
        }
        
        std::string temp_dir = std::string(CHUNK_TEMP_DIR) + "/" + md5;
        mkdir(temp_dir.c_str(), 0755);
        
        char keyinfo[2048] = {0};
        sprintf(keyinfo, "filename=%s&filesize=%lld&chunk_count=%d&user=%s&uploaded=",
                filename.c_str(), (long long)filesize, chunk_count, user.c_str());
        
        pCacheConn->setex(key, 86400, keyinfo);
        
        pCacheManager->RelCacheConn(pCacheConn);
        if (pDBConn) pDBManager->RelDBConn(pDBConn);
        
        response["code"] = 0;
        response["uploaded"] = "";
        str_json = response.toStyledString();
        return 0;
    }
    
    else if (cmd == "upload") {
        char user[64] = {0};
        char token[128] = {0};
        char md5[64] = {0};
        char index_str[32] = {0};
        int chunk_index = 0;
        
        char *begin = (char *)post_data.c_str();
        char *p1, *p2;
        
        p1 = strstr(begin, "\r\n");
        if (p1 == NULL) {
            response["code"] = 1;
            response["message"] = "Invalid multipart data";
            str_json = response.toStyledString();
            return 0;
        }
        
        char boundary[256] = {0};
        strncpy(boundary, begin, p1 - begin);
        boundary[p1 - begin] = '\0';
        
        p1 = p1 + 2;
        p2 = strstr(p1, "name=\"user\"");
        if (!p2) {
            response["code"] = 1;
            response["message"] = "Missing user field";
            str_json = response.toStyledString();
            return 0;
        }
        p2 = strstr(p2, "\r\n");
        p2 += 4;
        begin = p2;
        p2 = strstr(begin, "\r\n");
        strncpy(user, begin, p2 - begin);
        
        p1 = p2 + 2;
        p2 = strstr(p1, "name=\"token\"");
        if (!p2) {
            response["code"] = 1;
            response["message"] = "Missing token field";
            str_json = response.toStyledString();
            return 0;
        }
        p2 = strstr(p2, "\r\n");
        p2 += 4;
        begin = p2;
        p2 = strstr(begin, "\r\n");
        strncpy(token, begin, p2 - begin);
        
        p1 = p2 + 2;
        p2 = strstr(p1, "name=\"md5\"");
        if (!p2) {
            response["code"] = 1;
            response["message"] = "Missing md5 field";
            str_json = response.toStyledString();
            return 0;
        }
        p2 = strstr(p2, "\r\n");
        p2 += 4;
        begin = p2;
        p2 = strstr(begin, "\r\n");
        strncpy(md5, begin, p2 - begin);
        
        p1 = p2 + 2;
        p2 = strstr(p1, "name=\"index\"");
        if (!p2) {
            response["code"] = 1;
            response["message"] = "Missing index field";
            str_json = response.toStyledString();
            return 0;
        }
        p2 = strstr(p2, "\r\n");
        p2 += 4;
        begin = p2;
        p2 = strstr(begin, "\r\n");
        strncpy(index_str, begin, p2 - begin);
        chunk_index = atoi(index_str);
        
        p1 = p2 + 2;
        p1 = strstr(p1, "\r\n");
        if (p1) {
            begin = p1 + 4;
            size_t file_size = post_data.size() - (begin - (char*)post_data.c_str());
            
            std::string temp_file = std::string(CHUNK_TEMP_DIR) + "/" + md5 + "/" + std::to_string(chunk_index);
            mkdir((std::string(CHUNK_TEMP_DIR) + "/" + md5).c_str(), 0755);
            
            int fd = open(temp_file.c_str(), O_WRONLY | O_CREAT, 0644);
            if (fd >= 0) {
                write(fd, begin, file_size);
                close(fd);
            }
        }
        
        CacheManager *pCacheManager = CacheManager::getInstance();
        CacheConn *pCacheConn = pCacheManager->GetCacheConn("tuchuang_slave");
        if (!pCacheConn) {
            response["code"] = 1;
            response["message"] = "Redis connection failed";
            str_json = response.toStyledString();
            return 0;
        }
        
        std::string user_key = std::string(user) + "_token";
        if (!pCacheConn->isExists(user_key) || 
            pCacheConn->get(user_key) != std::string(token)) {
            response["code"] = 4;
            response["message"] = "Token expired";
            str_json = response.toStyledString();
            pCacheManager->RelCacheConn(pCacheConn);
            return 0;
        }
        
        std::string key = std::string(CHUNK_KEY_PREFIX) + md5;
        
        std::string info = pCacheConn->get(key);
        std::string existing;
        size_t eq_pos = info.find("uploaded=");
        if (eq_pos != std::string::npos) {
            existing = info.substr(eq_pos + 9);
        }
        
        if (!existing.empty()) {
            existing += "," + std::to_string(chunk_index);
        } else {
            existing = std::to_string(chunk_index);
        }
        
        char keyinfo[2048] = {0};
        if (!info.empty()) {
            size_t pos = info.find("&uploaded=");
            if (pos != std::string::npos) {
                std::string prefix = info.substr(0, pos);
                sprintf(keyinfo, "%s&uploaded=%s", prefix.c_str(), existing.c_str());
            } else {
                sprintf(keyinfo, "%s&uploaded=%s", info.c_str(), existing.c_str());
            }
        } else {
            sprintf(keyinfo, "uploaded=%s", existing.c_str());
        }
        pCacheConn->setex(key, 86400, keyinfo);
        
        pCacheManager->RelCacheConn(pCacheConn);
        
        response["code"] = 0;
        str_json = response.toStyledString();
        return 0;
    }
    
    else if (cmd == "merge") {
        std::string user, token, md5, filename;
        
        if (decodeChunkMergeJson(post_data, user, token, md5, filename) < 0) {
            response["code"] = 1;
            response["message"] = "Invalid parameters";
            str_json = response.toStyledString();
            return 0;
        }
        
        std::string key = std::string(CHUNK_KEY_PREFIX) + md5;
        
        CacheManager *pCacheManager = CacheManager::getInstance();
        CacheConn *pCacheConn = pCacheManager->GetCacheConn("tuchuang_slave");
        
        if (!pCacheConn) {
            response["code"] = 1;
            response["message"] = "Redis connection failed";
            str_json = response.toStyledString();
            return 0;
        }
        
        std::string temp_dir = std::string(CHUNK_TEMP_DIR) + "/" + md5;

        // 读取分片信息并解析文件大小
        int64_t filesize = 0;
        std::string chunk_info = pCacheConn->get(key);
        if (!chunk_info.empty()) {
            size_t pos = chunk_info.find("filesize=");
            if (pos != std::string::npos) {
                size_t amp_pos = chunk_info.find("&", pos);
                if (amp_pos != std::string::npos) {
                    filesize = atoll(chunk_info.substr(pos + 9, amp_pos - pos - 9).c_str());
                } else {
                    filesize = atoll(chunk_info.substr(pos + 9).c_str());
                }
            }
        }
        
        // 解析已上传的分片
        std::vector<std::string> chunk_files;
        DIR* dir = opendir(temp_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                std::string name(entry->d_name);
                if (name != "." && name != "..") {
                    chunk_files.push_back(name);
                }
            }
            closedir(dir);
        }
        
        // 排序分片
        std::sort(chunk_files.begin(), chunk_files.end(), 
            [](const std::string& a, const std::string& b) {
                return std::stoi(a) < std::stoi(b);
            });
        
        std::string fileid;
        std::string result_url;
        
        // 使用 FastDFS C API 上传分片
        if (!chunk_files.empty()) {
            // 获取文件后缀
            std::string suffix = "";
            size_t dot_pos = filename.rfind('.');
            if (dot_pos != std::string::npos && dot_pos < filename.length() - 1) {
                suffix = filename.substr(dot_pos + 1);
            }

            // 初始化 FastDFS 客户端（单例模式，只初始化一次）
            static bool fdfs_inited = false;
            if (!fdfs_inited) {
                int ret = fdfs_client_init(s_dfs_path_client.c_str());
                if (ret != 0) {
                    LOG_ERROR << "FastDFS client init failed, ret=" << ret;
                    response["code"] = 1;
                    response["message"] = "FastDFS init failed";
                    str_json = response.toStyledString();
                    pCacheManager->RelCacheConn(pCacheConn);
                    return 0;
                }
                fdfs_inited = true;
            }

            // 获取 tracker 连接
            ConnectionInfo *pTrackerServer = tracker_get_connection();
            if (pTrackerServer == NULL) {
                LOG_ERROR << "tracker_get_connection failed";
                response["code"] = 1;
                response["message"] = "Get tracker connection failed";
                str_json = response.toStyledString();
                pCacheManager->RelCacheConn(pCacheConn);
                return 0;
            }

            // 查询可用的 storage 服务器
            ConnectionInfo storageServer;
            char group_name[FDFS_GROUP_NAME_MAX_LEN + 1] = {0};
            int store_path_index = 0;
            int result = tracker_query_storage_store(pTrackerServer, &storageServer, group_name, &store_path_index);
            if (result != 0) {
                LOG_ERROR << "tracker_query_storage_store failed, result=" << result;
                tracker_close_connection_ex(pTrackerServer, true);
                response["code"] = 1;
                response["message"] = "Query storage failed";
                str_json = response.toStyledString();
                pCacheManager->RelCacheConn(pCacheConn);
                return 0;
            }

            // 上传第一个分片作为 appender 文件
            char file_id[256] = {0};
            std::string first_chunk = temp_dir + "/" + chunk_files[0];
            result = storage_upload_appender_by_filename1(
                pTrackerServer, &storageServer, store_path_index,
                first_chunk.c_str(), suffix.c_str(),
                NULL, 0,
                group_name, file_id);

            if (result != 0) {
                LOG_ERROR << "upload appender file failed, result=" << result;
                tracker_close_connection_ex(pTrackerServer, true);
                response["code"] = 1;
                response["message"] = "Upload first chunk failed";
                str_json = response.toStyledString();
                pCacheManager->RelCacheConn(pCacheConn);
                return 0;
            }

            // 删除第一个分片文件
            unlink(first_chunk.c_str());
            fileid = file_id;
            LOG_INFO << "Created appender file: " << fileid;

            // 追加后续分片
            for (size_t i = 1; i < chunk_files.size(); i++) {
                std::string chunk_file = temp_dir + "/" + chunk_files[i];

                struct stat st;
                if (stat(chunk_file.c_str(), &st) == 0) {
                    result = storage_append_by_filename1(
                        pTrackerServer, &storageServer,
                        chunk_file.c_str(), file_id);

                    if (result != 0) {
                        LOG_ERROR << "append chunk " << i << " failed, result=" << result;
                        break;
                    }

                    // 追加成功后删除分片
                    unlink(chunk_file.c_str());
                    LOG_INFO << "Appended chunk " << i;
                }
            }

            // 关闭 tracker 连接
            tracker_close_connection_ex(pTrackerServer, true);

            // 删除临时目录
            rmdir(temp_dir.c_str());

            // 生成 URL
            if (!fileid.empty()) {
                result_url = "http://" + s_storage_web_server_ip + ":" + s_storage_web_server_port + "/" + fileid;
            }
        }
        
        // 清理临时文件
        char rm_cmd[512] = {0};
        sprintf(rm_cmd, "rm -rf %s", temp_dir.c_str());
        system(rm_cmd);
        
        // 清理 Redis 记录
        pCacheConn->del(key);
        pCacheManager->RelCacheConn(pCacheConn);
        
        // 保存文件信息到数据库
        if (!fileid.empty()) {
            CDBManager *pDBManager = CDBManager::getInstance();
            CDBConn *pDBConn = pDBManager->GetDBConn("tuchuang_slave");

            if (pDBConn) {
                // 获取文件后缀
                std::string suffix = "";
                size_t dot_pos = filename.rfind('.');
                if (dot_pos != std::string::npos && dot_pos < filename.length() - 1) {
                    suffix = filename.substr(dot_pos + 1);
                }

                // 插入或更新 file_info 表
                char sql_cmd[2048] = {0};
                sprintf(sql_cmd,
                    "INSERT INTO file_info (md5, file_id, url, size, type, count) VALUES ('%s', '%s', '%s', %ld, '%s', 1) "
                    "ON DUPLICATE KEY UPDATE count = count + 1",
                    md5.c_str(), fileid.c_str(), result_url.c_str(), (long)filesize, suffix.c_str());
                pDBConn->ExecuteCreate(sql_cmd);

                // 插入 user_file_list 表
                sprintf(sql_cmd,
                    "INSERT INTO user_file_list (user, md5, file_name, shared_status, pv) VALUES ('%s', '%s', '%s', 0, 0)",
                    user.c_str(), md5.c_str(), filename.c_str());
                pDBConn->ExecuteCreate(sql_cmd);

                // 更新用户文件数量
                sprintf(sql_cmd,
                    "INSERT INTO user_file_count (user, count) VALUES ('%s', 1) "
                    "ON DUPLICATE KEY UPDATE count = count + 1",
                    user.c_str());
                pDBConn->ExecuteCreate(sql_cmd);

                pDBManager->RelDBConn(pDBConn);
            }
        }
        
        response["code"] = 0;
        response["url"] = result_url;
        response["fileid"] = fileid;
        str_json = response.toStyledString();
        return 0;
    }
    
    response["code"] = 1;
    response["message"] = "Unknown command";
    str_json = response.toStyledString();
    return 0;
}
