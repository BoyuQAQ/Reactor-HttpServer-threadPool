#ifndef _DASHSCOPE_API_H_
#define _DASHSCOPE_API_H_

#include <string>

struct AIResult {
    bool success;
    std::string error;
    std::string data;
};

AIResult dashscope_describe_image(const std::string &api_key, const std::string &image_url);

AIResult dashscope_get_embedding(const std::string &api_key, const std::string &model,
                                 const std::string &text, int dimension);

AIResult dashscope_describe_text(const std::string &api_key, const std::string &text_content,
                                 const std::string &filename);

#endif
