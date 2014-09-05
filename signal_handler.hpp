/**
 * 
 *  Copyright 2014 Bruce Ide
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * Signal handler class. This object is designed to make signal management
 * a bit easier. When multiple threads are running (on Linux,) any thread
 * in the process could receive a signal. This can lead to unwanted
 * termination of your program. One method of dealing with this is to
 * block signals you're likely to receive in the parent process prior
 * to starting any threads, and then run a thread that receives and
 * handles these signals.
 *
 * This particular signal handler handles the blocked signals with
 * signalfd. It also makes available a boost::signals2 signal
 * which provides listeners with the signal number and the
 * signalfd_siginfo structure created by the received signal.
 * This boost signal and any listeners that it executes run in
 * the thread of the signal handler when a signal is received.
 * You will need to manage your own synchronization and take care
 * to avoid deadlocks. This can be done reasonably with boost::mutex,
 * if you need it.
 *
 * There are some signals you should be wary of blocking:
 *
 * - SIGSEGV - If you receive one of these your stack is probably
 *   already corrupt. If you're feeling adventurous, you could try
 *   to build a SEGV handler that attempts to create a stack dump
 *   prior to the process dying. That would be be fairly nifty.
 *
 * - SIGBUS - same as sigsegv.
 *
 * - SIGINT - select() and some other functions that build timers
 *   around select() use SIGINT. If you block it, select-based timers
 *   won't work. Which is kind of a bummer, since you might want to
 *   block SIGINT if you want to catch and handle CTRL-C in a
 *   graceful fashion.
 *
 * - SIGUSR2 - This object uses SIGUSR2 to interrupt its blocking
 *   read of the signal file descriptor. You can still use it, but
 *   be aware that when you call handler.shutdown, you're going to
 *   get an extra SIGUSR2. I don't recall ever seeing anyone ever
 *   actually use the USR signals, so I hope this isn't a problem
 *   for anyone.
 *
 * You can not catch, block or ignore SIGKILL or SIGSTOP.
 *
 * This object provides convenience functions to block signals.
 * You can block individual signals. In this case, the signal will
 * be added to the current process signal mask. You can also provide
 * a sigset_t pointer. In this case, the current process signal mask
 * will be replaced by the sigset_t you provide.
 *
 * Basic usage of this object:
 *
 * 1) Create object - fr::signal_handler handler;
 * 2) Block some signals - handler.block(SIGQUIT);
 * 3) (optional) Subscribe a listener to the handler - handler.subscribe(boost::bind(&listener, _1, _2));
 *    Note that you can use any arbitrary handler that you create at any point
 *    in your program to subscribe to the signal handler, and that you may
 *    create a handler anywhere in your program for that express purpose.
 * 4) Install handler (This starts a thread) - handler.install();
 * 5) Shut down handler - handler.shutdown();
 *    Note that you can shutdown and join the handler thread from any
 *    handler object you've created.
 * 6) Rejoin the handler thread - handler.join()
 *
 */

#ifndef _HPP_SIGNAL_HANDLER
#define _HPP_SIGNAL_HANDLER

#include <boost/function.hpp>
#include <boost/ref.hpp>
#include <boost/signals2.hpp>
#include <boost/thread.hpp>
#include <signal.h>
#include <unistd.h>
#include <stdexcept>
#include <sys/signalfd.h>
#include <sys/types.h>

namespace fr {

  /**
   * Private signal handler body. For the most part you don't have a
   * "need to know" about this, but this is how it's implemented if
   * you're curious.
   */

  class signal_handler_body {
    friend class signal_handler;
    bool shut_down;
    bool installed;
    boost::thread thrd;

    boost::signals2::signal<void(int sig, signalfd_siginfo info)> notify;

    // Private constructor that signal_handler can access
    signal_handler_body() : shut_down(false), installed(false)
    {
    }

    // Start a new thread with this signal handler.
    // There will only ever be one of these, so this
    // is safe.
    void install()
    {
      if (!installed) {
	sigset_t usr2handler;
	sigemptyset(&usr2handler);
	sigaddset(&usr2handler, SIGUSR2);
	pthread_sigmask(SIG_BLOCK, &usr2handler, nullptr);
	  
	thrd = boost::thread(boost::ref(*this));
	installed = true;
      } // Should I print or throw if user tries to
      // reinstall an already-installed signal handler?
    }

    void join()
    {
      thrd.join();
    }

    // Shut down and exit this thread
    void shutdown()
    {
      pid_t mypid = getpid();
      shut_down = true;
      if (installed) {
	kill(mypid, SIGUSR2);
	installed = false;
      }
    }

    void process()
    {
      sigset_t current_set;
      // query current sigprocmask (be sure to have this set up
      // prior to running install()
      sigemptyset(&current_set);
      pthread_sigmask(SIG_SETMASK, nullptr, &current_set);
      int fd = signalfd(-1, &current_set, 0);
      if (-1 == fd) {
	// This should never actually happen
	throw std::logic_error("Unable to get file descriptor in signal handler");
      }
      while (!shut_down) {
	signalfd_siginfo info;
	int bytes = read(fd, &info, sizeof(signalfd_siginfo));
	if (bytes != sizeof(signalfd_siginfo)) {
	  // This should also never actually happen
	  throw std::logic_error("Read error in signal handler");
	}
	notify(info.ssi_signo, info);
      }
      // Reset shut_down in case user wants to restart the handler
      shut_down = false;
    }

  public:

    // Functor wrapper for boost::thread
    void operator()()
    {
      process();
    }

  };

  /**
   * Signal handler main class -- this is your interface for the
   * signal handler. Create and set up in your main thread
   * prior to starting any other threads. If your other threads
   * don't inherit the same signal mask, you're gonna have a bad
   * time.
   */

  class signal_handler {
    static signal_handler_body *body;

  public:

    signal_handler()
    {
      if (nullptr == body) {
	body = new signal_handler_body();
      }
    }

    // Subscribe to signal handler notify object. You can subscribe
    // to the same signal handler from any arbitrarily-created
    // signal_handler object. Note that if your object isn't going
    // to be around forever, you'll want to get the return value
    // from this function as use it to disconnect the listener
    // before your object goes away

    boost::signals2::connection subscribe(boost::function<void(int sig, signalfd_siginfo info)> listener)
    {
      return body->notify.connect(listener);
    }

    // Install the signal handler -- this starts the signal handler
    // thread
    void install()
    {
      body->install();
    }

    // Calls join on the signal handler thread (Do this after shutdown)
    void join()
    {
      body->join();
    }

    // Shut down signal handler and exit its thread (Sends a
    // SIGUSR2 to the process)
    void shutdown()
    {
      body->shutdown();
    }

    // Block an individual signal - adds signal to current
    // process signal mask
    void block(int signo)
    {
      sigset_t set;
      sigemptyset(&set);
      sigaddset(&set, signo);
      pthread_sigmask(SIG_BLOCK, &set, nullptr);
    }

    // Block an entire sigmask. This replaces the
    // current process signal mask with the provided
    // one. Note it takes a pointer, as it conforms
    // to what pthread_sigmask takes
    void block(sigset_t *set)
    {
      pthread_sigmask(SIG_SETMASK, set, nullptr);
    }

  };

}

#endif
