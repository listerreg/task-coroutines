# Task based coroutines
C++ coroutines mimicking the C# `async` `await` facilities and the .NET `Task` class. Inspired by the [boost::coroutines2::coroutine<T>::pull_type](https://www.boost.org/doc/libs/1_70_0/libs/coroutine2/doc/html/coroutine2/coroutine/asymmetric/pull_coro.html) and the [.NET awaiter pattern](https://devblogs.microsoft.com/pfxteam/await-anything/).

## Introduction
Idea behind this project was to find the simplest way to mimic coroutines in C++ with clean separation between C++ and assembly code. Another goal was to introduce notion of the asynchronous programming style present in C#, the language where the concept of coroutines is embedded into the language itself.

As the result we have the analogs to the C# `await` keyword and the `Task` class as a return value from an _async_ function.

### The construct

```c++
// Definition of the desired async function (coroutine)
ArbitraryResultType usefulCoroutine(Caller<ArbitraryArgumentType, ArbitraryResultType> caller, ArbitraryArgumentType arg)
{
/* implementation */
}

// Way to call it
int main() {
	Caller<ArbitraryArgumentType, ArbitraryResultType> caller{usefulCoroutine};

	ArbitraryArgumentType arg1; // prepare some argument for the coroutine
	std::shared_ptr<Task<ArbitraryResultType>> task1 = caller(arg1);

	// ...
}
```

### The coroutine

```c++
int usefulCoroutine(Caller<std::string, bool> caller, std::string arg) // the coroutine taking std::string and returning int (both are arbitrary although with some constraints, see later)
{
	// Call some asynchronous I/O function implemented usually as part of other "lower-level" framework
	std::shared_ptr<Task<std::string>> dbQueryTask = DataBase.queryAsync(arg); // execution doesn't stop here

	// do some other things that do not depend on the database response
	// ...
	// when it's time to consume the response "await" it

	std::string dbResponse = caller.await(dbQueryTask); // now the execution will be interrupted (assuming the database did not return an already resolved task) and the coroutine will return (via sorrounding call to the Caller) an unresolved task of the Task<int> type
	
	// the execution will resume here when the database will finish processing the request and will resolve the task
	// now consume the database response:
	std::cout << "Database responded: " << dbResponse << std::endl;

	return 0; // this is up to us what we want to return from our coroutine (though it has to return something, see later)
}
```
> **Note:** the `await` here is the Boost `sink` counterpart.

With this asynchronous flow we are dealing with two unresolved tasks:

 1. First one is the task returned from the `queryAsync()` function. This task will be resolved in a way forged by the authors of the framework consisting of the `queryAsync()`,

 2. Second one is the task returned by the invocation of our interrupted `coroutine` (see [Invoking a coroutine](#invoking-a-coroutine) later on). It will be resolved automatically when the `coroutine` will eventually get to the end. The result it will hold will be the one returned by the coroutine.

The important part here is that the task from the `queryAsync()` will be resolved on a different thread (thread from a thread pool, thread waiting on the `recv()` or any other thread the framework will use for this purpose). The importance of this stems from the fact that this thread will be the one on which the continuation of our coroutine will be executed. On our thread we'll be left only with the task.

### The task

#### An unresolved task
Having an unresolved task it's possible to:

 1. Wait on it. This will block the execution until the task is completed,

	```c++
	// ...

	task1->wait();
	```

 2. Provide it with a callback which will be executed when the task will complete (this will result in another task),

	```c++
	auto task2 = task1->continueWith(/* callback */);
	```

 3. Doing nothing. The task will do its job and will be destroyed,
 4. `await()`-ing the task. Although this can be done only from inside another coroutine.

_Waiting_ and _setting a callback_ can be done together in any order. Although if we'll first `wait()` for the task and then set the callback the call to the `continueWith()` will result in immediate execution of the callback on the calling thread.

The callback to pass to the continueWith has the type:

```c++
std::function<AnyType(Task<ArbitraryResultType>&)>
```

where the `ArbitraryResultType` is the "_inner_" type of the task being provided with the callback.

```c++
auto task2 = task1->continueWith<AnyType>([](Task<ArbitraryResultType>& prevTask){
	std::cout << "what a wonderful result: " << prevTask.getResult() << std::endl;
	return AnyType;
	});
```

Now having another task we can do any of the above with it.

#### Getting the result of a resolved task

When a task will get resolved, that is the call to `wait()` will return or we're inside the `continueWith` callback, it is legal to call the `getResult()` on it.

```c++
// ...
task1->wait();

ArbitraryResultType result = task1->getResult();
```

> **NOTE:** calling the `getResult()` on an unresolved task will result with an exception.

### Constraints on the coroutine signature
A coroutine is just a plain function except for the following restrictions:
 1. An argument passed to a coroutine cannot be of the _reference type_,
 2. At the moment a coroutine has to take and return something.

## Invoking a coroutine
In order to invoke a coroutine a `Caller` object is needed. Its constructor takes a function pointer to the coroutine.

```c++
Caller<ArbitraryArgumentType, ArbitraryResultType> caller{usefulCoroutine};
```

The actual invocation will occur upon calling the `Caller` object. It has the _function call operator_ overloaded which accepts an argument for the coroutine.

```c++
std::shared_ptr<Task<ArbitraryResultType>> task1 = caller(arg1);
```

By definition this call doesn't block and after it the caller object can be safely destroyed thus it ought to be stack-allocated.

_Calling_ the Caller will return with an (typically) unresolved task.

## Exceptions handling

As any function a coroutine can throw. The only requirement is the thrown exception be of or derived from the std::exception type. Rules for propagating exceptions are as follow:

 1. An exception thrown by a coroutine will be "_rethrow_" as the `Coroutine_error` by the `wait()` or the `await()` functions. The `Coroutine_error` type is derived from the `std::runtime_error`,
 2. An exception thrown by the `continueWith` callback will be treated the same way except now this refers to the calls made on the task returned by the `Task::continueWith()` itself,
 3. Other critical exceptions (e.g. due to lack of memory) that will arise in connection with efforts to resume coroutine's interrupted execution or to set a task's result will propagate through the `Task::setResult()` call.

The `Coroutine_error` will hold the message of the original exception. If the original exception was the `std::nested_exception` then the message will be a compound of every message in the chain.

### Exception safety guarantees

If the only exception comes from a coroutine then the coroutines mechanism wraps it in the `Coroutine_error` exception type and doesn't void the coroutine's exception safety guarantees.

The thing to remember here is that invoking and catching an exception from a coroutine is split between two calls: the `Caller::operator()` and the `Task::wait()` or alternatively the `Caller::operator()` and the `Caller::await(Task&)`. So after catching an exception of the type `Coroutine_error` from the wait/await call the way to rerun the coroutine is to call the `Caller::operator()` again and not the wait/await function.

#### wait()

The `Task::wait()` doesn't throw on its own. The only exceptions thrown by it will come from inside the coroutine.

#### await()

The `Caller::await(Task&)` can face a critical error (different than the `Coroutine_error`) in two cases: before the task is resolved and after. In both of them it is safe to call the `Caller::await(Task&)` again although it is not equal to strong exception guarantee since the task can be completed in the process.

#### setResult()

`Task::setResult(T)` has no exception safety. It only throws when a critical error occurred meaning that there was no way to continue the coroutine or the sub-coroutine (a coroutine called from inside another coroutine) or properly notify thread waiting for the task to complete. The coroutine mechanism doesn't implement a state machine of any sort to keep a track of the successfully completed frames of the coroutines hierarchy and thus provide no means to pick up failed execution.

## Writing asynchronous methods
To be able to call any asynchronous method some kind of framework providing them is needed. In C# these are implemented in the .NET framework.

This repository contains a greatly simplified [example](examples/) how a such framework could look like. It consists of the static library that fakes the mechanics of the Windows [completion port](https://docs.microsoft.com/en-us/windows/desktop/fileio/i-o-completion-ports). In the [main.cpp](examples/main.cpp) file is exemplary use of this framework.

Writing a framework like this would ultimately come down to setting up a thread waiting in a loop on whatever "_channel_" we are interested in (`epoll`, message queue, etc.) and make it either call `Task::setResult()` on a task associated somehow with the received data (and thus run coroutine's continuation) or dispatch this job to other thread (possibly via a thread pool).

If a task has a callback responsible for resuming interrupted execution set up via the `task->getAwaiter()->onCompleted()` then the `Task::setResult()` will internally call this callback. The coroutine mechanism sets this callback on an unresolved task when the task is "_awaited_" (calling the `Caller::await(Task)`).

If a task has no callback set (e.g. the task returned from a coroutine outside of any other coroutine) then `Task::setResult()` will only pass a result to this task and wake any thread waiting on the `Task::wait()`.

## Portability
This project should work on any x86-64 architecture with the POSIX-compliant
system which uses the ELF file format.

## Compiling
To compile this project a compiler with the C++14 support is needed. The included makefile will use g++ or clang++ whichever is available. You can override this by setting the CXX to the desired compiler at the command line.

```bash
$ make CXX=/usr/bin/g++-7
```

The simplest way to play with this project is to navigate to the [examples/](examples/) directory, fiddle with the [main.cpp](examples/main.cpp) file and type `make` in the terminal.

```bash
$ git clone TODO address
$ cd coroutines/examples
$ vi main.cpp
$ make
```

This will produce an executable called `sample` in the current directory. It will have `rpath` set to `$ORIGIN/../bin`.

To compile only the _coroutines_ shared library (without the _completion port_ fake-framework) type `make` in the root directory of the project.

```bash
$ git clone TODO address
$ cd coroutines
$ make
```

The library file `libtaskcoroutines.so` will be placed in the bin folder of the project.

## Licensing
This project is licensed under the terms of the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl.html).
