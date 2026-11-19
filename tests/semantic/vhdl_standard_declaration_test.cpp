// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_standard_declaration.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <stdexcept>

#include "fsim/semantic/vhdl_standard_catalog.hpp"

namespace {

using fsim::project::VhdlStandard;
using fsim::semantic::provided_type_of;
using fsim::semantic::standard_declaration_of;
using fsim::semantic::vhdl::ProvidedType;
using fsim::support::StandardDeclaration;
using fsim::semantic::available;
using fsim::support::since;
using fsim::support::until;

constexpr ProvidedType round_trip_types[] = {
    ProvidedType::boolean,
    ProvidedType::bit,
    ProvidedType::character,
    ProvidedType::severity_level,
    ProvidedType::integer,
    ProvidedType::natural,
    ProvidedType::positive,
    ProvidedType::real,
    ProvidedType::time,
    ProvidedType::delay_length,
    ProvidedType::file_open_kind,
    ProvidedType::file_open_status,
    ProvidedType::range_direction,
    ProvidedType::string,
    ProvidedType::bit_vector,
    ProvidedType::boolean_vector,
    ProvidedType::integer_vector,
    ProvidedType::real_vector,
    ProvidedType::time_vector,
};

} // namespace

int main()
{
    // IEEE 1076-2019 16.3: TIME exists from 1076-1987 on.
    assert(since(StandardDeclaration::time) == 1987U);
    assert(until(StandardDeclaration::time) == 0U);
    for (const auto standard : { VhdlStandard::vhdl_1987, VhdlStandard::vhdl_1993,
             VhdlStandard::vhdl_2000, VhdlStandard::vhdl_2002,
             VhdlStandard::vhdl_2008, VhdlStandard::vhdl_2019 }) {
        assert(available(StandardDeclaration::time, standard));
    }

    // The support form takes the edition year; 2000 and 2002 use the 1993 rows.
    assert(fsim::support::available(StandardDeclaration::time, 1987U));
    assert(fsim::support::available(StandardDeclaration::file_open_kind, 2000U));
    assert(!fsim::support::available(StandardDeclaration::boolean_vector, 2002U));
    assert(fsim::support::available(StandardDeclaration::boolean_vector, 2008U));
    bool threw = false;
    try {
        static_cast<void>(fsim::support::available(StandardDeclaration::time, 1999U));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    // Declarations of 1076-2008 and 1076-2019 are not in the STANDARD of 1993
    // (and 2000 and 2002 use the 1993 rows).
    assert(since(StandardDeclaration::boolean_vector) == 2008U);
    assert(!available(StandardDeclaration::boolean_vector, VhdlStandard::vhdl_1993));
    assert(!available(StandardDeclaration::boolean_vector, VhdlStandard::vhdl_2002));
    assert(available(StandardDeclaration::boolean_vector, VhdlStandard::vhdl_2008));
    assert(since(StandardDeclaration::range_direction) == 2019U);
    assert(!available(StandardDeclaration::range_direction, VhdlStandard::vhdl_2008));
    assert(available(StandardDeclaration::range_direction, VhdlStandard::vhdl_2019));
    assert(since(StandardDeclaration::time_range_record) == 2019U);
    assert(!available(StandardDeclaration::time_range_record, VhdlStandard::vhdl_1993));
    assert(since(StandardDeclaration::file_open_kind) == 1993U);
    assert(!available(StandardDeclaration::file_open_kind, VhdlStandard::vhdl_1987));
    assert(available(StandardDeclaration::file_open_kind, VhdlStandard::vhdl_2000));
    assert(since(StandardDeclaration::op_mod_time_time) == 2008U);
    assert(!available(StandardDeclaration::op_mod_time_time, VhdlStandard::vhdl_1993));

    // NOW: the 1987 function returns TIME and is replaced in 1993 by the
    // impure function that returns DELAY_LENGTH (16.3).
    assert(since(StandardDeclaration::now_time) == 1987U);
    assert(until(StandardDeclaration::now_time) == 1993U);
    assert(available(StandardDeclaration::now_time, VhdlStandard::vhdl_1987));
    assert(!available(StandardDeclaration::now_time, VhdlStandard::vhdl_1993));
    assert(since(StandardDeclaration::now_delay_length) == 1993U);
    assert(!available(StandardDeclaration::now_delay_length, VhdlStandard::vhdl_1987));
    assert(available(StandardDeclaration::now_delay_length, VhdlStandard::vhdl_2019));

    // The enumeration is the catalog: value row + 1, the same since, and the
    // names are unique.
    const auto catalog = fsim::semantic::vhdl_standard_catalog();
    assert(fsim::support::standard_declaration_count() == catalog.size());
    for (std::size_t row = 0; row < catalog.size(); ++row) {
        const auto declaration = static_cast<StandardDeclaration>(row + 1U);
        assert(fsim::support::catalog_row(declaration) == row);
        assert(since(declaration) == catalog[row].since);
        assert(!fsim::support::name(declaration).empty());
        for (std::size_t other = 0; other < row; ++other) {
            assert(fsim::support::name(declaration)
                != fsim::support::name(static_cast<StandardDeclaration>(other + 1U)));
        }
    }

    // ProvidedType and the STANDARD type declaration map both ways.
    for (const auto type : round_trip_types) {
        const auto declaration = standard_declaration_of(type);
        assert(declaration.has_value());
        const auto back = provided_type_of(*declaration);
        assert(back.has_value() && *back == type);
    }
    assert(standard_declaration_of(ProvidedType::time) == StandardDeclaration::time);
    assert(standard_declaration_of(ProvidedType::none) == std::nullopt);
    assert(standard_declaration_of(ProvidedType::std_logic) == std::nullopt);
    assert(standard_declaration_of(ProvidedType::numeric_std_unsigned) == std::nullopt);
    assert(provided_type_of(StandardDeclaration::op_add_time_time) == std::nullopt);
    assert(provided_type_of(StandardDeclaration::time_range_record) == std::nullopt);
    assert(provided_type_of(StandardDeclaration::none) == std::nullopt);

    std::cout << "vhdl standard declaration tests passed\n";
    return 0;
}
