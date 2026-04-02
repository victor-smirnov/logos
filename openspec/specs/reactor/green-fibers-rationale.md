**TL;DR**: Without fibers, there's no standard method to transition legacy code to a high-performance IO environment. This article delves into how a subset of coroutines can be optimized to match the speed of synchronous code. If you're already familiar with the debates surrounding stackless coroutines, you can skip the next two sections and jump directly to the [tests](#lets-bring-in-some-numbers).

# A Brief Introduction (Well, Sort of)

This isn't a deep dive into the inner workings of C++20 coroutines. For that, I'd recommend this [comprehensive overview](https://lewissbaker.github.io/). Neither is it a guide to how fibers operate internally. For insights on fibers, consider referring to the [Boost Fibers](https://www.boost.org/doc/libs/1_83_0/libs/fiber/doc/html/index.html) library or the [P0876R13](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p0876r13.pdf) proposal for the forthcoming C++ standard.

In a nutshell, traditional thread-based concurrency wasn't up to par for today's fast-paced networking needs. Threads consumed excessive memory for stacks and necessitated expensive context switches, sometimes for each packet. With 10G Ethernet potentially handling up to 14M packets/s, the OS kernel often became the choke point. This hurdle was overcome with advanced event-based interfaces like [Epoll](https://en.wikipedia.org/wiki/Epoll), [kqueue](https://en.wikipedia.org/wiki/Kqueue), and [IOCP](https://en.wikipedia.org/wiki/Input/output_completion_port), so currently, this segment of the IO stack isn't much of a concern (give or take some implementation specifics).

The real challenge now lies with the programmers. Two decades in, and we're still grappling with some unresolved aspects. This isn't just a C++ issue, which is admittedly a decade or so behind contemporary languages and platforms. Several other languages face similar dilemmas, with only a handful being exceptions. Allow me to elaborate on why that is.

To work with event-based IO interfaces, we employ the [event loop](https://en.wikipedia.org/wiki/Event_loop) design pattern. The essence of this approach is to retrieve data from the OS kernel in *batches of events*, thereby minimizing costly context switches and system calls. An in-memory dataflow graph represents operations, and each event directly corresponds to a specific operation within this graph. When an event is received, its associated operation is initiated for processing, providing a *straightforward* and *efficient* computational framework.

The challenge, however, arises when working with [dataflow](https://en.wikipedia.org/wiki/Dataflow)-style computational graphs in [control flow](https://en.wikipedia.org/wiki/Control_flow)-based languages (the majority of programming languages in use fall into this category). It's simply *difficult*. Early attempts to harness the event loop led to the notorious [Callback hell](http://callbackhell.com/).

The second generation of event-driven APIs utilized [Continuation Passing Style](https://docs.seastar.io/master/index.html) (CPS), resulting in more comprehensible code. However, in CPS, we abandon [stack-based memory allocation](https://en.wikipedia.org/wiki/Stack-based_memory_allocation), a significant aspect of the C++ programming paradigm. Additionally, CPS code remains challenging to debug interactively, especially if the debugger isn't CPS-aware—and most aren't. Yet, CPS offers a reasonably comprehensible codebase and efficient *concurrent code*, making it a current baseline for concurrent programming.

The third wave of APIs aims to enhance the understandability of dataflow-based code by overlaying a thread (of execution)-like *perspective*[1]. Currently, two main *implementations* of this perspective are in contention: [asynchronous functions](https://en.wikipedia.org/wiki/Async/await) and [fibers or green threads](https://en.wikipedia.org/wiki/Green_thread).

[1] It's worth noting that not all dataflow-style code can be aptly represented using control flow-like structures, making generic CPS still relevant.

From a programmer's standpoint, fibers are more intuitive. They resemble threads but aren't preempted by the OS. Preemption is either executed explicitly by the application or implicitly by the runtime. The OS remains uninvolved, eliminating costly context switches. Crucially, each fiber sustains a hardware stack compatible with both the OS and foreign function calls.

Asynchronous functions are more nuanced. While asynchronous code closely mirrors its synchronous counterpart and shares operational semantics, function frames get allocated on the *heap* instead of the *stack*, as is the case with fibers. Why this complexity? There are several reasons...

It's become evident that implementing and supporting stack switching, essential for fibers, *in C++*, poses significant challenges. A summary of these challenges can be found in the rationale for the stack-less C++ coroutines proposal: [Fibers under the magnifying glass](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1364r0.pdf). At its core, switching OS stacks in C++ is widely considered *undefined behavior* (UB) because compilers and software might be oblivious to it. Granted, both Boost Fibers and the Seastar framework offer high-performance fibers that run flawlessly, and the majority of software experiences *no* compatibility hitches — at least on Linux. However, results may vary on other platforms. C++ aims to accommodate not just Linux but a myriad of OSes, encompassing numerous proprietary and embedded systems. Hence, building a foundational feature like concurrent programming on a tenuously supported primitive could be *reckless at best* (leading to inherently flawed software).

It must be mentioned that critique of fibers has been addressed in [P0866](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0866r0.pdf). Basically, fibers in the language standard will be motivating software and platform developers to make code more compatible with the feature. Otherwise, at best, there will be many competing solutions, like, Seastar fibers, Boost Fibers and userver coroutines, to name the least.

A frequently cited downside of fibers is their memory consumption. Hardware stacks must align with page sizes, and adjusting their size dynamically is tricky. In contrast, languages devoid of stack allocation, like Golang, can expand stacks through reallocation and copying — a feat unattainable for C and C++. Consequently, stacks must maintain a consistent size[2], and if too diminutive, might again spawn software compatibility issues. Furthermore, hardware-protected stacks aren't swiftly allocated. Each allocation (and subsequent deallocation) mandates at least one system call — to establish or negate the protective page guarding against stack overflows (for memory safety).

[2] One can circumvent this constraint using the [split-stack](https://gcc.gnu.org/wiki/SplitStacks) feature, where stack space is apportioned in fixed-size segments rather than one expansive contiguous segment. However, for split-stacks to function effectively, *all associated code* needs to recognize it (for instance, it should be compiled in split-stack mode). This is feasible solely when one has comprehensive control over all source materials.

On the other hand, asynchronous functions sidestep these complications. They eschew stack switching since they don't utilize stacks, and an asynchronous function could have a memory footprint as minimal as a solitary function call's frame size. Under specific conditions, one might even be able to dispense with dynamic memory allocation. Thus, these functions *appear to be* a more promising *basis* for concurrent and dataflow programming compared to fibers. Yet, there exists a notable concern, largely glossed over, which constitutes *the primary motive* behind penning this article (along with the accompanying code).

# Function Coloring Problem

Asynchronous functions offer a lot of benefits, but they also come with a notable challenge: [function coloring](https://journal.stuffwithstuff.com/2015/02/01/what-color-is-your-function/). This issue can make them tricky to use in real-world projects. Think of asynchronous functions as stack-less coroutines, meaning _only another coroutine can call a coroutine_.

For clarity, let's label regular functions as "red" and coroutines as "green". Under this scheme, green functions can call both red and green ones, while red functions can only call other red functions. This can get even more intricate with more "colors" and rules. Here's the catch: once you invoke a green function, the entire code pathway right up to the main() also becomes "green".

This occurs because a coroutine, though it appears like a regular function, isn't quite the same. The compiler transforms coroutines into callbacks in a dataflow graph that's managed by the _event loop_. Therefore, you can't simply _wait_ for a coroutine to finish outside of the event loop[3].

[3] Well, it's not _always_ the case. With C++23 [generators](https://en.cppreference.com/w/cpp/coroutine/generator), you can call coroutines from regular code. the exception exists because this coroutine type (generator) conceals its own event loop at the call site. Thus, while using co_return, co_await, and co_yield is restricted to coroutines, regular "red" code can technically use coroutines if it handles the associated suspend/resume mechanism correctly.

So, how much does function coloring really impact practice?

Imagine we're on board with the idea that "fibers are fundamentally flawed". In this scenario, _peak performance_ IO can only be achieved via CPS and coroutines, and both can be interchanged since they're compatible. Therefore, if:

* your code touches any IO, even indirectly,
* or simply runs a tad too long (like over 100 microseconds),

it _has to be a coroutine_ or _continuation_. This can lead to a situation where almost every code turns into a coroutine. This transformation poses two main challenges:

1. There are vast amounts of existing software that would need to transition into coroutines.
2. Coroutines aren't free; there's a memory allocation cost at every invocation point.

The second point hits hard for C++ users as memory allocation is intrinsically linked to prolonged latencies. While languages with GC don't feel the pinch (given that GC is a source of high latencies anyway), [C++ certainly does](https://isocpp.org/files/papers/P2300R7.html#intro-prior-art-coroutines).

Regarding the first challenge, it's unrealistic to expect all, or even a significant portion of, legacy code to transition to coroutines. We need efficient methods to run legacy (synchronous) code in an event-driven IO environment. Using threads, as often suggested, isn't a one-size-fits-all solution. Fibers allow for easy data sharing between asynchronous and synchronous code, which explains the strong [defense of fibers](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0866r0.pdf) in C++ and their eventual [inclusion in the standard](https://medium.com/yandex/c-23-is-finalized-here-comes-c-26-1677a9cee5b2).

Lastly, it's worth mentioning that the justification for stackless coroutines in C++ isn't entirely [clear-cut](https://kavon.farvard.in/papers/pldi20-stacks.pdf). While many examples highlight their utility in _networking scenarios_ such as streaming data and web frameworks, it's essential to remember that these are relatively straightforward computational environments. High-speed data processing doesn't always require complex computations. However, as storage and database applications evolve to tackle both IO- and compute-intensive challenges, there's a need for sophisticated concurrency primitives to keep modern C++ competitive.

# Let's Bring in Some Numbers

Ok, CPS has been proven being rather effective for high performance _networking_ applications, both in terms of performance and in terms of code comprehensibility. Curious reader may take a look at [Seastar framework](https://seastar.io), as an example. The same framework provides efficient interfaces for files and block devices. But the problem is that designing and implementing abstraction typical for complex databases may impose unnecessary/prohibitive complexities. C++20 coroutines to the rescue and Seastar does support them out of the box, allowing to mix CPS and coroutines. 

The question is how efficient it will be given that stackless coroutines allocate their frames on the heap. Stackless coroutines are viral, so we will be allocating not only at the point of concurrency, but all the way up to the beginning of the thread. Basically, how efficient can be _tree traversal_ if it's implemented using CPS/coroutines/fibers, comparing to the native C-stack based implementation. For example, if a [recursive descend parser](https://www.antlr.org) is implemented with coroutines how slow it may be relative to the baseline? 

I will not be implementing and evaluating a whole parser, just plain tree traversal should be enough to spot the problem if it exists. So the basic traversal algorithm is pretty simple:

```c++
struct TreeWalker {  
  uint64_t counter_max_;
  uint64_t counter_{};

  void dig_into_natv(size_t depth = 0) {
    if (counter_ < counter_max_) {
      if (depth < 64) {
        counter_++;
        if (counter_ < counter_max_) {
          dig_into_natv(depth + 1);
          dig_into_natv(depth + 1);
        }
      }
    }
  }
};
```
Basically we are diving down the tree in a depth-first order, up to the depth of 64 and until we reach the number of `counter_max_` nodes. For all tests below `counter_max_ = 2^26` or about 67M nodes.

Below there is the list of five results, correspondingly for the cases when tree traversal is implemented with Seastar futures (CPS), coroutines, fibers (2 variants) and raw tree traversal without concurrency ([source version](https://github.com/victor-smirnov/green-coro/blob/3178615d02ddbe71b515f19665c2b60cdb92578b/seastar-bench.cpp)):

```
(1) Futures:      255 ms, 263.172M iters/sec
(2) Coroutines:   1972 ms, 34.03M iters/sec
(3) Fibers (opt): 211 ms, 318.051M iters/sec
(4) Fibers (raw): 4136 ms, 16.225M iters/sec
(5) Raw calls:    30 ms, 2236.96M iters/sec
```

## (5) Raw calls: Baseline

The baseline, raw tree traversal using native stack is number (5), we have 2.2G iterations per second. For the reference, the system is Ryzen 9 5950X, Ubuntu Linux 22.04 Gcc 13.1/libstdc++. Seastar's version is in the repository.

## (1) Futures

Below is the code sample for tree traversal using Seastar futures:

```c++
seastar::future<void> dig_into_fut(size_t depth = 0) {
  if (counter_ < counter_max_) {
    if (depth < 64) {
      counter_++;
      if (counter_ < counter_max_) {
        return dig_into_fut(depth + 1).then([this, depth]{
          return dig_into_fut(depth + 1);
        });
      }
      else {
        return seastar::make_ready_future();
      }
    }
  }
  return seastar::make_ready_future();
}
```
It shows 263M iterations per second, that is about 9 times less than baseline. I think that it's pretty good result for CPS program, given that it technically execute much more CPU instructions. Anyway, that is more than enough to handle 14M packets per second from 10G Ethernet adapter.

Nevertheless, there is a hidden catch here. First, Seastar future does not always preempt into scheduler. Normally, only 1 of 256 calls go into scheduler, and this ration is dynamic. So under the load real performance may be different. See case (4) below for the worst case estimations.

Second catch is lambda function. In this specific example the state captured by lambdas is pretty small, so std::function<> holding the continuation does not allocate. In real life this may no necessary be the case.

## (2) C++20 Coroutines

Below is the variant with coroutines. Seastar's support for coroutines is excellent. We can just `co_await` a future.

```c++
seastar::future<void> dig_into_coro(size_t depth = 0) {
  if (counter_ < counter_max_) {
    if (depth < 64) {
      counter_++;
      if (counter_ < counter_max_) {
        co_await dig_into_coro(depth + 1);
        co_await dig_into_coro(depth + 1);
      }
    }
  }
}
```
For coroutines we have only 34M iterations per second, that is almost 8 times smaller than for futures. This is a coroutine's overhead, consisting from dynamic memory allocation and some additional code for suspend/resume logic. But in raw numbers it's a pretty good result, just not necessary for a recursive descend parser. 

What is important specifically for Seastar, it's coroutines are very well-integrated with CPS code. It may be possible to use coroutines in cold paths, switching to raw CPS in hot paths. Unfortunately, it's not that easy for a recursive descend parser, again.

There is a hidden catch here too. Coroutine overhead heavily depends on the memory allocator's performance. In this specific benchmark only coroutines are allocating on the heap, so the heap is not that fragmented as it may be in real application. Given that futures may also allocate memory for the continuation's captured state, at the worst, allocator-bound, case futures may perform +/- on par with coroutines. So, YMMV.

## (3) Fibers with optimized yielding

```c++
void dig_into_fiber1(size_t depth = 0) {
  if (counter_ < counter_max_) {
    if (depth < 64) {
      counter_++;
      if (counter_ < counter_max_) {
        dig_into_fiber1(depth + 1);
        dig_into_fiber1(depth + 1);
      }
    }
  }

  seastar::thread::maybe_yield();
}
```

Here we are doing the entire traversal in a Seastar's `thread`, that is a cooperatively scheduled fiber, and try yielding on each node visit. Much like for futures, this code will be yielding only 1/256th of the time. And it's speed is pretty close to the case (1) of futures: 318M iterations per second. What we can say here is that the cost of handling a continuation (lambda function) is close to zero in _this case_, and the most of the extra time (comparing to the baseline) is spent in the scheduler. 

## (4) Fibers (raw), yielding on each node visit

```
void dig_into_fiber1(size_t depth = 0) {
  if (counter_ < counter_max_) {
    if (depth < 64) {
      counter_++;
      if (counter_ < counter_max_) {
        dig_into_fiber1(depth + 1);
        dig_into_fiber1(depth + 1);
      }
    }
  }

  seastar::thread::yield();
}
```

The same code, except we do yield each time we are visiting a node, and the speed is 'only' 16M iterations per second. Yielding a fiber is costly, and the cost consists from storing CPU registers and running the scheduler. _For high-performance networking code switching a fiber may be a bottleneck_.

Again, Seastar's fibers are well-integrated with futures, so we can use the latter for the hot IO paths, using the former for everything else.

Overall, Seastart's design is very well-balanced providing feasible _incremental_ path to migrate existing code into high performance IO environment.

# A Few More Numbers

For comparison, the same test but running on top of the Boost Fibers library: 

```
(6) Boost Fibers (raw): 7342 ms, 9.14M iters/sec
(7) Boost Fibers (opt): 230 ms, 291.777M iters/sec
```

When yielding on each node visit (6), Boost shows 'only' 9M iterations per second, that is relatively slow but the same order of magnitude with Seastar's number of 16M iterations per second. Given that register storing code is, more or less, the same in both cases, Boost's scheduler seems much less optimized. Note that this specific build of Boost Fibers 1.83.0 has atomics in the event loop.

If fiber is yielding only 1/256th of the time, the speed is 291M iterations per second that is on par with Seastar's fibers. So, +/- optimizations, both implementations are basically the same in terms of performance _in this microbenchmark_.

# Meet Green Fibers!

In this microbenchmark, CPS shines, whereas fibers falter when yielding on every iteration. However, the impact can be reduced with regilar IO buffering. Coroutines produce middling results due to dynamic memory allocation. It's important to point out that in real-world applications, if continuations allocate dynamic memory for captured data, their performance could be on par with coroutines. A notable aspect of coroutines in this microbenchmark is that even though the code appears as typical function calls allocating frames on the stack in FIFO order, the actual allocation might not follow this pattern. This discrepancy arises due to compiler optimizations and other underlying intricacies, particularly when combining CPS with coroutines.

However, the idea os using FIFO allocator (a "stack") is promising. The primary limitation of fibers is their requirement for an OS stack, which ensures legacy code runs without modification. The need to save/restore registers during stack switches also poses a challenge since non-C/C++ native code might rely on this, impacting performance. What if we introduced a new kind of fiber, termed a "green fiber" operating on a separate stack that the C++ compiler is _completely aware of_. Specially attributed "green" C++ code will be run on a green fiber, circumventing compatibility issues intinsic to reguular fibers. As it's currently works for coroutines, the compiler would delegate green fiber's stack management to the application. This stack could be segmented and use a flexible (dynamic) allocation policy, as discussed in [this paper](https://kavon.farvard.in/papers/pldi20-stacks.pdf). The initial size of the fiber stack could be minimal, equating the creation cost to that of a coroutine. The point is that all the subsequent green code calls will amortize allocations of the stack's segments. Actual performance of such code may vary, but is generally expected to be much more predictable than performance of stackless coroutines.

It's worth noting that green fibers are _not_ exempt from the function coloring problem. This means a "green" function cannot be invoked by a "red" one. However, apart from this distinction, both functions appear identical and maintain _the same semantics_, a contrast to coroutines. We could employ C++ attributes to signal the compiler to generate specific code, allowing these functions to run on a different stack:

```c++

[[green]] 
// will be called on the current green fiber's segmented stack
int ordinary_function(int a, int b) { 
  return a + b;
}

[[green]] 
// green code can be compiled separately
int ordinary_function(int a, int b);

// All member functions of this class are green
class [[green]] IOEngine {
};

namespace [[green]] io_enabled {
  // green by default
  void f0();

  // green by default
  class C1 {};

  [[red]] // will be called on a C-stack of current fiber/thread
  void f2();
}

[[green]]
void f0() {}

void f1() {}

void f2() {
  f1(); // OK, red function called red one
  f0(); // ERROR, red function trying to call green one
}

[[green]]
void f3() {
  f0(); // OK, will be called on the green fiber's stack;
  f1(); // OK, will be called on the current red fiber's stack or thread's main stack.
}

[[green_spawn_fn]]
// CPS-enabled, special function for spawning a new green fiber
template <typename Fn, typename R = return_type_of<Fn>>
future<R> green_async(Fn&& fn);

future<> f4();

// CPS
future<> f5() {
  // Should be as lightweight as a coroutine
  return green_async(f3).then(f4);
}


// Red function running in a normal fiber
void f6() {
  green_async(f3).get(); // Suspending current normal fiber until f3() is done.
}
```

So, if some code is regular fiber-ready (compatible with), porting it to the new environment will be as easy as applying an attribute or enabling a compiler switch. Coroutines are much more expensive and intrusive in this respect. Moreover, semantics of such program _may be_ different after coroutinization.

As a bonus, green fibers, unlike regular ones, technically can be resumed safely on a different thread because compiler known that the code is green and should be compiled in the corresponding way (disabling unsafe optimizations). This is good for some important use cases like web applications where fibers intrinsically do not share mutable data.

# Conclusions

So far, what we have. 

1. CPS matters because it enable generic high-performance dataflow programming in C++. It doesn't look ugly but is hard for debugging. 

2. C++20 Coroutines make CPS much more comprehensible, but at the expense of dynamic memory allocation at runtime.

3. Coroutines have function coloring problem, so legacy synchronous code is pretty hard to port into CPS-enabled high performance IO environments.

4. Fibers have compatibility issues on some platforms, but they are currently the best way to port existing code into new environment, they are pretty fast in terms of stack switching but not that fast in terms of creation time (kernel calls for stack allocation and protection). Without fibers in the C++ standard, _there is no standard way_ to port existing code into new environment. 

5. Green fibers are as lightweight as coroutines because initial stack size can be rather small, and may have basically the same sequential code performance as fibers and threads. 

6. They have function coloring problem but code migration is _declarative_, because its semantic is the same. 

7. Eventually, normal ("red") fibers will be needed only to run legacy code that can't be recompiled for green fibers and, for whatever reasons, can't be run under threads.

8. Given all of that, the future of high performance mixed IO-/compute-intensive code looks "green" :)

# Prototyping

That should be easy. Clang already has support for split stack mode, it should be possible to saddle on this feature by enabling it for the specially attributed code. The only thing we have to change is redirecting allocation of segments to the application instead of libgcc. Other interface of green fiber may be fully compatible with [P0876R13](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p0876r13.pdf) (except for the memory allocation part).

# Feedback

Feel free to leave your comments on the following [issue](https://github.com/victor-smirnov/green-fibers/issues/1).

