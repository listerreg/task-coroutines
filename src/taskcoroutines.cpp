#include "common.h"
#include <memory>
#include <cstring>

namespace aw_coroutines {

void copyNestedExceptionInfo(char* dst, const std::exception& exc, size_t size, bool first) {
	if (!first) {
		if (size >= 5) {
			sprintf(dst, " <-- ");
			size -= 5;
			dst += 5;
		} else {
			dst[0] = '\0';
			return;
		}
	}

	std::strncpy(dst, exc.what(), size-1);

	size_t length = strlen(exc.what());
	size_t nullPosition;
	if (length < size - 1)
		nullPosition = length;
	else {
		dst[size-1] = '\0';
		return;
	}

	try {
		std::rethrow_if_nested(exc);
		dst[nullPosition] = '\0';
	} catch (const std::exception& e) {
		copyNestedExceptionInfo(dst+length, e, size - length, false);
	}
}

Coroutine_error::Coroutine_error(const std::string& what_arg) : runtime_error(what_arg) {}
Coroutine_error::Coroutine_error(const char* what_arg) : runtime_error(what_arg) {}

bool TaskAwaiterBase::isCompleted() {
	mMtx.lock();
	bool tmp = mCompleted;
	mMtx.unlock();
	return tmp;
}

void TaskAwaiterBase::onCompleted(std::unique_ptr<AwaiterCallbackBase> upCallback) {
	mMtx.lock(); // there's nothing that could throw while the lock is held (thus no RAII)
	if (mCompleted) {
		mMtx.unlock();
		(*upCallback)();
	} else {
		mOnCompletedCallback = std::move(upCallback);
		mMtx.unlock();
	}
}

const char* TaskAwaiterBase::hasErrors() const {
	if (mError)
		return mExcWhats;
	else
		return nullptr;
}

void TaskAwaiterBase::setError(const std::exception& ex) {
	mError = true;
	copyNestedExceptionInfo(mExcWhats, ex, sizeof(mExcWhats));
}
}
