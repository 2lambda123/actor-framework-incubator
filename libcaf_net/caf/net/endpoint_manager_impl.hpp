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

#pragma once

#include "caf/abstract_actor.hpp"
#include "caf/actor_cast.hpp"
#include "caf/actor_clock.hpp"
#include "caf/actor_config.hpp"
#include "caf/actor_system.hpp"
#include "caf/detail/overload.hpp"
#include "caf/make_actor.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/timeout_proxy.hpp"

namespace caf::net {

template <class Transport>
class endpoint_manager_impl : public endpoint_manager {
public:
  // -- member types -----------------------------------------------------------

  using super = endpoint_manager;

  using transport_type = Transport;

  using application_type = typename transport_type::application_type;

  // -- constructors, destructors, and assignment operators --------------------

  endpoint_manager_impl(const multiplexer_ptr& parent, actor_system& sys,
                        Transport trans)
    : super(trans.handle(), parent, sys),
      transport_(std::move(trans)),
      next_timeout_id_(0) {
    // nop
  }

  ~endpoint_manager_impl() override {
    auto proxy = actor_cast<timeout_proxy*>(this->timeout_proxy_);
    proxy->kill_proxy(nullptr, exit_reason::normal);
  }

  // -- properties -------------------------------------------------------------

  transport_type& transport() {
    return transport_;
  }

  endpoint_manager_impl& manager() {
    return *this;
  }

  // -- timeout management -----------------------------------------------------

  template <class... Ts>
  uint64_t
  set_timeout(actor_clock::time_point tp, std::string tag, Ts&&... xs) {
    auto act = actor_cast<abstract_actor*>(timeout_proxy_);
    CAF_ASSERT(act != nullptr);
    sys_.clock().set_multi_timeout(tp, act, std::move(tag), next_timeout_id_);
    transport_.set_timeout(next_timeout_id_, std::forward<Ts>(xs)...);
    return next_timeout_id_++;
  }

  void cancel_timeout(std::string tag, uint64_t id) {
    auto act = actor_cast<abstract_actor*>(timeout_proxy_);
    CAF_ASSERT(act != nullptr);
    sys_.clock().cancel_ordinary_timeout(act, std::move(tag));
    transport_.cancel_timeout(id);
  }

  // -- interface functions ----------------------------------------------------

  error init() override {
    this->register_reading();
    auto& sys = this->sys_;
    actor_config cfg;
    cfg.add_flag(abstract_actor::is_hidden_flag);
    this->timeout_proxy_ = make_actor<timeout_proxy, actor>(
      sys.next_actor_id(), sys.node(), &sys, cfg, this);
    return transport_.init(*this);
  }

  bool handle_read_event() override {
    return transport_.handle_read_event(*this);
  }

  bool handle_write_event() override {
    if (!this->queue_.blocked()) {
      this->queue_.fetch_more();
      auto& q = std::get<0>(this->queue_.queue().queues());
      do {
        q.inc_deficit(q.total_task_size());
        for (auto ptr = q.next(); ptr != nullptr; ptr = q.next()) {
          auto f = detail::make_overload(
            [&](endpoint_manager_queue::event::resolve_request& x) {
              transport_.resolve(*this, x.locator, x.listener);
            },
            [&](endpoint_manager_queue::event::new_proxy& x) {
              transport_.new_proxy(*this, x.peer, x.id);
            },
            [&](endpoint_manager_queue::event::local_actor_down& x) {
              transport_.local_actor_down(*this, x.observing_peer, x.id,
                                          std::move(x.reason));
            },
            [&](endpoint_manager_queue::event::timeout& x) {
              transport_.timeout(*this, x.type, x.id);
            });
          visit(f, ptr->value);
        }
      } while (!q.empty());
    }
    if (!transport_.handle_write_event(*this)) {
      if (this->queue_.blocked())
        return false;
      return !(this->queue_.empty() && this->queue_.try_block());
    }
    return true;
  }

  void handle_error(sec code) override {
    transport_.handle_error(code);
  }

private:
  transport_type transport_;

  /// Stores the id for the next timeout.
  uint64_t next_timeout_id_;
};

} // namespace caf::net
