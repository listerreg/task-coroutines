#ifndef AW_TASKCORO_H
#define AW_TASKCORO_H

#include <condition_variable>
#include <functional>
#include "common.h"
#include "coro-concepts.h"

#if __cpp_lib_optional >= 201603
#include <optional>
#endif

extern "C" void sink_asm(void*);
extern "C" int saveandswitch_asm(void*);
extern "C" int unsink_asm(void*);

namespace aw_coroutines {

template <class T>
class TaskAwaiter: public TaskAwaiterBase {
public:
	~TaskAwaiter() {
		if (hasResult)
			reinterpret_cast<T*>(resultPlaceholder)->~T();
	}
	T getResult();
	T const* getResultPointer();
protected:
	T* mResult = reinterpret_cast<T*>(resultPlaceholder);
	bool hasResult = false;
private:
	alignas(T) char resultPlaceholder[sizeof(T)]; // we use this insted of the std::optional beacause we want a stable address of a result that the mResult holds
};

template <class T>
class Task: public TaskAwaiter<T> {
public:
	template<typename TResult>
	std::shared_ptr<Task<TResult>> continueWith(std::function<TResult(Task<T>&)>);
	TaskAwaiter<T> *getAwaiter();
	void setResult(T result);
	void setException(const std::exception&);
	void wait();
private:
	std::condition_variable cv;
	std::unique_ptr<AwaiterCallbackBase> mContinueWithCallback;
};

template<class TResult>
struct WholeState {
	struct StackState { // It might be as well an array. Struct was chosen for some clarity. All members are of the size_t type so there is no padding
		size_t save_returnAddress; // At the position 0; the return address of the save_asm()
		size_t save_RSP;
		size_t save_RBP;
		size_t save_RBX;
		size_t save_R12;
		size_t save_R13;
		size_t save_R14;
		size_t save_R15; // at the position 56

		size_t sink_returnAddress; // at the position 64
		size_t sink_RSP;
		size_t sink_RBP;
		size_t sink_RBX;
		size_t sink_R12;
		size_t sink_R13;
		size_t sink_R14;
		size_t sink_R15; // at the position 120

		size_t stackStoragePointer; // at the position 128 (8MB)
		size_t returnAddressPointer; // at the position 136
		size_t stackOffset; // at the position 144
	} mState;
	void const* mResolvedValue;
	std::mutex mMtx;
	std::shared_ptr<Task<TResult>> mTask = std::make_shared<Task<TResult>>(); // from C++20 you could use the atomic_shared_ptr
	TaskAwaiterBase* mTaskAwaiter = nullptr;
	std::unique_ptr<AwaiterCallbackBase> mTaskAwaiterCallback;
	char caughtException[256];
	bool isCaught = false;
};

#if __cpp_concepts >= 201507
template <NonReference TInput, NonReference TResult>
	requires CopyConstructible<TInput> && CopyConstructible<TResult>
#else
template <class TInput, class TResult>
#endif
class Caller {
public:
	Caller(TResult (*)(Caller, TInput));
	std::shared_ptr<Task<TResult>> operator()(TInput);
	template <typename TInterResult>
	TInterResult await(Task<TInterResult>&);
	void unsink(void const*);
private:
	WholeState<TResult> *firstLevel(TInput*) noexcept;
	void secondLevel(TInput*, WholeState<TResult>*) noexcept;
	std::shared_ptr<WholeState<TResult>> mWholeState;
	TResult (*mRoutine)(Caller, TInput);
};

template <class TTask, class TResult>
class AwaiterCallbackUnsink: public AwaiterCallbackBase {
public:
	AwaiterCallbackUnsink(std::shared_ptr<WholeState<TResult>>, TaskAwaiter<TTask>*);
	void operator()() override;
	void operator()(void const*) override;
private:
	std::shared_ptr<WholeState<TResult>> mWholeState; // this AwaiterCallbackUnsink is inside the intermediate task and the WholeState holds the shared_ptr only to the main task - so no cyclic reference
	TaskAwaiter<TTask> *mAwaiter;
};

template<class TPrevTask, class TResult>
class AwaiterCallbackContinueWith : public AwaiterCallbackBase {
public:
	AwaiterCallbackContinueWith(TPrevTask&, std::shared_ptr<Task<TResult>>, std::function<TResult(TPrevTask&)>);
	void operator()() override;
private:
	TPrevTask& mPrevTask;
	std::shared_ptr<Task<TResult>> mNextTask;
	std::function<TResult(TPrevTask&)> mFunc;
};

// TEMPLATED MEMBERS DEFINITIONS
template <class T>
T TaskAwaiter<T>::getResult() {
	std::unique_lock<std::mutex> lk(mMtx);
	if (hasResult)
		return *mResult;
	else
		if (mCompleted)
			throw std::runtime_error("Trying to get the result of a task that ended with an exception.");
		else
			throw std::runtime_error("Trying to get the result of an unresolved task.");
}

template <class T>
T const* TaskAwaiter<T>::getResultPointer() {
		return mResult;
}

template <class T>
template<typename TResult>
std::shared_ptr<Task<TResult>> Task<T>::continueWith(std::function<TResult(Task<T>&)> func) {
	std::shared_ptr<Task<TResult>> spResult = std::make_shared<Task<TResult>>();
	std::unique_lock<std::mutex> lk(TaskAwaiterBase::mMtx);
	if (TaskAwaiterBase::mCompleted) {
		TaskAwaiterBase::mMtx.unlock();
		AwaiterCallbackContinueWith<Task<T>, TResult> callback(*this, spResult, func);
		callback(); // this will throw only on an unrecoverable error
	} else {
		mContinueWithCallback = std::make_unique<AwaiterCallbackContinueWith<Task<T>, TResult>>(*this, spResult, func);
		TaskAwaiterBase::mMtx.unlock();
	}
	return spResult;
}

template <class T>
TaskAwaiter<T> *Task<T>::getAwaiter() {
	return static_cast<TaskAwaiter<T>*>(this);
}

template <class T>
void Task<T>::setResult(T result) {
	std::unique_lock<std::mutex> lk(TaskAwaiterBase::mMtx);
	if (TaskAwaiterBase::mCompleted)
		throw std::runtime_error("Trying to resolve a resolved or an erroneous task.");
	TaskAwaiterBase::mCompleted = true;
	new (TaskAwaiter<T>::mResult) T(std::move(result));
	TaskAwaiter<T>::hasResult = true;
	auto callback = std::move(TaskAwaiterBase::mOnCompletedCallback); // this way hypothetical immediate callback change won't cause errors
	auto callbackFunc = std::move(mContinueWithCallback);
	lk.unlock();
	if (callback)
		(*callback)(TaskAwaiter<T>::mResult);
	cv.notify_all();
	if (callbackFunc)
		(*callbackFunc)();
	/* --- if anything above throws we consider it as an unrecoverable error --- */
}

template <class T>
void Task<T>::setException(const std::exception& ex) {
	std::unique_lock<std::mutex> lk(TaskAwaiterBase::mMtx); // won't throw if there's no invalid use of the lock ("The behavior is undefined if the current thread already owns the mutex except when the mutex is recursive.")
	TaskAwaiterBase::mCompleted = true;
	TaskAwaiterBase::setError(ex);
	auto callback = std::move(TaskAwaiterBase::mOnCompletedCallback); // won't throw, AwaiterCallbackUnsink is known
	lk.unlock(); // won't throw ("std::system_error is thrown if there is no associated mutex or if the mutex is not locked.")
	/* --- nothing above can throw, though it doesn't really matter --- */
	/* --- if anything below throws we consider it as an unrecoverable error --- */
	auto callbackFunc = std::move(mContinueWithCallback);
	if (callback)
		(*callback)(nullptr);
	cv.notify_all();
	if (callbackFunc)
		(*callbackFunc)();
}

template <class T>
void Task<T>::wait() {
	std::unique_lock<std::mutex> lk(TaskAwaiterBase::mMtx);
	if (TaskAwaiterBase::mCompleted) {
		if (char const* what = TaskAwaiterBase::hasErrors())
			throw Coroutine_error(what);
		return;
	}
	cv.wait(lk, [this]{return this->mCompleted;}); // releases the lock and reacquires it upon return
	if (char const* what = TaskAwaiterBase::hasErrors())
		throw Coroutine_error(what);
}

#if __cpp_concepts >= 201507
template <NonReference TInput, NonReference TResult>
	requires CopyConstructible<TInput> && CopyConstructible<TResult>
#else
template <class TInput, class TResult>
#endif
Caller<TInput, TResult>::Caller(TResult (*f)(Caller, TInput)): mRoutine(f){}

#if __cpp_concepts >= 201507
template <NonReference TInput, NonReference TResult>
	requires CopyConstructible<TInput> && CopyConstructible<TResult>
#else
template <class TInput, class TResult>
#endif
std::shared_ptr<Task<TResult>> Caller<TInput, TResult>::operator()(TInput arg) {
	mWholeState = std::make_shared<WholeState<TResult>>();
	WholeState<TResult> *fromSink = firstLevel(&arg);
	if (fromSink && !mWholeState->mState.sink_returnAddress) {
		throw std::runtime_error("malloc did not succeed to allocate the stack.");
	}

	if (fromSink) { // didn't go synchronously (we're after the first call to the sink)
		mWholeState->mTaskAwaiter->onCompleted(std::move(mWholeState->mTaskAwaiterCallback));
		// if the callback will be executed right away and it will end with errors it will be like the coroutine has ended "synchronously". We will have two possibilities:
		// 1. the continuation of the coroutine threw - this is just an equivalent of the synchronous case (do nothing)
		// 2. or/and the setResult or the setException threw and we want it to propagate (the unrecoverable error):
	}
	if (mWholeState->isCaught) // only unrecoverable errors will be thrown from here. Exceptions from the coroutine will be thrown from the wait() call
		throw std::runtime_error(mWholeState->caughtException);

	return atomic_load(&mWholeState->mTask); // we need an atomic operation here because at the same time the task can be resolved (so the shared_ptr needs to be dereferenced) in the secondLevel() by another thread running the above onCompleted callback ("If multiple threads of execution access the same std::shared_ptr object without synchronization and any of those accesses uses a non-const member function of shared_ptr then a data race will occur unless all such access is performed through these functions")
	// NOTE: you cannot move atomically a shared shared_ptr so we have another copy from the atomic_load anyway (it will be "moved" since it's a temporary)
}

// return address of this routine will be replaced by the saveandswitch_asm() and it'll point to cleanup_asm(). It will be used when user's routine will end (synchronously or asynchronously)
#if __cpp_concepts >= 201507
template <NonReference TInput, NonReference TResult>
	requires CopyConstructible<TInput> && CopyConstructible<TResult>
#else
template <class TInput, class TResult>
#endif
WholeState<TResult> *Caller<TInput, TResult>::firstLevel(TInput *pArg) noexcept {
	// return from the save_asm will be mimic by all the sink_asm() calls (in that case the returned value is set to true). The stack can be in a various state though
	// save_asm saves its return address and the RBX, RBP, and the R12–R15 registers and sets the return value to false
	int fromSink = saveandswitch_asm(mWholeState.get()); // save the registers and switch the stacks
	if (fromSink == 2)
		return mWholeState.get(); // Aaah, error! Serves as a flag

	if (fromSink) {
		// after first call to the sink() - stack is valid (switched back by the sink), return address is unchanged
		return mWholeState.get(); // serves as a flag
	}
	// after actual call to the saveandswitch_asm(). We're on the alternative stack
	WholeState<TResult> *pWholeState = mWholeState.get();
	secondLevel(pArg, pWholeState);
	// coroutine has ended, no valid "this" pointer from now on
	return pWholeState; // this is so the cleanup_asm (synchronous or asynchronous return from the user's routine) has somehow got a reference to the original registers
}

#if __cpp_concepts >= 201507
template <NonReference TInput, NonReference TResult>
	requires CopyConstructible<TInput> && CopyConstructible<TResult>
#else
template <class TInput, class TResult>
#endif
void Caller<TInput, TResult>::secondLevel(TInput* pArg, WholeState<TResult>* pWholeState) noexcept {
	std::shared_ptr<Task<TResult>> aspMainTask = std::atomic_load(&pWholeState->mTask); // noexcept; this effectively copies a shared_ptr (increments the use_count)
	// "To avoid data races, once a shared pointer is passed to any of these functions, it cannot be accessed non-atomically. In particular, you cannot dereference such a shared_ptr without first atomically loading it into another shared_ptr object, and then dereferencing through the second object."
	// NOTE: in our case it could be avoided by creating a separate shared_ptr for each thread but isn't it fun? :)
	try {
		TResult result(mRoutine(*this, std::move(*pArg))); // if this throws it will be inside user's coroutine (either in user code or upon return - thanks to the copy elision). In that case we want to clean up and "rethrow" from the wait() or the await() (but not the caller() because we want uniform behaviour independently of whether the coroutine managed to return asynchronously or not)
		// if the above mRoutine has returned asynchronously (we're not on the "main" thread) then the caller no longer exists - invalid "this" pointer and no access to the member variables
		try {
			aspMainTask->setResult(std::move(result)); // on the other hand if this throws we need to propagate it above our noexcept barrier
		} catch (const std::exception& ex) {
			pWholeState->isCaught = true;
			copyNestedExceptionInfo(pWholeState->caughtException, ex, sizeof(WholeState<TResult>::caughtException));
		}
	} catch (const std::exception& ex) {
		try {
			aspMainTask->setException(ex); // same here if this throws we need to propagate it above our noexcept barrier
			// if the mainTask is awaited then this will causes the await() to throw (on this very thread)
			// if the mainTask is wait()-ed then this causes the wait() to throw (on another thread)
			// if the coroutine went synchronously then this causes the caller() to throw
		} catch (const std::exception& ex) {
			pWholeState->isCaught = true;
			copyNestedExceptionInfo(pWholeState->caughtException, ex, sizeof(WholeState<TResult>::caughtException));
		}
	}

	pWholeState->mTaskAwaiter = nullptr; // function as a flag signaling there's no task left
}

#if __cpp_concepts >= 201507
template <NonReference TInput, NonReference TResult>
	requires CopyConstructible<TInput> && CopyConstructible<TResult>
#else
template <class TInput, class TResult>
#endif
template <typename TInterResult>
TInterResult Caller<TInput, TResult>::await(Task<TInterResult> &rTask) {
	TaskAwaiter<TInterResult> *pAwaiter = rTask.getAwaiter();
	mWholeState->mTaskAwaiter = pAwaiter;
	if (pAwaiter->isCompleted()) {
		if (char const* what = mWholeState->mTaskAwaiter->hasErrors())
			throw Coroutine_error(what);
		return pAwaiter->getResult();
	}

	mWholeState->mTaskAwaiterCallback = std::make_unique<AwaiterCallbackUnsink<TInterResult, TResult>>(mWholeState, pAwaiter);
	sink_asm(mWholeState.get()); // noexcept
	// we're here only because the unsink_asm() has returned as the above sink_asm()

	if (char const* what = mWholeState->mTaskAwaiter->hasErrors())
		throw Coroutine_error(what);

	return *static_cast<TInterResult const*>(mWholeState->mResolvedValue);
}

template<typename TResult>
void unsink(WholeState<TResult>& rWholeState, void const* pValue) {
	rWholeState.mResolvedValue = pValue; // possibly nullptr
	bool fromSink = unsink_asm(&rWholeState);

	// we're here because the above unsink_asm saved its return address in the StackState.sink_returnAddress and:
	// a. user's routine has ended and the cleanup_asm() has returned as the above unsink_asm()
	// b. consecutive calls to the sink() has returned as it
	if (fromSink) { // means no exceptions were intercepted
		rWholeState.mTaskAwaiter->onCompleted(std::move(rWholeState.mTaskAwaiterCallback));
		// if the callback will be executed right away and it will end with errors it will be like the coroutine has ended. We will have two possibilities:
		// 1. the continuation of the coroutine threw - it doesn't propagate, we don't have to worry
		// 2. or/and the setResult or the setException threw and we want it to propagate (the unrecoverable error):
	}

	if (rWholeState.isCaught)
		throw std::runtime_error(rWholeState.caughtException);
}

template <class TTask, class TResult>
AwaiterCallbackUnsink<TTask, TResult>::AwaiterCallbackUnsink(std::shared_ptr<WholeState<TResult>> spWholeState, TaskAwaiter<TTask>* pAwaiter) : mWholeState(spWholeState), mAwaiter(pAwaiter) {}

template <class TTask, class TResult>
void AwaiterCallbackUnsink<TTask, TResult>::operator()(void const* pResult) {
	unsink(*mWholeState.get(), pResult); // if this throws we consider it as an unrecoverable error
}

template <class TTask, class TResult>
void AwaiterCallbackUnsink<TTask, TResult>::operator()() { // version for the "right away" execution
	unsink(*mWholeState.get(), mAwaiter->getResultPointer()); // continuation
}

template<class TPrevTask, class TResult>
AwaiterCallbackContinueWith<TPrevTask, TResult>::AwaiterCallbackContinueWith(TPrevTask& prevTask, std::shared_ptr<Task<TResult>> nextTask, std::function<TResult(TPrevTask&)> func) : mPrevTask(prevTask), mNextTask(nextTask), mFunc(func) {}

template<class TPrevTask, class TResult>
void AwaiterCallbackContinueWith<TPrevTask, TResult>::operator()() {
#if __cpp_lib_optional >= 201603
	std::optional<TResult> result;
	try {
		result = mFunc(mPrevTask);
	} catch (std::exception& ex) {
		mNextTask->setException(ex);
		return;
	}
	mNextTask->setResult(result.value());
#else
	alignas(TResult) char result[sizeof(TResult)]; // we cannot write TResult result; because it could not have the default ctor and...
	try {
		new (result) TResult(mFunc(mPrevTask)); // we need to distinguish if an exception was thrown by the continueWith callback or by the Task::setResult(). In the later case we want to let the exception freely propagate
	} catch (std::exception& ex) {
		mNextTask->setException(ex);
		return;
	}
	mNextTask->setResult(*reinterpret_cast<TResult*>(result));
	reinterpret_cast<TResult*>(result)->~TResult(); // standard: The notation for explicit call of a destructor can be used for any scalar type name. Allowing this makes it possible to write code without having to know if a destructor exists for a given type.
#endif
}
}
#endif
