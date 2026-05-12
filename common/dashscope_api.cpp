#include "../include/dashscope_api.h"
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <sstream>
#include <vector>
#include <cstring>

struct CurlBuffer {
    char *data;
    size_t size;
    size_t capacity;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    struct CurlBuffer *buf = (struct CurlBuffer *)userdata;

    while (buf->size + total + 1 > buf->capacity) {
        buf->capacity = buf->capacity * 2 + total;
        buf->data = (char *)realloc(buf->data, buf->capacity);
        if (!buf->data) return 0;
    }

    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void curl_buffer_init(struct CurlBuffer *buf)
{
    buf->capacity = 4096;
    buf->data = (char *)malloc(buf->capacity);
    buf->data[0] = '\0';
    buf->size = 0;
}

static void curl_buffer_free(struct CurlBuffer *buf)
{
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->size = 0;
    buf->capacity = 0;
}

AIResult dashscope_describe_image(const std::string &api_key, const std::string &image_url)
{
    AIResult result = {false, "", ""};

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    struct CurlBuffer response;
    curl_buffer_init(&response);

    Json::Value root;
    root["model"] = "qwen-vl-plus";

    Json::Value input;
    Json::Value messages;
    Json::Value msg;
    msg["role"] = "user";

    Json::Value content;
    Json::Value img_item;
    img_item["image"] = image_url;
    content.append(img_item);

    Json::Value text_item;
    text_item["text"] = "请用中文详细描述这张图片的内容，包括主要物体、场景、颜色、文字等信息。";
    content.append(text_item);

    msg["content"] = content;
    messages.append(msg);
    input["messages"] = messages;
    root["input"] = input;

    Json::FastWriter writer;
    std::string json_str = writer.write(root);

    struct curl_slist *headers = NULL;
    std::string auth_header = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Reader reader;
    Json::Value resp;
    if (!reader.parse(response.data, resp)) {
        result.error = "JSON parse failed";
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    if (resp["code"].isString()) {
        result.error = resp["message"].asString();
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Value output = resp["output"];
    if (output.isNull()) {
        result.error = "output is null";
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Value choices = output["choices"];
    if (choices.isNull() || choices.size() == 0) {
        result.error = "choices is empty";
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Value choice0 = choices[0];
    Json::Value message = choice0["message"];
    Json::Value cont = message["content"];

    std::string desc_text;
    if (cont.isArray() && cont.size() > 0) {
        Json::Value first = cont[0];
        if (first["text"].isString()) {
            desc_text = first["text"].asString();
        }
    } else if (cont.isString()) {
        desc_text = cont.asString();
    }

    if (!desc_text.empty()) {
        result.success = true;
        result.data = desc_text;
    } else {
        result.error = "Failed to extract description";
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_buffer_free(&response);
    return result;
}

AIResult dashscope_get_embedding(const std::string &api_key, const std::string &model,
                                 const std::string &text, int dimension)
{
    AIResult result = {false, "", ""};

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    struct CurlBuffer response;
    curl_buffer_init(&response);

    Json::Value root;
    root["model"] = model;

    Json::Value input;
    Json::Value texts;
    texts.append(text);
    input["texts"] = texts;
    root["input"] = input;

    Json::Value parameters;
    parameters["dimension"] = dimension;
    root["parameters"] = parameters;

    Json::FastWriter writer;
    std::string json_str = writer.write(root);

    struct curl_slist *headers = NULL;
    std::string auth_header = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://dashscope.aliyuncs.com/api/v1/services/embeddings/text-embedding/text-embedding");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    result.success = true;
    result.data = response.data;

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_buffer_free(&response);
    return result;
}

AIResult dashscope_describe_text(const std::string &api_key, const std::string &text_content,
                                 const std::string &filename)
{
    AIResult result = {false, "", ""};

    if (text_content.empty()) {
        result.error = "Empty text content";
        return result;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    struct CurlBuffer response;
    curl_buffer_init(&response);

    Json::Value root;
    root["model"] = "qwen-turbo";

    Json::Value input;
    Json::Value messages;
    Json::Value msg;
    msg["role"] = "user";

    std::string prompt = "请简洁描述以下文件（文件名：" + filename + "）的主要内容，用50字以内的中文总结：\n\n" + text_content;
    if (prompt.length() > 2000) {
        prompt = prompt.substr(0, 2000);
    }

    msg["content"] = prompt;
    messages.append(msg);
    input["messages"] = messages;
    root["input"] = input;

    Json::FastWriter writer;
    std::string json_str = writer.write(root);

    struct curl_slist *headers = NULL;
    std::string auth_header = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Reader reader;
    Json::Value resp;
    if (!reader.parse(response.data, resp)) {
        result.error = "JSON parse failed";
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    if (resp["code"].isString()) {
        result.error = resp["message"].asString();
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Value output = resp["output"];
    if (output.isNull()) {
        result.error = "output is null";
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Value choices = output["choices"];
    if (choices.isNull() || choices.size() == 0) {
        result.error = "choices is empty";
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_buffer_free(&response);
        return result;
    }

    Json::Value choice0 = choices[0];
    Json::Value message = choice0["message"];
    std::string desc_text = message["content"].asString();

    if (!desc_text.empty()) {
        result.success = true;
        result.data = desc_text;
    } else {
        result.error = "Failed to extract description";
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_buffer_free(&response);
    return result;
}
