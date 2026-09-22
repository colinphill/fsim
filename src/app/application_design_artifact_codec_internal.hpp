// SPDX-License-Identifier: Apache-2.0
#pragma once

#define FSIM_DESIGN_ARTIFACT_CODEC_RUNTIME
#define FSIM_DESIGN_ARTIFACT_CODEC_COVERAGE_UVM
#define FSIM_DESIGN_ARTIFACT_CODEC_DESIGN_IR
#define FSIM_DESIGN_ARTIFACT_CODEC_SEMANTIC
#define FSIM_DESIGN_ARTIFACT_CODEC_SV_HIR
#define FSIM_DESIGN_ARTIFACT_CODEC_VHDL_HIR
#include "fsim/app/design_artifact.hpp"
#include "fsim/support/sha256.hpp"
#include "application_design_artifact_codec_validation.hpp"

#include "../diagnostic/artifact_identity.hpp"

#include <boost/pfr/core.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <limits>
#include <memory>
#include <new>
#include <ostream>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::app {
namespace codec_detail {

    constexpr std::size_t kMaximumNesting = 1024;
    constexpr std::string_view kCode = "FSIM-ART-0013";

    class DecodeBudget final {
    public:
        explicit DecodeBudget(const std::size_t maximum
            = kCompiledHirDecodeBudgetBytes)
            : remaining_(maximum)
        {
        }

        [[nodiscard]] bool consume(const std::uint64_t count,
            const std::size_t element_size) noexcept
        {
            if (element_size != 0U
                && count > remaining_ / element_size) {
                return false;
            }
            remaining_ -= static_cast<std::size_t>(count) * element_size;
            return true;
        }

    private:
        std::size_t remaining_ { };
    };

    template <typename Result, typename Operation>
    std::optional<Result> translate_decode_allocation_failure(
        const std::string_view source_name,
        diagnostic::Engine& diagnostics,
        Operation&& operation)
    {
        try {
            return std::invoke(std::forward<Operation>(operation));
        } catch (const std::bad_alloc&) {
            diagnostics.error(
                std::string { kCode },
                "design state allocation failed while decoding",
                { std::string { source_name }, { 1, 1, 0 }, { 1, 1, 0 } });
        } catch (const std::length_error&) {
            diagnostics.error(
                std::string { kCode },
                "design state allocation exceeds the host container limit",
                { std::string { source_name }, { 1, 1, 0 }, { 1, 1, 0 } });
        }
        return std::nullopt;
    }

    struct SystemVerilogConstraintHirState {
        std::vector<semantic::sv::Unit> units;
        std::vector<semantic::sv::Declaration> declarations;
        std::vector<semantic::sv::TypeDefinition> types;
        std::vector<semantic::sv::Expression> expressions;
        std::vector<semantic::sv::Statement> statements;
        std::vector<semantic::sv::Process> processes;
        std::vector<semantic::sv::ClassDeclaration> classes;
        std::vector<semantic::sv::Instance> instances;
        std::vector<semantic::sv::UdpDeclaration> udps;
        std::vector<semantic::sv::DpiDeclaration> dpi_declarations;
        std::vector<semantic::sv::CovergroupInstance> covergroup_instances;
    };

    struct SystemVerilogConstraintHirView {
        std::span<const semantic::sv::Unit> units;
        std::span<const semantic::sv::Declaration> declarations;
        std::span<const semantic::sv::TypeDefinition> types;
        std::span<const semantic::sv::Expression> expressions;
        std::span<const semantic::sv::Statement> statements;
        std::span<const semantic::sv::Process> processes;
        std::span<const semantic::sv::ClassDeclaration> classes;
        std::span<const semantic::sv::Instance> instances;
        std::span<const semantic::sv::UdpDeclaration> udps;
        std::span<const semantic::sv::DpiDeclaration> dpi_declarations;
        std::span<const semantic::sv::CovergroupInstance>
            covergroup_instances;
    };

    struct VhdlHirState {
        std::vector<semantic::vhdl::Unit> units;
        std::vector<semantic::vhdl::Declaration> declarations;
        std::vector<semantic::vhdl::TypeDefinition> types;
        std::vector<semantic::vhdl::OverloadSet> overload_sets;
        std::vector<semantic::vhdl::Expression> expressions;
        std::vector<semantic::vhdl::Statement> statements;
        std::vector<semantic::vhdl::Process> processes;
        std::vector<semantic::vhdl::Instance> instances;
    };

    struct VhdlHirView {
        std::span<const semantic::vhdl::Unit> units;
        std::span<const semantic::vhdl::Declaration> declarations;
        std::span<const semantic::vhdl::TypeDefinition> types;
        std::span<const semantic::vhdl::OverloadSet> overload_sets;
        std::span<const semantic::vhdl::Expression> expressions;
        std::span<const semantic::vhdl::Statement> statements;
        std::span<const semantic::vhdl::Process> processes;
        std::span<const semantic::vhdl::Instance> instances;
    };

    struct CompiledHirBundleState {
        std::string semantic_state;
        std::string systemverilog_hir_state;
        std::string vhdl_hir_state;
        std::vector<semantic::CompiledDependency> dependencies;
        std::vector<semantic::CompiledReference> references;
    };

    template <typename T>
    struct IsVector : std::false_type { };
    template <typename T, typename Allocator>
    struct IsVector<std::vector<T, Allocator>> : std::true_type { };
    template <typename T>
    struct IsVector<support::RareVector<T>> : std::true_type { };

    template <typename T>
    struct IsSpan : std::false_type { };
    template <typename T, std::size_t Extent>
    struct IsSpan<std::span<T, Extent>> : std::true_type { };

    template <typename T>
    struct IsOptional : std::false_type { };
    template <typename T>
    struct IsOptional<std::optional<T>> : std::true_type { };
    template <typename T>
    struct IsOptional<support::RareOptional<T>> : std::true_type { };

    template <typename T>
    struct IsVariant : std::false_type { };
    template <typename... Values>
    struct IsVariant<std::variant<Values...>> : std::true_type { };

    template <typename T>
    struct IsPair : std::false_type { };
    template <typename First, typename Second>
    struct IsPair<std::pair<First, Second>> : std::true_type { };

    template <typename T>
    struct IsArray : std::false_type { };
    template <typename Value, std::size_t Size>
    struct IsArray<std::array<Value, Size>> : std::true_type { };

    template <typename T>
    struct IsSharedPtr : std::false_type { };
    template <typename T>
    struct IsSharedPtr<std::shared_ptr<T>> : std::true_type { };

    template <typename T>
    constexpr bool valid_archive_enum(const T value) noexcept
    {
        if constexpr (std::same_as<T, semantic::CompiledReferenceKind>) {
            return value >= semantic::CompiledReferenceKind::package
                && value <= semantic::CompiledReferenceKind::context;
        } else if constexpr (std::same_as<T, frontend::SystemVerilogScalarKind>) {
            return value >= frontend::SystemVerilogScalarKind::None
                && value <= frontend::SystemVerilogScalarKind::Chandle;
        } else if constexpr (
            std::same_as<T, frontend::SystemVerilogDecimalLiteralKind>) {
            return value >= frontend::SystemVerilogDecimalLiteralKind::Real
                && value <= frontend::SystemVerilogDecimalLiteralKind::Time;
        } else if constexpr (std::same_as<T, frontend::PackedAggregateKind>) {
            return value >= frontend::PackedAggregateKind::None
                && value <= frontend::PackedAggregateKind::TaggedUnion;
        } else if constexpr (
            std::same_as<T, frontend::VhdlUnspecifiedTypeClass>) {
            return value >= frontend::VhdlUnspecifiedTypeClass::None
                && value <= frontend::VhdlUnspecifiedTypeClass::File;
        } else if constexpr (
            std::same_as<T, frontend::VhdlPredefinedSubtypeAttribute>) {
            return value >= frontend::VhdlPredefinedSubtypeAttribute::None
                && value
                    <= frontend::VhdlPredefinedSubtypeAttribute::DesignatedSubtype;
        } else if constexpr (
            std::same_as<T, frontend::VhdlModeViewElementKind>) {
            return value >= frontend::VhdlModeViewElementKind::direction
                && value <= frontend::VhdlModeViewElementKind::array_view;
        } else if constexpr (
            std::same_as<T, frontend::VhdlModeViewIndicationKind>) {
            return value >= frontend::VhdlModeViewIndicationKind::record
                && value <= frontend::VhdlModeViewIndicationKind::array;
        } else if constexpr (
            std::same_as<T, frontend::SystemVerilogModportMemberKind>) {
            return value >= frontend::SystemVerilogModportMemberKind::Signal
                && value <= frontend::SystemVerilogModportMemberKind::Clocking;
        } else if constexpr (
            std::same_as<T, semantic::sv::ModportMemberKind>) {
            return value >= semantic::sv::ModportMemberKind::signal
                && value <= semantic::sv::ModportMemberKind::clocking;
        } else if constexpr (std::same_as<T, semantic::sv::UnitKind>) {
            return value >= semantic::sv::UnitKind::module
                && value <= semantic::sv::UnitKind::compilation_unit;
        } else if constexpr (std::same_as<T, semantic::sv::ActualKind>) {
            return value >= semantic::sv::ActualKind::expression
                && value <= semantic::sv::ActualKind::open;
        } else if constexpr (
            std::same_as<T, semantic::sv::ConfigurationRuleKind>) {
            return value >= semantic::sv::ConfigurationRuleKind::instance
                && value <= semantic::sv::ConfigurationRuleKind::cell;
        } else if constexpr (
            std::same_as<T, semantic::sv::ConfigurationSelectionKind>) {
            return value >= semantic::sv::ConfigurationSelectionKind::use
                && value
                    <= semantic::sv::ConfigurationSelectionKind::liblist;
        } else if constexpr (std::same_as<T, semantic::sv::ClassLifetime>) {
            return value >= semantic::sv::ClassLifetime::inherited
                && value <= semantic::sv::ClassLifetime::automatic;
        } else if constexpr (std::same_as<T, semantic::sv::ClassMethodKind>) {
            return value >= semantic::sv::ClassMethodKind::constructor
                && value <= semantic::sv::ClassMethodKind::task;
        } else if constexpr (
            std::same_as<T, semantic::sv::UnconnectedDrive>) {
            return value >= semantic::sv::UnconnectedDrive::none
                && value <= semantic::sv::UnconnectedDrive::pull_one;
        } else if constexpr (std::same_as<T, semantic::sv::UdpLevel>) {
            return value >= semantic::sv::UdpLevel::zero
                && value <= semantic::sv::UdpLevel::binary;
        } else if constexpr (std::same_as<T, semantic::sv::UdpEdge>) {
            return value >= semantic::sv::UdpEdge::none
                && value <= semantic::sv::UdpEdge::explicit_edge;
        } else if constexpr (std::same_as<T, semantic::sv::UdpOutput>) {
            return value >= semantic::sv::UdpOutput::zero
                && value <= semantic::sv::UdpOutput::no_change;
        } else if constexpr (std::same_as<T, semantic::sv::DpiDirection>) {
            return value >= semantic::sv::DpiDirection::import
                && value <= semantic::sv::DpiDirection::export_declaration;
        } else if constexpr (std::same_as<T, semantic::sv::DpiOwnerKind>) {
            return value >= semantic::sv::DpiOwnerKind::compilation_unit
                && value <= semantic::sv::DpiOwnerKind::design_unit;
        } else if constexpr (std::same_as<T, semantic::sv::DpiQualifier>) {
            return value >= semantic::sv::DpiQualifier::none
                && value <= semantic::sv::DpiQualifier::context;
        } else if constexpr (std::same_as<T, semantic::sv::DpiCallableKind>) {
            return value >= semantic::sv::DpiCallableKind::function
                && value <= semantic::sv::DpiCallableKind::task;
        } else if constexpr (
            std::same_as<T, semantic::sv::AssertionDeclarationKind>) {
            return value >= semantic::sv::AssertionDeclarationKind::sequence
                && value <= semantic::sv::AssertionDeclarationKind::checker;
        } else if constexpr (
            std::same_as<T, semantic::sv::AssertionFormalKind>) {
            return value >= semantic::sv::AssertionFormalKind::value
                && value <= semantic::sv::AssertionFormalKind::untyped;
        } else if constexpr (
            std::same_as<T, semantic::sv::AssertionReferenceKind>) {
            return value >= semantic::sv::AssertionReferenceKind::formal
                && value <= semantic::sv::AssertionReferenceKind::package;
        } else if constexpr (
            std::same_as<T, semantic::sv::SequenceRepetitionKind>) {
            return value >= semantic::sv::SequenceRepetitionKind::none
                && value
                    <= semantic::sv::SequenceRepetitionKind::goto_repetition;
        } else if constexpr (
            std::same_as<T, semantic::sv::SequenceBinaryKind>) {
            return value >= semantic::sv::SequenceBinaryKind::throughout
                && value <= semantic::sv::SequenceBinaryKind::within;
        } else if constexpr (
            std::same_as<T, semantic::sv::SequenceEndpointKind>) {
            return value >= semantic::sv::SequenceEndpointKind::matched
                && value <= semantic::sv::SequenceEndpointKind::triggered;
        } else if constexpr (
            std::same_as<T, semantic::sv::PropertyImplicationKind>) {
            return value >= semantic::sv::PropertyImplicationKind::overlapped
                && value
                    <= semantic::sv::PropertyImplicationKind::nonoverlapped;
        } else if constexpr (
            std::same_as<T, semantic::sv::PropertyUntilKind>) {
            return value >= semantic::sv::PropertyUntilKind::until
                && value <= semantic::sv::PropertyUntilKind::strong_until_with;
        } else if constexpr (
            std::same_as<T, semantic::sv::PropertyNexttimeKind>) {
            return value >= semantic::sv::PropertyNexttimeKind::nexttime
                && value <= semantic::sv::PropertyNexttimeKind::strong_nexttime;
        } else if constexpr (
            std::same_as<T, semantic::sv::PropertyRecurrenceKind>) {
            return value >= semantic::sv::PropertyRecurrenceKind::always
                && value
                    <= semantic::sv::PropertyRecurrenceKind::strong_eventually;
        } else if constexpr (
            std::same_as<T,
                semantic::sv::PropertySequenceStrengthKind>) {
            return value
                    >= semantic::sv::PropertySequenceStrengthKind::strong
                && value <= semantic::sv::PropertySequenceStrengthKind::weak;
        } else if constexpr (
            std::same_as<T, semantic::sv::PropertyAbortOutcome>) {
            return value >= semantic::sv::PropertyAbortOutcome::vacuous_success
                && value <= semantic::sv::PropertyAbortOutcome::failure;
        } else if constexpr (
            std::same_as<T, semantic::sv::CovergroupOwnerKind>) {
            return value >= semantic::sv::CovergroupOwnerKind::design_unit
                && value
                    <= semantic::sv::CovergroupOwnerKind::class_declaration;
        } else if constexpr (
            std::same_as<T, semantic::sv::CovergroupSamplingKind>) {
            return value >= semantic::sv::CovergroupSamplingKind::event
                && value
                    <= semantic::sv::CovergroupSamplingKind::with_function_sample;
        } else if constexpr (
            std::same_as<T, semantic::sv::CovergroupOptionScope>) {
            return value >= semantic::sv::CovergroupOptionScope::instance
                && value <= semantic::sv::CovergroupOptionScope::type;
        } else if constexpr (
            std::same_as<T, semantic::sv::CoverageItemKind>) {
            return value >= semantic::sv::CoverageItemKind::coverpoint
                && value <= semantic::sv::CoverageItemKind::cross;
        } else if constexpr (
            std::same_as<T, semantic::sv::CoverageReferenceKind>) {
            return value
                    >= semantic::sv::CoverageReferenceKind::constructor_formal
                && value <= semantic::sv::CoverageReferenceKind::qualified;
        } else if constexpr (
            std::same_as<T, semantic::sv::CoverageBinKind>) {
            return value >= semantic::sv::CoverageBinKind::regular
                && value <= semantic::sv::CoverageBinKind::illegal;
        } else if constexpr (
            std::same_as<T, semantic::sv::CoverageBinSelection>) {
            return value
                    >= semantic::sv::CoverageBinSelection::explicit_selection
                && value
                    <= semantic::sv::CoverageBinSelection::default_sequence;
        } else if constexpr (
            std::same_as<T,
                semantic::sv::CoverageTransitionRepetitionKind>) {
            return value
                    >= semantic::sv::CoverageTransitionRepetitionKind::none
                && value <= semantic::sv::CoverageTransitionRepetitionKind::nonconsecutive;
        } else if constexpr (
            std::same_as<T, semantic::sv::CoverageScalarKind>) {
            return value >= semantic::sv::CoverageScalarKind::none
                && value <= semantic::sv::CoverageScalarKind::chandle;
        } else if constexpr (std::same_as<T, semantic::sv::OutputFormat>) {
            return value >= semantic::sv::OutputFormat::binary
                && value <= semantic::sv::OutputFormat::unformatted4;
        } else if constexpr (std::same_as<T, semantic::sv::TypeForm>) {
            return value >= semantic::sv::TypeForm::unresolved
                && value <= semantic::sv::TypeForm::unpacked_union;
        } else if constexpr (
            std::same_as<T, runtime::simir::ContainerOrderingOperator>) {
            return value >= runtime::simir::ContainerOrderingOperator::reverse
                && value <= runtime::simir::ContainerOrderingOperator::shuffle;
        } else if constexpr (
            std::same_as<T, runtime::simir::FileTextTargetKind>) {
            return value >= runtime::simir::FileTextTargetKind::string_register
                && value <= runtime::simir::FileTextTargetKind::packed_signal;
        } else if constexpr (
            std::same_as<T, runtime::simir::InputScanFormat>) {
            return value >= runtime::simir::InputScanFormat::binary
                && value <= runtime::simir::InputScanFormat::unformatted4;
        } else if constexpr (std::same_as<T, runtime::simir::OutputFormat>) {
            return value >= runtime::simir::OutputFormat::binary
                && value <= runtime::simir::OutputFormat::unformatted4;
        } else if constexpr (
            std::same_as<T, runtime::simir::VhdlReflectionClass>) {
            return value >= runtime::simir::VhdlReflectionClass::enumeration
                && value
                    <= runtime::simir::VhdlReflectionClass::protected_type;
        } else if constexpr (
            std::same_as<T, runtime::simir::VhdlReflectionApiKind>) {
            return value
                    >= runtime::simir::VhdlReflectionApiKind::create_subtype
                && value
                    <= runtime::simir::VhdlReflectionApiKind::file_open_kind;
        } else if constexpr (
            std::same_as<T, runtime::simir::CoverageSampleTrigger>) {
            return value >= runtime::simir::CoverageSampleTrigger::explicit_sample
                && value <= runtime::simir::CoverageSampleTrigger::event;
        } else if constexpr (
            std::same_as<T, runtime::CodeCoverageMetric>) {
            return value >= runtime::CodeCoverageMetric::Statement
                && value <= runtime::CodeCoverageMetric::Line;
        } else if constexpr (
            std::same_as<T,
                runtime::simir::CoverageDatabaseControlKind>) {
            return value
                >= runtime::simir::CoverageDatabaseControlKind::set_name
                && value
                <= runtime::simir::CoverageDatabaseControlKind::load;
        } else if constexpr (
            std::same_as<T,
                runtime::simir::SystemVerilogCoverageAccessKind>) {
            return value
                >= runtime::simir::SystemVerilogCoverageAccessKind::get
                && value
                <= runtime::simir::SystemVerilogCoverageAccessKind::save;
        } else if constexpr (
            std::same_as<T, runtime::SystemVerilogConstraintDomainKind>) {
            return value >= runtime::SystemVerilogConstraintDomainKind::BitVector
                && value
                <= runtime::SystemVerilogConstraintDomainKind::Enumeration;
        } else if constexpr (
            std::same_as<T, runtime::SystemVerilogConstraintTemplateKind>) {
            return value >= runtime::SystemVerilogConstraintTemplateKind::Name
                && value
                <= runtime::SystemVerilogConstraintTemplateKind::Unique;
        } else if constexpr (std::same_as<T, semantic::vhdl::UnitKind>) {
            return value >= semantic::vhdl::UnitKind::entity
                && value <= semantic::vhdl::UnitKind::psl_verification_unit;
        } else if constexpr (std::same_as<T, semantic::vhdl::Direction>) {
            return value >= semantic::vhdl::Direction::unknown
                && value <= semantic::vhdl::Direction::buffer;
        } else if constexpr (std::same_as<T, semantic::vhdl::ObjectClass>) {
            return value >= semantic::vhdl::ObjectClass::constant
                && value <= semantic::vhdl::ObjectClass::file;
        } else if constexpr (std::same_as<T, semantic::vhdl::DeclarationForm>) {
            return value >= semantic::vhdl::DeclarationForm::type
                && value <= semantic::vhdl::DeclarationForm::mode_view;
        } else if constexpr (std::same_as<T, semantic::vhdl::TypeForm>) {
            return value >= semantic::vhdl::TypeForm::unresolved
                && value <= semantic::vhdl::TypeForm::alias;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::UnspecifiedTypeClass>) {
            return value >= semantic::vhdl::UnspecifiedTypeClass::none
                && value <= semantic::vhdl::UnspecifiedTypeClass::file;
        } else if constexpr (std::same_as<T, semantic::vhdl::RangeKind>) {
            return value >= semantic::vhdl::RangeKind::integer
                && value <= semantic::vhdl::RangeKind::array_index;
        } else if constexpr (std::same_as<T, semantic::vhdl::ContextKind>) {
            return value >= semantic::vhdl::ContextKind::library_clause
                && value <= semantic::vhdl::ContextKind::context_reference;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PslVerificationUnitKind>) {
            return value >= semantic::vhdl::PslVerificationUnitKind::unit
                && value <= semantic::vhdl::PslVerificationUnitKind::mode;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PslDeclarationKind>) {
            return value >= semantic::vhdl::PslDeclarationKind::default_clock
                && value <= semantic::vhdl::PslDeclarationKind::endpoint;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PslDirectiveKind>) {
            return value >= semantic::vhdl::PslDirectiveKind::assert_directive
                && value <= semantic::vhdl::PslDirectiveKind::cover;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PslExpressionClass>) {
            return value >= semantic::vhdl::PslExpressionClass::invalid
                && value <= semantic::vhdl::PslExpressionClass::static_integer;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PslTemporalOperatorKind>) {
            return value
                >= semantic::vhdl::PslTemporalOperatorKind::sequence_concatenation
                && value <= semantic::vhdl::PslTemporalOperatorKind::within;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PslUnknownPolicy>) {
            return value == semantic::vhdl::PslUnknownPolicy::false_value;
        } else if constexpr (std::same_as<T, semantic::vhdl::AssociationKind>) {
            return value >= semantic::vhdl::AssociationKind::expression
                && value <= semantic::vhdl::AssociationKind::default_box;
        } else if constexpr (std::same_as<T, semantic::vhdl::BindingKind>) {
            return value >= semantic::vhdl::BindingKind::entity
                && value <= semantic::vhdl::BindingKind::open;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::InstanceSelection>) {
            return value >= semantic::vhdl::InstanceSelection::labels
                && value <= semantic::vhdl::InstanceSelection::others;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::DisconnectionSelection>) {
            return value
                    >= semantic::vhdl::DisconnectionSelection::explicit_names
                && value <= semantic::vhdl::DisconnectionSelection::others;
        } else if constexpr (std::same_as<T, semantic::vhdl::GenerateKind>) {
            return value >= semantic::vhdl::GenerateKind::block
                && value <= semantic::vhdl::GenerateKind::selection;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::PredefinedAttribute>) {
            return value >= semantic::vhdl::PredefinedAttribute::left
                && value <= semantic::vhdl::PredefinedAttribute::reflect;
        } else if constexpr (std::same_as<T, semantic::vhdl::ExpressionKind>) {
            return value >= semantic::vhdl::ExpressionKind::invalid
                && value <= semantic::vhdl::ExpressionKind::conditional;
        } else if constexpr (std::same_as<T, semantic::vhdl::StatementKind>) {
            return value >= semantic::vhdl::StatementKind::signal_assignment
                && value <= semantic::vhdl::StatementKind::null_statement;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::ModeViewElementForm>) {
            return value >= semantic::vhdl::ModeViewElementForm::direction
                && value <= semantic::vhdl::ModeViewElementForm::array_view;
        } else if constexpr (
            std::same_as<T, semantic::vhdl::ModeViewCompositionState>) {
            return value
                    >= semantic::vhdl::ModeViewCompositionState::uncomposed
                && value
                    <= semantic::vhdl::ModeViewCompositionState::recursive;
        } else if constexpr (std::same_as<T, semantic::vhdl::DelayMechanism>) {
            return value >= semantic::vhdl::DelayMechanism::implicit_inertial
                && value <= semantic::vhdl::DelayMechanism::transport;
        } else {
            return true;
        }
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::PackedMemberMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.name, value.domain, value.spelling, value.packed_range,
            value.is_signed, value.lsb_offset, value.span,
            value.nested_types);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::VhdlArrayDimensionMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.index_subtype, value.index_span, value.index_base_range,
            value.range, value.null, value.stride, value.unconstrained);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::VhdlArrayMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.index_subtype, value.index_span, value.index_base_range,
            value.element_spelling, value.element_named_type,
            value.element_span, value.element_domain, value.unconstrained,
            value.flat_width, value.dimensions, value.element_types);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::VhdlAccessMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.designated_types, value.designated_span,
            value.handle_width, value.maximum_objects, value.nullable,
            value.owns_designated_object,
            value.deallocate_releases_storage,
            value.reclaim_when_unreachable, value.simulation_lifetime);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::VhdlPhysicalUnitMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(value.name, value.scale_factor, value.span);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::VhdlPhysicalMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(value.units, value.resolved_range);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, elaboration::PackedTypeMetadata>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.domain, value.spelling, value.systemverilog_scalar,
            value.systemverilog_net_type,
            value.systemverilog_resolution_function, value.packed_range,
            value.is_signed, value.named_type, value.named_type_span,
            value.integer_range, value.integer_base_range,
            value.vhdl_integer_storage_width, value.nominal_type,
            value.vhdl_type_declaration, value.vhdl_resolution_function,
            value.enumeration_literals, value.enumeration_range,
            value.enumeration_base_range,
            value.vhdl_array, value.vhdl_access, value.vhdl_physical,
            value.packed_members, value.packed_aggregate);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, runtime::simir::ContainerValue>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.type,
            value.elements,
            value.string_elements,
            value.nested_elements,
            value.keys,
            value.string_keys);
    }

    template <typename T>
        requires std::same_as<
            std::remove_cv_t<T>, runtime::simir::Signal>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.name, value.initial_value, value.resolution, value.value_kind,
            value.implicit_driver, value.implicit_drive_strength,
            value.charge_strength, value.charge_decay,
            value.systemverilog_scalar, value.event_variable);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileOpen>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.destination, value.path, value.mode, value.status, value.vhdl);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileClose>
    auto archive_fields(T& value)
    {
        return std::tie(value.handle, value.clear_handle, value.ignore_zero);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileEndOfFile>
    auto archive_fields(T& value)
    {
        return std::tie(value.destination, value.handle, value.lookahead);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileScan>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.destination, value.handle, value.source, value.string_source,
            value.conversions, value.trailing_text, value.require_assignments,
            value.success, value.consume_string_source);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::WaitFor>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.delay, value.source, value.source_width, value.source_kind,
            value.source_signed, value.rounding_quantum);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::WaitRegion>
    auto archive_fields(T& value)
    {
        return std::tie(value.phase);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::WaitOn>
    auto archive_fields(T& value)
    {
        return std::tie(
            value.signals, value.edges, value.timeout, value.timeout_result,
            value.timeout_origin);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::WaitOrder>
    auto archive_fields(T& value)
    {
        return std::tie(value.events, value.result);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::EventTriggered>
    auto archive_fields(T& value)
    {
        return std::tie(value.destination, value.event);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::EventAlias>
    auto archive_fields(T& value)
    {
        return std::tie(value.target, value.source, value.has_source);
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, runtime::simir::DebugPoint>
    auto archive_fields(T& value)
    {
        return std::tie(value.kind, value.source, value.scope);
    }

    template <typename T>
    concept DenseId = requires(T value, std::uint32_t index) {
        { value.valid() } -> std::same_as<bool>;
        { value.value() } -> std::same_as<std::uint32_t>;
        { T::from_index(index) } -> std::same_as<T>;
    };

    template <typename T>
    concept StorageWrapper = requires(T value) {
        value.storage;
    } && IsVariant<std::remove_cvref_t<decltype(std::declval<T>().storage)>>::value;

    class Writer;
    class Reader;

    void write_operation(Writer& writer, const runtime::simir::Operation& operation);
    bool read_operation(Reader& reader, runtime::simir::Operation& operation);

    void write_operation_group(Writer& writer, const runtime::simir::ValueOperationGroup& group);
    void write_operation_group(Writer& writer, const runtime::simir::SignalOperationGroup& group);
    void write_operation_group(Writer& writer, const runtime::simir::StringOperationGroup& group);
    void write_operation_group(
        Writer& writer, const runtime::simir::ContainerOperationGroup& group);
    void write_operation_group(Writer& writer, const runtime::simir::FileOperationGroup& group);
    void write_operation_group(
        Writer& writer, const runtime::simir::SchedulingOperationGroup& group);
    void write_operation_group(Writer& writer, const runtime::simir::ControlOperationGroup& group);
    void write_operation_group(Writer& writer, const runtime::simir::OutputOperationGroup& group);
    void write_operation_group(Writer& writer, const runtime::simir::ClassOperationGroup& group);

    bool read_operation_group(Reader& reader, runtime::simir::ValueOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::SignalOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::StringOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::ContainerOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::FileOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::SchedulingOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::ControlOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::OutputOperationGroup& group);
    bool read_operation_group(Reader& reader, runtime::simir::ClassOperationGroup& group);

    // Keep the initial allocation bounded: the hint is intended to eliminate
    // geometric string growth for the common artifact payload, not to reserve
    // an arbitrarily large amount of address space for a malformed model.
    constexpr std::size_t kMaximumSerializationReserve = 8U * 1024U * 1024U;

    [[nodiscard]] constexpr std::size_t add_serialization_capacity(
        const std::size_t left, const std::size_t right) noexcept
    {
        if (left >= kMaximumSerializationReserve
            || right >= kMaximumSerializationReserve - left) {
            return kMaximumSerializationReserve;
        }
        return left + right;
    }

    [[nodiscard]] constexpr std::size_t multiply_serialization_capacity(
        const std::size_t count, const std::size_t width) noexcept
    {
        if (count != 0U
            && width >= kMaximumSerializationReserve / count) {
            return kMaximumSerializationReserve;
        }
        return count * width;
    }

    template <typename T>
    [[nodiscard]] std::size_t serialization_capacity_hint(const T& value)
    {
        using Value = std::remove_cvref_t<T>;
        if constexpr (std::same_as<Value, bool>) {
            return 1U;
        } else if constexpr (std::is_enum_v<Value>) {
            return sizeof(std::underlying_type_t<Value>);
        } else if constexpr (std::is_integral_v<Value>) {
            return sizeof(Value);
        } else if constexpr (
            std::same_as<Value, runtime::simir::InternedString>
            || std::same_as<Value, frontend::SourceName>) {
            return add_serialization_capacity(sizeof(std::uint64_t),
                std::min(value.str().size(), kMaximumSerializationReserve));
        } else if constexpr (std::same_as<Value, std::string>) {
            return add_serialization_capacity(sizeof(std::uint64_t),
                std::min(value.size(), kMaximumSerializationReserve));
        } else if constexpr (DenseId<Value>) {
            return sizeof(value.value());
        } else if constexpr (requires {
            typename Value::rare_optional_value_type;
        }) {
            return add_serialization_capacity(1U,
                value ? serialization_capacity_hint(*value) : 0U);
        } else if constexpr (
            IsVector<Value>::value || IsSpan<Value>::value) {
            return add_serialization_capacity(sizeof(std::uint64_t),
                multiply_serialization_capacity(
                    value.size(), sizeof(typename Value::value_type)));
        } else if constexpr (IsOptional<Value>::value) {
            return add_serialization_capacity(1U,
                value ? serialization_capacity_hint(*value) : 0U);
        } else if constexpr (IsVariant<Value>::value) {
            return add_serialization_capacity(sizeof(std::uint64_t),
                std::visit([](const auto& item) {
                    return serialization_capacity_hint(item);
                }, value));
        } else if constexpr (IsPair<Value>::value) {
            return add_serialization_capacity(
                serialization_capacity_hint(value.first),
                serialization_capacity_hint(value.second));
        } else if constexpr (IsArray<Value>::value) {
            std::size_t result { };
            for (const auto& item : value) {
                result = add_serialization_capacity(
                    result, serialization_capacity_hint(item));
            }
            return result;
        } else if constexpr (IsSharedPtr<Value>::value) {
            return add_serialization_capacity(1U, sizeof(Value));
        } else if constexpr (StorageWrapper<Value>) {
            return serialization_capacity_hint(value.storage);
        } else if constexpr (requires { archive_fields(value); }) {
            std::size_t result { };
            std::apply([&](const auto&... fields) {
                ((result = add_serialization_capacity(
                      result, serialization_capacity_hint(fields))), ...);
            }, archive_fields(value));
            return result;
        } else if constexpr (std::is_aggregate_v<Value>) {
            std::size_t result { };
            boost::pfr::for_each_field(value, [&](const auto& field) {
                result = add_serialization_capacity(
                    result, serialization_capacity_hint(field));
            });
            return result;
        } else {
            return std::min(sizeof(Value), kMaximumSerializationReserve);
        }
    }

    class Writer {
    public:
        Writer() = default;
        explicit Writer(const std::size_t output_capacity)
        {
            bytes_.reserve(output_capacity);
        }
        Writer(std::ostream* output, support::Sha256* checksum)
            : output_(output)
            , checksum_(checksum)
        {
        }

        void raw(std::string_view bytes)
        {
            if (output_ == nullptr && checksum_ == nullptr) {
                bytes_.append(bytes);
                return;
            }
            while (!bytes.empty() && failure_.empty()) {
                const auto available = buffer_.size() - buffered_;
                const auto count = std::min(available, bytes.size());
                std::ranges::copy_n(bytes.begin(),
                    static_cast<std::ptrdiff_t>(count),
                    buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
                buffered_ += count;
                bytes.remove_prefix(count);
                if (buffered_ == buffer_.size()) {
                    flush();
                }
            }
        }

        void byte(const char value)
        {
            if (output_ == nullptr && checksum_ == nullptr) {
                bytes_.push_back(value);
                return;
            }
            if (!failure_.empty()) {
                return;
            }
            buffer_[buffered_++] = value;
            if (buffered_ == buffer_.size()) {
                flush();
            }
        }

        void u64(const std::uint64_t value)
        {
            std::array<char, sizeof(value)> bytes { };
            for (unsigned shift = 0; shift < 64; shift += 8) {
                bytes[shift / 8U]
                    = static_cast<char>((value >> shift) & 0xffU);
            }
            raw(std::string_view { bytes.data(), bytes.size() });
        }

        template <typename T>
        void write(const T& value)
        {
            if (!failure_.empty()) {
                return;
            }
            if (++depth_ > kMaximumNesting) {
                --depth_;
                failure_ = "design state exceeds the safe structural nesting depth";
                return;
            }
            using Value = std::remove_cv_t<T>;
            if constexpr (std::same_as<Value, bool>) {
                byte(value ? '\1' : '\0');
            } else if constexpr (std::is_enum_v<Value>) {
                if (!valid_archive_enum(value)) {
                    failure_ = "design state contains an invalid scalar enumeration";
                    --depth_;
                    return;
                }
                write(static_cast<std::underlying_type_t<Value>>(value));
            } else if constexpr (std::is_integral_v<Value>) {
                // Integral fields dominate elaborated runtime payloads.  Their
                // C++ types already define the portable artifact width, so do
                // not inflate every uint8/uint32 field to eight bytes.  Keep
                // the byte order explicit and retain u64 for sequence/string
                // lengths, whose archive width is intentionally fixed.
                using Unsigned = std::make_unsigned_t<Value>;
                const auto encoded = std::bit_cast<Unsigned>(value);
                std::array<char, sizeof(Value)> bytes { };
                for (unsigned shift = 0;
                     shift < sizeof(Value) * 8U; shift += 8U) {
                    bytes[shift / 8U] = static_cast<char>(
                        (encoded >> shift) & static_cast<Unsigned>(0xffU));
                }
                raw(std::string_view { bytes.data(), bytes.size() });
            } else if constexpr (
                std::same_as<Value, runtime::simir::InternedString>) {
                write(value.str());
            } else if constexpr (
                std::same_as<Value, frontend::SourceName>) {
                write(value.str());
            } else if constexpr (std::same_as<Value, std::string>) {
                u64(value.size());
                raw(value);
            } else if constexpr (std::same_as<Value, std::filesystem::path>) {
                write(value.generic_string());
            } else if constexpr (std::same_as<Value, runtime::PackedLogic4>) {
                write(value.is_logic9());
                write(value.to_msb_string());
            } else if constexpr (std::same_as<Value, runtime::simir::Operation>) {
                write_operation(*this, value);
            } else if constexpr (
                std::same_as<Value, runtime::simir::OperationList>) {
                u64(value.size());
                for (std::size_t index = 0; index < value.size(); ++index) {
                    write(value.expanded(index));
                }
            } else if constexpr (
                std::same_as<Value,
                    runtime::simir::ExpressionProfileList>) {
                u64(value.size());
                for (const auto& profile : value) {
                    write(profile);
                }
            } else if constexpr (DenseId<Value>) {
                write(value.value());
            } else if constexpr (requires {
                typename Value::rare_optional_value_type;
            }) {
                write(value.has_value());
                if (value) {
                    write(*value);
                }
            } else if constexpr (
                IsVector<Value>::value || IsSpan<Value>::value) {
                u64(value.size());
                for (const auto& item : value) {
                    write(item);
                }
            } else if constexpr (IsOptional<Value>::value) {
                write(value.has_value());
                if (value) {
                    write(*value);
                }
            } else if constexpr (IsVariant<Value>::value) {
                u64(value.index());
                std::visit([&](const auto& item) { write(item); }, value);
            } else if constexpr (IsPair<Value>::value) {
                write(value.first);
                write(value.second);
            } else if constexpr (IsArray<Value>::value) {
                for (const auto& item : value) {
                    write(item);
                }
            } else if constexpr (IsSharedPtr<Value>::value) {
                write(static_cast<bool>(value));
                if (value) {
                    write(*value);
                }
            } else if constexpr (StorageWrapper<Value>) {
                write(value.storage);
            } else if constexpr (requires { archive_fields(value); }) {
                std::apply(
                    [&](const auto&... fields) { (write(fields), ...); },
                    archive_fields(value));
            } else if constexpr (std::is_aggregate_v<Value>) {
                boost::pfr::for_each_field(
                    value, [&](const auto& field) { write(field); });
            } else {
                static_assert(sizeof(Value) == 0, "unsupported design-state field");
            }
            --depth_;
        }

        std::string finish() && { return std::move(bytes_); }
        bool complete()
        {
            flush();
            return failure_.empty();
        }
        const std::string& failure() const noexcept { return failure_; }

    private:
        void flush()
        {
            if (buffered_ == 0 || !failure_.empty()) {
                return;
            }
            const std::string_view bytes { buffer_.data(), buffered_ };
            if (checksum_ != nullptr) {
                checksum_->update(bytes);
            }
            if (output_ != nullptr) {
                output_->write(bytes.data(),
                    static_cast<std::streamsize>(bytes.size()));
                if (!*output_) {
                    failure_ = "could not write design state";
                }
            }
            buffered_ = 0;
        }

        std::string bytes_;
        std::ostream* output_ { };
        support::Sha256* checksum_ { };
        std::array<char, 64 * 1024> buffer_ { };
        std::size_t buffered_ { };
        std::size_t depth_ { };
        std::string failure_;
    };

    class Reader {
    public:
        explicit Reader(
            const std::string_view bytes, DecodeBudget* const budget = nullptr)
            : bytes_(bytes)
            , remaining_(bytes.size())
            , budget_(budget)
        {
        }

        Reader(std::istream& input, const std::uint64_t size,
            DecodeBudget* const budget = nullptr)
            : input_(&input)
            , remaining_(size)
            , budget_(budget)
        {
        }

        bool raw(const std::string_view expected)
        {
            if (remaining() < expected.size()) {
                return fail("design state has an invalid magic header");
            }
            for (const auto expected_byte : expected) {
                unsigned char actual { };
                if (!read_byte(actual)
                    || actual != static_cast<unsigned char>(expected_byte)) {
                    return fail("design state has an invalid magic header");
                }
            }
            return true;
        }

        bool u64(std::uint64_t& value)
        {
            if (remaining() < 8) {
                return fail("design state is truncated");
            }
            std::array<char, sizeof(value)> bytes { };
            if (!read_exact(bytes.data(), bytes.size())) {
                return false;
            }
            value = 0;
            for (unsigned shift = 0; shift < 64; shift += 8) {
                const auto byte = static_cast<unsigned char>(bytes[shift / 8U]);
                value |= static_cast<std::uint64_t>(byte) << shift;
            }
            return true;
        }

        template <std::size_t Index = 0, typename... Values>
        bool read_variant(
            std::variant<Values...>& value,
            const std::size_t selected)
        {
            if constexpr (Index == sizeof...(Values)) {
                return fail("design state contains an invalid variant alternative");
            } else {
                if (selected == Index) {
                    value.template emplace<Index>();
                    return read(std::get<Index>(value));
                }
                return read_variant<Index + 1>(value, selected);
            }
        }

        template <typename T>
        bool read(T& value)
        {
            using Value = std::remove_cv_t<T>;
            if (!enter()) {
                return false;
            }
            const bool result = [&]() {
                if constexpr (std::same_as<Value, bool>) {
                    unsigned char byte { };
                    if (!read_byte(byte) || byte > 1) {
                        return fail("design state contains an invalid boolean");
                    }
                    value = byte != 0;
                    return true;
                } else if constexpr (std::is_enum_v<Value>) {
                    std::underlying_type_t<Value> decoded { };
                    if (!read(decoded)) {
                        return false;
                    }
                    value = static_cast<Value>(decoded);
                    return valid_archive_enum(value)
                        || fail("design state contains an invalid scalar enumeration");
                } else if constexpr (std::is_integral_v<Value>) {
                    if (remaining() < sizeof(Value)) {
                        return fail("design state is truncated");
                    }
                    using Unsigned = std::make_unsigned_t<Value>;
                    static_assert(sizeof(Value) <= sizeof(std::uint64_t));
                    std::array<char, sizeof(Value)> bytes { };
                    if (!read_exact(bytes.data(), bytes.size())) {
                        return false;
                    }
                    std::uint64_t encoded { };
                    for (unsigned shift = 0;
                         shift < sizeof(Value) * 8U; shift += 8U) {
                        const auto byte = static_cast<unsigned char>(
                            bytes[shift / 8U]);
                        encoded |= static_cast<std::uint64_t>(byte) << shift;
                    }
                    value = std::bit_cast<Value>(
                        static_cast<Unsigned>(encoded));
                    return true;
                } else if constexpr (
                    std::same_as<Value, runtime::simir::InternedString>) {
                    std::string decoded;
                    if (!read(decoded)) {
                        return false;
                    }
                    value = std::move(decoded);
                    return true;
                } else if constexpr (
                    std::same_as<Value, frontend::SourceName>) {
                    std::string decoded;
                    if (!read(decoded)) {
                        return false;
                    }
                    value = std::move(decoded);
                    return true;
                } else if constexpr (std::same_as<Value, std::string>) {
                    std::uint64_t size { };
                    if (!u64(size) || size > remaining()
                        || !consume_allocation(size, sizeof(char))) {
                        return fail("design state string exceeds the payload");
                    }
                    value.resize(static_cast<std::size_t>(size));
                    return read_exact(value.data(), value.size());
                } else if constexpr (std::same_as<Value, std::filesystem::path>) {
                    std::string spelling;
                    if (!read(spelling)) {
                        return false;
                    }
                    if (!consume_allocation(spelling.size(), sizeof(char))) {
                        return false;
                    }
                    value = std::filesystem::path { spelling };
                    return true;
                } else if constexpr (std::same_as<Value, runtime::PackedLogic4>) {
                    bool logic9 { };
                    std::string spelling;
                    if (!read(logic9) || !read(spelling)) {
                        return false;
                    }
                    constexpr std::size_t bits_per_word
                        = std::numeric_limits<std::uint64_t>::digits;
                    const auto words = spelling.size() / bits_per_word
                        + (spelling.size() % bits_per_word != 0U ? 1U : 0U);
                    if (spelling.size() > bits_per_word
                        && !consume_allocation(
                            words, (logic9 ? 4U : 2U)
                                * sizeof(std::uint64_t))) {
                        return false;
                    }
                    value = logic9
                        ? runtime::PackedLogic4::from_logic9_msb_string(spelling)
                        : runtime::PackedLogic4::from_msb_string(spelling);
                    return true;
                } else if constexpr (std::same_as<Value, runtime::simir::Operation>) {
                    return read_operation(*this, value);
                } else if constexpr (
                    std::same_as<Value, runtime::simir::OperationList>) {
                    std::uint64_t size { };
                    auto operations = std::move(operation_scratch_);
                    operations.clear();
                    if (!u64(size)
                        || size > static_cast<std::uint64_t>(remaining()) + 1U
                        || size > operations.max_size()
                        || !consume_allocation(size,
                            sizeof(runtime::simir::Operation))) {
                        return fail("design state vector exceeds the payload");
                    }
                    operations.reserve(static_cast<std::size_t>(size));
                    for (std::uint64_t index = 0; index < size; ++index) {
                        operations.emplace_back();
                        if (!read(operations.back())) {
                            return false;
                        }
                    }
                    value = std::move(operations);
                    return true;
                } else if constexpr (
                    std::same_as<Value,
                        runtime::simir::ExpressionProfileList>) {
                    std::uint64_t size { };
                    runtime::simir::ExpressionProfileList::Storage profiles;
                    if (!u64(size)
                        || size > static_cast<std::uint64_t>(remaining()) + 1U
                        || size > profiles.max_size()
                        || !consume_allocation(size,
                            sizeof(runtime::simir::ExpressionProfile))) {
                        return fail("design state vector exceeds the payload");
                    }
                    profiles.reserve(static_cast<std::size_t>(size));
                    for (std::uint64_t index = 0; index < size; ++index) {
                        profiles.emplace_back();
                        if (!read(profiles.back())) {
                            return false;
                        }
                    }
                    value = runtime::simir::ExpressionProfileList {
                        std::move(profiles) };
                    return true;
                } else if constexpr (DenseId<Value>) {
                    std::uint32_t index { };
                    if (!read(index)) {
                        return false;
                    }
                    value = Value::from_index(index);
                    return true;
                } else if constexpr (requires {
                    typename Value::rare_optional_value_type;
                }) {
                    bool present { };
                    if (!read(present)) {
                        return false;
                    }
                    if (!present) {
                        value.reset();
                        return true;
                    }
                    typename Value::rare_optional_value_type decoded { };
                    if (!read(decoded)) {
                        return false;
                    }
                    value = decoded;
                    return true;
                } else if constexpr (IsVector<Value>::value) {
                    std::uint64_t size { };
                    if (!u64(size) || size > static_cast<std::uint64_t>(remaining()) + 1U
                        || size > value.max_size()
                        || !consume_allocation(
                            size, sizeof(typename Value::value_type))) {
                        return fail("design state vector exceeds the payload");
                    }
                    value.clear();
                    value.reserve(static_cast<std::size_t>(size));
                    for (std::uint64_t index = 0; index < size; ++index) {
                        value.emplace_back();
                        if (!read(value.back())) {
                            return false;
                        }
                        if constexpr (std::same_as<
                                          typename Value::value_type,
                                          runtime::simir::Process>) {
                            canonicalize_process(value.back());
                        }
                    }
                    if constexpr (std::same_as<
                                      typename Value::value_type,
                                      runtime::simir::Signal>) {
                        signals_ = &value;
                    }
                    return true;
                } else if constexpr (IsOptional<Value>::value) {
                    bool present { };
                    if (!read(present)) {
                        return false;
                    }
                    if (!present) {
                        value.reset();
                        return true;
                    }
                    value.emplace();
                    return read(*value);
                } else if constexpr (IsVariant<Value>::value) {
                    std::uint64_t selected { };
                    if (!u64(selected)) {
                        return false;
                    }
                    if (selected >= std::variant_size_v<Value>) {
                        return invalid_variant();
                    }
                    return read_variant(
                        value, static_cast<std::size_t>(selected));
                } else if constexpr (IsPair<Value>::value) {
                    return read(value.first) && read(value.second);
                } else if constexpr (IsArray<Value>::value) {
                    for (auto& item : value) {
                        if (!read(item)) {
                            return false;
                        }
                    }
                    return true;
                } else if constexpr (IsSharedPtr<Value>::value) {
                    bool present { };
                    if (!read(present)) {
                        return false;
                    }
                    if (!present) {
                        value.reset();
                        return true;
                    }
                    if (!consume_allocation(1U,
                            sizeof(typename Value::element_type)
                                + 2U * sizeof(void*))) {
                        return false;
                    }
                    value = std::make_shared<typename Value::element_type>();
                    return read(*value);
                } else if constexpr (StorageWrapper<Value>) {
                    return read(value.storage);
                } else if constexpr (requires { archive_fields(value); }) {
                    bool ok = true;
                    std::apply(
                        [&](auto&... fields) { ((ok = ok && read(fields)), ...); },
                        archive_fields(value));
                    return ok;
                } else if constexpr (std::is_aggregate_v<Value>) {
                    bool ok = true;
                    boost::pfr::for_each_field(
                        value, [&](auto& field) { ok = ok && read(field); });
                    return ok;
                } else {
                    static_assert(sizeof(Value) == 0, "unsupported design-state field");
                }
            }();
            leave();
            return result;
        }

        std::size_t remaining() const noexcept
        {
            return static_cast<std::size_t>(remaining_);
        }
        const std::string& failure() const noexcept { return failure_; }
        bool invalid_variant()
        {
            return fail("design state contains an invalid variant alternative");
        }

    private:
        void canonicalize_process(runtime::simir::Process& process)
        {
            if (signals_ == nullptr
                || !runtime::simir::process_operations_shareable(process)) {
                return;
            }
            std::uint64_t bucket = UINT64_C(1469598103934665603);
            const auto mix = [&](const std::uint64_t value) {
                bucket ^= value;
                bucket *= UINT64_C(1099511628211);
            };
            mix(process.operations.size());
            mix(process.register_count);
            mix(process.string_register_count);
            mix(process.container_register_count);
            for (const auto kind : process.register_value_kinds) {
                mix(static_cast<std::uint64_t>(kind));
            }
            for (const auto& operation : process.operations) {
                mix(runtime::simir::operation_group_index(operation));
                mix(runtime::simir::operation_alternative_index(operation));
            }
            auto& representatives = process_bodies_[bucket];
            for (const auto* representative : representatives) {
                if (runtime::simir::share_process_operations(
                        *representative, process, *signals_,
                        &operation_scratch_)) {
                    return;
                }
            }
            representatives.push_back(&process);
        }

        bool enter()
        {
            if (++depth_ > kMaximumNesting) {
                --depth_;
                return fail("design state exceeds the safe structural nesting depth");
            }
            return true;
        }
        void leave() noexcept { --depth_; }
        bool fail(std::string message)
        {
            if (failure_.empty()) {
                failure_ = std::move(message);
            }
            return false;
        }

        bool consume_allocation(
            const std::uint64_t count, const std::size_t element_size)
        {
            if (budget_ == nullptr || budget_->consume(count, element_size)) {
                return true;
            }
            return fail(
                "design state exceeds the aggregate decode allocation budget");
        }

        bool read_byte(unsigned char& value)
        {
            char byte { };
            if (!read_exact(&byte, 1)) {
                return false;
            }
            value = static_cast<unsigned char>(byte);
            return true;
        }

        bool read_exact(char* destination, std::size_t size)
        {
            if (size > remaining_) {
                return fail("design state is truncated");
            }
            if (input_ == nullptr) {
                std::ranges::copy_n(
                    bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                    static_cast<std::ptrdiff_t>(size), destination);
                position_ += size;
                remaining_ -= size;
                return true;
            }
            while (size > 0) {
                if (buffer_position_ == buffer_size_) {
                    const auto request = static_cast<std::streamsize>(
                        std::min<std::uint64_t>(buffer_.size(), remaining_));
                    input_->read(buffer_.data(), request);
                    buffer_size_ = static_cast<std::size_t>(input_->gcount());
                    buffer_position_ = 0;
                    if (buffer_size_ == 0) {
                        return fail("design state is truncated");
                    }
                }
                const auto count = std::min(
                    size, buffer_size_ - buffer_position_);
                std::ranges::copy_n(
                    buffer_.begin()
                        + static_cast<std::ptrdiff_t>(buffer_position_),
                    static_cast<std::ptrdiff_t>(count), destination);
                destination += count;
                size -= count;
                buffer_position_ += count;
                remaining_ -= count;
            }
            return true;
        }

        std::string_view bytes_;
        std::istream* input_ { };
        std::uint64_t remaining_ { };
        std::size_t position_ { };
        std::array<char, 64 * 1024> buffer_ { };
        std::size_t buffer_position_ { };
        std::size_t buffer_size_ { };
        std::size_t depth_ { };
        std::string failure_;
        DecodeBudget* budget_ { };
        const std::vector<runtime::simir::Signal>* signals_ { };
        std::unordered_map<std::uint64_t,
            std::vector<const runtime::simir::Process*>> process_bodies_;
        runtime::simir::OperationList::Storage operation_scratch_;
    };

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_DESIGN_IR)
    struct DesignIrRecords {
        std::string top;
        std::vector<std::string> roots;
        std::vector<semantic::design::Specialization> specializations;
        std::vector<semantic::design::InstanceOccurrence> instances;
        std::vector<semantic::design::Object> objects;
        std::vector<semantic::design::Port> ports;
        std::vector<semantic::design::ProcessOccurrence> processes;
        std::vector<semantic::design::Sensitivity> sensitivities;
        std::vector<semantic::design::Driver> drivers;
        std::vector<semantic::design::Transaction> transactions;
        std::vector<semantic::design::Conversion> conversions;
        std::vector<semantic::design::Boundary> boundaries;
    };
#endif

    template <typename Value>
    std::optional<std::string> serialize(
        const std::string_view magic,
        const std::uint32_t schema,
        const Value& value,
        diagnostic::Engine& diagnostics)
    {
        const auto output_capacity = add_serialization_capacity(
            add_serialization_capacity(magic.size(), sizeof(schema)),
            serialization_capacity_hint(value));
        Writer writer { output_capacity };
        writer.raw(magic);
        writer.write(schema);
        writer.write(value);
        if (!writer.failure().empty()) {
            diagnostics.error(std::string { kCode }, writer.failure());
            return std::nullopt;
        }
        return std::move(writer).finish();
    }

    template <typename Value>
    std::optional<std::string> serialized_checksum(
        const std::string_view magic,
        const std::uint32_t schema,
        const Value& value,
        diagnostic::Engine& diagnostics)
    {
        support::Sha256 checksum;
        Writer writer { nullptr, &checksum };
        writer.raw(magic);
        writer.write(schema);
        writer.write(value);
        if (!writer.complete()) {
            diagnostics.error(std::string { kCode }, writer.failure());
            return std::nullopt;
        }
        return support::Sha256::hex(checksum.finish());
    }

    template <typename Value>
    bool serialize_to_stream(
        const std::string_view magic,
        const std::uint32_t schema,
        const Value& value,
        std::ostream& output,
        diagnostic::Engine& diagnostics)
    {
        Writer writer { &output, nullptr };
        writer.raw(magic);
        writer.write(schema);
        writer.write(value);
        if (!writer.complete()) {
            diagnostics.error(std::string { kCode }, writer.failure());
            return false;
        }
        return true;
    }

    template <typename Value>
    std::optional<Value> deserialize(
        const std::string_view magic,
        const std::uint32_t expected_schema,
        const std::string_view bytes,
        std::string source_name,
        diagnostic::Engine& diagnostics,
        DecodeBudget* const budget = nullptr,
        const std::string_view regeneration_artifact = ".fsimdesign")
    {
        Reader reader { bytes, budget };
        std::uint32_t schema { };
        Value value;
        if (!reader.raw(magic) || !reader.read(schema)
            || schema != expected_schema || !reader.read(value)
            || reader.remaining() != 0) {
            auto message = reader.failure();
            if (message.empty() && schema != expected_schema) {
                message = diagnostic::unsupported_artifact_identity(
                    "design state " + std::string { magic },
                    "schema " + std::to_string(schema),
                    "schema " + std::to_string(expected_schema),
                    regeneration_artifact);
            } else if (message.empty()) {
                message = "design state contains trailing bytes";
            }
            diagnostics.error(
                std::string { kCode }, std::move(message),
                { std::move(source_name), { 1, 1, 0 }, { 1, 1, 0 } });
            return std::nullopt;
        }
        return value;
    }

    template <typename Value>
    std::optional<Value> deserialize(
        const std::string_view magic,
        const std::uint32_t expected_schema,
        std::istream& input,
        const std::uint64_t size,
        std::string source_name,
        diagnostic::Engine& diagnostics,
        DecodeBudget* const budget = nullptr,
        const std::string_view regeneration_artifact = ".fsimdesign")
    {
        Reader reader { input, size, budget };
        std::uint32_t schema { };
        Value value;
        if (!reader.raw(magic) || !reader.read(schema)
            || schema != expected_schema || !reader.read(value)
            || reader.remaining() != 0) {
            auto message = reader.failure();
            if (message.empty() && schema != expected_schema) {
                message = diagnostic::unsupported_artifact_identity(
                    "design state " + std::string { magic },
                    "schema " + std::to_string(schema),
                    "schema " + std::to_string(expected_schema),
                    regeneration_artifact);
            } else if (message.empty()) {
                message = "design state contains trailing bytes";
            }
            diagnostics.error(
                std::string { kCode }, std::move(message),
                { std::move(source_name), { 1, 1, 0 }, { 1, 1, 0 } });
            return std::nullopt;
        }
        return value;
    }

    std::optional<semantic::Model> deserialize_semantic_state_with_budget(
        std::string_view bytes,
        std::string source_name,
        diagnostic::Engine& diagnostics,
        DecodeBudget& budget);
    std::optional<semantic::sv::Hir>
    deserialize_systemverilog_constraint_hir_state_with_budget(
        std::string_view bytes,
        std::string source_name,
        const semantic::Model& semantics,
        diagnostic::Engine& diagnostics,
        DecodeBudget& budget);
    std::optional<semantic::vhdl::Hir> deserialize_vhdl_hir_state_with_budget(
        std::string_view bytes,
        std::string source_name,
        const semantic::Model& semantics,
        diagnostic::Engine& diagnostics,
        DecodeBudget& budget);
    std::optional<semantic::CompiledDesign>
    deserialize_compiled_hir_bundle_with_budget(
        std::string_view bytes,
        const std::string& source_name,
        diagnostic::Engine& diagnostics,
        DecodeBudget& budget);

} // namespace codec_detail
} // namespace fsim::app
