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

namespace caf::net {

// -- actor_shell --------------------------------------------------------------

actor_shell::actor_shell(actor_config& cfg, socket_manager* owner)
  : super(cfg, owner) {
  // nop
}

actor_shell::~actor_shell() {
  // nop
}

const char* actor_shell::name() const {
  return "caf.net.actor-shell";
}

// -- actor_shell_ptr ----------------------------------------------------------

actor_shell_ptr::actor_shell_ptr(strong_actor_ptr ptr) noexcept
  : ptr_(std::move(ptr)) {
  // nop
}

actor_shell_ptr::~actor_shell_ptr() {
  if (auto ptr = get())
    ptr->quit(exit_reason::normal);
}

actor_shell_ptr::handle_type actor_shell_ptr::as_actor() const noexcept {
  return actor_cast<actor>(ptr_);
}

void actor_shell_ptr::detach(error reason) {
  if (auto ptr = get()) {
    ptr->quit(std::move(reason));
    ptr_.release();
  }
}

actor_shell_ptr::element_type* actor_shell_ptr::get() const noexcept {
  if (ptr_) {
    auto ptr = actor_cast<abstract_actor*>(ptr_);
    return static_cast<actor_shell*>(ptr);
  } else {
    return nullptr;
  }
}

} // namespace caf::net
