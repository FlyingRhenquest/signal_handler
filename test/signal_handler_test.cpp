// Test the signal handler

/*
 * Copyright 2014 Bruce Ide
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
 */

#include "boost/bind.hpp"
#include <cppunit/extensions/HelperMacros.h>
#include "signal_handler.hpp"

class signal_handler_test : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(signal_handler_test);
  CPPUNIT_TEST(basic_test);
  CPPUNIT_TEST(shutdown_restart_test);
  CPPUNIT_TEST_SUITE_END();

  bool got_quit, got_usr1, got_hup;

  // I don't really care about the signalfd structure, so I'm
  // going to ignore it in my listener
  void notify(int signo)
  {
    switch(signo) {
    case SIGQUIT:
      got_quit = true;
      break;
    case SIGUSR1:
      got_usr1 = true;
      break;
    case SIGHUP:
      got_hup = true;
      break;
    case SIGUSR2:
      break;
    default:
      std::cout << "Unknown signal: " << signo << std::endl;
    }
  }


public:

  void basic_test()
  {
    got_quit = got_usr1 = got_hup = false;
    fr::signal_handler handler;
    handler.block(SIGQUIT);
    handler.block(SIGUSR1);
    handler.block(SIGHUP);
    boost::signals2::connection subscription = handler.subscribe(boost::bind(&signal_handler_test::notify, this, _1));
    handler.install();
    // Wait a while for the handler thread to start
    boost::this_thread::yield();
    pid_t thispid = getpid();
    kill(thispid, SIGQUIT);
    kill(thispid, SIGUSR1);
    kill(thispid, SIGHUP);
    // Wait a second for sigs to catch up
    boost::this_thread::sleep_for(boost::chrono::seconds(1));
    CPPUNIT_ASSERT(got_quit);
    CPPUNIT_ASSERT(got_usr1);
    CPPUNIT_ASSERT(got_hup);
    subscription.disconnect();
    handler.shutdown();
    handler.join();
  }

  void shutdown_restart_test()
  {
    got_quit = got_usr1 = got_hup = false;
    fr::signal_handler handler;
    boost::signals2::connection subscription = handler.subscribe(boost::bind(&signal_handler_test::notify, this, _1));
    handler.install(); // Last test should already have shut it down
    boost::this_thread::yield();
    pid_t thispid = getpid();
    kill(thispid, SIGUSR1);
    boost::this_thread::sleep_for(boost::chrono::seconds(2));
    CPPUNIT_ASSERT(got_usr1);
    subscription.disconnect();
    handler.shutdown(); // again
    handler.join();
  }



};

CPPUNIT_TEST_SUITE_REGISTRATION(signal_handler_test);
