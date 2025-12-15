#include "ThreadPool.h"
#include "HttpRequest.h"
#include <string>
#include <map>
#include <sys/epoll.h>
#include <memory>


//连接信息
struct Connection
{
	int fd;
	HttpRequest request;
};

class HttpServer
{
public:
	HttpServer(unsigned short port, const std::string& baseDir);
	~HttpServer();

	void run();
	void stop();

	//设置线程池大小
	void setThreadPoolSize(int minThreads, int maxThreads);

	//添加状态查询接口
	ThreadPool<std::shared_ptr<Connection>>::PoolStatus getThreadPoolStatus()
	{
		return threadPool_.getPoolStatus();
	}

	//添加监控输出函数
	void printThreadPoolStatus();

private:
	
	//初始化监听socket
	bool initListenSocket();

	//处理epoll事件
	void handEpollEvents();

	//接收新连接
	void acceptNewConnection();

	//处理HTTP请求(在线程池中执行)
	void processRequest(HttpServer* server,void* arg);

	//发送响应
	void sendResponse(int cfd, int status, const std::string& content);

	//文件操作
	std::string getFileType(const std::string& fileName);
	void sendDir(const std::string& difName, const std::string& urlPath, int cfd);
	void sendFile(const std::string& fileName, int cfd);
	void sendHeadMsg(int cfd, int status, const std::string& descr, const std::string& type, int len);
	void sendErrorResponse(int cfd, int status, const std::string& description);

	//线程池任务完成回调
	void onTaskComplete(std::shared_ptr<Connection> conn);
	
	int listenFd_;
	int epollFd_;
	unsigned short port_;
	std::string baseDir_;
	bool running_;

	//线程池
	ThreadPool<std::shared_ptr<Connection>> threadPool_;//T=Connection

	//连接管理
	std::map<int, std::shared_ptr<Connection>> connections_;
};