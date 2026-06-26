// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string>

#include <logos/writ/document.hpp>
#include <logos/writ/any_val.hpp>

namespace logos::writ {

// Render a Writ document (or a single value) to a JSON-compatible text form:
//   null / true / false / <int> / <float> / "string" / [array] / {"k": v}
// The JSON core (null, bool, integer, float, string, array, map) round-trips
// exactly through text_parse(); the Writ extension types (tiny/typed maps, typed
// arrays, decimal, boxed wide scalars, typed-value, parameter) render readably but
// are not all reparsed to their exact type.
std::string stringify(const WritCtr& doc);
std::string stringify_value(AnyVal value);

} // namespace logos::writ
