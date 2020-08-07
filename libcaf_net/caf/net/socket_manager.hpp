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

#include "caf/detail/net_export.hpp"
#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/make_counted.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/operation.hpp"
#include "caf/net/socket.hpp"
#include "caf/ref_counted.hpp"

namespace caf::net {

/// Manages the lifetime of a single socket and handles any I/O events on it.
class CAF_NET_EXPORT socket_manager : public ref_counted {
public:
  // -- constructors, destructors, and assignment operators --------------------

  /// @pre `parent != nullptr`
  /// @pre `handle != invalid_socket`
  socket_manager(socket handle, const multiplexer_ptr& parent);

  ~socket_manager() override;

  socket_manager(const socket_manager&) = delete;

  socket_manager& operator=(const socket_manager&) = delete;

  // -- properties -------------------------------------------------------------

  /// Returns the managed socket.
  socket handle() const noexcept {
    return handle_;
  }

  /// Returns a pointer to the multiplexer running this `socket_manager`.
  multiplexer_ptr multiplexer() const {
    return parent_.lock();
  }

  /// Returns registered operations (read, write, or both).
  operation mask() const noexcept {
    return mask_;
  }

  /// Adds given flag(s) to the event mask.
  /// @returns `false` if `mask() | flag == mask()`, `true` otherwise.
  /// @pre `flag != operation::none`
  bool mask_add(operation flag) noexcept;

  /// Tries to clear given flag(s) from the event mask.
  /// @returns `false` if `mask() & ~flag == mask()`, `true` otherwise.
  /// @pre `flag != operation::none`
  bool mask_del(operation flag) noexcept;

  const error& abort_reason() const noexcept {
    return abort_reason_;
  }

  void abort_reason(error reason) noexcept {
    abort_reason_ = std::move(reason);
  }

  template <class... Ts>
  const error& abort_reason_or(Ts&&... xs) {
    if (!abort_reason_)
      abort_reason_ = make_error(std::forward<Ts>(xs)...);
    return abort_reason_;
  }

  // -- event loop management --------------------------------------------------

  void register_reading();

  void register_writing();

  // -- pure virtual member functions ------------------------------------------

  /// Called whenever the socket received new data.
  virtual bool handle_read_event() = 0;

  /// Called whenever the socket is allowed to send data.
  virtual bool handle_write_event() = 0;

  /// Called when the remote side becomes unreachable due to an error.
  /// @param code The error code as reported by the operating system.
  virtual void handle_error(sec code) = 0;

protected:
  // -- member variables -------------------------------------------------------

  socket handle_;

  operation mask_;

  weak_multiplexer_ptr parent_;

  error abort_reason_;
};

template <class Protocol>
class socket_manager_impl : public socket_manager {
public:
  template <class... Ts>
  socket_manager_impl(Ts&&... xs) : protocol_(std::forward<Ts>(xs)...) {
    // nop
  }

  bool handle_read_event() override {
    return protocol_.handle_read_event(*this);
  }

  bool handle_write_event() override {
    return protocol_.handle_write_event(*this);
  }

  void handle_error(sec code) override {
    abort_reason_ = code;
    return protocol_.abort(*this, abort_reason_);
  }

  auto& protocol() noexcept {
    return protocol_;
  }

  const auto& protocol() const noexcept {
    return protocol_;
  }

private:
  Protocol protocol_;
};

/// @relates socket_manager
using socket_manager_ptr = intrusive_ptr<socket_manager>;

template <class B, template <class> class... Layers>
struct make_socket_manager_helper;

template <class B>
struct make_socket_manager_helper<B> {
  using type = B;
};

template <class B, template <class> class Layer,
          template <class> class... Layers>
struct make_socket_manager_helper<B, Layer, Layers...>
  : make_socket_manager_helper<Layer<B>, Layers...> {
  // no content
};

template <class App, template <class> class... Layers, class... Ts>
auto make_socket_manager(Ts&&... xs) {
  using impl = make_socket_manager_helper<App, Layers..., socket_manager_impl>;
  return make_counted<impl>(std::forward<Ts>(xs)...);
}

} // namespace caf::net
