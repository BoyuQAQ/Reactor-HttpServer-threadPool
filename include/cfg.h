#ifndef _CFG_H_
#define _CFG_H_

#include <string>

#define CFG_PATH    "./tc_http_server.conf"

int get_cfg_value(const char *profile, const char *title, const char *key, char *value);

int get_string_value(const char *profile, const char *title, const char *key, std::string &value);

int get_int_value(const char *profile, const char *title, const char *key, int &value);

#endif
