#include "HttpServer.h"
#include <iostream>
#include <locale.h>

int main(int argc,char* argv[]) //两个命令行参数，一个是服务器的端口，一个是服务器访问的资源目录
{
	setlocale(LC_ALL, "en_US.UTF-8");//设置程序的区域信息，解决中文乱码问题
	std::cout<<"准备开始运行服务器...\n"<<endl;

	if (argc < 3)
	{
		std::cout<<"./a.out port path\n"<<endl;
		return -1;
	}	
	unsigned short port = static_cast<unsigned short>(atoi(argv[1])); //获取端口号（把port转换成无符号短整型)
	std::string baseDir = argv[2];

	//创建HTTP服务器
	HttpServer server(port, baseDir);

	//可选：设置线程池大小
	//server.setThreadPoolSize(4,16);

	//显示初始线程池状态
	server.printThreadPoolStatus();

	//运行服务器
	server.run();

	return 0;
}

