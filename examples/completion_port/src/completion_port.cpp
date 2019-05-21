#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <queue>
#include "completion_port.h"

namespace aw_completionPort {

class CompletionPort;
bool getQueuedCompletionStatus(CompletionPort *cp, void **completionKey);

class TaskResolverBase {
public:
	virtual ~TaskResolverBase() {};
	virtual void resolve() = 0;
};

class CompletionPort {
public:
	~CompletionPort() {
		std::unique_lock<std::mutex> lk(mutex);
		goHome = true;
		lk.unlock();
		cv.notify_one();
		thread.join();
	}
	std::mutex mutex;
	std::condition_variable cv;
	std::queue<int> socketsPending;
	bool goHome = false;
	void* completionKeys[20] = {nullptr};
	std::thread thread = {std::thread(&CompletionPort::completionPortAppWorkerThread, this)};

	void completionPortAppWorkerThread() {
		void *completionKey = nullptr;
		while (true) {
			if(!getQueuedCompletionStatus(this, &completionKey))
				break;
			TaskResolverBase* pResolver = static_cast<TaskResolverBase*>(completionKey);
			pResolver->resolve();
			delete pResolver;
		}
	}
};

void kernelWakeThreadFor(CompletionPort *cp, int socket) {
	std::unique_lock<std::mutex> lk(cp->mutex);
	cp->socketsPending.push(socket);
	lk.unlock();
	cp->cv.notify_one();
}

static class Kernel {
public:
	Kernel() : kernelFileTable(100) {}

	~Kernel() {
		for (auto & t : threads)
			t.join();
	}

	int socket() {
		return ++socketNumber;
	}
	void connect(int socket, std::string address) {
		std::cout << "connecting the socket: " << socket << " to the server: " << address << " BEEP..." << std::endl;
	}
	void send(int socket, std::string query) {
		std::cout << "sending query \"" << query << "\" through the socket: " << socket << " BOOP..." << std::endl;
	}
	std::string read(int socket) {
		std::unique_lock<std::mutex> lk(mutex);
		return kernelFileTable[socket].second;
	}
	void readAsync(int socket) {
		threads.emplace_back(&Kernel::kernel_read, this, socket);
	}
	void fillSocketBuffer(int socket, std::string value) {
		std::unique_lock<std::mutex> lk(mutex);
		kernelFileTable[socket].second = value;
	}
	void addSocketToCompletionPort(int socket, CompletionPort* cp) {
		std::unique_lock<std::mutex> lk(mutex);
		kernelFileTable[socket].first = cp;
	}
	std::pair<CompletionPort*, std::string> readFileTable(int socket) {
		std::unique_lock<std::mutex> lk(mutex);
		return kernelFileTable[socket];
	}
private:
	void kernel_read(int socket) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		fillSocketBuffer(socket, responses[(socket-1)%5]); // some value received over network
		kernelWakeThreadFor(readFileTable(socket).first, socket);
	}
	std::mutex mutex;
	std::vector<std::thread> threads;
	std::vector<std::pair<CompletionPort*, std::string>> kernelFileTable;
	int socketNumber = 0;
	std::string responses[5] = { "June", "Moone", "RESPONSE_3", "RESPONSE_4", "RESPONSE_5" };
} kernel;

CompletionPort* createIoCompletionPort(int fileHandle, CompletionPort *existingCompletionPort, void *completionKey) {
	CompletionPort* result = nullptr;
	if (existingCompletionPort) {
		existingCompletionPort->completionKeys[fileHandle] = completionKey;
		kernel.addSocketToCompletionPort(fileHandle, existingCompletionPort);
	} else {
		result = new CompletionPort();
	}
	return result;
}

void closeCompletionPort(CompletionPort* cp) {
	delete cp;
}

bool getQueuedCompletionStatus(CompletionPort *cp, void **completionKey) {
	std::unique_lock<std::mutex> lk(cp->mutex);

	if (cp->socketsPending.empty() && !cp->goHome)
		cp->cv.wait(lk, [cp]{return !cp->socketsPending.empty() || cp->goHome;}); // releases the lock and reacquires it upon return
	if (cp->goHome)
		return false;
	*completionKey = cp->completionKeys[cp->socketsPending.front()];
	cp->socketsPending.pop();
	return true;
}

void dbTaskResolvingThread(std::shared_ptr<Task<std::string>> task, std::string result) {
	try {
		task->setResult(result);
	} catch(std::exception& ex) {
		std::cout << "Logging caught exception: " << ex.what() << std::endl; // print nested XXX
	}
}

class StringReadTaskResolver : public TaskResolverBase {
public:
	StringReadTaskResolver(DataBase* db, int socket, std::shared_ptr<Task<std::string>> spTask): mDataBase(db), mSocket(socket), mTask(spTask) {}
	void resolve() override {
		std::string result = kernel.read(mSocket);
		mDataBase->threads.emplace_back(dbTaskResolvingThread, mTask, result);
	}
private:
	DataBase* mDataBase;
	int mSocket;
	std::shared_ptr<Task<std::string>> mTask;
};

DataBase::DataBase() : completionPort(createIoCompletionPort(0, nullptr, nullptr)) {}

DataBase::~DataBase() {
	for (auto& t : threads)
		t.join();

	closeCompletionPort(completionPort);
}

std::shared_ptr<Task<std::string>> DataBase::queryAsync(std::string address, std::string query) {
	int socket = kernel.socket();
	kernel.connect(socket, address);
	kernel.send(socket, query);
	std::shared_ptr<Task<std::string>> spTask = std::make_shared<Task<std::string>>();
	createIoCompletionPort(socket, completionPort, new StringReadTaskResolver(this, socket, spTask));
	kernel.readAsync(socket);
	return spTask;
}
}
