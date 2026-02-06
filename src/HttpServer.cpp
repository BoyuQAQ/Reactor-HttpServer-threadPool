#include "HttpServer.h"
#include "ThreadPool.h"
#include <unistd.h>
#include <sys/socket.h>
#include <thread>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/sendfile.h>
#include <cstdlib>
#include <nlohmann/json.hpp>
using json = nlohmann::json;


HttpServer::HttpServer(unsigned short port, const std::string& baseDir)
	: port_(port), baseDir_(baseDir), running_(false),
	threadPool_(
		//使用lamabda确保只计算一次硬件并发数
		[] {
			unsigned num = std::thread::hardware_concurrency();
			return num > 0 ? num : 4;
		}(),
		[] {
			unsigned num = std::thread::hardware_concurrency();
			return num > 0 ? num * 2 : 8;
		}()
)
{
	//设置任务完成回调
	threadPool_.setTaskCallback([this](std::shared_ptr<Connection> conn) {
		this->onTaskComplete(conn);
		});
}

HttpServer::~HttpServer()
{
	stop();

	//等待线程池任务完成并清理回调
	threadPool_.setshutdown(true);
	//清空可能持有this指针的回调
	threadPool_.clearTaskCallback();
}

void HttpServer::stop()
{
	running_ = false;
	//关闭所有连接
	for (auto& pair : connections_)
	{
		close(pair.first);
	}
	connections_.clear();

	if (epollFd_ != -1)close(epollFd_);
	if (listenFd_ != -1)close(listenFd_);
	listenFd_ = epollFd_ = 0;
}

void HttpServer::printThreadPoolStatus()
{
	auto status = threadPool_.getPoolStatus();
	std::cout << "=== ThreadPool Status ==="<< std::endl;
	std::cout << "Min Threads:" << status.minThreads<<std::endl;
	std::cout << "Max Threads:" << status.maxThreads << std::endl;
	std::cout << "Live Threads:" << status.LiveThreads << std::endl;
	std::cout << "Busy Threads:" << status.busyThreads << std::endl;
	std::cout << "Queue Size:" << status.queueSize << std::endl;
	std::cout << "Load Factor:" << status.loadFactor * 100 << "%" << std::endl;
	std::cout << "=====================" << std::endl;
}

bool HttpServer::initListenSocket()
{
	//1、创建监听的套接字
	listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (listenFd_ == -1)
	{
		perror("socket");
		return false;
	}

	//2、设置端口复用
	int opt = 1;
	int ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret == -1)
	{
		perror("setsockopt");
		close(listenFd_);
		return false;
	}

	//3、绑定
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ret = bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		perror("bind");
		close(listenFd_);
		return false;
	}

	//4、监听
	ret = listen(listenFd_, 128);
	if (ret == -1)
	{
		perror("listen");
		close(listenFd_);
		return false;
	}

	std::cout << "Server started successfully on port:" << port_ << std::endl;

	//5、设置非阻塞
	int flags = fcntl(listenFd_, F_GETFL, 0);
	fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);

	return true;
}

void HttpServer::run()
{
	if (!initListenSocket()) {
		return;
	}
	running_ = true;

	time_t lastStatusTime = time(nullptr);

	//创建epoll实例
	epollFd_ = epoll_create1(0);
	if (epollFd_ == -1) {
		perror("epoll_create1");
		return;
	}
	
	//添加监听socket到epoll
	struct epoll_event ev = {};
	ev.events = EPOLLIN;
	ev.data.fd = listenFd_;
	if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev) == -1) {
		perror("epoll_ctl:Listen_sock");
		return;
	}

	std::cout << "开始进行正式运行,监听端口:" << port_ << std::endl;
	std::cout << "epoll实例:" << epollFd_ << ",监听socket:" << listenFd_ << std::endl;

	//事件循环
	struct epoll_event events[1024];
	while (running_)
	{
		std::cout << "等待epoll事件..." << std::endl;

		int nfds = epoll_wait(epollFd_, events, 1024, -1);
		if (nfds == -1) {
			if (errno == EINTR) {
				std::cout << "epoll_wait被中断,继续等待" << std::endl;
				continue;
			}
			perror("epoll_wait");
			break;
		}

		if (nfds == 0)
		{
			std::cout << "epoll_wait超时，无事件" << std::endl;
			continue;
		}

		//每30秒输出一次线程池状态
		time_t currentTime = time(nullptr);
		if (currentTime - lastStatusTime >= 30)
		{
			printThreadPoolStatus();
			lastStatusTime = currentTime;
		}

		std::cout << "epoll返回" << nfds << "个事件" << std::endl;

		for (int i = 0; i < nfds; ++i) {
			if (events[i].data.fd == listenFd_) {
				std::cout << "检测到新连接事件" << std::endl;
				acceptNewConnection();
			}
			else {
				//处理客户端请求
				std::cout << "客户端数据可读:fd=" << events[i].data.fd << std::endl;
				int cfd = events[i].data.fd;

				//检查连接是否存在
				auto it = connections_.find(cfd);
				if (it == connections_.end())
				{
					std::cout << "错误:连接不存在，fd=" << cfd << std::endl;
					continue;
				}

				std::shared_ptr<Connection> conn = connections_[cfd];

				//读取数据
				char buf[8192];
				ssize_t nread = recv(cfd, buf, sizeof(buf), 0);
				if (nread > 0) {
					//解析HTTP请求
					int ret = conn->request.parse(buf, static_cast<int>(nread));
					if (ret == 1)//解析完成
					{//将任务提交到线程池
						std::shared_ptr<Connection>conn = connections_[cfd];
						std::cout << "将任务提交到线程池" << endl;
						threadPool_.addTask([this](void* arg) //值捕获，引用计数=4
							{
								Connection* conn = static_cast<Connection*>(arg);
								this->processRequest(conn);
							},
							conn);
					}
					else if (ret == -1) {//解析错误，关闭连接
						close(cfd);
						connections_.erase(cfd);
						break;
					}
					//ret==0表示需要更多数据，继续等待
				}
				else if (nread == 0) {
					close(cfd);
					connections_.erase(cfd);
				}
				else 
				{
					if (errno != EAGAIN && errno != EWOULDBLOCK) {
						std::cout << "暂无数据可用" << std::endl;
					}
					else
					{
						perror("recv");
						close(cfd);
						connections_.erase(cfd);
					}
					//如果是EAGAIN，表示没有数据了，正常退出循环
					perror("recv");
				}
			}
		}
	}																																																																																																																																																																																																													
}

void HttpServer::acceptNewConnection()
{
	std::cout << "===进入acceptNewConnection===" << std::endl;

	int acceptCount = 0;
	while (true) 
	{
		struct sockaddr_in clientAddr = {};
		socklen_t clientLen = sizeof(clientAddr);
		int cfd = accept(listenFd_, (struct sockaddr*)&clientAddr, &clientLen);
		if (cfd == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				//没有更多连接了
				std::cout << "没有更多连接，本次已接收" << std::endl;
				break;
			}
			perror("accept");
			break;
		}

		acceptCount++;
		std::cout << "接收新连接 #" << acceptCount << ",文件描述符:" << cfd << std::endl;
		std::cout << "客户端地址:" << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << std::endl;
		//设置非阻塞
		int flags = fcntl(cfd, F_GETFL, 0);
		flags |= O_NONBLOCK;//添加非阻塞标志
		fcntl(cfd, F_SETFL, flags );

		//创建连接对象
		//创建智能指针shared_ptr(引用计数=1)
		auto conn = std::make_shared<Connection>();
		conn->fd = cfd;
		conn->request.reset();

		//添加到连接映射
		connections_[cfd] = conn;

		//添加到epoll
		struct epoll_event ev = {};
		ev.events = EPOLLIN | EPOLLET |EPOLLONESHOT;
		ev.data.fd = cfd;
		if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, cfd, &ev) == -1) {
			perror("epoll_ctl:client_sock");
			close(cfd);
			connections_.erase(cfd);
			continue;
		}
		else {
			std::cout << "成功将客户端socket:" << cfd << "添加到epoll" << std::endl;
		}
		std::cout << "连接处理完成" << std::endl;
	}
	std::cout << "===离开 acceptNewConnection ===" << std::endl;
}

void HttpServer::sendDirectoryJson(const std::string& dirPath, int cfd)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         
{
	std::cout << "DEBUG: 正在尝试打开目录: " << dirPath << std::endl;
	//1.打开目录
	DIR* dir = opendir(dirPath.c_str());
	if (dir == nullptr)
	{
		//【健壮性】如果目录无法打开，返回空的JSON数组，而不是让程序崩溃或无响应
		std::cout << "无法打开目录" << dirPath << std::endl;
		sendResponse(cfd, 500, "[]", "appliction/json");
		return;  
	}

	//2.创建一个JSON数组对象
	//【可维护性】这里不在拼接字符串，而是像操作数组一样操作json对象
	json jsonArray = json::array();
	struct dirent* entry;

	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;

		//过滤掉当前目录(.)和上级目录(..)
		if (name == "." || name == "..")continue;

		//3.创建单个文件的JSON对象
		json fileItem;

		//【安全性 & 健壮性】核心点
		//如果name包含特殊字符(如"或换行符),库会自动进行转义
		//例如:a"b.txt 会自动变成 "a\"b.txt",保证JSON格式会永远合法。
		fileItem["name"] = name;

		//获取详细信息
		std::string fullPath = dirPath + "/" + name;
		struct stat st;
		if (stat(fullPath.c_str(), &st) == -1) {
			continue;//获取失败跳过该文件
		}

		//填充更多属性
		//【可维护性】想要添加新字段直接赋值即可，不用改字符串拼接逻辑
		fileItem["size"] = st.st_size;

		if (S_ISDIR(st.st_mode)) {
			fileItem["type"] = "dir";
		}
		else {
			fileItem["type"] = "file";
		}

		//将这个对象添加到数组里
		jsonArray.push_back(fileItem);
	}
	closedir(dir);

	//4、序列化：将JSON对象转换成字符串
	//dump()方法将对象变成string
	//dump(4)表示以4空格缩进格式化输出，方便调试查看(生产环境可以直接dump()不换行)
	std::string jsonString = jsonArray.dump();

	//5、发送响应
	std::cout << "生成JSON成功，长度:" << jsonString.length() << std::endl;
	sendResponse(cfd, 200, jsonString, "application/json");
}

void HttpServer::processRequest(Connection* conn) {
	HttpRequest& req = conn->request;

	//处理管理接口
	if (req.url == "/admin/threadpool-status")
	{
		auto status = getThreadPoolStatus();
		std::string jsonBody = "HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n\r\n";
		jsonBody += "{";
		jsonBody += "\"minThreads\":" + std::to_string(status.minThreads) + ",";
		jsonBody += "\"maxThreads\":" + std::to_string(status.maxThreads) + ",";
		jsonBody += "\"LiveThreads\":" + std::to_string(status.LiveThreads) + ",";
		jsonBody += "\"busyThreads\":" + std::to_string(status.busyThreads) + ",";
		jsonBody += "\"queueSize\":" + std::to_string(status.queueSize) + ",";
		jsonBody += "\"oadFactor\":" + std::to_string(status.loadFactor);
		jsonBody += "}";

		std::cout << "发送管理接口响应" << std::endl;
		sendResponse(conn->fd, 200, jsonBody, "application/json");
		return;
	}

	//URL解码
	std::string decodeUrl;
	HttpRequest::urlDecode(decodeUrl, req.url);
	std::cout << "解码后URL:" << decodeUrl << std::endl;

	//1.【新增】处理API请求
	//浏览器JS会请求http://ip:port/api/list?dir=/
	if (decodeUrl.find("/api/list") == 0)
	{
		std::cout << "收到目录列表API请求" << std::endl;

		//1.解析参数 ?dir=
		std::string relativePath = "/";//默认为根
		size_t queryPos = decodeUrl.find("?");
		if (queryPos != std::string::npos) {
			//提取query字符串，例如:dir=/test/data
			std::string query = decodeUrl.substr(queryPos + 1);
			//简单的查找 dir=
			size_t dirPos = query.find("dir=");
			if (dirPos != std::string::npos)
			{
				//提取dir=后面内容，实际生产中可能需要多个参数(&分割)
				relativePath = query.substr(dirPos + 4);
			}
		}
			//组合绝对路径，将前端传过来的和服务器根目录进行结合，同时判断/的位置是否正确，如果没有需要补上
			if (!relativePath.empty() && relativePath.front() != '/')
			{
				relativePath = '/' + relativePath;
			}
			std::string targetDir = baseDir_ + relativePath;

			//使用realpath规范化路径
			char resolved_path[PATH_MAX];
			if (realpath(targetDir.c_str(), resolved_path) == NULL) {
				cout << "resolved_path路径失败resolved_path:" << resolved_path << endl;
				perror("realpath");
				sendResponse(conn->fd, 404, "[]", "application/json");
				return;
			}

			//检查路径遍历攻击
			std::string finalPath(resolved_path);
			if (finalPath.compare(0, baseDir_.size(), baseDir_) != 0)
			{
				sendErrorResponse(conn->fd, 403, "Forbidden");
				return;
			}
			std::cout << "API请求子目录:" << finalPath << endl;

			//列出服务器根目录baseDir_
			//未来可以解析decodeUrl里的参数来实现子目录访问
			sendDirectoryJson(finalPath, conn->fd);
			return;
	}
		std::string fullpath = baseDir_;
		//构建完整路径
		if (decodeUrl == "/" || decodeUrl.empty())
		{
			std::string defaultFile = baseDir_ + "/index.html";
			if (access(defaultFile.c_str(), F_OK) == 0)
			{
				fullpath = defaultFile;
			}
		}
		else
		{
			fullpath += decodeUrl;
		}

		//2.安全检查
		char resolved_path[PATH_MAX];
		if (realpath(fullpath.c_str(), resolved_path) == NULL)
		{
			perror("realpath");
			sendErrorResponse(conn->fd, 404, "Not Found");
			return;
		}

		if (strncmp(resolved_path, baseDir_.c_str(), baseDir_.length()) != 0)
		{
			sendErrorResponse(conn->fd, 403, "Forbidden");
			return;
		}

		//获取文件属性
		struct stat st;
		if (stat(resolved_path, &st) == -1) {
			//尝试发送404页面
			std::string not_found_path = baseDir_ + "/404.html";
			if (access(not_found_path.c_str(), R_OK) == 0) {
				sendFile(not_found_path, conn->fd);
			}
			else
			{
				cout << "读写文件属性失败" << endl;
				sendErrorResponse(conn->fd, 404, "Not Found");
			}
			return;
		}
		else if (S_ISDIR(st.st_mode))
		{
			sendDir(resolved_path, decodeUrl, conn->fd);
		}
		else
			{
				//发送文件前先发送HTTP头部
				std::string fileType = getFileType(resolved_path);
				sendFile(resolved_path, conn->fd);
			}
		return;
}

void HttpServer::sendResponse(int cfd, int status, const std::string& content, const std::string& contentType)
{
	//根据状态码确定状态描述
	std::string statusDescription = getStatusDescription(status);

	//构建HTTP响应头
	std::string response = std::string("HTTP/1.1") + " " + std::to_string(status) + " " + statusDescription + "\r\n";
	response+="Content-Type: "+(contentType.empty()? "text/html;charset=utf-8":contentType)+"\r\n";
	response += "Content-Length: " + std::to_string(content.length()) + "\r\n";
	response += "Connection: close\r\n";
	response += "Server: SimpleHttpServer/1.0\r\n"; //添加服务器标识
	response += "Date " + getHttpDate() + "\r\n";	//添加日期
	response += "\r\n";	//空行分隔头部和正文
	response += content;

	//发送响应
	ssize_t sent = send(cfd, response.c_str(), response.length(), 0);
	if (sent == -1)
	{
		std::cerr << "发送HTTP相应失败:状态码="<<status << ",类型=" << contentType << ",长度=" << content.length() << std::endl;
	}
	else
	{
		std::cout << "发送HTTP相应:状态码=" << status << "，类型=" << contentType << ",长度=" << content.length() << std::endl;
	}
}

//辅助函数:获取HTTP状态描述
std::string HttpServer::getStatusDescription(int status)
{
	static std::map<int, std::string> statusMap = {
		 {100, "Continue"},
		{101, "Switching Protocols"},
		{200, "OK"},
		{201, "Created"},
		{202, "Accepted"},
		{204, "No Content"},
		{301, "Moved Permanently"},
		{302, "Found"},
		{304, "Not Modified"},
		{400, "Bad Request"},
		{401, "Unauthorized"},
		{403, "Forbidden"},
		{404, "Not Found"},
		{405, "Method Not Allowed"},
		{500, "Internal Server Error"},
		{501, "Not Implemented"},
		{503, "Service Unavailable"}
	};

	auto it = statusMap.find(status);
	return it != statusMap.end() ? it->second : "Unknown Status";
}

//辅助函数:获取HTTP格式的日期
std::string HttpServer::getHttpDate()
{
	time_t now = time(nullptr);
	struct tm tm;
	gmtime_r(&now, &tm);//使用线程安全的gmtime_r

	char buf[64];
	strftime(buf, sizeof(buf), "%a,%d %b %Y %H:%M:%S GMT", &tm);
	return std::string(buf);
}

void HttpServer::onTaskComplete(std::shared_ptr<Connection> conn) {
	
	//验证conn是否是空指针以及无效指针
	if (!conn|| conn->fd<=0)
	{
		std::cout << "错误:conn为无效的Connection指针" << std::endl;
		return;
	}
	
	//处理任务完成后的逻辑
	if (!conn->request.keep_alive) {
		close(conn->fd);
		connections_.erase(conn->fd);//引用计数-1，变成1
		//conn参数析构，引用计数=0，自动删除Connection对象
	}
	else {
		//重置请求状态，准备处理下一个请求
		conn->request.reset();

		//重新激活EPOLL事件
		struct epoll_event ev = {};
		ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
		ev.data.fd = conn->fd;
		epoll_ctl(epollFd_, EPOLL_CTL_MOD, conn->fd, &ev);
	}
}

std::string HttpServer::getFileType(const std::string& fileName)
{
	const char* dot = strrchr(fileName.c_str(), '.');
	if (dot == NULL)
		return "text/plain; charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html;charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".js") == 0)
		return "application/javascript";
	if (strcmp(dot, ".pdf") == 0)
		return "application/pdf";
	if (strcmp(dot, ".zip") == 0)
		return "application/zip";
	if (strcmp(dot, ".ico") == 0)
		return "image/x-icon";

	return "text/plain;charset=utf-8";
}

void HttpServer::sendDir(const std::string& dirName, const std::string& urlPath, int cfd)
{
	std::string buf = "<html><head><title>Index of" + urlPath + "</title></head><body><h1>Index of"
		+ urlPath + "</h1><hr><table>";

	DIR* dir = opendir(dirName.c_str());
	if (dir == NULL) {
		sendErrorResponse(cfd, 500, "Internal Server Error");
		return;
	}

	//目录扫描
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string name = entry->d_name;
		if (name == "." || name == "..")continue;

		std::string subPath = dirName + "/" + name;
		struct stat st;
		if (stat(subPath.c_str(), &st) == -1)continue;

		std::string link = urlPath;
		if (link.empty()||link.back() != '/')link += "/";
		link += name;

		if (S_ISDIR(st.st_mode)) {
			link = "/";
			buf += "<tr><td><a href=\"" + link + "\">" + name + "/</a></td><td>" + std::to_string(st.st_size) + "</td></tr>";
		}
		else
		{
			buf += "<tr><td><a href=\"" + link + "\">" + name + "</a></td><td>" + std::to_string(st.st_size) + "</td></tr>";

		}
	}
	closedir(dir);
	       
	buf += "</table><hr></body></html>";

	sendResponse(cfd, 200, buf, "text/html;charset=utf-8");
}

void HttpServer::sendFile(const std::string& fileName, int cfd)
{
	int fd = open(fileName.c_str(), O_RDONLY);
	if (fd == -1)
	{
		sendErrorResponse(cfd, 404, "NotFound");
		return;
	}

	struct stat st;
	if (fstat(fd, &st) == -1)
	{
		close(fd);
		sendErrorResponse(cfd, 500, "Internal Server Error");
		return;
	}	

	//获取文件类型
	std::string FileType = getFileType(fileName);
	//发送头部
	sendHeadMsg(cfd, 200, "OK", FileType, static_cast<int>(st.st_size));

	off_t offset = 0;
	ssize_t sent = 0;
	while (offset < st.st_size) {
		sent = sendfile(cfd, fd, &offset, st.st_size - offset);

		std::cout << "[sendfile] fd=" << cfd
			<< " sent=" << sent
			<< " offset=" << offset
			<< "/" << st.st_size << std::endl;

		if (sent < 0)
		{
			if (errno == EAGAIN || errno == EINTR)
			{
				std::cout << "[sendfile] EAGAIN, sleep..." << std::endl;
				usleep(200 * 1000);  // 👈 人为拖慢
				continue;
			}
			perror("sendfile");
			break;
		}
	}
	close(fd);
}

void HttpServer::sendHeadMsg(int cfd, int status, const std::string& descr, const std::string& type, int len)
{
	std::string heards = "HTTP/1.1 " + std::to_string(status) + " " + descr + "\r\n";
	heards += "Content-Type:" + type + "\r\n";
	if (len >= 0)
	{
		heards += "Content-Length:" + std::to_string(len) + "\r\n";
	}
	heards += "Connection:close\r\n\r\n";

	ssize_t sent=send(cfd, heards.c_str(), heards.length(), 0);
	if(sent == -1)
	{
		perror("send headers failed");
	}
}

void HttpServer::sendErrorResponse(int cfd, int status, const std::string& description)
{
	std::string body = "<html><head><title>" + std::to_string(status) + " " + description +
		"</title></head><body><h1>" + std::to_string(status) + " " + description +
		"</h1></body></html>";

	sendResponse(cfd, status, body,"text/html;cahrset=utf-8");
}

void HttpServer::setThreadPoolSize(int minThreads, int maxThreads)
{
	//重新初始化线程池
	threadPool_ = ThreadPool<Connection>(minThreads, maxThreads);
	threadPool_.setTaskCallback([this](std::shared_ptr<Connection> conn) {
		this->onTaskComplete(conn);
		});
}                                                                                                                                                   