#include "cfg.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

int get_cfg_value(const char *profile, const char *title, const char *key, char *value)
{
    std::ifstream file(profile);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << profile << std::endl;
        return -1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    std::string search_title = "[" + std::string(title) + "]";
    size_t title_pos = content.find(search_title);
    if (title_pos == std::string::npos) {
        return -1;
    }

    size_t key_pos = content.find(key, title_pos);
    if (key_pos == std::string::npos) {
        return -1;
    }

    size_t equals_pos = content.find('=', key_pos);
    if (equals_pos == std::string::npos) {
        return -1;
    }

    size_t line_end = content.find('\n', equals_pos);
    if (line_end == std::string::npos) {
        line_end = content.length();
    }

    std::string val = content.substr(equals_pos + 1, line_end - equals_pos - 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t' || val[0] == '\r')) {
        val = val.substr(1);
    }
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) {
        val.pop_back();
    }

    strcpy(value, val.c_str());
    return 0;
}

int get_string_value(const char *profile, const char *title, const char *key, std::string &value)
{
    char buf[1024] = {0};
    int ret = get_cfg_value(profile, title, key, buf);
    if (ret == 0) {
        value = buf;
    }
    return ret;
}

int get_int_value(const char *profile, const char *title, const char *key, int &value)
{
    char buf[64] = {0};
    int ret = get_cfg_value(profile, title, key, buf);
    if (ret == 0) {
        value = atoi(buf);
    }
    return ret;
}
