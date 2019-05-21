#include <string>
#include <iostream>
#include <memory>
#include "completion_port.h"

#include <unistd.h>
#include <sys/syscall.h>
#include <thread>
#include <chrono>

#ifdef SYS_gettid
#define MY_GETTID syscall(SYS_gettid)
#else
#define MY_GETTID "---"
#endif

#ifndef NDEBUG
#define PRINT_THREAD printf("[Thread %lx (%ld) at %s:%d in '%s']\n", pthread_self(), MY_GETTID, __FILE__, __LINE__, __PRETTY_FUNCTION__);
#else
#define PRINT_THREAD
#endif

using namespace aw_completionPort;
Task<double> globalTask;
int counter{1};

class MyClass {
public:
	MyClass() = delete;
	MyClass(int arg) : mArg(arg) {
		//std::cout << "MyClass ctor" << std::endl;
	}
	MyClass(const MyClass& other) : mArg(other.mArg) {
		//std::cout << "MyClass copy ctor" << std::endl;
	}
	MyClass& operator=(const MyClass& other) {
		//std::cout << "MyClass copy assignment" << std::endl;
		mArg = other.mArg;
		return *this;
	}
	MyClass(MyClass&& other) {
		//std::cout << "MyClass move ctor" << std::endl;
		std::swap(mArg, other.mArg);
	}
	MyClass& operator=(MyClass&& other) {
		//std::cout << "MyClass move assignment" << std::endl;
		std::swap(mArg, other.mArg);
		return *this;
	}
	~MyClass() {
		//std::cout << "MyClass DTOR" << std::endl;
	}
private:
	int mArg;
friend std::ostream& operator<<(std::ostream& os, const MyClass& myClass);
};

std::ostream& operator<<(std::ostream& os, const MyClass& myClass) {
	return os << myClass.mArg;
}

// forward declaration
int coroutine2(Caller<std::shared_ptr<Task<std::string>>, int>, std::shared_ptr<Task<std::string>>);

MyClass coroutine(Caller<DataBase*, MyClass> caller, DataBase* pDb) {
	PRINT_THREAD
	// call some asynchronous method provided you have such available
	// the one below comes from our fake completion port framework
	std::shared_ptr<Task<std::string>> task = pDb->queryAsync("192.168.1.99", "SELECT Name FROM Users WHERE Id = 1");

	// do some unrelated stuff
	std::cout << "execution is uninterrupted while our request is being processed" << std::endl;
	PRINT_THREAD

	// when there is nothing more to do without the result from the database...
	// ..."block" the execution and wait for the task to be resolved (containing the result)
	// "blocking" here means blocking this line of execution. In fact the execution will be interrupted and the coroutine will return
	
	std::string result = caller.await(*task);
	std::cout << "now execution was picked up on a different thread" << std::endl;
	PRINT_THREAD

	// use the result in a desired way
	std::cout << "result of querying database for the first time: " << result << std::endl;

	// now we can make subsequent asynchronous calls
	std::shared_ptr<Task<std::string>> task2 = pDb->queryAsync("192.168.1.99", "SELECT Surname FROM Users WHERE Name = " + result);

	// we are not required to await every task. If we don't care for the result we can carry on

	// the coroutines mechanism is reentrant so we can construct a caller and invoke another coroutine right here
	// we can even pass our previous task to the new coroutine to await it there
	Caller<std::shared_ptr<Task<std::string>>, int> clr{coroutine2};
	std::shared_ptr<Task<int>> coroTask = clr(task2);


	// to not lose track of our executions we'll await second coroutine here
	// since the task returned from the coroutine is no different from other tasks we can synchronously wait() for it or asynchronously await() it

	/*
	coroTask->wait(); // synchronous
	std::cout << "result of the second coroutine: " << coroTask->getResult() << std::endl;
	*/
	int coroResult = caller.await(*coroTask); // asynchronous
	std::cout << "result of the second coroutine: " << coroResult << std::endl;
	PRINT_THREAD

	return MyClass{77};
}

int coroutine2(Caller<std::shared_ptr<Task<std::string>>, int> caller, std::shared_ptr<Task<std::string>> task) {
	PRINT_THREAD
	std::string result = caller.await(*task);
	PRINT_THREAD
	std::cout << "result of the second database query (task passed to the second coroutine): " << result << std::endl;

	// just for demonstration lets await the task that is already resolved
	double result2 = caller.await(globalTask);
	// the execution won't be interrupted by the above call
	PRINT_THREAD
	std::cout << "result of the global task resolved in advance: " << result2 << std::endl;

	// our coroutine can throw as any function. An exception although should be of or derived from the type std::exception. It can be also the std::nested_exception exception
	try {
		if (--counter >= 0) {
			std::cout << "throwing... now" << std::endl;
			throw std::runtime_error("inner exception");
		}
	} catch (...) {
		std::throw_with_nested(std::runtime_error("outer exception"));
	}
	
	return 2;
}

int main() {
	globalTask.setResult(2.71828); // just so we have a resolved task for use in the coroutine2
	//globalTask.setException(std::runtime_error("oh la la!")); // it can be erroneous instead

	Caller<DataBase*, MyClass> clr{coroutine};
	DataBase db;

	std::shared_ptr<Task<MyClass>> coroTask = clr(&db);
	PRINT_THREAD

///	std::shared_ptr<Task<char>> nextTask = coroTask->continueWith<char>([](Task<int>& task){ std::cout << "result from the inside of the continueWith: " << task.getResult() << ". Now we will throw from the inside..." << std::endl; throw std::runtime_error("ERROR FROM INSIDE"); return 'B'; });
	std::shared_ptr<Task<char>> nextTask = coroTask->continueWith<char>([](Task<MyClass>& task){
		std::cout << "we're in the continuation. wait()-ing for the task..." << std::endl;
		PRINT_THREAD
		try {
			task.wait();
		} catch(std::exception& ex) {
			std::cout << "we've caught an exception: " << ex.what() << std::endl;
		}
		std::cout << " ...leaving continuation" << std::endl;
		return 'A';
	});

	try {
		std::cout << "waiting for the next task nextTask->wait() " << std::endl;
		nextTask->wait();
		std::cout << "result of the nextTask: " << nextTask->getResult() << std::endl;
	} catch (const std::exception& ex) {
		std::cout << "nextTask->wait() threw an exception: " << ex.what() << std::endl;
	}
	try {
		std::cout << "waiting for the main task coroTask->wait() " << std::endl;
		coroTask->wait();
		std::cout << "result of the first coroutine from main(): " << coroTask->getResult() << std::endl;
	} catch (const std::exception& ex) {
		std::cout << "coroTask->wait() threw an exception: " << ex.what() << std::endl;
		std::cout << "reinvoking main coroutine clr(&db)" << std::endl;
		PRINT_THREAD
		try {
		auto retryTask = clr(&db);
		std::cout << "waiting for the retry task retryTask->wait() " << std::endl;
		retryTask->wait();
		std::cout << "result of the retried coroutine from main(): " << retryTask->getResult() << std::endl;
		} catch(std::exception& ex) {
			std::cout << "the retry threw also: " << ex.what() << std::endl;
		}
	}
	PRINT_THREAD
}
