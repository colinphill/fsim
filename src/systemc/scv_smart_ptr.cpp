// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_smart_ptr.hpp"

#include <scv.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace fsim::systemc::scv_native_detail {

std::atomic_size_t live_payloads { };

struct Lifetime {
    Lifetime() { live_payloads.fetch_add(1U, std::memory_order_relaxed); }
    Lifetime(const Lifetime&)
    {
        live_payloads.fetch_add(1U, std::memory_order_relaxed);
    }
    ~Lifetime() { live_payloads.fetch_sub(1U, std::memory_order_relaxed); }
};

struct Scalar {
    Lifetime lifetime;
    std::int64_t value { };
};

struct Nested {
    std::int32_t flag { };
    std::uint64_t count { };
};

struct Aggregate {
    Lifetime lifetime;
    std::int32_t scalar { };
    Nested nested;
    std::int32_t values[4] { };
};

enum NativeMode {
    idle = 0,
    running = 1,
    stopped = 2,
};

struct Introspection {
    Lifetime lifetime;
    bool enabled { true };
    NativeMode mode { running };
    std::string label { "introspection-payload" };
    Nested nested { -7, 19U };
    std::int32_t samples[3] { 4, -5, 6 };
    sc_dt::sc_biguint<257> wide;
    sc_dt::sc_lv<193> logic;

    Introspection()
        : wide(0)
        , logic(sc_dt::Log_0)
    {
        wide[256] = true;
        wide[129] = true;
        wide[0] = true;
        logic[192] = sc_dt::Log_1;
        logic[129] = sc_dt::Log_X;
        logic[64] = sc_dt::Log_Z;
        logic[0] = sc_dt::Log_1;
    }
};

} // namespace fsim::systemc::scv_native_detail

SCV_ENUM_EXTENSIONS(fsim::systemc::scv_native_detail::NativeMode) {
    public :
        SCV_ENUM_CTOR(fsim::systemc::scv_native_detail::NativeMode) {
            SCV_ENUM(fsim::systemc::scv_native_detail::idle);
SCV_ENUM(fsim::systemc::scv_native_detail::running);
SCV_ENUM(fsim::systemc::scv_native_detail::stopped);
}
}
;

SCV_EXTENSIONS(fsim::systemc::scv_native_detail::Scalar)
{
public:
    scv_extensions<std::int64_t> value;
    SCV_EXTENSIONS_CTOR(fsim::systemc::scv_native_detail::Scalar)
    {
        SCV_FIELD(value);
    }
};

SCV_EXTENSIONS(fsim::systemc::scv_native_detail::Nested)
{
public:
    scv_extensions<std::int32_t> flag;
    scv_extensions<std::uint64_t> count;
    SCV_EXTENSIONS_CTOR(fsim::systemc::scv_native_detail::Nested)
    {
        SCV_FIELD(flag);
        SCV_FIELD(count);
    }
};

SCV_EXTENSIONS(fsim::systemc::scv_native_detail::Aggregate)
{
public:
    scv_extensions<std::int32_t> scalar;
    scv_extensions<fsim::systemc::scv_native_detail::Nested> nested;
    scv_extensions<std::int32_t[4]> values;
    SCV_EXTENSIONS_CTOR(fsim::systemc::scv_native_detail::Aggregate)
    {
        SCV_FIELD(scalar);
        SCV_FIELD(nested);
        SCV_FIELD(values);
    }
};

SCV_EXTENSIONS(fsim::systemc::scv_native_detail::Introspection)
{
public:
    scv_extensions<bool> enabled;
    scv_extensions<fsim::systemc::scv_native_detail::NativeMode> mode;
    scv_extensions<std::string> label;
    scv_extensions<fsim::systemc::scv_native_detail::Nested> nested;
    scv_extensions<std::int32_t[3]> samples;
    scv_extensions<sc_dt::sc_biguint<257>> wide;
    scv_extensions<sc_dt::sc_lv<193>> logic;
    SCV_EXTENSIONS_CTOR(fsim::systemc::scv_native_detail::Introspection)
    {
        SCV_FIELD(enabled);
        SCV_FIELD(mode);
        SCV_FIELD(label);
        SCV_FIELD(nested);
        SCV_FIELD(samples);
        SCV_FIELD(wide);
        SCV_FIELD(logic);
    }
};

namespace fsim::systemc {
namespace {

    bool valid_limits(
        const ScvNativeSmartPtrLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_handles == 0U
            || limits.max_handles > std::numeric_limits<std::uint32_t>::max()
            || limits.max_name_bytes == 0U
            || limits.max_name_bytes
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_extension_depth == 0U
            || limits.max_extension_depth
                > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error("FSIM-SCV-P003",
                "native SCV smart-pointer resource limits are inconsistent");
            return false;
        }
        return true;
    }

    bool valid_name(
        const std::string_view name,
        const ScvNativeSmartPtrLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        const auto invalid = std::ranges::find_if(name, [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7fU || value == '\\';
        });
        if (name.empty() || name.size() > limits.max_name_bytes
            || invalid != name.end() || name.front() == ' '
            || name.back() == ' ') {
            diagnostics.error("FSIM-SCV-P001",
                "native SCV smart-pointer name must be bounded canonical text");
            return false;
        }
        return true;
    }

    ScvNativeExtensionKind extension_kind(const scv_extensions_if& extension)
    {
        switch (extension.get_type()) {
        case scv_extensions_if::BOOLEAN:
            return ScvNativeExtensionKind::boolean;
        case scv_extensions_if::INTEGER:
            return ScvNativeExtensionKind::signed_integer;
        case scv_extensions_if::UNSIGNED:
            return ScvNativeExtensionKind::unsigned_integer;
        case scv_extensions_if::RECORD:
            return ScvNativeExtensionKind::record;
        case scv_extensions_if::ARRAY:
            return ScvNativeExtensionKind::array;
        case scv_extensions_if::ENUMERATION:
            return ScvNativeExtensionKind::enumeration;
        case scv_extensions_if::STRING:
            return ScvNativeExtensionKind::string;
        case scv_extensions_if::BIT_VECTOR:
            return ScvNativeExtensionKind::bit_vector;
        case scv_extensions_if::LOGIC_VECTOR:
            return ScvNativeExtensionKind::logic_vector;
        case scv_extensions_if::FLOATING_POINT_NUMBER:
            return ScvNativeExtensionKind::floating_point;
        default:
            return ScvNativeExtensionKind::unsupported;
        }
    }

    std::optional<ScvNativeExtensionInfo> describe(
        scv_extensions_if& extension)
    {
        ScvNativeExtensionInfo result;
        result.kind = extension_kind(extension);
        result.name = extension.get_short_name();
        result.type_name = extension.get_type_name();
        result.bit_width = extension.get_bitwidth() > 0
            ? static_cast<std::size_t>(extension.get_bitwidth())
            : 0U;
        result.randomization_enabled = extension.is_randomization_enabled();
        result.signed_type = extension.is_integer();
        if (extension.is_record()) {
            result.children = static_cast<std::size_t>(extension.get_num_fields());
        } else if (extension.is_array()) {
            result.children = static_cast<std::size_t>(extension.get_array_size());
        } else if (extension.is_bool()) {
            result.unsigned_value = extension.get_bool() ? 1U : 0U;
        } else if ((extension.is_integer() || extension.is_unsigned())
            && result.bit_width > 64U) {
            const auto width = result.bit_width;
            result.aval_words.assign((width + 63U) / 64U, 0U);
            result.bval_words.assign((width + 63U) / 64U, 0U);
            sc_dt::sc_bv_base value { static_cast<int>(width) };
            extension.get_value(value);
            for (std::size_t index = 0; index < width; ++index) {
                if (value[static_cast<int>(index)].to_bool()) {
                    result.aval_words[index / 64U] |= UINT64_C(1)
                        << (index % 64U);
                }
            }
        } else if (extension.is_integer()) {
            result.signed_value = extension.get_integer();
        } else if (extension.is_unsigned()) {
            result.unsigned_value = extension.get_unsigned();
        } else if (extension.is_enum()) {
            result.enum_value = extension.get_integer();
            result.enum_name = extension.get_enum_string(
                static_cast<int>(*result.enum_value));
        } else if (extension.is_string()) {
            result.string_value = extension.get_string();
        } else if (extension.is_bit_vector() || extension.is_logic_vector()) {
            const auto width = result.bit_width;
            result.aval_words.assign((width + 63U) / 64U, 0U);
            result.bval_words.assign((width + 63U) / 64U, 0U);
            if (extension.is_bit_vector()) {
                sc_dt::sc_bv_base value { static_cast<int>(width) };
                extension.get_value(value);
                for (std::size_t index = 0; index < width; ++index) {
                    if (value[static_cast<int>(index)].to_bool()) {
                        result.aval_words[index / 64U] |= UINT64_C(1)
                            << (index % 64U);
                    }
                }
            } else {
                sc_dt::sc_lv_base value { static_cast<int>(width) };
                extension.get_value(value);
                for (std::size_t index = 0; index < width; ++index) {
                    const auto state = value[static_cast<int>(index)].value();
                    const auto mask = UINT64_C(1) << (index % 64U);
                    if (state == sc_dt::Log_1 || state == sc_dt::Log_X) {
                        result.aval_words[index / 64U] |= mask;
                    }
                    if (state == sc_dt::Log_X || state == sc_dt::Log_Z) {
                        result.bval_words[index / 64U] |= mask;
                    }
                }
            }
        }
        return result;
    }

    class NativeOwner {
    public:
        virtual ~NativeOwner() = default;
        [[nodiscard]] virtual std::unique_ptr<NativeOwner> copy_alias() const = 0;
        [[nodiscard]] virtual bool assign_alias(const NativeOwner& source) = 0;
        [[nodiscard]] virtual scv_extensions_if& root() = 0;
        [[nodiscard]] virtual const scv_extensions_if& root() const = 0;
        [[nodiscard]] virtual ScvNativeValueKind kind() const noexcept = 0;
        [[nodiscard]] virtual std::string name() const = 0;
    };

    template <typename Value, ScvNativeValueKind Kind>
    class TypedNativeOwner final : public NativeOwner {
    public:
        TypedNativeOwner(const std::string& name, const std::uint64_t seed)
            : value_(name)
        {
            if constexpr (Kind == ScvNativeValueKind::introspection) {
                std::vector<scv_extensions_if*> pending {
                    value_.get_extensions_ptr()
                };
                while (!pending.empty()) {
                    auto* extension = pending.back();
                    pending.pop_back();
                    extension->disable_randomization();
                    if (extension->is_record()) {
                        for (int index = 0; index < extension->get_num_fields();
                            ++index) {
                            pending.push_back(extension->get_field(
                                static_cast<unsigned>(index)));
                        }
                    } else if (extension->is_array()) {
                        for (int index = 0; index < extension->get_array_size();
                            ++index) {
                            pending.push_back(extension->get_array_elt(index));
                        }
                    }
                }
                auto* root = value_.get_extensions_ptr();
                root->get_field(0U)->assign(true);
                root->get_field(1U)->assign(
                    static_cast<int>(scv_native_detail::running));
                root->get_field(2U)->assign(
                    std::string { "introspection-payload" });
                root->get_field(3U)->get_field(0U)->assign(-7);
                root->get_field(3U)->get_field(1U)->assign(
                    static_cast<unsigned long long>(19U));
                root->get_field(4U)->get_array_elt(0)->assign(4);
                root->get_field(4U)->get_array_elt(1)->assign(-5);
                root->get_field(4U)->get_array_elt(2)->assign(6);
                sc_dt::sc_bv_base wide { 257 };
                wide = 0;
                wide[256] = true;
                wide[129] = true;
                wide[0] = true;
                root->get_field(5U)->assign(wide);
                sc_dt::sc_lv_base logic { 193 };
                logic = sc_dt::Log_0;
                logic[192] = sc_dt::Log_1;
                logic[129] = sc_dt::Log_X;
                logic[64] = sc_dt::Log_Z;
                logic[0] = sc_dt::Log_1;
                root->get_field(6U)->assign(logic);
            } else {
                scv_shared_ptr<scv_random> random {
                    new scv_random(name.c_str(), seed)
                };
                value_->set_random(random);
            }
        }

        TypedNativeOwner(const TypedNativeOwner& source)
            : value_(source.value_)
        {
        }

        std::unique_ptr<NativeOwner> copy_alias() const override
        {
            return std::make_unique<TypedNativeOwner>(*this);
        }

        bool assign_alias(const NativeOwner& source) override
        {
            const auto* typed = dynamic_cast<const TypedNativeOwner*>(&source);
            if (!typed) {
                return false;
            }
            value_ = typed->value_;
            return true;
        }

        scv_extensions_if& root() override { return *value_; }
        const scv_extensions_if& root() const override { return *value_; }
        ScvNativeValueKind kind() const noexcept override { return Kind; }
        std::string name() const override { return value_.get_name(); }

    private:
        scv_smart_ptr<Value> value_;
    };

    scv_extensions_if* select_extension(
        scv_extensions_if& root,
        const std::span<const ScvNativeExtensionStep> path,
        const ScvNativeSmartPtrLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (path.size() > limits.max_extension_depth) {
            diagnostics.error(
                "FSIM-SCV-P003", "native SCV extension path exceeds its limit");
            return nullptr;
        }
        auto* selected = &root;
        for (const auto step : path) {
            if (step.kind == ScvNativeExtensionStepKind::field
                && selected->is_record()
                && step.index
                    < static_cast<std::size_t>(selected->get_num_fields())) {
                selected = selected->get_field(static_cast<unsigned>(step.index));
            } else if (step.kind == ScvNativeExtensionStepKind::element
                && selected->is_array()
                && step.index
                    < static_cast<std::size_t>(selected->get_array_size())) {
                selected = selected->get_array_elt(static_cast<int>(step.index));
            } else {
                diagnostics.error("FSIM-SCV-P002",
                    "native SCV extension path has an invalid step or index");
                return nullptr;
            }
        }
        return selected;
    }

} // namespace

struct ScvNativeSmartPtrRegistry::Impl {
    struct Entry {
        std::uint64_t generation { 1U };
        ScvObjectId object;
        std::unique_ptr<NativeOwner> owner;
    };

    explicit Impl(ScvNativeSmartPtrLimits selected_limits)
        : limits(selected_limits)
    {
        entries.emplace_back();
    }

    [[nodiscard]] Entry* find(
        const ScvNativeSmartPtrHandle handle,
        diagnostic::Engine& diagnostics)
    {
        if (!handle.valid() || handle.slot >= entries.size()
            || entries[handle.slot].generation != handle.generation
            || !entries[handle.slot].owner) {
            diagnostics.error(
                "FSIM-SCV-P001", "native SCV smart-pointer handle is stale");
            return nullptr;
        }
        return &entries[handle.slot];
    }

    [[nodiscard]] const Entry* find(
        const ScvNativeSmartPtrHandle handle,
        diagnostic::Engine& diagnostics) const
    {
        return const_cast<Impl*>(this)->find(handle, diagnostics);
    }

    [[nodiscard]] std::optional<ScvNativeSmartPtrHandle> insert(
        ScvObjectId object,
        std::unique_ptr<NativeOwner> owner,
        diagnostic::Engine& diagnostics)
    {
        if (live >= limits.max_handles) {
            diagnostics.error(
                "FSIM-SCV-P003", "native SCV smart-pointer handle limit reached");
            return std::nullopt;
        }
        for (std::size_t index = 1U; index < entries.size(); ++index) {
            if (!entries[index].owner) {
                entries[index].object = object;
                entries[index].owner = std::move(owner);
                ++live;
                return ScvNativeSmartPtrHandle {
                    static_cast<std::uint64_t>(index), entries[index].generation
                };
            }
        }
        Entry entry;
        entry.object = object;
        entry.owner = std::move(owner);
        entries.push_back(std::move(entry));
        ++live;
        return ScvNativeSmartPtrHandle {
            static_cast<std::uint64_t>(entries.size() - 1U), 1U
        };
    }

    ScvNativeSmartPtrLimits limits;
    std::vector<Entry> entries;
    std::size_t live { };
};

ScvNativeSmartPtrRegistry::ScvNativeSmartPtrRegistry(
    ScvNativeSmartPtrLimits limits)
    : impl_(std::make_unique<Impl>(limits))
{
}

ScvNativeSmartPtrRegistry::~ScvNativeSmartPtrRegistry() = default;
ScvNativeSmartPtrRegistry::ScvNativeSmartPtrRegistry(
    ScvNativeSmartPtrRegistry&&) noexcept = default;
ScvNativeSmartPtrRegistry& ScvNativeSmartPtrRegistry::operator=(
    ScvNativeSmartPtrRegistry&&) noexcept = default;

std::optional<ScvNativeSmartPtrHandle> ScvNativeSmartPtrRegistry::create(
    const ScvNativeValueKind kind,
    const ScvObjectId object,
    std::string name,
    const std::uint64_t random_seed,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(impl_->limits, diagnostics)
        || !valid_name(name, impl_->limits, diagnostics) || !object.valid()) {
        if (!object.valid()) {
            diagnostics.error(
                "FSIM-SCV-P001", "native SCV smart pointer needs an object ID");
        }
        return std::nullopt;
    }
    std::unique_ptr<NativeOwner> owner;
    switch (kind) {
    case ScvNativeValueKind::scalar:
        owner = std::make_unique<TypedNativeOwner<scv_native_detail::Scalar,
            ScvNativeValueKind::scalar>>(name, random_seed);
        break;
    case ScvNativeValueKind::aggregate:
        owner = std::make_unique<TypedNativeOwner<scv_native_detail::Aggregate,
            ScvNativeValueKind::aggregate>>(name, random_seed);
        break;
    case ScvNativeValueKind::introspection:
        owner = std::make_unique<TypedNativeOwner<
            scv_native_detail::Introspection,
            ScvNativeValueKind::introspection>>(name, random_seed);
        break;
    default:
        diagnostics.error(
            "FSIM-SCV-P001", "native SCV smart-pointer kind is unsupported");
        return std::nullopt;
    }
    return impl_->insert(object, std::move(owner), diagnostics);
}

std::optional<ScvNativeSmartPtrHandle> ScvNativeSmartPtrRegistry::copy(
    const ScvNativeSmartPtrHandle source,
    diagnostic::Engine& diagnostics)
{
    const auto* entry = impl_->find(source, diagnostics);
    if (!entry) {
        return std::nullopt;
    }
    return impl_->insert(
        entry->object, entry->owner->copy_alias(), diagnostics);
}

bool ScvNativeSmartPtrRegistry::assign(
    const ScvNativeSmartPtrHandle destination,
    const ScvNativeSmartPtrHandle source,
    diagnostic::Engine& diagnostics)
{
    auto* destination_entry = impl_->find(destination, diagnostics);
    const auto* source_entry = impl_->find(source, diagnostics);
    if (!destination_entry || !source_entry) {
        return false;
    }
    if (!destination_entry->owner->assign_alias(*source_entry->owner)) {
        diagnostics.error("FSIM-SCV-P002",
            "native SCV smart-pointer assignment requires matching value kinds");
        return false;
    }
    destination_entry->object = source_entry->object;
    return true;
}

bool ScvNativeSmartPtrRegistry::release(
    const ScvNativeSmartPtrHandle handle,
    diagnostic::Engine& diagnostics)
{
    auto* entry = impl_->find(handle, diagnostics);
    if (!entry) {
        return false;
    }
    if (entry->generation == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics.error("FSIM-SCV-P003",
            "native SCV smart-pointer handle generation exhausted");
        return false;
    }
    entry->owner.reset();
    entry->object = { };
    ++entry->generation;
    --impl_->live;
    return true;
}

std::optional<ScvNativeSmartPtrInfo> ScvNativeSmartPtrRegistry::info(
    const ScvNativeSmartPtrHandle handle,
    diagnostic::Engine& diagnostics) const
{
    const auto* entry = impl_->find(handle, diagnostics);
    if (!entry) {
        return std::nullopt;
    }
    return ScvNativeSmartPtrInfo {
        handle, entry->object, entry->owner->kind(), entry->owner->name()
    };
}

std::optional<ScvNativeExtensionInfo> ScvNativeSmartPtrRegistry::extension(
    const ScvNativeSmartPtrHandle handle,
    const std::span<const ScvNativeExtensionStep> path,
    diagnostic::Engine& diagnostics) const
{
    const auto* entry = impl_->find(handle, diagnostics);
    if (!entry) {
        return std::nullopt;
    }
    auto& root = const_cast<scv_extensions_if&>(entry->owner->root());
    auto* selected = select_extension(
        root, path, impl_->limits, diagnostics);
    return selected ? describe(*selected) : std::nullopt;
}

bool ScvNativeSmartPtrRegistry::assign_signed(
    const ScvNativeSmartPtrHandle handle,
    const std::span<const ScvNativeExtensionStep> path,
    const std::int64_t value,
    diagnostic::Engine& diagnostics)
{
    auto* entry = impl_->find(handle, diagnostics);
    auto* selected = entry
        ? select_extension(entry->owner->root(), path, impl_->limits, diagnostics)
        : nullptr;
    if (!selected || !selected->is_integer()) {
        if (selected) {
            diagnostics.error("FSIM-SCV-P002",
                "native SCV signed assignment requires an integer extension");
        }
        return false;
    }
    selected->assign(static_cast<long long>(value));
    return true;
}

bool ScvNativeSmartPtrRegistry::set_randomization(
    const ScvNativeSmartPtrHandle handle,
    const std::span<const ScvNativeExtensionStep> path,
    const bool enabled,
    diagnostic::Engine& diagnostics)
{
    auto* entry = impl_->find(handle, diagnostics);
    auto* selected = entry
        ? select_extension(entry->owner->root(), path, impl_->limits, diagnostics)
        : nullptr;
    if (!selected) {
        return false;
    }
    if (enabled) {
        selected->enable_randomization();
    } else {
        selected->disable_randomization();
    }
    return true;
}

bool ScvNativeSmartPtrRegistry::randomize(
    const ScvNativeSmartPtrHandle handle,
    const std::span<const ScvNativeExtensionStep> path,
    diagnostic::Engine& diagnostics)
{
    auto* entry = impl_->find(handle, diagnostics);
    auto* selected = entry
        ? select_extension(entry->owner->root(), path, impl_->limits, diagnostics)
        : nullptr;
    if (!selected) {
        return false;
    }
    selected->next();
    return true;
}

std::size_t ScvNativeSmartPtrRegistry::live_handles() const noexcept
{
    return impl_->live;
}

std::size_t ScvNativeSmartPtrRegistry::live_native_payloads() noexcept
{
    return scv_native_detail::live_payloads.load(std::memory_order_relaxed);
}

} // namespace fsim::systemc
