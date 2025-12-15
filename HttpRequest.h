#pragma once 
#include <string>
#include <map>



enum class HttpState 
{
	REQUEST_LINE,
	HEADER,
	BODY,
	DONE,
	ERROR
};

class HttpRequest
{
public:
	HttpRequest();
	void reset();

	//解析HTTP请求
	int parse(const char* buf, int len);

	//URL解码
	static void urlDecode(std::string& dst, const std::string& src);

	//成员变量
	HttpState state;
	std::string method;
	std::string url;
	std::string version;
	std::string headers;
	int content_length;
	std::string body;
	int body_received;
	bool keep_alive;
};