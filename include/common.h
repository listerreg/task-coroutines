#include <mutex>
#include <memory>

namespace aw_coroutines {
void copyNestedExceptionInfo(char*, const std::exception&, size_t, bool = true);

class Coroutine_error: public std::runtime_error {
public:
	explicit Coroutine_error(const std::string& what_arg);
	explicit Coroutine_error(const char* what_arg);
};

class TaskAwaiterBase;
class AwaiterCallbackBase {
public:
	AwaiterCallbackBase() = default; // lines below would prevent implicit generation of the default constructor
	virtual ~AwaiterCallbackBase() = default;
	AwaiterCallbackBase(const AwaiterCallbackBase&) = delete;
	AwaiterCallbackBase& operator=(const AwaiterCallbackBase&) = delete;
	// move semantics will be deleted also
	virtual void operator()() = 0;
	virtual void operator()(void const*) {};
};

class TaskAwaiterBase {
public:
	bool isCompleted();
	void onCompleted(std::unique_ptr<AwaiterCallbackBase>);
protected:
	char const* hasErrors() const;
	void setError(const std::exception&);
	std::mutex mMtx;
	bool mCompleted = false;
	std::unique_ptr<AwaiterCallbackBase> mOnCompletedCallback;
	char mExcWhats[256];
	bool mError = false;

template <class TInput, class TResult>
friend class Caller;

template <class TTask, class TResult>
friend class AwaiterCallbackUnsink;
};
}
