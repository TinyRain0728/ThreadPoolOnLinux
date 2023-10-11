#pragma once
#include "TaskQueue.hpp"
#include <pthread.h>
using namespace std;

// 线程池类
template <typename T>
class ThreadPool
{
public:
	ThreadPool(int minNum, int maxNum);
	~ThreadPool();

	// 给线程池添加任务
	void addTask(Task<T> task);
	// 获取线程池中工作的线程个数
	int getBusyNum();
	// 获取线程池中活着的线程个数
	int getAliveNum();

private:
	// 工作线程（消费者）任务函数
	static void* worker(void* arg);
	// 管理者线程任务函数
	static void* manager(void* arg);
	// 单个线程退出函数
	void threadExit();

private:
	TaskQueue<T>* taskQ;		// 任务队列

	pthread_t managerID;	// 管理者线程ID
	pthread_t* threadIDs;	// 工作者线程ID

	int minNum;				// 最小线程数
	int maxNum;				// 最大线程数
	int busyNum;			// 忙的线程的个数
	int liveNum;			// 存活的线程的个数
	int exitNum;			// 要销毁的线程个数

	pthread_mutex_t mutexPool;	// 锁整个线程池
	pthread_cond_t notEmpty;	// 任务队列是否空了

	bool shutdown;			// 是否要销毁线程池
	static const int NUMBER = 2;
};

//////////////////////////////////////////////////////////
template <typename T>
ThreadPool<T>::ThreadPool(int minNum, int maxNum)
{
	do
	{
		// 初始化任务队列
		taskQ = new TaskQueue<T>();
		if (taskQ == nullptr)
		{
			cout << "new taskQ fail..." << endl;
			break;
		}

		// 初始化工作线程数组
		threadIDs = new pthread_t[maxNum]{ 0 };
		if (threadIDs == nullptr)
		{
			cout << "new threadIDs fail..." << endl;
			break;
		}

		// 初始化相关数据
		this->minNum = minNum;
		this->maxNum = maxNum;
		this->busyNum = 0;
		this->liveNum = minNum;
		this->exitNum = 0;

		// 初始化同步量
		if (pthread_mutex_init(&mutexPool, NULL) != 0 ||
			pthread_cond_init(&notEmpty, NULL) != 0)
		{
			cout << "mutex or condition init fail..." << endl;
			break;
		}

		shutdown = false;

		// 创建线程
		pthread_create(&managerID, NULL, manager, this);
		for (int i = 0; i < minNum; i++)
		{
			pthread_create(&threadIDs[i], NULL, worker, this);
		}

		return;
	} while (0);

	// 初始化失败释放资源
	if (threadIDs) delete[] threadIDs;
	if (taskQ) delete taskQ;
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
	shutdown = true;
	// 阻塞回收管理者线程
	pthread_join(managerID, NULL);
	// 唤醒阻塞的消费者线程
	for (int i = 0; i < liveNum; ++i)
	{
		pthread_cond_signal(&notEmpty);
	}
	// 释放堆内存
	if (threadIDs) delete[] threadIDs;
	if (taskQ) delete taskQ;

	//释放互斥锁资源
	pthread_mutex_destroy(&mutexPool);
	pthread_cond_destroy(&notEmpty);
}

template <typename T>
void ThreadPool<T>::addTask(Task<T> task)
{
	pthread_mutex_lock(&mutexPool);
	if (shutdown)
	{
		pthread_mutex_unlock(&mutexPool);
		return;
	}
	// 添加任务
	taskQ->addTask(task);
	// 唤醒阻塞的工作进程
	pthread_cond_signal(&notEmpty);
	pthread_mutex_unlock(&mutexPool);
}

template <typename T>
int ThreadPool<T>::getBusyNum()
{
	pthread_mutex_lock(&mutexPool);
	int busyNum = this->busyNum;
	pthread_mutex_unlock(&mutexPool);
	return busyNum;
}

template <typename T>
int ThreadPool<T>::getAliveNum()
{
	pthread_mutex_lock(&mutexPool);
	int aliveNum = this->liveNum;
	pthread_mutex_unlock(&mutexPool);
	return aliveNum;
}

template <typename T>
void* ThreadPool<T>::worker(void* arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);

	while (true)
	{
		pthread_mutex_lock(&pool->mutexPool);
		// 当前队列是否为空
		while (pool->taskQ->getTaskNum() == 0 && !pool->shutdown)
		{
			// 阻塞工作线程
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			// 判断是否要销毁线程
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

		// 判断线程池是否关闭
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			pool->threadExit();
		}

		// 从任务队列中取出一个任务
		Task<T> task = pool->taskQ->takeTask();
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexPool);

		// 执行任务
		cout << "thread " << pthread_self() << " start working..." << endl;
		task.function(task.arg);
		delete task.arg;
		task.arg = nullptr;

		cout << "thread " << pthread_self() << " end working..." << endl;
		pthread_mutex_lock(&pool->mutexPool);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexPool);
	}
	return NULL;
}

template <typename T>
void* ThreadPool<T>::manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (!pool->shutdown)
	{
		// 每隔3s检测一次
		sleep(3);

		// 取出线程池中任务的数量、当前线程的数量、忙的线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = static_cast<int>(pool->taskQ->getTaskNum());
		int liveNum = pool->liveNum;
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexPool);

		// 添加线程
		// 任务个数 > 存活的线程个数 && 存活的线程个数 < 最大线程数
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER
				&& pool->liveNum < pool->maxNum; ++i)
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

		// 销毁线程
		// 忙的线程*2 < 存活的线程数 && 存活的线程 > 最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			// 让工作的线程自杀
			for (int i = 0; i < NUMBER; ++i)
			{
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

template <typename T>
void ThreadPool<T>::threadExit()
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < maxNum; ++i)
	{
		if (threadIDs[i] == tid)
		{
			threadIDs[i] = 0;
			cout << "threadExit() called, " << tid << " exiting..." << endl;
			break;
		}
	}
	pthread_exit(NULL);
}