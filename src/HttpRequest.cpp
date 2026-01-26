#include "HttpRequest.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cctype>

using namespace std;

HttpRequest::HttpRequest()
{
	reset();
}

void HttpRequest::reset()
{
	state = HttpState::REQUEST_LINE;
	method.clear();
	url.clear();
	version.clear();
	headers.clear();
	content_length = 0;
	body.clear();
	body_received = 0;
	keep_alive = false;
}

int HttpRequest::parse(const char* buf, int len)
{
	cout << "准备开始解码" << endl;
	int i = 0;
	while (i < len)
	{
		switch (state)
		{
		case HttpState::REQUEST_LINE:
		{
			const char* line_end = strstr(buf + i, "\r\n");
			if (!line_end)return 0;

			ptrdiff_t line_len = line_end - (buf + i);//由于int会导致指针的精度丢失，指针相减的结果是ptrdiff_t类型(通常是long int)
			std::string line(buf + i, line_len);

			//简单的请求行解析
			size_t pos1 = line.find(' ');
			if (pos1 == std::string::npos) {
				state = HttpState::ERROR;
				return -1;
			}

			size_t pos2 = line.find(' ', pos1 + 1);
			if (pos2 == std::string::npos)
			{
				state = HttpState::ERROR;
				return -1;
			}

			method = line.substr(0, pos1);
			url = line.substr(pos1 + 1, pos2 - pos1 - 1);
			version = line.substr(pos2 + 1);

			state = HttpState::HEADER;
			i += static_cast<int>(line_len) + 2;//需要进行转换，因为i是int类型
			break; 
		}
		case HttpState::HEADER:
		{
			const char* line_end = strstr(buf + i, "\r\n");
			if (!line_end)return 0;

			if (line_end == buf + i)
			{
				//空行,头部结束
				state = (content_length > 0) ? HttpState::BODY : HttpState::DONE;
				i += 2;
				if (state == HttpState::DONE) {
					return 1;
				}
				break;
			}

			ptrdiff_t line_len = line_end - (buf + i); //由于int会导致指针的精度丢失，指针相减的结果是ptrdiff_t类型(通常是long int)
			std::string header_line(buf + i, line_len);

			//解析content-Length
			if (header_line.find("Content-Length:") == 0 ||
				header_line.find("Content-length:") == 0) {
				content_length = std::stoi(header_line.substr(15));
			}
			//解析Connection
			else if (header_line.find("Conneciton:") == 0) {
				if (header_line.find("keep-alive") != std::string::npos) {
					keep_alive = true;
				}
			}

			headers += header_line + "\n";
			i += static_cast<int>(line_len) + 2;
			break;
		}
		case HttpState::BODY:
		{
			int remain = len - i;
			int need = content_length - body_received;
			int copy_len = (remain < need) ? remain : need;

			body.append(buf + i, copy_len);
			body_received += copy_len;
			i += copy_len;

			if (body_received >= content_length) {
				state = HttpState::DONE;
			}
			break;
		}
		case HttpState::DONE:
			return 1;
		case HttpState::ERROR:
			return -1;
		}	
	}
	//修复:添加循环结束后的返回语句
	//如果循环结束但请求还未完成，返回0表示需要更多数据
	return (state == HttpState::DONE) ? 1 : 0;
}

void HttpRequest::urlDecode(std::string& dst, const std::string& src)
{
	dst.clear();
	char a, b;

	for (size_t i = 0; i < src.length(); i++)
	{
		if (src[i] == '%' && i + 2 < src.length() && isxdigit(src[i + 1]) && isxdigit(src[i + 2]))
		{
			a = src[i + 1];
			b = src[i + 2];

			if (a >= 'a') a -= 'a' - 'A';
			if (a >= 'A') a -= ('A' - 10);
			else a -= '0';

			if (b >= 'a') b -= 'a' - 'A';
			if (b >= 'A') b -= ('A' - 10);
			else b -= '0';

			dst += (char)(16 * a + b);
			i += 2;
		}
		else if (src[i] == '+')
		{
			dst+=' ';
		}
		else
		{
			dst += src[i];
		}
	}
}

	
