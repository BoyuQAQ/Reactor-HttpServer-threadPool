#pragma once 
#include <queue>
#include <functional>
#include <memory>



using namespace std;
using callback = void (*)(void* arg);
//任务结构体

//template<class T>
//struct Task
//{
//	//struct在C++中也相当于一个类，因此写了一个构造函数用于初始化
//	Task<T>()
//	{
//		function = nullptr;
//		arg = nullptr;
//	}
//	Task<T>(callback f, void* arg)
//	{
//		this->arg =static_cast<T*>(arg);	//显式类型转换
//		function = f;
//	}
//	callback function;//函数指针
//	T* arg;
//};


//使用std::function来增加灵活性，并使用lambda表达式，避免适配器函数
template<class T>//这里的T应该是std::shared_ptr<Connection>
struct Task
{
	Task():function(nullptr),arg(nullptr){}
	/*Task(callback f,void* arg):arg(static_cast<T*>(arg)),function(f){}*/
	
	//添加新的构造函数，接收std::function
	//Task(std::function<void(void*)>f,void* arg):function(f),arg(static_cast<T*>(arg)){}

	Task(std::function<void(void*)> f,std::shared_ptr<T> a):function(f),arg(a){}
	std::function<void(void*)>function; 
	std::shared_ptr<T> arg;
};

template<class T>
class TaskQueue
{
public:
	TaskQueue()
	{
		pthread_mutex_init(&m_mutex, NULL);
	}
	~TaskQueue()
	{
		pthread_mutex_destroy(&m_mutex);
	}

	//添加任务
	void addTask(Task<T> task)
	{
		pthread_mutex_lock(&m_mutex);
		m_taskQ.push(task);
		pthread_mutex_unlock(&m_mutex);
	}
	void addTask(callback f, std::shared_ptr<T> arg)//为了编译操作，可以添加一个重载函数
	{
		pthread_mutex_lock(&m_mutex);
		Task<T> task;
		task.function = f;	//设置回调函数
		task.arg = arg;		//设置参数
		m_taskQ.push(task);	//第一次入队
		m_taskQ.push(Task<T>(f, arg));	//第二次入队
		pthread_mutex_unlock(&m_mutex);
	}

	//添加对std::function的支持
	void addTask(std::function<void(void*)>f, std::shared_ptr<T> arg) {
		addTask(Task<T>(f, arg));
	}

	//取出一个任务d
	Task<T> takeTask()
	{
		Task<T> t;
		pthread_mutex_lock(&m_mutex);
		if (!m_taskQ.empty())
		{
			t = m_taskQ.front();
			m_taskQ.pop();
		}
		pthread_mutex_unlock(&m_mutex);
		return t;
	}
	//获取当前任务的个数
	inline size_t taskNumber()
	{
		return m_taskQ.size();
	}

private:
	pthread_mutex_t	m_mutex;
	queue<Task<T>> m_taskQ;
};
