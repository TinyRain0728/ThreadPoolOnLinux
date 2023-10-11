#pragma once
#include "TaskQueue.hpp"
#include <pthread.h>
using namespace std;

// �̳߳���
template <typename T>
class ThreadPool
{
public:
	ThreadPool(int minNum, int maxNum);
	~ThreadPool();

	// ���̳߳��������
	void addTask(Task<T> task);
	// ��ȡ�̳߳��й������̸߳���
	int getBusyNum();
	// ��ȡ�̳߳��л��ŵ��̸߳���
	int getAliveNum();

private:
	// �����̣߳������ߣ�������
	static void* worker(void* arg);
	// �������߳�������
	static void* manager(void* arg);
	// �����߳��˳�����
	void threadExit();

private:
	TaskQueue<T>* taskQ;		// �������

	pthread_t managerID;	// �������߳�ID
	pthread_t* threadIDs;	// �������߳�ID

	int minNum;				// ��С�߳���
	int maxNum;				// ����߳���
	int busyNum;			// æ���̵߳ĸ���
	int liveNum;			// �����̵߳ĸ���
	int exitNum;			// Ҫ���ٵ��̸߳���

	pthread_mutex_t mutexPool;	// �������̳߳�
	pthread_cond_t notEmpty;	// ��������Ƿ����

	bool shutdown;			// �Ƿ�Ҫ�����̳߳�
	static const int NUMBER = 2;
};

//////////////////////////////////////////////////////////
template <typename T>
ThreadPool<T>::ThreadPool(int minNum, int maxNum)
{
	do
	{
		// ��ʼ���������
		taskQ = new TaskQueue<T>();
		if (taskQ == nullptr)
		{
			cout << "new taskQ fail..." << endl;
			break;
		}

		// ��ʼ�������߳�����
		threadIDs = new pthread_t[maxNum]{ 0 };
		if (threadIDs == nullptr)
		{
			cout << "new threadIDs fail..." << endl;
			break;
		}

		// ��ʼ���������
		this->minNum = minNum;
		this->maxNum = maxNum;
		this->busyNum = 0;
		this->liveNum = minNum;
		this->exitNum = 0;

		// ��ʼ��ͬ����
		if (pthread_mutex_init(&mutexPool, NULL) != 0 ||
			pthread_cond_init(&notEmpty, NULL) != 0)
		{
			cout << "mutex or condition init fail..." << endl;
			break;
		}

		shutdown = false;

		// �����߳�
		pthread_create(&managerID, NULL, manager, this);
		for (int i = 0; i < minNum; i++)
		{
			pthread_create(&threadIDs[i], NULL, worker, this);
		}

		return;
	} while (0);

	// ��ʼ��ʧ���ͷ���Դ
	if (threadIDs) delete[] threadIDs;
	if (taskQ) delete taskQ;
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
	shutdown = true;
	// �������չ������߳�
	pthread_join(managerID, NULL);
	// �����������������߳�
	for (int i = 0; i < liveNum; ++i)
	{
		pthread_cond_signal(&notEmpty);
	}
	// �ͷŶ��ڴ�
	if (threadIDs) delete[] threadIDs;
	if (taskQ) delete taskQ;

	//�ͷŻ�������Դ
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
	// �������
	taskQ->addTask(task);
	// ���������Ĺ�������
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
		// ��ǰ�����Ƿ�Ϊ��
		while (pool->taskQ->getTaskNum() == 0 && !pool->shutdown)
		{
			// ���������߳�
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			// �ж��Ƿ�Ҫ�����߳�
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

		// �ж��̳߳��Ƿ�ر�
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			pool->threadExit();
		}

		// �����������ȡ��һ������
		Task<T> task = pool->taskQ->takeTask();
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexPool);

		// ִ������
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
		// ÿ��3s���һ��
		sleep(3);

		// ȡ���̳߳����������������ǰ�̵߳�������æ���̵߳�����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = static_cast<int>(pool->taskQ->getTaskNum());
		int liveNum = pool->liveNum;
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexPool);

		// ����߳�
		// ������� > �����̸߳��� && �����̸߳��� < ����߳���
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

		// �����߳�
		// æ���߳�*2 < �����߳��� && �����߳� > ��С�߳���
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			// �ù������߳���ɱ
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