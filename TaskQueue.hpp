#pragma once
#include <queue>
#include <pthread.h>

using callback = void (*)(void* arg);
// ����ṹ��
template <typename T>
struct Task
{
	Task<T>()
	{
		function = nullptr;
		arg = nullptr;
	}
	Task<T>(callback f, void* arg)
	{
		function = f;
		this->arg = (T*)arg;
	}
	callback function;
	T* arg;
};
// ���������
template <typename T>
class TaskQueue
{
public:
	TaskQueue();
	~TaskQueue();

	// �������
	void addTask(Task<T> task);
	void addTask(callback f, void* arg);
	// ȡ��һ������
	Task<T> takeTask();
	// ��ȡ�������
	inline size_t getTaskNum()
	{
		return m_taskQ.size();
	}

private:
	std::queue<Task<T>> m_taskQ;
	pthread_mutex_t m_mutex;
};

//////////////////////////////////////////////////////////
template <typename T>
TaskQueue<T>::TaskQueue()
{
	pthread_mutex_init(&m_mutex, NULL);
}

template <typename T>
TaskQueue<T>::~TaskQueue<T>()
{
	pthread_mutex_destroy(&m_mutex);
}

template <typename T>
void TaskQueue<T>::addTask(Task<T> task)
{
	pthread_mutex_lock(&m_mutex);
	m_taskQ.push(task);
	pthread_mutex_unlock(&m_mutex);
}

template <typename T>
void TaskQueue<T>::addTask(callback f, void* arg)
{
	Task<T> task(f, arg);
	pthread_mutex_lock(&m_mutex);
	m_taskQ.push(task);
	pthread_mutex_unlock(&m_mutex);
}

template <typename T>
Task<T> TaskQueue<T>::takeTask()
{
	Task<T> task;
	pthread_mutex_lock(&m_mutex);
	if (!m_taskQ.empty())
	{
		task = m_taskQ.front();
		m_taskQ.pop();
	}
	pthread_mutex_unlock(&m_mutex);
	return task;
}
