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

#pragma once

#include "caf/actor_traits.hpp"
#include "caf/callback.hpp"
#include "caf/detail/net_export.hpp"
#include "caf/detail/unordered_flat_map.hpp"
#include "caf/extend.hpp"
#include "caf/fwd.hpp"
#include "caf/intrusive/drr_queue.hpp"
#include "caf/intrusive/fifo_inbox.hpp"
#include "caf/local_actor.hpp"
#include "caf/mixin/requester.hpp"
#include "caf/mixin/sender.hpp"
#include "caf/net/fwd.hpp"
#include "caf/none.hpp"
#include "caf/policy/normal_messages.hpp"

namespace caf::net {

///
class CAF_NET_EXPORT actor_shell
  : public extend<local_actor, actor_shell>::with<mixin::sender,
                                                  mixin::requester>,
    public dynamically_typed_actor_base,
    public non_blocking_actor_base {
public:
  // -- friends ----------------------------------------------------------------

  friend class actor_shell_ptr;

  // -- member types -----------------------------------------------------------

  using super
    = extend<local_actor, actor_shell>::with<mixin::sender, mixin::requester>;

  using signatures = none_t;

  using behavior_type = behavior;

  struct mailbox_policy {
    using queue_type = intrusive::drr_queue<policy::normal_messages>;

    using deficit_type = policy::normal_messages::deficit_type;

    using mapped_type = policy::normal_messages::mapped_type;

    using unique_pointer = policy::normal_messages::unique_pointer;
  };

  using mailbox_type = intrusive::fifo_inbox<mailbox_policy>;

  using fallback_handler = unique_callback_ptr<result<message>(message&)>;

  // -- constructors, destructors, and assignment operators --------------------

  actor_shell(actor_config& cfg, socket_manager* owner);

  ~actor_shell() override;

  // -- state modifiers --------------------------------------------------------

  /// Detaches the shell from its owner and closes the mailbox.
  void quit(error reason);

  /// Overrides the callbacks for incoming messages.
  template <class... Fs>
  void set_behavior(Fs... fs) {
    bhvr_ = behavior{std::move(fs)...};
  }

  /// Overrides the default handler for unexpected messages.
  template <class F>
  void set_fallback(F f) {
    fallback_ = make_type_erased_callback(std::move(f));
  }

  // -- mailbox access ---------------------------------------------------------

  auto& mailbox() noexcept {
    return mailbox_;
  }

  /// Dequeues and returns the next message from the mailbox or returns
  /// `nullptr` if the mailbox is empty.
  mailbox_element_ptr next_message();

  /// Tries to put the mailbox into the `blocked` state, causing the next
  /// enqueue to register the owning socket manager for write events.
  bool try_block_mailbox();

  // -- message processing -----------------------------------------------------

  /// Dequeues and processes the next message from the mailbox.
  /// @returns `true` if a message was dequeued and process, `false` if the
  ///          mailbox was empty.
  bool consume_message();

  /// Adds a callback for a multiplexed response.
  void add_multiplexed_response_handler(message_id response_id, behavior bhvr);

  // -- overridden functions of abstract_actor ---------------------------------

  using abstract_actor::enqueue;

  void enqueue(mailbox_element_ptr ptr, execution_unit* eu) override;

  mailbox_element* peek_at_next_mailbox_element() override;

  // -- overridden functions of local_actor ------------------------------------

  const char* name() const override;

  void launch(execution_unit* eu, bool lazy, bool hide) override;

  bool cleanup(error&& fail_state, execution_unit* host) override;

private:
  // Stores incoming actor messages.
  mailbox_type mailbox_;

  // Guards access to owner_.
  std::mutex owner_mtx_;

  // Points to the owning manager (nullptr after quit was called).
  socket_manager* owner_;

  // Handler for consuming messages from the mailbox.
  behavior bhvr_;

  // Handler for unexpected messages.
  fallback_handler fallback_;

  // Stores callbacks for multiplexed responses.
  detail::unordered_flat_map<message_id, behavior> multiplexed_responses_;
};

/// An "owning" pointer to an actor shell in the sense that it calls `quit()` on
/// the shell when going out of scope.
class CAF_NET_EXPORT actor_shell_ptr {
public:
  friend class socket_manager;

  constexpr actor_shell_ptr() noexcept {
    // nop
  }

  constexpr actor_shell_ptr(std::nullptr_t) noexcept {
    // nop
  }

  actor_shell_ptr(actor_shell_ptr&& other) noexcept = default;

  actor_shell_ptr& operator=(actor_shell_ptr&& other) noexcept = default;

  actor_shell_ptr(const actor_shell_ptr& other) = delete;

  actor_shell_ptr& operator=(const actor_shell_ptr& other) = delete;

  ~actor_shell_ptr();

  /// Returns a strong handle to the managed actor shell.
  actor as_actor() const noexcept;

  /// Returns a weak handle to the managed actor shell.
  actor_addr as_actor_addr() const noexcept;

  void detach(error reason);

  actor_shell* get() const noexcept;

  actor_shell* operator->() const noexcept {
    return get();
  }

  actor_shell& operator*() const noexcept {
    return *get();
  }

  bool operator!() const noexcept {
    return !ptr_;
  }

  explicit operator bool() const noexcept {
    return static_cast<bool>(ptr_);
  }

private:
  /// @pre `ptr != nullptr`
  explicit actor_shell_ptr(strong_actor_ptr ptr) noexcept;

  strong_actor_ptr ptr_;
};

} // namespace caf::net
