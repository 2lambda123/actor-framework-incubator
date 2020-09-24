/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2020 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/net/actor_shell.hpp"

#include "caf/callback.hpp"
#include "caf/config.hpp"
#include "caf/detail/default_invoke_result_visitor.hpp"
#include "caf/detail/sync_request_bouncer.hpp"
#include "caf/invoke_message_result.hpp"
#include "caf/logger.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/socket_manager.hpp"

namespace caf::net {

// -- constructors, destructors, and assignment operators ----------------------

actor_shell::actor_shell(actor_config& cfg, socket_manager* owner)
  : super(cfg), mailbox_(policy::normal_messages{}), owner_(owner) {
  mailbox_.try_block();
}

actor_shell::~actor_shell() {
  // nop
}

// -- state modifiers ----------------------------------------------------------

void actor_shell::quit(error reason) {
  cleanup(std::move(reason), nullptr);
}

// -- mailbox access -----------------------------------------------------------

mailbox_element_ptr actor_shell::next_message() {
  if (!mailbox_.blocked()) {
    mailbox_.fetch_more();
    auto& q = mailbox_.queue();
    if (q.total_task_size() > 0) {
      q.inc_deficit(1);
      return q.next();
    }
  }
  return nullptr;
}

bool actor_shell::try_block_mailbox() {
  return mailbox_.try_block();
}

// -- message processing -------------------------------------------------------

bool actor_shell::consume_message() {
  CAF_LOG_TRACE("");
  if (auto msg = next_message()) {
    current_element_ = msg.get();
    CAF_LOG_RECEIVE_EVENT(current_element_);
    CAF_BEFORE_PROCESSING(this, *msg);
    auto mid = msg->mid;
    if (!mid.is_response()) {
      detail::default_invoke_result_visitor<actor_shell> visitor{this};
      if (auto result = bhvr_(msg->payload)) {
        visitor(*result);
      } else {
        auto fallback_result = (*fallback_)(msg->payload);
        visit(visitor, fallback_result);
      }
    } else if (auto i = multiplexed_responses_.find(mid);
               i != multiplexed_responses_.end()) {
      auto bhvr = std::move(i->second);
      multiplexed_responses_.erase(i);
      auto res = bhvr(msg->payload);
      if (!res) {
        CAF_LOG_DEBUG("got unexpected_response");
        auto err_msg = make_message(
          make_error(sec::unexpected_response, std::move(msg->payload)));
        bhvr(err_msg);
      }
    }
    CAF_AFTER_PROCESSING(this, invoke_message_result::consumed);
    CAF_LOG_SKIP_OR_FINALIZE_EVENT(invoke_message_result::consumed);
    return true;
  }
  return false;
}

void actor_shell::add_multiplexed_response_handler(message_id response_id,
                                                   behavior bhvr) {
  if (bhvr.timeout() != infinite)
    request_response_timeout(bhvr.timeout(), response_id);
  multiplexed_responses_.emplace(response_id, std::move(bhvr));
}

// -- overridden functions of abstract_actor -----------------------------------

void actor_shell::enqueue(mailbox_element_ptr ptr, execution_unit*) {
  CAF_ASSERT(ptr != nullptr);
  CAF_ASSERT(!getf(is_blocking_flag));
  CAF_LOG_TRACE(CAF_ARG(*ptr));
  CAF_LOG_SEND_EVENT(ptr);
  auto mid = ptr->mid;
  auto sender = ptr->sender;
  auto collects_metrics = getf(abstract_actor::collects_metrics_flag);
  if (collects_metrics) {
    ptr->set_enqueue_time();
    metrics_.mailbox_size->inc();
  }
  switch (mailbox().push_back(std::move(ptr))) {
    case intrusive::inbox_result::unblocked_reader: {
      CAF_LOG_ACCEPT_EVENT(true);
      std::unique_lock<std::mutex> guard{owner_mtx_};
      // The owner can only be null if this enqueue succeeds, then we close the
      // mailbox and reset owner_ in cleanup() before acquiring the mutex here.
      // Hence, the mailbox element has already been disposed and we can simply
      // skip any further processing.
      if (owner_)
        owner_->mpx().register_writing(owner_);
      break;
    }
    case intrusive::inbox_result::queue_closed: {
      CAF_LOG_REJECT_EVENT();
      home_system().base_metrics().rejected_messages->inc();
      if (collects_metrics)
        metrics_.mailbox_size->dec();
      if (mid.is_request()) {
        detail::sync_request_bouncer f{exit_reason()};
        f(sender, mid);
      }
      break;
    }
    case intrusive::inbox_result::success:
      // Enqueued to a running actors' mailbox: nothing to do.
      CAF_LOG_ACCEPT_EVENT(false);
      break;
  }
}

mailbox_element* actor_shell::peek_at_next_mailbox_element() {
  return mailbox().closed() || mailbox().blocked() ? nullptr : mailbox().peek();
}

const char* actor_shell::name() const {
  return "caf.net.actor-shell";
}

void actor_shell::launch(execution_unit*, bool, bool hide) {
  CAF_PUSH_AID_FROM_PTR(this);
  CAF_LOG_TRACE(CAF_ARG(hide));
  CAF_ASSERT(!getf(is_blocking_flag));
  if (!hide)
    register_at_system();
}

bool actor_shell::cleanup(error&& fail_state, execution_unit* host) {
  CAF_LOG_TRACE(CAF_ARG(fail_state));
  // Clear mailbox.
  if (!mailbox_.closed()) {
    mailbox_.close();
    detail::sync_request_bouncer bounce{fail_state};
    auto dropped = mailbox_.queue().new_round(1000, bounce).consumed_items;
    while (dropped > 0) {
      if (getf(abstract_actor::collects_metrics_flag)) {
        auto val = static_cast<int64_t>(dropped);
        metrics_.mailbox_size->dec(val);
      }
      dropped = mailbox_.queue().new_round(1000, bounce).consumed_items;
    }
  }
  // Detach from owner.
  {
    std::unique_lock<std::mutex> guard{owner_mtx_};
    owner_ = nullptr;
  }
  // Dispatch to parent's `cleanup` function.
  return super::cleanup(std::move(fail_state), host);
}

actor_shell_ptr::actor_shell_ptr(strong_actor_ptr ptr) noexcept
  : ptr_(std::move(ptr)) {
  // nop
}

actor actor_shell_ptr::as_actor() const noexcept {
  return actor_cast<actor>(ptr_);
}

void actor_shell_ptr::detach(error reason) {
  if (auto ptr = get()) {
    ptr->quit(std::move(reason));
    ptr_.release();
  }
}

actor_shell_ptr::~actor_shell_ptr() {
  if (auto ptr = get())
    ptr->quit(exit_reason::normal);
}

actor_shell* actor_shell_ptr::get() const noexcept {
  if (ptr_) {
    auto ptr = actor_cast<abstract_actor*>(ptr_);
    return static_cast<actor_shell*>(ptr);
  }
  return nullptr;
}

} // namespace caf::net