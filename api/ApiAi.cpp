#include "ApiAi.h"
#include "ApiCommon.h"
#include "../include/dashscope_api.h"
#include "../include/faiss_wrapper.h"
#include "../log/Logging.h"
#include <jsoncpp/json/json.h>
#include <fstream>
#include <sstream>

#define EMBEDDING_DIM 1024

std::string g_api_key = "";
FaissIndex g_faiss_index;
bool g_index_loaded = false;
std::string g_index_path = "./faiss_index.bin";

int ApiAiInit()
{
    if (g_index_loaded) {
        return 0;
    }

    if (g_faiss_index.load(g_index_path)) {
        g_index_loaded = true;
        LOG_INFO << "FAISS index loaded successfully, path: " << g_index_path;
    } else {
        LOG_WARN << "FAISS index not found or load failed, will rebuild on demand";
        g_faiss_index.init(EMBEDDING_DIM, g_index_path);
    }
    return 0;
}

int decodeAiJson(const std::string &str_json, std::string &cmd, std::string &user, 
                std::string &token, std::string &md5, std::string &filename,
                std::string &search_text)
{
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(str_json, root)) {
        LOG_ERROR << "parse ai json failed";
        return -1;
    }
    
    if (!root["cmd"].isNull()) {
        cmd = root["cmd"].asString();
    }
    
    if (!root["user"].isNull()) {
        user = root["user"].asString();
    }
    
    if (!root["token"].isNull()) {
        token = root["token"].asString();
    }
    
    if (!root["md5"].isNull()) {
        md5 = root["md5"].asString();
    }
    
    if (!root["filename"].isNull()) {
        filename = root["filename"].asString();
    }
    
    if (!root["search"].isNull()) {
        search_text = root["search"].asString();
    }
    
    return 0;
}

int handleAiDescribe(CDBConn *pDBConn, const std::string &md5, const std::string &filename,
                   const std::string &file_url, std::string &description)
{
    if (g_api_key.empty()) {
        LOG_WARN << "API key not set";
        return -1;
    }

    AIResult result;
    if (isImageFile(filename)) {
        result = dashscope_describe_image(g_api_key, file_url);
    } else if (isTextFile(filename)) {
        std::string local_path = "./files/" + md5;
        std::string content = readFileContent(local_path);
        if (content.empty()) {
            LOG_ERROR << "Failed to read file content: " << local_path;
            return -1;
        }
        result = dashscope_describe_text(g_api_key, content, filename);
    } else {
        LOG_WARN << "Unsupported file type: " << filename;
        description = "暂不支持此文件类型的AI描述";
        return 0;
    }

    if (!result.success) {
        LOG_ERROR << "AI describe failed: " << result.error;
        return -1;
    }

    description = result.data;
    return 0;
}

int handleAiSearch(CDBConn *pDBConn, const std::string &search_text, std::string &result_json)
{
    if (g_api_key.empty() || !g_index_loaded) {
        LOG_WARN << "Index not loaded or API key not set";
        return -1;
    }
    
    std::vector<float> query_vector(EMBEDDING_DIM);
    AIResult result = dashscope_get_embedding(g_api_key, "text-embedding-v3", search_text, EMBEDDING_DIM);
    if (!result.success) {
        LOG_ERROR << "Get embedding failed: " << result.error;
        return -1;
    }
    
    Json::Value resp;
    Json::Reader reader;
    if (!reader.parse(result.data, resp)) {
        LOG_ERROR << "Parse embedding response failed";
        return -1;
    }
    
    Json::Value output = resp["output"];
    Json::Value embeddings = output["embeddings"];
    Json::Value emb0 = embeddings[0];
    Json::Value embedding = emb0["embedding"];
    
    for (int i = 0; i < EMBEDDING_DIM && i < (int)embedding.size(); i++) {
        query_vector[i] = (float)embedding[i].asDouble();
    }
    
    auto search_results = g_faiss_index.search(query_vector.data(), 10, 0.45);
    
    Json::Value results;
    for (const auto &pair : search_results) {
        int faiss_id = pair.first;
        float score = pair.second;
        
        char sql_cmd[SQL_MAX_LEN] = {0};
        sprintf(sql_cmd, "SELECT md5, description, file_name FROM file_ai_desc WHERE faiss_id = %d", faiss_id);
        
        CResultSet *pResultSet = pDBConn->ExecuteQuery(sql_cmd);
        if (pResultSet) {
            while (pResultSet->Next()) {
                Json::Value item;
                item["md5"] = pResultSet->GetString("md5");
                item["description"] = pResultSet->GetString("description");
                item["file_name"] = pResultSet->GetString("file_name");
                item["score"] = score;
                results.append(item);
            }
            delete pResultSet;
        }
    }
    
    Json::FastWriter writer;
    result_json = writer.write(results);
    return 0;
}

int ApiAi(std::string &url, std::string &post_data, std::string &str_json)
{
    std::string cmd, user, token, md5, filename, search_text;
    
    if (decodeAiJson(post_data, cmd, user, token, md5, filename, search_text) < 0) {
        Json::Value root;
        root["code"] = 1;
        root["message"] = "Invalid request";
        str_json = root.toStyledString();
        return 0;
    }
    
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);
    
    Json::Value response;
    
    if (cmd == "set_apikey") {
        g_api_key = token;
        response["code"] = 0;
        response["message"] = "API key set";
        str_json = response.toStyledString();
        return 0;
    }
    
    if (cmd == "get_apikey") {
        response["code"] = 0;
        response["api_key"] = g_api_key.empty() ? "" : "****";
        str_json = response.toStyledString();
        return 0;
    }
    
    if (cmd == "describe") {
        std::string description;
        char sql_cmd[SQL_MAX_LEN] = {0};
        sprintf(sql_cmd, "SELECT url FROM file_info WHERE md5 = '%s'", md5.c_str());
        
        CResultSet *pResultSet = pDBConn->ExecuteQuery(sql_cmd);
        if (pResultSet && pResultSet->Next()) {
            std::string file_url = pResultSet->GetString("url");
            delete pResultSet;
            
            if (handleAiDescribe(pDBConn, md5, filename, file_url, description) == 0) {
                sprintf(sql_cmd, "INSERT INTO file_ai_desc (md5, description, status) VALUES ('%s', '%s', 1) ON DUPLICATE KEY UPDATE description='%s', status=1",
                        md5.c_str(), description.c_str(), description.c_str());
                pDBConn->ExecuteCreate(sql_cmd);
                
                response["code"] = 0;
                response["description"] = description;
            } else {
                response["code"] = 1;
                response["message"] = "AI describe failed";
            }
        } else {
            if (pResultSet) delete pResultSet;
            response["code"] = 1;
            response["message"] = "File not found";
        }
        
        str_json = response.toStyledString();
        return 0;
    }
    
    if (cmd == "search") {
        std::string result;
        if (handleAiSearch(pDBConn, search_text, result) == 0) {
            response["code"] = 0;
            response["results"] = result;
        } else {
            response["code"] = 1;
            response["message"] = "Search failed";
        }
        str_json = response.toStyledString();
        return 0;
    }
    
    if (cmd == "rebuild") {
        g_faiss_index.reset();
        g_faiss_index.init(EMBEDDING_DIM);
        
        char sql_cmd[SQL_MAX_LEN] = {0};
        sprintf(sql_cmd, "SELECT md5, description FROM file_ai_desc WHERE status = 1 AND description != ''");
        
        CResultSet *pResultSet = pDBConn->ExecuteQuery(sql_cmd);
        int faiss_id = 0;
        if (pResultSet) {
            while (pResultSet->Next()) {
                std::string desc_md5 = pResultSet->GetString("md5");
                std::string description = pResultSet->GetString("description");
                
                AIResult emb_result = dashscope_get_embedding(g_api_key, "text-embedding-v3", description, EMBEDDING_DIM);
                if (emb_result.success) {
                    Json::Value resp;
                    Json::Reader reader;
                    if (reader.parse(emb_result.data, resp)) {
                        Json::Value embeddings = resp["output"]["embeddings"];
                        Json::Value embedding = embeddings[0]["embedding"];
                        
                        std::vector<float> vec(EMBEDDING_DIM);
                        for (int i = 0; i < EMBEDDING_DIM && i < (int)embedding.size(); i++) {
                            vec[i] = (float)embedding[i].asDouble();
                        }
                        
                        g_faiss_index.add_vector(vec.data(), faiss_id);
                        
                        char update_cmd[SQL_MAX_LEN] = {0};
                        sprintf(update_cmd, "UPDATE file_ai_desc SET faiss_id = %d WHERE md5 = '%s'", faiss_id, desc_md5.c_str());
                        pDBConn->ExecutePassQuery(update_cmd);
                        
                        faiss_id++;
                    }
                }
            }
            delete pResultSet;
        }
        
        g_faiss_index.save(g_index_path);
        g_index_loaded = true;
        
        response["code"] = 0;
        response["count"] = faiss_id;
        str_json = response.toStyledString();
        return 0;
    }
    
    response["code"] = 1;
    response["message"] = "Unknown command";
    str_json = response.toStyledString();
    return 0;
}
