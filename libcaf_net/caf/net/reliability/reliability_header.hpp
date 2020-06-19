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

#include <cstdint>

namespace caf::net::reliability {

using id_type = uint16_t;

struct reliability_header {
  id_type id;
  bool is_ack;
};

constexpr size_t reliability_header_size = sizeof(id_type) + sizeof(bool);

/// @relates header
template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, reliability_header& x) {
  return f(meta::type_name("reliability::reliability_header"), x.id, x.is_ack);
}

} // namespace caf::net::reliability
