/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2019 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE net.backend.tcp

#include "caf/net/backend/tcp.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include <string>
#include <thread>

#include "caf/actor_system_config.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/net/tcp_accept_socket.hpp"
#include "caf/uri.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::literals::string_literals;

namespace {

behavior dummy_actor(event_based_actor*) {
  return {
    // nop
  };
}

struct earth_node {
  uri operator()() {
    return unbox(make_uri("tcp://localhost:12345"));
  }

  uint16_t port() {
    return 12345;
  }
};

struct mars_node {
  uri operator()() {
    return unbox(make_uri("tcp://localhost:12346"));
  }

  uint16_t port() {
    return 12346;
  }
};

template <class Node>
struct config : actor_system_config {
  config() {
    Node this_node;
    put(content, "middleman.this-node", this_node());
    put(content, "middleman.tcp-port", this_node.port());
    load<middleman, backend::tcp>();
  }
};

class planet_driver {
public:
  virtual ~planet_driver() {
    // nop
  }

  virtual bool consume_message() = 0;

  virtual bool handle_io_event() = 0;

  virtual bool trigger_timeout() = 0;
};

template <class Node>
class planet : public test_coordinator_fixture<config<Node>> {
public:
  planet(planet_driver& driver)
    : mm(this->sys.network_manager()), mpx(mm.mpx()), driver_(driver) {
    mpx->set_thread_id();
  }

  node_id id() const {
    return this->sys.node();
  }

  bool consume_message() override {
    return driver_.consume_message();
  }

  bool handle_io_event() override {
    return driver_.handle_io_event();
  }

  bool trigger_timeout() override {
    return driver_.trigger_timeout();
  }

  actor resolve(string_view locator) {
    auto hdl = actor_cast<actor>(this->self);
    this->sys.network_manager().resolve(unbox(make_uri(locator)), hdl);
    this->run();
    actor result;
    this->self->receive(
      [&](strong_actor_ptr& ptr, const std::set<std::string>&) {
        CAF_MESSAGE("resolved " << locator);
        result = actor_cast<actor>(std::move(ptr));
      });
    return result;
  }

  net::middleman& mm;
  multiplexer_ptr mpx;

private:
  planet_driver& driver_;
};

struct fixture : host_fixture, planet_driver {
  fixture() : earth(*this), mars(*this) {
    run();
    CAF_REQUIRE_EQUAL(earth.mpx->num_socket_managers(), 2);
    CAF_REQUIRE_EQUAL(mars.mpx->num_socket_managers(), 2);
  }

  bool consume_message() override {
    return earth.sched.try_run_once() || mars.sched.try_run_once();
  }

  bool handle_io_event() override {
    return earth.mpx->poll_once(false) || mars.mpx->poll_once(false);
  }

  bool trigger_timeout() override {
    return earth.sched.trigger_timeout() || mars.sched.trigger_timeout();
  }

  void set_thread_id() {
    earth.mpx->set_thread_id();
    mars.mpx->set_thread_id();
  }

  void run() {
    earth.run();
  }

  planet<earth_node> earth;
  planet<mars_node> mars;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(tcp_backend_tests, fixture)

CAF_TEST(doorman accept) {
  auto backend = earth.mm.backend("tcp");
  CAF_CHECK(backend);
  uri::authority_type auth;
  auth.host = "localhost"s;
  auth.port = backend->port();
  CAF_MESSAGE("trying to connect to earth at " << CAF_ARG(auth));
  auto sock = make_connected_tcp_stream_socket(auth);
  handle_io_event();
  CAF_CHECK(sock);
  auto guard = make_socket_guard(*sock);
  CAF_REQUIRE_EQUAL(earth.mpx->num_socket_managers(), 3);
}

CAF_TEST(connect) {
  uri::authority_type auth;
  auth.host = "0.0.0.0"s;
  auth.port = 0;
  auto acceptor = unbox(make_tcp_accept_socket(auth, false));
  auto acc_guard = make_socket_guard(acceptor);
  auto port = unbox(local_port(acc_guard.socket()));
  auto uri_str = std::string("tcp://localhost:") + std::to_string(port);
  CAF_MESSAGE("connecting to " << CAF_ARG(uri_str));
  CAF_CHECK(earth.mm.connect(*make_uri(uri_str)));
  auto sock = unbox(accept(acc_guard.socket()));
  auto sock_guard = make_socket_guard(sock);
  handle_io_event();
  CAF_REQUIRE_EQUAL(earth.mpx->num_socket_managers(), 3);
}

CAF_TEST(publish) {
  auto dummy = earth.sys.spawn(dummy_actor);
  auto path = "name/dummy"s;
  CAF_MESSAGE("publishing actor " << CAF_ARG(path));
  earth.mm.publish(dummy, path);
  CAF_MESSAGE("check registry for " << CAF_ARG(path));
  CAF_CHECK(earth.sys.registry().get(path) != nullptr);
}

CAF_TEST(remote_actor) {
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  auto dummy = earth.sys.spawn(dummy_actor);
  auto name = "dummy"s;
  earth.mm.publish(dummy, name);
  auto port = unbox(earth.mm.port("tcp"));
  auto ep_str = "tcp://localhost:"s + std::to_string(port);
  auto locator = unbox(make_uri(ep_str));
  CAF_MESSAGE("connecting mars to earth at " << CAF_ARG(locator));
  CAF_CHECK(mars.mm.connect(locator));
  handle_io_event();
  CAF_CHECK_EQUAL(mars.mpx->num_socket_managers(), 3);
  CAF_CHECK_EQUAL(earth.mpx->num_socket_managers(), 3);
  locator = unbox(make_uri(ep_str + "/name/"s + name));
  CAF_MESSAGE("resolve " << CAF_ARG(locator));
  mars.mm.resolve(locator, mars.self);
  bool running = true;
  auto f = [&]() {
    set_thread_id();
    while (running) {
      handle_io_event();
      std::this_thread::sleep_for(milliseconds(100));
    }
  };
  std::thread t{f};
  auto proxy = unbox(mars.mm.remote_actor(locator));
  CAF_MESSAGE("resolved actor");
  CAF_CHECK(proxy != nullptr);
  running = false;
  t.join();
  set_thread_id();
}

CAF_TEST_FIXTURE_SCOPE_END()