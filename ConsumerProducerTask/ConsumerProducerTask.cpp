#include <iostream>
#include <random>
#include <thread>
#include <string>
#include <chrono>
#include <queue>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <condition_variable>



struct TRandomData
{
	unsigned int Number{ 0 };
	unsigned long TimeToGenerate{ 0 };
	unsigned int Order{ 0 };

	TRandomData() = default;

	TRandomData(unsigned int num, unsigned long time, unsigned int order) : Number(num), TimeToGenerate(time), Order(order) {}
};


class TRandomizer
{
private:
	std::random_device Rd;
	std::mt19937 Gen;
	std::uniform_int_distribution<unsigned int> Dist;

public:
	explicit TRandomizer(unsigned int first, unsigned int last) : Gen(Rd()), Dist(first, last) {}
	explicit TRandomizer(unsigned int n) : Gen(Rd()), Dist(1, n) {}

	unsigned int GetNext() { return Dist(Gen); }
};


class TIntegerQueue
{
private:
	std::queue<unsigned int> Queue;
	std::mutex Mutex;

	const unsigned int MaxQueueSize = 1000;

	std::condition_variable_any	QueueNotFullCond;
	std::condition_variable_any	QueueNotEmptyCond;

public:
	void SaveIntoQueue(unsigned int, std::stop_token);
	unsigned int GetFromQueue(std::stop_token);
};


void TIntegerQueue::SaveIntoQueue(unsigned int element, std::stop_token st)
{
	std::unique_lock<std::mutex> lock(Mutex);

	// wait until place in the queue appears
	QueueNotFullCond.wait(lock, st, [this] {return Queue.size() < MaxQueueSize; });

	// push element into queue
	Queue.push(element);

	// notify that queue not empty
	QueueNotEmptyCond.notify_all();
}

unsigned int TIntegerQueue::GetFromQueue(std::stop_token st)
{
	std::unique_lock<std::mutex> lock(Mutex);

	// wait until there is something in the queue
	QueueNotEmptyCond.wait(lock, st, [this] {return !Queue.empty(); });

	// pop the element from the queue 
	unsigned int element = Queue.front();
	Queue.pop();

	// notify that size of the queue decreased
	QueueNotFullCond.notify_all();

	return element;
}


class TStorage
{
private:
	std::vector<TRandomData> Storage;
	unsigned int StorageSize;
	unsigned int Counter = 0;

	std::mutex Mutex;
	std::stop_source StopSource;

	std::chrono::time_point<std::chrono::high_resolution_clock> Start;

	int DigitsInN = 5;

public:
	explicit TStorage(unsigned int storage_size);

	void ProcessNext(unsigned int);

	std::stop_token GetStopToken() { return StopSource.get_token(); }

	const std::vector<TRandomData>& GetStorage() const { return Storage; }
};

TStorage::TStorage(unsigned int storage_size) : StorageSize(storage_size), Storage(storage_size)
{
	Start = std::chrono::high_resolution_clock::now();

	// We expect that storage_size is always in [1, RAND_MAX]
	DigitsInN = log10(storage_size) + 1.0;
}

void TStorage::ProcessNext(unsigned int element)
{
	std::scoped_lock<std::mutex> lock(Mutex);

	// element should be in [1;StorageSize]
	if (element > 0 && element <= StorageSize)
	{
		unsigned int index = element - 1;

		// element not exist in the storage yet
		if (Storage[index].Number == 0)
		{
			std::chrono::time_point<std::chrono::high_resolution_clock> end = std::chrono::high_resolution_clock::now();

			Storage[index].Number = element;
			Storage[index].Order = ++Counter;
			Storage[index].TimeToGenerate = std::chrono::duration_cast<std::chrono::microseconds> (end - Start).count();

			std::cout << "Thread: " << std::setw(5) << std::this_thread::get_id()
				<< ", Number: " << std::setw(DigitsInN) << Storage[index].Number
				<< ", Order: " << std::setw(DigitsInN) << Storage[index].Order
				<< ", Elapsed time in microseconds: " << Storage[index].TimeToGenerate << std::endl;

			Start = end;
		}

		// all elements stored
		if (Counter == StorageSize)
			StopSource.request_stop();
	}
}


int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "No arguments provided.\nCorrect usage: " << argv[0] << " [Number of elements to generate]" << std::endl;
		return 0;
	}

	std::string argument(argv[1]);

	std::string usage = "\n\nCorrect usage: " + std::string(argv[0]) + " N\n\n     where N is Number of elements to generate\n";

	try
	{
		long element_number = std::stol(argument);

		if (element_number <= 0 || element_number > RAND_MAX)
		{
			std::cout << "Bad argument: " << element_number << ". N should be in [1," << RAND_MAX << "]" << usage;
			return 0;
		}

		TIntegerQueue queue;
		TStorage storage(element_number);
		TRandomizer rzr(1, element_number);

		auto producer_l = [&queue, &rzr, &storage] {
			while (!storage.GetStopToken().stop_requested())
				queue.SaveIntoQueue(rzr.GetNext(), storage.GetStopToken());
		};

		auto consumer_l = [&queue, &storage] {
			while (!storage.GetStopToken().stop_requested())
				storage.ProcessNext(queue.GetFromQueue(storage.GetStopToken()));
		};

		{
			std::vector<std::jthread> produce_threads;
			std::vector<std::jthread> consume_threads;

			constexpr auto thread_number = 3;

			produce_threads.reserve(thread_number);
			consume_threads.reserve(thread_number);

			for (auto i = 0; i < thread_number; ++i)
			{
				produce_threads.emplace_back(std::jthread(producer_l));
				consume_threads.emplace_back(std::jthread(consumer_l));
			}
		}

		if (!storage.GetStorage().empty())
		{
			unsigned long sum = 0, num = 0;
			auto avgl = [&sum, &num](const TRandomData& item) { num++; sum += item.TimeToGenerate; };
			for_each(storage.GetStorage().begin(), storage.GetStorage().end(), avgl);
			std::cout << "\nAverage time, microseconds: " << double(sum) / double(num) << std::endl;
		}
	}
	catch (...) // mostly for stol exceptions
	{
		std::cout << "Bad argument: " << argv[1] << usage;
	}

	return 0;
}