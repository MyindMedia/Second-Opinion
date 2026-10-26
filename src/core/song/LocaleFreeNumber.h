#pragma once

// Number <-> text conversions that ignore the process locale. Hosts can switch the C locale to one
// with a comma decimal separator, which breaks strtod/atof/snprintf("%f") for JSON and saved state.
// These always use the classic "C" rules ('.' as the decimal point, no grouping).

#include <cmath>
#include <ios>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace so::text
{

/** Parses the whole of `s` as a finite double. Returns nullopt on any trailing junk. */
inline std::optional<double> parseDouble (std::string_view s)
{
    if (s.empty())
        return std::nullopt;
    std::istringstream in { std::string (s) };
    in.imbue (std::locale::classic());
    double d = 0.0;
    in >> d;
    if (in.fail() || ! std::isfinite (d))
        return std::nullopt;
    in >> std::ws;
    if (! in.eof())
        return std::nullopt;
    return d;
}

/** Fixed notation with `decimals` digits after the point ("%.Nf"). */
inline std::string formatFixed (double d, int decimals)
{
    std::ostringstream out;
    out.imbue (std::locale::classic());
    out.setf (std::ios::fixed, std::ios::floatfield);
    out.precision (decimals);
    out << d;
    return out.str();
}

/** Shortest-ish exact round trip ("%.17g"). */
inline std::string formatRoundTrip (double d)
{
    std::ostringstream out;
    out.imbue (std::locale::classic());
    out.precision (17);
    out << d;
    return out.str();
}

} // namespace so::text
