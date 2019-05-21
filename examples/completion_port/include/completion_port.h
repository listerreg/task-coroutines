#ifndef AW_COMPLETIONPORT_H
#define AW_COMPLETIONPORT_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "taskcoroutines.h"

using namespace aw_coroutines;
namespace aw_completionPort {
class CompletionPort;

class DataBase {
public:
	DataBase();
	~DataBase();
	DataBase(const DataBase&) = delete;
	DataBase& operator=(const DataBase&) = delete;
	DataBase(DataBase&&) = default;
	DataBase& operator=(DataBase&&) = default;
	std::shared_ptr<Task<std::string>> queryAsync(std::string address, std::string query);
	std::vector<std::thread> threads;
private:
	CompletionPort* completionPort;
};
}
#endif
