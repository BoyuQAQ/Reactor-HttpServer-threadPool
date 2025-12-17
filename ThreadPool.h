#pragma once 
#include "TaskQueue.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <pthread.h>
#include <iostream> 
#include <vector>
#include <ctime>
#include <functional>
#include <unistd.h>
#include <memory>

template<class T>
class ThreadPool
{
public:
	//线程池状态结构
	struct PoolStatus {
		int minThreads;
		int maxThreads;
		int LiveThreads;
		int busyThreads;
		int queueSize;
		float loadFactor;
	};

	//在类内部定义智能指针类型
	using SmartPtr = std::shared_ptr<T>;

	//创建线程池并且初始化
	ThreadPool<T>(int min, int max)
	{
		std::cout << "线程池初始化:min=" << min << ",max=" << max << std::endl;
		std::cout << "创建管理者线程和工作线程……" << std::endl;
		//实例化任务队列
		do
		{
			taskQ = new TaskQueue<T>;
			if (taskQ == nullptr)
			{
				cout << ("malloc taskQ faild...") << endl;
				break;
			}
			//对线程ID进行初始化
			threadIDs = new pthread_t[max];
			if (threadIDs == nullptr)
			{
				cout << ("malloc threadIDs faild...") << endl;
				break;
			}
			memset(threadIDs, 0, sizeof(pthread_t) * max);
			minNum = min;
			maxNum = max;
			busyNum = 0;
			liveNum = min;	//和最小个数相等
			exitNum = 0;

			//对互斥锁和条件变量初始化
			if (pthread_mutex_init(&mutexPool, NULL) != 0 ||
				pthread_mutex_init(&mutexOutput, NULL) != 0 ||
				pthread_cond_init(&notEmpty, NULL) != 0)
			{
				cout << ("mutex or condition init faild...") << endl;
				break;
			}

			shutdown = false;

			//创建管理者和工作的线程
			pthread_create(&managerID, NULL, manager, this);
			for (int i = 0; i < min; ++i)
			{
				pthread_create(&threadIDs[i], NULL, worker, this);
			}
			return;
		} while (0);

	}

	//销毁线程池
	~ThreadPool()
	{
		//关闭线程池
		shutdown = true;
		//阻塞并回收管理者线程
		pthread_join(managerID, NULL);
		for (int i = 0; i < liveNum; ++i)
		{
			pthread_cond_signal(&notEmpty);
		}
		//释放堆内存
		if (taskQ)
		{
			delete taskQ;
		}
		if (threadIDs)
		{
			delete[]threadIDs;
		}

		pthread_mutex_destroy(&mutexPool);
		pthread_cond_destroy(&notEmpty);
	}

	//添加任务完成回调方法
	void setTaskCallback(std::function<void(std::shared_ptr<T>)> callback) {
		taskCallback= callback;	//让外部代码告诉当前对象，任务执行完成后，将结果保存在taskCallback中，以便后续使用
	}

	//给线程池添加任务
	void addTask(std::function<void(void*)>func,SmartPtr arg)
	{
		if (shutdown)return;
		
		//创建Task，arg是SmartPtr(st::shared_ptr<T>)
		Task<T> task;
		task.function = func;
		task.arg = arg;//发生拷贝，增加引用计数

		//添加任务
		taskQ->addTask(task);

		//有任务了，唤醒阻塞在工作函数中的线程
		pthread_cond_signal(&notEmpty);
	}

	//获取线程池中工作的线程的个数
	int getBusyNum()
	{
		pthread_mutex_lock(&mutexPool);
		int busyNum = this->busyNum;
		pthread_mutex_unlock(&mutexPool);
		return busyNum;
	}

	//获取线程池中存货的线程的个数
	int getLiveNum()
	{
		pthread_mutex_lock(&mutexPool);
		int liveNum = this->liveNum;
		pthread_mutex_unlock(&mutexPool);
		return liveNum;
	}

	ThreadPool& operator=(ThreadPool&& other)noexcept {
		if (this != &other) {
			//实现移动复制逻辑
			//转移资源所有权
		}
		return *this;
	}

	//获取线程池状态,应用于外部获得线程池当前状态
	PoolStatus getPoolStatus() {
		pthread_mutex_lock(&mutexPool);
		PoolStatus status;
		status.minThreads = minNum;
		status.maxThreads = maxNum;
		status.LiveThreads = liveNum;
		status.busyThreads = busyNum;
		status.queueSize = static_cast<int>(taskQ->taskNumber());
		status.loadFactor = liveNum > 0 ? static_cast<float>(busyNum) / static_cast<float>(liveNum) : 0.0f;
		pthread_mutex_unlock(&mutexPool);
		return status;
	}

	//获取当前负载(供manager管理者线程函数调用)
	int getCurrentLoad()
	{
		pthread_mutex_lock(&mutexPool);
		int load = busyNum;//可以用帮忙线程数作为负载指标
		pthread_mutex_unlock(&mutexPool);
		return load;
	}

	void setshutdown(bool value) {
		shutdown = value;
	}

	void clearTaskCallback()
	{
		taskCallback = nullptr;
	}
private: 
	//任务回调 - 使用智能指针
	std::function<void(SmartPtr)> taskCallback;

	//工作中的线程（消费者线程）任务函数
	static void* worker(void* arg)
	{
		ThreadPool* pool = static_cast<ThreadPool*>(arg);

		while (1)
		{
			pthread_mutex_lock(&pool->mutexPool);
			//判断线程池是否被关闭了
			if (pool->shutdown == 1)
			{
				pthread_mutex_unlock(&pool->mutexPool);
				pool->threadExit();
			}

			//判断当前队列是否为空
			while (pool->taskQ->taskNumber() == 0 && pool->shutdown != 1)	//当消费者消费后，该线程队列中这个位置就为空了，继续往下就会阻塞在下一语句了，这就是为什么要写while
			{
				pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

				//判断是否要销毁线程
				if (pool->exitNum > 0)
				{
					pool->exitNum--;
					if (pool->liveNum > pool->minNum)
					{
						pool->liveNum--;
						pthread_mutex_unlock(&pool->mutexPool);
						pool->threadExit();
					}
				}
			}

			//再次检查shutdown
			if (pool->shutdown == 1)
			{
				pthread_mutex_unlock(&pool->mutexPool);
				pool->threadExit();
			}		

			//从任务队列中取出一个任务
			auto task = pool->taskQ->takeTask();

			//增加忙碌线程计数
			pool->busyNum++;
			
			pthread_mutex_unlock(&pool->mutexPool);//释放锁，让其他线程可以取任务

			//任务函数和添加忙碌线程
			pthread_mutex_lock(&pool->mutexOutput);
			cout << "thread" << to_string(pthread_self()) << "start working..." << endl;
			pthread_mutex_unlock(&pool->mutexOutput);

			// 执行任务函数。task.arg 是 SmartPtr (即 std::shared_ptr<T>)
			// task.function 的签名应为 void(*)(void*)，所以我们需要传递 get() 得到的原始指针
			// 但要注意，task.arg 的生命周期由智能指针保证，在执行期间有效。
			if (task.function && task.arg)
			{
				task.function(task.arg.get());//传递原始指针给任务函数
			}

			//添加回调函数调用----任务完成后通知
			if (pool->taskCallback && task.arg) {
				pool->taskCallback(task.arg);//直接传递SmartPtr类型
			}

			//任务函数和线程创建和添加成功
			pthread_mutex_lock(&pool->mutexOutput);
			cout << "thread " << to_string(pthread_self()) << "end working..." << endl;
			pthread_mutex_unlock(&pool->mutexOutput);

			pthread_mutex_lock(&pool->mutexPool);
			pool->busyNum--;
			pthread_mutex_unlock(&pool->mutexPool);
		}
		return NULL;
	}

	//管理者线程任务函数
	static void* manager(void* arg)

	{
		ThreadPool* pool = static_cast<ThreadPool*>(arg);
		while (pool->shutdown != 1)
		{
			//每间隔3S检测一次
			sleep(3);

			//获取当前负载(可以是繁忙线程数、队列长度等)
			int currentLoad = pool->getCurrentLoad();

			//更新历史记录
			pool->updateLoadHistory(currentLoad);

			//扩容判断
			if (pool->shouldExpandBaseDnHistory() && pool->liveNum < pool->maxNum)
			{
				//执行扩容逻辑
				pthread_mutex_lock(&pool->mutexPool);
				int counter = 0;
				for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i)
				{
					if (pool->threadIDs[i] == 0)
					{
						pthread_create(&pool->threadIDs[i], NULL, worker, pool);
						counter++;
						pool->liveNum++;
					}
				}
				pthread_mutex_unlock(&pool->mutexPool);
			}

			//缩容判断
			if (pool->shouldShrinkBaseDnHistory() && pool->liveNum > pool->minNum)
			{
				pthread_mutex_lock(&pool->mutexPool);
				pool->exitNum = NUMBER;
				pthread_mutex_unlock(&pool->mutexPool);

				//通知工作线程退出
				for (int i = 0; i < NUMBER; ++i)
				{
					pthread_cond_signal(&pool->notEmpty);
				}

				//更新缩容时间
				pool->lastShrinkTime = time(nullptr);
			}

			//取出线程池中的任务的数量和当前线程的数量
			//取出之前先加上锁，防止再取出的时候别的线程正在写入
			pthread_mutex_lock(&pool->mutexPool);
			size_t queueSize = pool->taskQ->taskNumber();
			int liveNum = pool->liveNum;
			int busyNum = pool->busyNum;
			pthread_mutex_unlock(&pool->mutexPool);

			//取出忙的线程个数

			//添加线程
			//任务的个数>存活的线程个数&&存活的线程数<最大线程数
			if (queueSize > static_cast<size_t>(liveNum) && liveNum < pool->maxNum)
			{
				pthread_mutex_lock(&pool->mutexPool);
				int counter = 0;
				for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i)
				{
					if (pool->threadIDs[i] == 0)
					{
						pthread_create(&pool->threadIDs[i], NULL, worker, pool);
						counter++;
						pool->liveNum++;
					}
				}
				pthread_mutex_unlock(&pool->mutexPool);
			}
			//销毁线程
			//忙的线程*2<存活的线程数&&存活的线程数>最小线程个数
			if (busyNum * 2 < pool->liveNum && pool->liveNum > pool->minNum)
			{
				pthread_mutex_lock(&pool->mutexPool);
				pool->exitNum = NUMBER;
				pthread_mutex_unlock(&pool->mutexPool);
				//让工作的线程自杀
				for (int i = 0; i < NUMBER; ++i)
				{
					pthread_cond_signal(&pool->notEmpty);
				}
			}
		}
		return NULL;
	}

	//单个线程的退出
	void threadExit()
	{
		pthread_t tid = pthread_self();
		for (int i = 0; i < maxNum; ++i)
		{
			if (threadIDs[i] == tid)
			{
				threadIDs[i] = 0;
				cout << "threadExit() called,<<to_string(tid) exiting..." << endl;
				break;
			}
		}
		pthread_exit(NULL);
	}

	//基于历史负载的扩容判断
	bool shouldExpandBaseDnHistory()
	{		
		pthread_mutex_lock(&mutexPool);
		if (loadHistory.size() < HISTORY_SIZE) {
			pthread_mutex_unlock(&mutexPool);
			return false;//历史数据不足
		}

		int sum = 0;
		for (int load : loadHistory)
		{
			sum += load;
		}
		float averageLoad = static_cast<float>(sum) / static_cast<float>(loadHistory.size());
		pthread_mutex_unlock(&mutexPool);
		//扩展阈值可根据实际情况调整,设定平时负载超过70%时考虑扩容
		return averageLoad > 0.7f;
	}
		//基于历史负载的缩容判断
	bool shouldShrinkBaseDnHistory()
	{ 
		time_t currentTime = time(nullptr);//检查冷却时间
		if (currentTime - lastShrinkTime < SHRINK_COOLDOWN)
		{
			return false;//冷却期间内，不进行缩容
		}

		pthread_mutex_lock(&mutexPool);
		if (loadHistory.size() < HISTORY_SIZE) {
			pthread_mutex_unlock(&mutexPool);
			return false;//历史数据不足
		}

		int sum = 0;
		for (int load : loadHistory)
		{
			sum += load;
		}
		float averageLoad = static_cast<float>(sum) / static_cast<float>(loadHistory.size());
		pthread_mutex_unlock(&mutexPool);
		//扩展阈值可根据实际情况调整,设定平时负载超过30%时考虑缩容
		return averageLoad < 0.3f;
	}

	//在适当的地方(如任务处理完成后)更新负载历史
	void updateLoadHistory(int currentLoad)
	{
		pthread_mutex_lock(&mutexPool);
		loadHistory.push_back(currentLoad);

		//保持历史记录大小
		if (loadHistory.size() > HISTORY_SIZE)
		{
			loadHistory.erase(loadHistory.begin(),
				loadHistory.begin() + (loadHistory.size() - HISTORY_SIZE));
		}
		pthread_mutex_unlock(&mutexPool);
	}

private:
	//任务队列
	TaskQueue<T>* taskQ;

	pthread_t managerID;	//管理者线程ID
	pthread_t* threadIDs;	//工作线程ID，由于有多个ID所以定义成一个指针类型
	int busyNum;			//忙碌的线程个数
	int liveNum;			//存活的线程个数
	int minNum;				//最小的线程数量
	int maxNum;				//最大的线程数量
	int exitNum;			//要销毁的线程个数

	//动态调整相关
	std::vector<int> loadHistory; //负载历史记录
	const size_t HISTORY_SIZE = 10;  //历史记录大小
	time_t lastShrinkTime;		  //上次缩容时间
	const int SHRINK_COOLDOWN = 10;	//缩容冷却时间(秒)

	//任务完成回调
	std::function<void(SmartPtr)> taskCallback;

	pthread_mutex_t mutexPool;	//线程池的互斥锁，锁整个线程
	pthread_mutex_t mutexOutput;	//在线程退出时上的一把输出锁，防止线程退出时候仍会有输出竞争
	//条件变量
	pthread_cond_t	notEmpty;	//任务队列是否为空
	static const int NUMBER = 2;	//添加一个全局常量，和宏定义类似

	bool shutdown;			//是否销毁线程池，要销毁扣1，不要扣0
};
