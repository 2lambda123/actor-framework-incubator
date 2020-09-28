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

#include <cstddef>
#include <cstdint>
#include <memory>

#include "caf/actor.hpp"
#include "caf/actor_clock.hpp"
#include "caf/detail/net_export.hpp"
#include "caf/fwd.hpp"
#include "caf/intrusive/drr_queue.hpp"
#include "caf/intrusive/fifo_inbox.hpp"
#include "caf/intrusive/singly_linked.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/net/consumer_queue.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/variant.hpp"

namespace caf::net {

/// Manages a communication endpoint.
class CAF_NET_EXPORT endpoint_manager : public socket_manager {
public:
  // -- member types -----------------------------------------------------------

  using super = socket_manager;

  // -- constructors, destructors, and assignment operators --------------------

  endpoint_manager(socket handle, const multiplexer_ptr& parent,
                   actor_system& sys);

  ~endpoint_manager() override;

  // -- properties -------------------------------------------------------------

  actor_system& system() noexcept {
    return sys_;
  }

  const actor_system_config& config() const noexcept;

  // -- queue access -----------------------------------------------------------

  bool at_end_of_message_queue();

  consumer_queue::message_ptr next_message();

  // -- event management -------------------------------------------------------

  /// Resolves a path to a remote actor.
  void resolve(uri locator, actor listener);

  /// Enqueues a message to the endpoint.
  void enqueue(mailbox_element_ptr msg, strong_actor_ptr receiver);

  /// Enqueues an event to the endpoint.
  template <class... Ts>
  void enqueue_event(Ts&&... xs) {
    enqueue(new consumer_queue::event(std::forward<Ts>(xs)...));
  }

  // -- pure virtual member functions ------------------------------------------

  /// Initializes the manager before adding it to the multiplexer's event loop.
  // virtual error init() = 0;

protected:
  bool enqueue(consumer_queue::element* ptr);

  /// Points to the hosting actor system.
  actor_system& sys_;

  /// Stores control events and outbound messages.
  consumer_queue::type queue_;

  /// Stores a proxy for interacting with the actor clock.
  actor timeout_proxy_;
};

using endpoint_manager_ptr = intrusive_ptr<endpoint_manager>;

} // namespace caf::net
