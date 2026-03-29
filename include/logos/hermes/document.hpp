// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

// document.hpp is now a convenience header that pulls in the full Hermes API.
// The actual implementation is split across:
//   config.hpp      — arena_offset_t, DocumentHeader
//   mem_holder.hpp  — MemHolder (refcounted arena owner)
//   view.hpp        — Views, Own<>, HermesCtr, make_doc()

#include <logos/hermes/view.hpp>
