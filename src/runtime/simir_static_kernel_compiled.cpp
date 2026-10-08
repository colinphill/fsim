// SPDX-License-Identifier: Apache-2.0
//
// Compiled narrow tier of the engine v4 static kernel. A member whose SimIR
// registers have statically inferable widths of at most 64 bits, and whose
// signal and memory accesses are narrow, is translated one-to-one into a
// compact register-machine program over aval/bval word pairs. Operation
// semantics are the exact word functions in simir_kernel_word_ops.hpp, fuzzed
// against the reference value functions. Everything else stays on the
// generic evaluator.
//
// This unit translates members to KIR (compile_members, compile); the
// analyses, native units and execution live in the sibling
// simir_static_kernel_*.cpp units.
#include "simir_static_kernel_compiled_internal.hpp"

#include <chrono>
#include <cxxabi.h>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <typeinfo>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;
namespace kw = kernel_word;

namespace {

/// Compiled member bodies as bytes: every integer as a LEB128 varint, field
/// by field (most are small or zero). They are read back only by the same
/// build (the cache key includes static_kernel_compiler_identity).
class BodyWriter {
public:
    explicit BodyWriter(std::string& out) : out_ { out } { }

    void put(std::uint64_t value)
    {
        while (value >= 0x80U) {
            out_.push_back(static_cast<char>((value & 0x7fU) | 0x80U));
            value >>= 7U;
        }
        out_.push_back(static_cast<char>(value));
    }
    void put_signed(const std::int64_t value)
    {
        // Zigzag: small negative values stay short.
        put((static_cast<std::uint64_t>(value) << 1U)
            ^ static_cast<std::uint64_t>(value >> 63));
    }
    template <typename T, typename F>
    void list(const std::vector<T>& values, F&& each)
    {
        put(values.size());
        for (const auto& value : values) {
            each(value);
        }
    }
    template <typename T>
    void integers(const std::vector<T>& values)
    {
        list(values, [&](const T value) { put(static_cast<std::uint64_t>(value)); });
    }
    void body(const static_kernel_detail::CompiledBody& body)
    {
        list(body.code, [&](const KInst& inst) {
            put(static_cast<std::uint64_t>(inst.op));
            put(inst.sub);
            put(inst.flags);
            put(inst.width);
            put(inst.d);
            put(inst.x);
            put(inst.y);
            put(inst.z);
            put(inst.offset);
            put(inst.aux);
            put(inst.imm_a);
            put(inst.imm_b);
        });
        const bool zero = std::ranges::all_of(body.registers,
            [](const kernel_word::Word& word) { return word.a == 0U && word.b == 0U; });
        put(body.registers.size());
        put(zero ? 1U : 0U);
        if (!zero) {
            for (const auto& word : body.registers) {
                put(word.a);
                put(word.b);
            }
        }
        integers(body.register_widths);
        list(body.indices, [&](const DynamicIndex& index) {
            put(index.index);
            put_signed(index.left);
            put_signed(index.right);
            put(index.base_offset);
            put(index.strict ? 1U : 0U);
        });
        list(body.parts, [&](const DynamicPartIndex& part) {
            put_signed(part.left);
            put_signed(part.right);
            put(part.base);
            put(part.base_offset);
            put(part.width);
            put(part.increasing ? 1U : 0U);
            put(part.source_descending ? 1U : 0U);
        });
        list(body.concat, [&](const ConcatOperand& operand) {
            put(operand.reg);
            put(operand.width);
        });
        integers(body.containers);
        put(body.entry);
        list(body.wide_registers, [&](const PackedLogic4& value) {
            put(value.is_logic9() ? 1U : 0U);
            const auto spelling = value.to_msb_string();
            put(spelling.size());
            out_.append(spelling);
        });
        integers(body.return_targets);
        integers(body.live_at_entry);
        put(body.shadow_base);
        integers(body.tracked);
        integers(body.u_mode);
        integers(body.u_operand_begin);
        integers(body.u_operands);
        put(body.call_stack_base);
        integers(body.resume_entries);
    }

private:
    std::string& out_;
};

class BodyReader {
public:
    explicit BodyReader(std::string_view bytes) : bytes_ { bytes } { }

    bool get(std::uint64_t& value)
    {
        value = 0U;
        for (unsigned shift = 0U; shift < 64U; shift += 7U) {
            if (at_ == bytes_.size()) {
                return false;
            }
            const auto byte = static_cast<unsigned char>(bytes_[at_++]);
            value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0U) {
                return true;
            }
        }
        return false;
    }
    template <typename T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    bool get(T& value)
    {
        std::uint64_t raw { };
        if (!get(raw)) {
            return false;
        }
        if constexpr (std::is_same_v<T, bool>) {
            if (raw > 1U) {
                return false;
            }
            value = raw != 0U;
        } else if constexpr (std::is_enum_v<T>) {
            using U = std::underlying_type_t<T>;
            if (raw > std::numeric_limits<U>::max()) {
                return false;
            }
            value = static_cast<T>(static_cast<U>(raw));
        } else {
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return false;
            }
            value = static_cast<T>(raw);
        }
        return true;
    }
    bool get_signed(std::int64_t& value)
    {
        std::uint64_t raw { };
        if (!get(raw)) {
            return false;
        }
        value = static_cast<std::int64_t>(raw >> 1U) ^ -static_cast<std::int64_t>(raw & 1U);
        return true;
    }
    /// A count beyond the remaining bytes is corrupt (each item takes one).
    template <typename T, typename F>
    bool list(std::vector<T>& values, F&& each)
    {
        std::uint64_t count { };
        if (!get(count) || count > bytes_.size() - at_) {
            return false;
        }
        values.resize(static_cast<std::size_t>(count));
        for (auto& value : values) {
            if (!each(value)) {
                return false;
            }
        }
        return true;
    }
    template <typename T>
    bool integers(std::vector<T>& values)
    {
        return list(values, [&](T& value) { return get(value); });
    }
    bool body(static_kernel_detail::CompiledBody& body)
    {
        std::uint64_t registers { };
        bool zero { };
        if (!list(body.code, [&](KInst& inst) {
                return get(inst.op) && get(inst.sub) && get(inst.flags)
                    && get(inst.width) && get(inst.d) && get(inst.x) && get(inst.y)
                    && get(inst.z) && get(inst.offset) && get(inst.aux)
                    && get(inst.imm_a) && get(inst.imm_b);
            })
            || !get(registers) || !get(zero)
            || registers > (std::uint64_t { 1 } << 32U)) {
            return false;
        }
        body.registers.assign(static_cast<std::size_t>(registers), { });
        if (!zero) {
            for (auto& word : body.registers) {
                if (!get(word.a) || !get(word.b)) {
                    return false;
                }
            }
        }
        return integers(body.register_widths)
            && list(body.indices, [&](DynamicIndex& index) {
                   return get(index.index) && get_signed(index.left)
                       && get_signed(index.right) && get(index.base_offset)
                       && get(index.strict);
               })
            && list(body.parts, [&](DynamicPartIndex& part) {
                   return get_signed(part.left) && get_signed(part.right)
                       && get(part.base) && get(part.base_offset) && get(part.width)
                       && get(part.increasing) && get(part.source_descending);
               })
            && list(body.concat, [&](ConcatOperand& operand) {
                   return get(operand.reg) && get(operand.width);
               })
            && integers(body.containers) && get(body.entry)
            && list(body.wide_registers, [&](PackedLogic4& value) {
                   bool logic9 { };
                   std::uint64_t size { };
                   if (!get(logic9) || !get(size) || size > bytes_.size() - at_) {
                       return false;
                   }
                   const auto spelling = bytes_.substr(at_, static_cast<std::size_t>(size));
                   at_ += static_cast<std::size_t>(size);
                   try {
                       value = logic9 ? PackedLogic4::from_logic9_msb_string(spelling)
                                      : PackedLogic4::from_msb_string(spelling);
                   } catch (const std::exception&) {
                       return false;
                   }
                   return true;
               })
            && integers(body.return_targets) && integers(body.live_at_entry)
            && get(body.shadow_base) && integers(body.tracked)
            && integers(body.u_mode) && integers(body.u_operand_begin)
            && integers(body.u_operands) && get(body.call_stack_base)
            && integers(body.resume_entries);
    }
    [[nodiscard]] bool done() const noexcept { return at_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t at_ { };
};

constexpr std::string_view bodies_magic { "fsim-static-kernel-bodies-v2" };

} // namespace

std::string static_kernel_compiler_identity()
{
    return std::string { bodies_magic } + ";" __DATE__ " " __TIME__ ";"
        + static_kernel_specializer_identity();
}

void Interpreter::Impl::StaticKernel::compile_members()
{
    std::map<std::string, std::size_t> failures;
    const char* dump = std::getenv("FSIM_KERNEL_DUMP");
    std::size_t dumped = 0U;
    // Diagnostics: compile only members whose name contains
    // FSIM_KERNEL_COMPILE_ONLY, or skip those containing
    // FSIM_KERNEL_COMPILE_SKIP.
    const char* only = std::getenv("FSIM_KERNEL_COMPILE_ONLY");
    const char* skip = std::getenv("FSIM_KERNEL_COMPILE_SKIP");
    const char* specialize_text = std::getenv("FSIM_STATIC_KERNEL_SPECIALIZE");
    const bool specialize = specialize_text == nullptr
        || std::string_view { specialize_text } != "0";
    // Members that run once (constant drivers, mostly) run on the reference
    // evaluator at start; compiling them would cost more than running them.
    const bool compile_once = std::getenv("FSIM_STATIC_KERNEL_COMPILE_ONCE") != nullptr;
    // Restored bodies, by member: (process, operation count, body).
    struct RestoredBody {
        bool present { };
        ProcessId process { };
        std::uint64_t operations { };
        std::optional<static_kernel_detail::CompiledBody> body;
    };
    std::vector<RestoredBody> restored_bodies;
    if (restored_bodies_ && restored_bodies_->starts_with(bodies_magic)) {
        BodyReader reader { std::string_view { *restored_bodies_ }.substr(
            bodies_magic.size()) };
        std::vector<RestoredBody> entries(members_.size());
        std::uint64_t count { };
        bool valid = reader.get(count) && count <= members_.size();
        for (std::uint64_t entry = 0U; valid && entry < count; ++entry) {
            std::uint32_t index { };
            bool has_body { };
            RestoredBody restored;
            valid = reader.get(index) && index < members_.size()
                && !entries[index].present && reader.get(restored.process)
                && reader.get(restored.operations) && reader.get(has_body);
            if (valid && has_body) {
                restored.body.emplace();
                valid = reader.body(*restored.body);
            }
            restored.present = true;
            if (valid) {
                entries[index] = std::move(restored);
            }
        }
        if (valid && reader.done()) {
            restored_bodies = std::move(entries);
        }
    }
    std::string recorded_bodies;
    std::uint64_t recorded_count = 0U;
    std::unordered_map<ProcessId, const StaticKernelSpecialization*> restored_by_process;
    if (restored_specializations_) {
        for (const auto& entry : restored_specializations_->members) {
            restored_by_process.emplace(entry.process, &entry);
        }
    }
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        if (!compile_once && members_[index].kind == StaticKernelMemberKind::once) {
            continue;
        }
        if (only != nullptr || skip != nullptr) {
            const auto name
                = impl_.processes.program_view(members_[index].process).name();
            if ((only != nullptr && name.find(only) == std::string::npos)
                || (skip != nullptr && name.find(skip) != std::string::npos)) {
                continue;
            }
        }
        if (specialize && members_[index].vhdl) {
            const auto specialize_start = std::chrono::steady_clock::now();
            auto& member = members_[index];
            const auto original_size = static_cast<std::uint32_t>(member.operations.size());
            const auto* restored = [&]() -> const StaticKernelSpecialization* {
                const auto found = restored_by_process.find(member.process);
                return found != restored_by_process.end()
                        && found->second->original_size == original_size
                    ? found->second : nullptr;
            }();
            bool specialized = false;
            if (restored != nullptr) {
                // As specialize_member applies a recipe.
                specialized = restored->specialized;
                if (specialized) {
                    member.origin.resize(original_size);
                    for (std::uint32_t op = 0U; op < original_size; ++op) {
                        member.origin[op] = op;
                    }
                    member.origin.insert(member.origin.end(),
                        restored->origin.begin(), restored->origin.end());
                    member.operations.insert(member.operations.end(),
                        restored->operations.begin(), restored->operations.end());
                    member.body_begin = original_size;
                }
            } else {
                specialized = specialize_member(index);
            }
            if (specialized) {
                ++specialized_members_;
                member.body_identity = nullptr;
            }
            if (recorded_specializations_) {
                auto& entry = recorded_specializations_->members.emplace_back();
                entry.process = member.process;
                entry.original_size = original_size;
                entry.specialized = specialized;
                if (specialized) {
                    entry.operations.assign(
                        member.operations.begin() + original_size,
                        member.operations.end());
                    entry.origin.assign(member.origin.begin() + original_size,
                        member.origin.end());
                }
            }
            specialize_seconds_ += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - specialize_start).count();
        }
        try {
            if (index < restored_bodies.size() && restored_bodies[index].present
                && restored_bodies[index].process == members_[index].process
                && restored_bodies[index].operations
                    == members_[index].operations.size()) {
                members_[index].compiled = std::move(restored_bodies[index].body);
                static const bool verify_bodies
                    = std::getenv("FSIM_KERNEL_VERIFY_BODIES") != nullptr;
                if (verify_bodies) {
                    // Diagnostic: the restored body must be what compiling gives.
                    std::optional<CompiledBody> fresh;
                    try {
                        fresh = compile(index);
                    } catch (const CompileFailure&) {
                    }
                    const auto* a = fresh ? &*fresh : nullptr;
                    const auto* b = members_[index].compiled
                        ? &*members_[index].compiled : nullptr;
                    std::string differs;
                    if ((a == nullptr) != (b == nullptr)) {
                        differs = "presence";
                    } else if (a != nullptr) {
                        const auto same_inst = [](const KInst& x, const KInst& y) {
                            return x.op == y.op && x.sub == y.sub && x.flags == y.flags
                                && x.width == y.width && x.d == y.d && x.x == y.x
                                && x.y == y.y && x.z == y.z && x.offset == y.offset
                                && x.aux == y.aux && x.imm_a == y.imm_a
                                && x.imm_b == y.imm_b;
                        };
                        const auto same_words = [](const auto& x, const auto& y) {
                            return x.size() == y.size()
                                && std::equal(x.begin(), x.end(), y.begin(),
                                    [](const auto& l, const auto& r) {
                                        return l.a == r.a && l.b == r.b;
                                    });
                        };
                        if (!std::equal(a->code.begin(), a->code.end(), b->code.begin(),
                                b->code.end(), same_inst)) {
                            differs = "code";
                        } else if (!same_words(a->registers, b->registers)) {
                            differs = "registers";
                        } else if (a->register_widths != b->register_widths) {
                            differs = "register_widths";
                        } else if (a->indices != b->indices) {
                            differs = "indices";
                        } else if (a->parts != b->parts) {
                            differs = "parts";
                        } else if (a->containers != b->containers) {
                            differs = "containers";
                        } else if (a->entry != b->entry) {
                            differs = "entry";
                        } else if (a->wide_registers != b->wide_registers) {
                            differs = "wide_registers";
                        } else if (a->return_targets != b->return_targets) {
                            differs = "return_targets";
                        } else if (a->live_at_entry != b->live_at_entry) {
                            differs = "live_at_entry";
                        } else if (a->shadow_base != b->shadow_base) {
                            differs = "shadow_base";
                        } else if (a->tracked != b->tracked) {
                            differs = "tracked";
                        } else if (a->u_mode != b->u_mode) {
                            differs = "u_mode";
                        } else if (a->u_operand_begin != b->u_operand_begin
                            || a->u_operands != b->u_operands) {
                            differs = "u_operands";
                        } else if (a->call_stack_base != b->call_stack_base) {
                            differs = "call_stack_base";
                        } else if (a->resume_entries != b->resume_entries) {
                            differs = "resume_entries";
                        }
                    }
                    if (!differs.empty()) {
                        std::cerr << "fsim-kernel: restored body differs member="
                                  << index << " field=" << differs << '\n';
                    }
                }
            } else {
                members_[index].compiled = compile(index);
            }
            if (recorded_bodies_) {
                BodyWriter writer { recorded_bodies };
                writer.put(index);
                writer.put(members_[index].process);
                writer.put(static_cast<std::uint64_t>(members_[index].operations.size()));
                writer.put(members_[index].compiled.has_value());
                if (members_[index].compiled) {
                    writer.body(*members_[index].compiled);
                }
                ++recorded_count;
            }
            if (members_[index].compiled) {
                // compile() admits frames only when they are no-ops.
                members_[index].frames_elided = members_[index].vhdl
                    || members_[index].kind == StaticKernelMemberKind::behavioral;
                // Generic instructions read memories through the static
                // container-register bindings.
                for (std::size_t reg = 0U;
                     reg < members_[index].compiled->containers.size()
                     && reg < members_[index].container_registers.size(); ++reg) {
                    members_[index].container_registers[reg]
                        = members_[index].compiled->containers[reg];
                }
                ++compiled_members_;
                if (dump != nullptr && dumped < 6U
                    && index < profile_names_.size()
                    && profile_names_[index].find(dump) != std::string::npos) {
                    ++dumped;
                    std::cerr << "fsim-kernel-dump: " << profile_names_[index]
                              << " kind=" << static_cast<int>(members_[index].kind)
                              << '\n';
                    for (const auto& inst : members_[index].compiled->code) {
                        std::cerr << "  op=" << static_cast<int>(inst.op)
                                  << " sub=" << static_cast<int>(inst.sub)
                                  << " d=" << inst.d << " x=" << inst.x
                                  << " y=" << inst.y << " z=" << inst.z
                                  << " off=" << inst.offset << " w=" << inst.width
                                  << " imm=" << inst.imm_a << '/' << inst.imm_b
                                  << '\n';
                    }
                }
            }
        } catch (const CompileFailure& failure) {
            if (failures[failure.reason]++ == 0U && profile_
                && index < profile_names_.size()) {
                std::size_t widest = 0U;
                for (const auto& operation : members_[index].operations) {
                    visit_operation([&](const auto& op) {
                        using T = std::decay_t<decltype(op)>;
                        if constexpr (std::is_same_v<T, LoadConstant>) {
                            widest = std::max(widest, op.value.width());
                        } else if constexpr (std::is_same_v<T, Concatenate>
                            || std::is_same_v<T, Extract>) {
                            widest = std::max<std::size_t>(widest, op.width);
                        }
                    }, operation);
                }
                std::cerr << "fsim-kernel: not compiled example reason="
                          << failure.reason << " member="
                          << profile_names_[index] << " widest=" << widest
                          << " ops=" << members_[index].operations.size()
                          << ' ' << compile_failure_detail_ << '\n';
                for (std::size_t pc = 0U; pc < members_[index].operations.size(); ++pc) {
                    std::cerr << "    " << pc << ": variant="
                              << members_[index].operations[pc].storage.index();
                    visit_operation([&](const auto& op) {
                        if constexpr (requires { op.destination; }) {
                            if constexpr (std::is_integral_v<
                                              std::decay_t<decltype(op.destination)>>) {
                                std::cerr << " dst=" << op.destination;
                            }
                        }
                        int status = 0;
                        char* name = abi::__cxa_demangle(
                            typeid(op).name(), nullptr, nullptr, &status);
                        std::cerr << ' ' << (name != nullptr ? name : "?");
                        std::free(name);
                        if constexpr (requires { op.source; }) {
                            if constexpr (std::is_integral_v<
                                              std::decay_t<decltype(op.source)>>) {
                                std::cerr << " src=" << op.source;
                            }
                        }
                    }, members_[index].operations[pc]);
                    std::cerr << '\n';
                }
            }
        }
    }
    if (recorded_bodies_) {
        recorded_bodies_->assign(bodies_magic);
        BodyWriter writer { *recorded_bodies_ };
        writer.put(recorded_count);
        recorded_bodies_->append(recorded_bodies);
    }
    if (profile_) {
        std::cerr << "fsim-kernel: wide members=" << wide_members_
                  << " wide_ops=" << wide_member_ops_
                  << " total_ops=" << wide_member_total_ops_ << '\n';
        for (const auto& [reason, count] : failures) {
            std::cerr << "fsim-kernel: not compiled reason=" << reason
                      << " count=" << count << '\n';
        }
    }
}

std::optional<Interpreter::Impl::StaticKernel::CompiledBody>
Interpreter::Impl::StaticKernel::compile(const std::uint32_t member_index)
{
    const auto& member = members_[member_index];
    // A VHDL member compiles its whole operation stream (subprogram bodies
    // may follow the loop): it enters after its wait and leaves when it
    // reaches the wait (or the halt of a process without sensitivity). A
    // behavioral member also compiles its whole stream and enters at any
    // resume point (body.resume_entries).
    const bool behavioral = member.kind == StaticKernelMemberKind::behavioral;
    // A VHDL member (delta semantics); a mixed kernel holds both languages.
    const bool vhdl = member.vhdl;
    const bool whole = vhdl || behavioral;
    const auto begin = whole ? 0U : member.body_begin;
    const auto end = whole ? static_cast<std::uint32_t>(member.operations.size())
                           : member.body_end;
    const auto stop = member.body_end;
    const auto register_count = member.registers.size();
    const auto reject = [](const char* reason) -> CompileFailure {
        return CompileFailure { reason };
    };
    const auto kind_of = [&](const RegisterId reg) {
        return reg < member.register_kinds.size() ? member.register_kinds[reg]
                                                  : ValueKind::logic4;
    };
    if (whole) {
        // Automatic frames compile to nothing when each callable owns its
        // registers (native_isolated) and no call chain recurses.
        std::vector<std::vector<std::uint32_t>> callees(end);
        std::vector<std::uint8_t> is_function(end, 0U);
        bool frames = false;
        for (std::uint32_t pc = 0U; pc < end; ++pc) {
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, CallableFramePush>) {
                    frames = true;
                    if (!op.native_isolated) {
                        throw reject("frame_not_isolated");
                    }
                } else if constexpr (std::is_same_v<T, Call>) {
                    if (op.target >= end || op.return_target >= end) {
                        throw reject("call_target");
                    }
                    is_function[op.target] = 1U;
                }
            }, member.operations[pc]);
        }
        if (frames) {
            // Callees reachable from each function entry, following control
            // flow up to its returns.
            for (std::uint32_t entry = 0U; entry < end; ++entry) {
                if (is_function[entry] == 0U) {
                    continue;
                }
                std::vector<std::uint8_t> seen(end, 0U);
                std::vector<std::uint32_t> work { entry };
                while (!work.empty()) {
                    const auto pc = work.back();
                    work.pop_back();
                    if (pc >= end || seen[pc] != 0U) {
                        continue;
                    }
                    seen[pc] = 1U;
                    visit_operation([&](const auto& op) {
                        using T = std::decay_t<decltype(op)>;
                        if constexpr (std::is_same_v<T, Jump>) {
                            work.push_back(op.target);
                        } else if constexpr (std::is_same_v<T, Branch>) {
                            work.push_back(op.when_true);
                            work.push_back(op.when_false);
                        } else if constexpr (std::is_same_v<T, Call>) {
                            callees[entry].push_back(op.target);
                            work.push_back(op.return_target);
                        } else if constexpr (std::is_same_v<T, Fork>) {
                            for (const auto branch : op.branches) {
                                work.push_back(branch);
                            }
                            work.push_back(pc + 1U);
                        } else if constexpr (std::is_same_v<T, Return>
                            || std::is_same_v<T, WaitSensitivity>
                            || std::is_same_v<T, Halt>
                            || std::is_same_v<T, ForkEnd>
                            || std::is_same_v<T, Stop>) {
                        } else {
                            work.push_back(pc + 1U);
                        }
                    }, member.operations[pc]);
                }
            }
            std::vector<std::uint8_t> state(end, 0U);
            const auto cyclic = [&](const auto& self, const std::uint32_t node)
                -> bool {
                if (state[node] == 1U) {
                    return true;
                }
                if (state[node] == 2U) {
                    return false;
                }
                state[node] = 1U;
                for (const auto callee : callees[node]) {
                    if (self(self, callee)) {
                        return true;
                    }
                }
                state[node] = 2U;
                return false;
            };
            for (std::uint32_t entry = 0U; entry < end; ++entry) {
                if (is_function[entry] != 0U && cyclic(cyclic, entry)) {
                    throw reject("recursive_call");
                }
            }
        }
    }
    // 0. VHDL: operations the compiled entry never reaches (the process
    // prologue runs only in the first, reference-evaluated run) and constant
    // loads no later read can see are left out; they would otherwise give
    // registers conflicting widths.
    std::vector<std::uint8_t> skip(end, 0U);
    std::vector<std::uint64_t> live_at_entry;
    if (vhdl) {
        if (member.body_identity != nullptr) {
            const std::tuple key { member.body_identity, member.body_begin,
                member.body_end };
            if (const auto found = prune_cache_.find(key);
                found != prune_cache_.end()) {
                skip = found->second.first;
                live_at_entry = found->second.second;
            } else {
                prune_operations(member_index, skip, live_at_entry);
                prune_cache_.emplace(key, std::pair { skip, live_at_entry });
            }
        } else {
            prune_operations(member_index, skip, live_at_entry);
        }
    }

    // 1. Flow-insensitive register widths.
    constexpr auto polymorphic = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> width(register_count, 0U);
    const auto signal_width = [&](const SignalId signal) -> std::uint32_t {
        if (const auto slot = slot_of_signal_[signal]; slot != no_slot) {
            return slots_[slot].width;
        }
        if (const auto family = family_of_signal_[signal]; family != no_slot) {
            return families_[family].width;
        }
        return static_cast<std::uint32_t>(
            impl_.get_signal(signal).initial_value.width());
    };
    // Private arrays are bound from the start; design memories bind at
    // ReadContainerObject.
    std::vector<std::uint32_t> container_of_register(
        member.container_registers.begin(), member.container_registers.end());
    bool changed = true;
    std::uint32_t defining = 0U;
    for (std::size_t pass = 0U; changed; ++pass) {
        if (pass > 64U) {
            throw reject("width_fixpoint");
        }
        changed = false;
        const auto define = [&](const RegisterId reg, const std::uint32_t value) {
            if (reg >= register_count) {
                throw reject("register_range");
            }
            if (value == 0U || width[reg] == polymorphic) {
                return;
            }
            if (width[reg] == 0U) {
                width[reg] = value;
                changed = true;
            } else if (width[reg] != value) {
                // A register holding values of several widths (a shared
                // subprogram result, for example) is kept as a reference
                // value; every instruction touching it runs generically.
                if (!vhdl) {
                    throw reject("width_conflict");
                }
                if (profile_ && value != polymorphic) {
                    std::string name = impl_.processes.program_view(member.process).name();
                    std::erase_if(name, [](const char c) {
                        return std::isdigit(static_cast<unsigned char>(c)) != 0;
                    });
                    ++profile_generic_ops_["polymorphic " + name + " r"
                        + std::to_string(reg) + " " + std::to_string(width[reg])
                        + "/" + std::to_string(value) + " at "
                        + std::to_string(defining)];
                }
                width[reg] = polymorphic;
                changed = true;
            }
        };
        const auto of = [&](const RegisterId reg) -> std::uint32_t {
            if (reg >= register_count) {
                throw reject("register_range");
            }
            return width[reg];
        };
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            defining = pc;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, LoadConstant>) {
                    define(op.destination,
                        static_cast<std::uint32_t>(op.value.width()));
                } else if constexpr (std::is_same_v<T, CopyRegister>
                    || std::is_same_v<T, UnaryNot>
                    || std::is_same_v<T, ConvertToTwoState>) {
                    define(op.destination, of(op.source));
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    define(op.destination, signal_width(op.signal));
                } else if constexpr (std::is_same_v<T, Binary>) {
                    define(op.destination,
                        comparison(op.operation) ? 1U : of(op.lhs));
                } else if constexpr (std::is_same_v<T, Reduction>
                    || std::is_same_v<T, LogicalNot>
                    || std::is_same_v<T, LogicalBinary>
                    || std::is_same_v<T, DynamicExtract>) {
                    define(op.destination, 1U);
                } else if constexpr (std::is_same_v<T, Shift>) {
                    define(op.destination, of(op.value));
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, Concatenate>) {
                    define(op.destination, op.width);
                } else if constexpr (std::is_same_v<T, Insert>
                    || std::is_same_v<T, DynamicInsert>
                    || std::is_same_v<T, DynamicPartInsert>) {
                    define(op.destination, of(op.target));
                } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                    define(op.destination, of(op.when_true));
                    define(op.destination, of(op.when_false));
                } else if constexpr (std::is_same_v<T, IntegerBinary>) {
                    define(op.destination, of(op.lhs));
                } else if constexpr (std::is_same_v<T, IntegerUnary>) {
                    define(op.destination, of(op.source));
                } else if constexpr (std::is_same_v<T, PlusArgSelect>) {
                    define(op.destination, 32U);
                } else if constexpr (std::is_same_v<T, Call>) {
                    if (op.stack.capacity != 0U) {
                        define(op.stack.pointer, 32U);
                        for (std::uint32_t entry = 0U; entry < op.stack.capacity;
                             ++entry) {
                            define(op.stack.entries + entry, 32U);
                        }
                    }
                } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                    if (op.destination >= container_of_register.size()
                        || op.object >= container_of_object_.size()
                        || container_of_object_[op.object] == no_container) {
                        throw reject("container_binding");
                    }
                    auto& bound = container_of_register[op.destination];
                    const auto container = container_of_object_[op.object];
                    if (bound != no_container && bound != container) {
                        throw reject("container_rebinding");
                    }
                    bound = container;
                } else if constexpr (std::is_same_v<T, ContainerRead>) {
                    if (op.source >= container_of_register.size()
                        || container_of_register[op.source] == no_container) {
                        return;
                    }
                    define(op.destination, static_cast<std::uint32_t>(
                        containers_[container_of_register[op.source]]
                            .type.element_width));
                }
            }, member.operations[pc]);
        }
    }

    static const char* const widths_dump = std::getenv("FSIM_KERNEL_WIDTHS");
    if (const char* dump = widths_dump) {
        const auto name = impl_.processes.program_view(member.process).name();
        if (name.find(dump) != std::string::npos) {
            std::cerr << "fsim-kernel-widths: " << name;
            for (std::size_t reg = 0U; reg < register_count; ++reg) {
                std::cerr << ' ' << reg << ':'
                          << (width[reg] == polymorphic ? std::string { "P" }
                                                       : std::to_string(width[reg]));
            }
            std::cerr << '\n';
        }
    }
    if (profile_) {
        std::size_t wide_ops = 0U;
        std::size_t total_ops = 0U;
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            bool wide = false;
            visit_operation([&](const auto& op) {
                if constexpr (requires { op.destination; }) {
                    if constexpr (std::is_integral_v<
                                      std::decay_t<decltype(op.destination)>>) {
                        wide = op.destination < register_count
                            && width[op.destination] > 64U;
                    }
                }
            }, member.operations[pc]);
            wide_ops += wide ? 1U : 0U;
            ++total_ops;
        }
        if (wide_ops != 0U) {
            ++wide_members_;
            wide_member_ops_ += wide_ops;
            wide_member_total_ops_ += total_ops;
        }
    }
    // 2. Translate.
    CompiledBody body;
    // VHDL calls on the runtime-owned stack use a synthetic fixed stack in
    // the register file: a pointer and call_stack_depth entries.
    if (whole && std::ranges::any_of(member.operations, [](const Operation& op) {
            const auto* call = operation_get_if<Call>(&op);
            return call != nullptr && call->stack.capacity == 0U;
        })) {
        body.call_stack_base = static_cast<std::uint32_t>(width.size());
        width.resize(width.size() + 1U + call_stack_depth, 32U);
    }
    body.registers.assign(width.size(), { });
    body.register_widths = width;
    body.containers = container_of_register;
    std::uint32_t translating = 0U;
    const auto narrow = [&](const RegisterId reg) -> std::uint32_t {
        if (reg >= register_count || width[reg] == 0U || width[reg] > 64U) {
            if (profile_) {
                compile_failure_detail_ = "reg=" + std::to_string(reg) + " width="
                    + std::to_string(reg < register_count ? width[reg] : 0U)
                    + " op_index=" + std::to_string(
                        member.operations[translating].storage.index())
                    + " pc=" + std::to_string(translating);
            }
            throw reject("register_width");
        }
        return reg;
    };
    const auto w = [&](const RegisterId reg) { return width[narrow(reg)]; };
    const auto map_target = [&](const InstructionIndex target) -> std::uint32_t {
        if (vhdl && target == stop) {
            return end - begin;
        }
        return target >= begin && target < end ? target - begin : end - begin;
    };
    const auto owned_narrow_slot = [&](const SignalId signal)
        -> std::optional<std::uint32_t> {
        if (signal >= slot_of_signal_.size()) {
            throw reject("signal_range");
        }
        if (family_of_signal_[signal] != no_slot) {
            throw reject("proxy_access");
        }
        auto slot = slot_of_signal_[signal];
        if (slot == no_slot && mirror_of_signal_[signal] != no_slot
            && slots_[mirror_of_signal_[signal]].planes == 2U) {
            // A mirrored host signal reads like a slot.
            slot = mirror_of_signal_[signal];
        }
        if (slot == no_slot) {
            return std::nullopt;
        }
        if (slots_[slot].words != 1U) {
            throw reject("wide_slot");
        }
        return slot;
    };
    const auto owned_slot = [&](const SignalId signal)
        -> std::optional<std::uint32_t> {
        if (signal >= slot_of_signal_.size()) {
            throw reject("signal_range");
        }
        if (family_of_signal_[signal] != no_slot) {
            throw reject("proxy_access");
        }
        const auto slot = slot_of_signal_[signal];
        if (slot == no_slot) {
            return std::nullopt;
        }
        return slot;
    };
    body.code.reserve(end - begin);
    std::vector<RegisterId> operand_reads;
    std::optional<RegisterId> operand_write;
    bool has_wide = false;
    // A wide owned slot read whose register only feeds narrow selections,
    // in a member that never writes the slot, is read in place.
    std::vector<std::uint32_t> field_slot(register_count, no_slot);
    // Likewise a family proxy read whose narrow selections each lie in one
    // leaf, in a member that never writes the family: each selection loads
    // its leaf (family_leaf).
    std::vector<std::uint32_t> field_family(register_count, no_slot);
    const auto family_leaf = [&](const std::uint32_t family,
                                 const std::uint32_t offset,
                                 const std::uint32_t count) -> const FamilyLeaf* {
        for (const auto& leaf : families_[family].leaves) {
            if (leaf.offset <= offset && count <= leaf.width
                && offset - leaf.offset <= leaf.width - count) {
                return &leaf;
            }
        }
        return nullptr;
    };
    // The leaf a write of `count` bits at `offset` of a family proxy
    // replaces exactly.
    const auto proxy_leaf = [&](const SignalId signal, const std::uint32_t offset,
                                const std::uint32_t count) -> const FamilyLeaf* {
        if (signal >= family_of_signal_.size()
            || family_of_signal_[signal] == no_slot) {
            return nullptr;
        }
        const auto* leaf = family_leaf(family_of_signal_[signal], offset, count);
        return leaf != nullptr && leaf->offset == offset && leaf->width == count
            ? leaf : nullptr;
    };
    {
        // Families and slots this member writes, as sorted lists: a member
        // writes few of them, and dense flags over every slot cost more to
        // clear than the member takes to compile.
        std::vector<std::uint32_t> written_families;
        std::vector<std::uint32_t> definitions(register_count, 0U);
        std::vector<std::uint8_t> other_use(register_count, 0U);
        // A written slot may still be read in place when no write to it,
        // no backward jump and no call lies between the read and its last
        // selection (straight-line read-before-write, as in RAM models).
        std::vector<std::uint32_t> definition_pc(register_count, 0U);
        std::vector<std::uint32_t> last_use(register_count, 0U);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> slot_writes;
        std::vector<std::uint32_t> barriers;
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            const auto& operation = member.operations[pc];
            operation_registers(operation, operand_reads, operand_write);
            if (operand_write && *operand_write < register_count) {
                ++definitions[*operand_write];
                definition_pc[*operand_write] = pc;
            }
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    if (op.target <= pc) {
                        barriers.push_back(pc);
                    }
                } else if constexpr (std::is_same_v<T, Branch>) {
                    if (op.when_true <= pc || op.when_false <= pc) {
                        barriers.push_back(pc);
                    }
                } else if constexpr (std::is_same_v<T, Call>
                    || std::is_same_v<T, Return> || std::is_same_v<T, Halt>
                    || std::is_same_v<T, WaitSensitivity>
                    || behavioral_suspension<T>) {
                    barriers.push_back(pc);
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, DynamicExtract>) {
                    if (op.source < register_count) {
                        last_use[op.source] = std::max(last_use[op.source], pc);
                    }
                }
            }, operation);
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (op.signal < family_of_signal_.size()
                        && family_of_signal_[op.signal] != no_slot
                        && op.destination < register_count) {
                        field_family[op.destination]
                            = family_of_signal_[op.signal];
                    }
                    auto slot = op.signal < slot_of_signal_.size()
                        ? slot_of_signal_[op.signal] : no_slot;
                    if (slot == no_slot && op.signal < mirror_of_signal_.size()) {
                        slot = mirror_of_signal_[op.signal];
                    }
                    if (slot != no_slot && slots_[slot].words != 1U
                        && op.destination < register_count) {
                        field_slot[op.destination] = slot;
                    }
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, DynamicExtract>) {
                    for (const auto reg : operand_reads) {
                        if (reg != op.source && reg < register_count) {
                            other_use[reg] = 1U;
                        }
                    }
                    if constexpr (requires { op.width; }) {
                        if (op.width > 64U && op.source < register_count) {
                            other_use[op.source] = 1U;
                        }
                    }
                    if (op.source < register_count
                        && field_family[op.source] != no_slot) {
                        bool fits = false;
                        if constexpr (std::is_same_v<T, Extract>) {
                            fits = op.width != 0U && op.width <= 64U
                                && family_leaf(field_family[op.source],
                                       op.offset, op.width) != nullptr;
                        }
                        if (!fits) {
                            field_family[op.source] = no_slot;
                        }
                    }
                    return;
                }
                if constexpr (requires { op.signal; }) {
                    // VHDL signal assignments are deferred to the end of the
                    // round, so only immediate (shared-variable) writes can
                    // change a slot under an in-place read.
                    constexpr bool immediate = std::is_same_v<T, WriteBlocking>
                        || std::is_same_v<T, WriteBlockingSlice>
                        || std::is_same_v<T, WriteBlockingDynamicSlice>
                        || std::is_same_v<T, WriteBlockingDynamicPartSlice>;
                    if constexpr (std::is_same_v<std::decay_t<decltype(op.signal)>,
                                      SignalId>
                        && !std::is_same_v<T, ReadSignal>) {
                        if (op.signal < family_of_signal_.size()
                            && family_of_signal_[op.signal] != no_slot) {
                            written_families.push_back(family_of_signal_[op.signal]);
                        }
                        if (op.signal < slot_of_signal_.size()
                            && slot_of_signal_[op.signal] != no_slot
                            && (!vhdl || immediate)) {
                            slot_writes.emplace_back(slot_of_signal_[op.signal], pc);
                        }
                    }
                }
                for (const auto reg : operand_reads) {
                    if (reg < register_count) {
                        other_use[reg] = 1U;
                    }
                }
            }, operation);
        }
        std::ranges::sort(written_families);
        std::vector<std::uint32_t> written_slots;
        written_slots.reserve(slot_writes.size());
        for (const auto& [slot, at] : slot_writes) {
            written_slots.push_back(slot);
        }
        std::ranges::sort(written_slots);
        const auto written_slot = [&](const std::uint32_t slot) {
            return std::ranges::binary_search(written_slots, slot);
        };
        const auto unwritten_between = [&](const std::uint32_t reg) {
            const auto from = definition_pc[reg];
            const auto to = last_use[reg];
            if (to <= from) {
                return false;
            }
            return std::ranges::none_of(slot_writes, [&](const auto& write) {
                       return write.first == field_slot[reg] && write.second > from
                           && write.second < to;
                   })
                && std::ranges::none_of(barriers, [&](const std::uint32_t pc) {
                       return pc >= from && pc <= to;
                   });
        };
        for (std::uint32_t reg = 0U; reg < register_count; ++reg) {
            if (field_slot[reg] != no_slot
                && (definitions[reg] != 1U || other_use[reg] != 0U
                    || (written_slot(field_slot[reg])
                        && !unwritten_between(reg)))) {
                field_slot[reg] = no_slot;
            }
        }
        // A leaf written by this member (directly or through the proxy)
        // or a barrier between the read and its last selection could
        // change what an in-place read sees.
        for (std::uint32_t reg = 0U; reg < register_count; ++reg) {
            if (field_family[reg] == no_slot) {
                continue;
            }
            const auto& family = families_[field_family[reg]];
            const bool leaf_written = std::ranges::any_of(family.leaves,
                [&](const FamilyLeaf& leaf) {
                    return written_slot(leaf.slot);
                });
            const auto from = definition_pc[reg];
            const auto to = last_use[reg];
            if (definitions[reg] != 1U || other_use[reg] != 0U || to <= from
                || std::ranges::binary_search(written_families, field_family[reg])
                || leaf_written
                || std::ranges::any_of(barriers, [&](const std::uint32_t pc) {
                       return pc >= from && pc <= to;
                   })) {
                field_family[reg] = no_slot;
            }
        }
    }
    // Wide moves (SystemVerilog): ReadSignal of a wide slot whose register
    // only feeds an optional DynamicPartSelect and then one write of a wide
    // slot, in straight-line code that does not write the source, run as
    // one word-level copy (KOp::wide_move).
    std::vector<std::uint8_t> fused_away(end - begin, 0U);
    std::vector<std::optional<KInst>> fused_move(end - begin);
    if (!vhdl) {
        std::vector<std::uint32_t> defs(register_count, 0U);
        std::vector<std::uint32_t> uses(register_count, 0U);
        std::vector<std::uint32_t> use_pc(register_count, 0U);
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            operation_registers(member.operations[pc], operand_reads,
                operand_write);
            if (operand_write && *operand_write < register_count) {
                ++defs[*operand_write];
            }
            for (const auto reg : operand_reads) {
                if (reg < register_count) {
                    ++uses[reg];
                    use_pc[reg] = pc;
                }
            }
        }
        const auto wide_slot = [&](const SignalId signal) -> std::uint32_t {
            if (signal >= slot_of_signal_.size()
                || family_of_signal_[signal] != no_slot
                || slot_of_signal_[signal] == no_slot) {
                return no_slot;
            }
            const auto slot = slot_of_signal_[signal];
            return slots_[slot].words > 1U && slots_[slot].planes == 2U
                ? slot : no_slot;
        };
        const auto single_use = [&](const RegisterId reg) {
            return reg < register_count && defs[reg] == 1U && uses[reg] == 1U
                && width[reg] > 64U && width[reg] != polymorphic;
        };
        // No control flow in (from, to) and no write there to `signal`.
        const auto straight = [&](const std::uint32_t from, const std::uint32_t to,
                                  const SignalId signal) {
            for (auto pc = from + 1U; pc < to; ++pc) {
                bool ok = true;
                visit_operation([&](const auto& op) {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (std::is_same_v<T, Branch> || std::is_same_v<T, Jump>
                        || std::is_same_v<T, Call> || std::is_same_v<T, Return>
                        || std::is_same_v<T, Halt>
                        || std::is_same_v<T, WaitSensitivity>
                        || behavioral_suspension<T>) {
                        ok = false;
                    } else if constexpr (requires { op.signal; }) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(op.signal)>,
                                          SignalId>
                            && !std::is_same_v<T, ReadSignal>) {
                            ok = op.signal != signal;
                        }
                    }
                }, member.operations[pc]);
                if (!ok) {
                    return false;
                }
            }
            return true;
        };
        for (std::uint32_t read_pc = begin; read_pc < end; ++read_pc) {
            if (skip[read_pc] != 0U) {
                continue;
            }
            const auto* read = operation_get_if<ReadSignal>(
                &member.operations[read_pc]);
            if (read == nullptr || read->kind != SignalReadKind::current
                || read->ticks != 1U || read->clock || read->gate
                || !single_use(read->destination)) {
                continue;
            }
            const auto source = wide_slot(read->signal);
            if (source == no_slot
                || slots_[source].width != width[read->destination]) {
                continue;
            }
            KInst move;
            move.op = KOp::wide_move;
            move.x = source;
            auto value = read->destination;
            auto write_pc = use_pc[value];
            std::uint32_t select_pc = no_slot;
            std::optional<DynamicPartIndex> selection;
            if (const auto* select = operation_get_if<DynamicPartSelect>(
                    &member.operations[write_pc])) {
                if (select->source != value || !single_use(select->destination)
                    || select->width != width[select->destination]
                    || select->base >= register_count
                    || width[select->base] != 32U) {
                    continue;
                }
                select_pc = write_pc;
                move.sub = 1U;
                move.y = select->base;
                move.flags = static_cast<std::uint8_t>(
                    (select->increasing ? flag_increasing : 0U)
                    | (select->source_descending ? flag_descending : 0U)
                    | (select->two_state ? flag_two_state : 0U));
                selection.emplace();
                selection->left = select->left;
                selection->right = select->right;
                selection->base_offset = select->base_offset;
                selection->width = select->width;
                selection->increasing = select->increasing;
                selection->source_descending = select->source_descending;
                value = select->destination;
                write_pc = use_pc[value];
            }
            if (write_pc <= read_pc || write_pc >= end || skip[write_pc] != 0U) {
                continue;
            }
            bool matched = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                constexpr bool whole = std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteBlocking>;
                constexpr bool slice = std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteBlockingSlice>;
                if constexpr (whole || slice) {
                    if (op.source != value) {
                        return;
                    }
                    const auto target = wide_slot(op.signal);
                    if (target == no_slot) {
                        return;
                    }
                    std::uint32_t offset = 0U;
                    if constexpr (slice) {
                        offset = op.offset;
                    }
                    if (offset > slots_[target].width
                        || width[value] > slots_[target].width - offset
                        || (whole && width[value] != slots_[target].width)) {
                        return;
                    }
                    bool deferred = false;
                    if constexpr (std::is_same_v<T, WriteUpdate>
                        || std::is_same_v<T, WriteUpdateSlice>) {
                        if (op.domain == SignalUpdateDomain::generic) {
                            return;
                        }
                        deferred = op.domain == SignalUpdateDomain::systemverilog_nba;
                    }
                    move.d = target;
                    move.offset = offset;
                    move.width = width[value];
                    move.flags = static_cast<std::uint8_t>(
                        move.flags | (deferred ? flag_nba : 0U));
                    matched = true;
                }
            }, member.operations[write_pc]);
            if (!matched || !straight(read_pc, write_pc, read->signal)) {
                continue;
            }
            if (select_pc != no_slot) {
                // The base must still hold its value at the write.
                bool base_kept = true;
                for (auto pc = select_pc + 1U; pc < write_pc; ++pc) {
                    operation_registers(member.operations[pc], operand_reads,
                        operand_write);
                    base_kept = base_kept
                        && (!operand_write || *operand_write != move.y);
                }
                if (!base_kept) {
                    continue;
                }
                fused_away[select_pc - begin] = 1U;
                move.aux = static_cast<std::uint32_t>(body.parts.size());
                body.parts.push_back(*selection);
            }
            fused_away[read_pc - begin] = 1U;
            fused_move[write_pc - begin] = move;
        }
    }
    for (std::uint32_t pc = begin; pc < end; ++pc) {
        translating = pc;
        KInst inst;
        if (skip[pc] != 0U || fused_away[pc - begin] != 0U) {
            body.code.push_back(inst);
            continue;
        }
        if (fused_move[pc - begin]) {
            body.code.push_back(*fused_move[pc - begin]);
            continue;
        }
        if (behavioral) {
            // Suspensions return to the thread runner; runtime-library
            // operations run through the generic bridge; calls use the
            // synthetic stack; isolated nonrecursive frames compile away.
            bool translated = true;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (behavioral_suspension<T>
                    || std::is_same_v<T, WaitSensitivity>
                    || std::is_same_v<T, Halt>) {
                    inst.op = KOp::suspend;
                    inst.x = pc;
                } else if constexpr (std::is_same_v<T, Display>
                    || std::is_same_v<T, FormatDisplay>
                    || std::is_same_v<T, StringDisplay>
                    || std::is_same_v<T, TimeDisplay>
                    || std::is_same_v<T, LoadStringConstant>
                    || std::is_same_v<T, CopyStringRegister>
                    || std::is_same_v<T, PlusArgSelect>) {
                    inst.op = KOp::generic;
                    inst.x = pc;
                    inst.y = 0U;
                    if constexpr (std::is_same_v<T, FormatDisplay>) {
                        if (op.source >= register_count || width[op.source] == 0U
                            || width[op.source] == polymorphic) {
                            throw reject("display_source");
                        }
                        has_wide = has_wide || width[op.source] > 64U;
                    }
                } else if constexpr (std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>) {
                    // Isolated and nonrecursive (checked above); frames that
                    // save private arrays run through the generic bridge.
                    if (member.private_frame(op.identity)) {
                        inst.op = KOp::generic;
                        inst.x = pc;
                        inst.y = 0U;
                    } else {
                        inst.op = KOp::nop;
                    }
                } else if constexpr (std::is_same_v<T, Call>) {
                    inst.op = KOp::call;
                    inst.d = map_target(op.target);
                    if (op.stack.capacity == 0U) {
                        // The synthetic stack; overflow deoptimizes.
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base + 1U;
                        inst.z = call_stack_depth;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                    inst.imm_a = op.return_target;
                    body.return_targets.push_back(op.return_target);
                } else if constexpr (std::is_same_v<T, Return>) {
                    inst.op = KOp::ret;
                    if (op.stack.capacity == 0U) {
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base + 1U;
                        inst.z = call_stack_depth;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                } else {
                    translated = false;
                }
            }, member.operations[pc]);
            if (translated) {
                body.code.push_back(inst);
                continue;
            }
        }
        // Operations on values wider than 64 bits, wide or proxy slots, and
        // dynamic host writes run through the reference value functions.
        {
            const auto& operation = member.operations[pc];
            operation_registers(operation, operand_reads, operand_write);
            bool generic = false;
            // Narrow selections may read a wide register directly.
            std::optional<RegisterId> wide_source;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, DynamicExtract>) {
                    if (op.source < register_count && width[op.source] > 64U
                        && width[op.source] != polymorphic
                        && field_slot[op.source] == no_slot
                        && field_family[op.source] == no_slot
                        && op.destination < register_count
                        && width[op.destination] <= 64U) {
                        wide_source = op.source;
                    }
                }
            }, operation);
            const auto check = [&](const RegisterId reg) {
                if (reg >= register_count || width[reg] == 0U) {
                    if (profile_) {
                        compile_failure_detail_ = "undefined reg="
                            + std::to_string(reg) + " op_index="
                            + std::to_string(operation.storage.index())
                            + " pc=" + std::to_string(pc);
                    }
                    throw reject("register_width");
                }
                generic = generic
                    || (width[reg] > 64U && field_slot[reg] == no_slot
                        && field_family[reg] == no_slot
                        && (!wide_source || reg != *wide_source));
            };
            // Control flow never runs generically (the bridge does not
            // transfer control); a wide condition is read in place.
            const bool control = operation_holds<Branch>(operation);
            for (const auto reg : operand_reads) {
                check(reg);
            }
            if (operand_write) {
                check(*operand_write);
            }
            if (control) {
                generic = false;
            }
            const auto wide_signal = [&](const SignalId signal) {
                if (signal >= slot_of_signal_.size()) {
                    throw reject("signal_range");
                }
                if (family_of_signal_[signal] != no_slot) {
                    return true;
                }
                const auto slot = slot_of_signal_[signal];
                return slot != no_slot ? slots_[slot].words != 1U
                                       : signal_width(signal) > 64U;
            };
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                const auto proxy = [&](const SignalId signal) {
                    return family_of_signal_[signal] != no_slot;
                };
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (op.destination < register_count
                        && (field_slot[op.destination] != no_slot
                            || field_family[op.destination] != no_slot)) {
                        return;
                    }
                    generic = generic || wide_signal(op.signal);
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteProjected>) {
                    if (op.signal >= slot_of_signal_.size()) {
                        throw reject("signal_range");
                    }
                    generic = generic || (owned(op.signal) && wide_signal(op.signal));
                } else if constexpr (std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    // Narrow slices of wide owned slots are field stores; a
                    // SystemVerilog slice that is exactly one family leaf
                    // stores the leaf.
                    if (op.signal >= slot_of_signal_.size()) {
                        throw reject("signal_range");
                    }
                    bool leaf = false;
                    if constexpr (!std::is_same_v<T, WriteProjectedSlice>) {
                        leaf = !vhdl && op.source < register_count
                            && proxy_leaf(op.signal, op.offset, width[op.source])
                                != nullptr;
                    }
                    generic = generic
                        || (owned(op.signal) && proxy(op.signal) && !leaf);
                } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    if (op.signal >= slot_of_signal_.size()) {
                        throw reject("signal_range");
                    }
                    generic = generic || !owned(op.signal) || proxy(op.signal);
                    // VHDL part-select assignments are rare; the reference
                    // evaluator stages them.
                    generic = generic
                        || (vhdl
                            && (std::is_same_v<T, WriteBlockingDynamicPartSlice>
                                || std::is_same_v<T, WriteUpdateDynamicPartSlice>));
                } else if constexpr (std::is_same_v<T, ContainerRead>) {
                    generic = generic || op.string_index;
                }
            }, operation);
            if (generic) {
                inst.op = KOp::generic;
                inst.x = pc;
                inst.y = 0U;
                has_wide = true;
                body.code.push_back(inst);
                continue;
            }
        }
        bool handled = false;
        if (vhdl) {
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                handled = true;
                if constexpr (std::is_same_v<T, WaitSensitivity>
                    || std::is_same_v<T, Halt>) {
                    if (pc != stop) {
                        throw reject("vhdl_suspension");
                    }
                    inst.op = KOp::jump;
                    inst.d = end - begin;
                } else if constexpr (std::is_same_v<T, LoadConstant>) {
                    if (kind_of(op.destination) != ValueKind::logic9
                        || !op.value.is_logic9()) {
                        handled = false;
                        return;
                    }
                    if (op.value.width() == 0U || op.value.width() > 64U) {
                        throw reject("wide_constant");
                    }
                    inst.d = narrow(op.destination);
                    if (const auto word = exact_uword(op.value)) {
                        inst.op = KOp::constant;
                        inst.imm_a = word->value.a;
                        inst.imm_b = word->value.b;
                        inst.offset = static_cast<std::uint32_t>(word->unknown);
                        inst.aux = static_cast<std::uint32_t>(word->unknown >> 32U);
                    } else {
                        inst.op = KOp::deopt;
                    }
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (op.destination < register_count
                        && (field_slot[op.destination] != no_slot
                            || field_family[op.destination] != no_slot)) {
                        // Read in place by its narrow selections.
                        inst.op = KOp::nop;
                        return;
                    }
                    auto slot = owned_slot(op.signal);
                    if (!slot && mirror_of_signal_[op.signal] != no_slot) {
                        slot = mirror_of_signal_[op.signal];
                    }
                    if (slot && slots_[*slot].planes == 4U) {
                        if (slots_[*slot].words != 1U) {
                            throw reject("wide_logic9_slot");
                        }
                        inst.op = KOp::load_slot9;
                        inst.d = narrow(op.destination);
                        inst.x = *slot;
                        inst.width = slots_[*slot].width;
                        // sub 1: a Logic4 destination coerces Logic9 codes.
                        inst.sub = kind_of(op.destination) == ValueKind::logic9
                            ? 0U : 1U;
                    } else if (!slot) {
                        if (signal_width(op.signal) > 64U) {
                            throw reject("wide_host_read");
                        }
                        inst.op = KOp::load_host;
                        inst.d = narrow(op.destination);
                        inst.x = op.signal;
                        // sub 1: a Logic9 destination keeps exact codes.
                        inst.sub = kind_of(op.destination) == ValueKind::logic9
                            ? 1U : 0U;
                    } else {
                        handled = false;
                    }
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjected>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    constexpr bool blocking = std::is_same_v<T, WriteBlocking>
                        || std::is_same_v<T, WriteBlockingSlice>;
                    constexpr bool slice = std::is_same_v<T, WriteBlockingSlice>
                        || std::is_same_v<T, WriteUpdateSlice>
                        || std::is_same_v<T, WriteProjectedSlice>;
                    std::uint32_t offset = 0U;
                    if constexpr (slice) {
                        offset = op.offset;
                    }
                    const auto value_width = w(op.source);
                    inst.x = op.source;
                    inst.offset = offset;
                    inst.width = value_width;
                    if (const auto slot = owned_slot(op.signal)) {
                        if (offset > slots_[*slot].width
                            || value_width > slots_[*slot].width - offset
                            || (!slice && value_width != slots_[*slot].width)) {
                            throw reject("write_width");
                        }
                        inst.op = KOp::store_vhdl;
                        inst.d = *slot;
                        inst.flags = blocking ? flag_blocking : 0U;
                    } else {
                        SignalUpdateDomain domain = SignalUpdateDomain::generic;
                        if constexpr (std::is_same_v<T, WriteUpdate>
                            || std::is_same_v<T, WriteUpdateSlice>) {
                            domain = op.domain;
                        }
                        inst.op = KOp::store_host;
                        inst.d = op.signal;
                        inst.sub = static_cast<std::uint8_t>(domain);
                        inst.flags = static_cast<std::uint8_t>(
                            (blocking ? flag_blocking : 0U)
                            | (slice ? flag_linear : 0U));
                    }
                } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>) {
                    const auto slot = owned_slot(op.signal);
                    if (!slot || w(op.selection.index) != 32U) {
                        throw reject("dynamic_write");
                    }
                    inst.op = KOp::store_vhdl;
                    inst.sub = 1U;
                    inst.d = *slot;
                    inst.x = op.source;
                    inst.y = op.selection.index;
                    inst.width = w(op.source);
                    // The target's width, for the generated range check.
                    inst.imm_a = slots_[*slot].width;
                    inst.aux = static_cast<std::uint32_t>(body.indices.size());
                    body.indices.push_back(op.selection);
                    inst.flags = std::is_same_v<T, WriteBlockingDynamicSlice>
                        ? flag_blocking : 0U;
                } else if constexpr (std::is_same_v<T, IntegerBinary>) {
                    if (w(op.lhs) != w(op.rhs)) {
                        throw reject("integer_widths");
                    }
                    inst.op = KOp::integer_binary;
                    inst.sub = static_cast<std::uint8_t>(op.operation);
                    inst.d = narrow(op.destination);
                    inst.x = narrow(op.lhs);
                    inst.y = narrow(op.rhs);
                    inst.width = w(op.lhs);
                } else if constexpr (std::is_same_v<T, IntegerUnary>) {
                    inst.op = KOp::integer_unary;
                    inst.sub = static_cast<std::uint8_t>(op.operation);
                    inst.d = narrow(op.destination);
                    inst.x = narrow(op.source);
                    inst.width = w(op.source);
                } else if constexpr (std::is_same_v<T, IntegerCheck>) {
                    inst.op = KOp::integer_check;
                    inst.x = narrow(op.source);
                    inst.width = w(op.source);
                    inst.imm_a = static_cast<std::uint64_t>(op.lower);
                    inst.imm_b = static_cast<std::uint64_t>(op.upper);
                } else if constexpr (std::is_same_v<T, Call>) {
                    inst.op = KOp::call;
                    inst.d = map_target(op.target);
                    if (op.stack.capacity == 0U) {
                        // The synthetic stack; overflow deoptimizes.
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base + 1U;
                        inst.z = call_stack_depth;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                    inst.imm_a = op.return_target;
                    body.return_targets.push_back(op.return_target);
                } else if constexpr (std::is_same_v<T, Return>) {
                    inst.op = KOp::ret;
                    if (op.stack.capacity == 0U) {
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base + 1U;
                        inst.z = call_stack_depth;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                } else if constexpr (std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>) {
                    // Isolated and nonrecursive (checked above).
                    inst.op = KOp::nop;
                } else if constexpr (std::is_same_v<T, Assert>) {
                    inst.op = KOp::assert_check;
                    inst.x = narrow(op.condition);
                } else {
                    handled = false;
                }
            }, member.operations[pc]);
        }
        if (handled) {
            body.code.push_back(inst);
            continue;
        }
        visit_operation([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, DebugPoint>) {
                inst.op = KOp::nop;
            } else if constexpr (std::is_same_v<T, LoadConstant>) {
                const auto value
                    = Impl::coerce_value_kind(op.value, ValueKind::logic4);
                if (value.width() > 64U || value.width() == 0U) {
                    throw reject("wide_constant");
                }
                inst.op = KOp::constant;
                inst.d = narrow(op.destination);
                const auto aval = value.aval_words();
                const auto bval = value.bval_words();
                inst.imm_a = aval.empty() ? 0U : aval[0];
                inst.imm_b = bval.empty() ? 0U : bval[0];
            } else if constexpr (std::is_same_v<T, CopyRegister>) {
                inst.op = KOp::copy;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
            } else if constexpr (std::is_same_v<T, ReadSignal>) {
                if (op.destination < register_count
                    && (field_slot[op.destination] != no_slot
                        || field_family[op.destination] != no_slot)) {
                    inst.op = KOp::nop;
                    return;
                }
                inst.d = narrow(op.destination);
                if (const auto slot = owned_narrow_slot(op.signal)) {
                    inst.op = KOp::load_slot;
                    inst.x = slots_[*slot].offset;
                } else {
                    if (signal_width(op.signal) > 64U) {
                        throw reject("wide_host_read");
                    }
                    inst.op = KOp::load_host;
                    inst.x = op.signal;
                }
            } else if constexpr (std::is_same_v<T, Binary>) {
                if (op.operation == BinaryOperator::vhdl_match_equal) {
                    throw reject("vhdl_match");
                }
                if (w(op.lhs) != w(op.rhs)) {
                    throw reject("binary_widths");
                }
                inst.op = KOp::binary;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = op.lhs;
                inst.y = op.rhs;
                inst.width = w(op.lhs);
            } else if constexpr (std::is_same_v<T, Reduction>) {
                inst.op = KOp::reduce;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
                inst.width = w(op.source);
            } else if constexpr (std::is_same_v<T, UnaryNot>) {
                inst.op = KOp::unary_not;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
                inst.width = w(op.source);
            } else if constexpr (std::is_same_v<T, LogicalNot>) {
                inst.op = KOp::logical_not;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
            } else if constexpr (std::is_same_v<T, LogicalBinary>) {
                inst.op = KOp::logical_binary;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = narrow(op.lhs);
                inst.y = narrow(op.rhs);
            } else if constexpr (std::is_same_v<T, Shift>) {
                inst.op = KOp::shift;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = narrow(op.value);
                inst.y = narrow(op.amount);
                inst.width = w(op.value);
                inst.offset = w(op.amount);
                inst.flags = op.signed_amount ? flag_signed : 0U;
            } else if constexpr (std::is_same_v<T, Extract>) {
                if (op.source < register_count
                    && field_family[op.source] != no_slot) {
                    const auto* leaf = family_leaf(field_family[op.source],
                        op.offset, op.width);
                    if (leaf == nullptr) {
                        throw reject("extract_range");
                    }
                    const auto slot = leaf->slot;
                    inst.d = narrow(op.destination);
                    inst.x = slot;
                    inst.offset = op.offset - leaf->offset;
                    inst.width = op.width;
                    inst.imm_a = slots_[slot].words;
                    if (slots_[slot].planes == 4U) {
                        inst.op = KOp::load_field9;
                        inst.sub = kind_of(op.source) == ValueKind::logic9 ? 0U : 1U;
                    } else {
                        inst.op = KOp::load_field;
                    }
                    return;
                }
                if (op.source < register_count && field_slot[op.source] != no_slot) {
                    const auto slot = field_slot[op.source];
                    if (op.width == 0U || op.width > 64U
                        || op.offset > slots_[slot].width
                        || op.width > slots_[slot].width - op.offset) {
                        throw reject("extract_range");
                    }
                    if (slots_[slot].planes == 4U) {
                        // The read register's kind decides exact or coerced.
                        inst.op = KOp::load_field9;
                        inst.sub = kind_of(op.source) == ValueKind::logic9 ? 0U : 1U;
                        inst.d = narrow(op.destination);
                        inst.x = slot;
                        inst.offset = op.offset;
                        inst.width = op.width;
                        inst.imm_a = slots_[slot].words;
                        return;
                    }
                    inst.op = KOp::load_field;
                    inst.d = narrow(op.destination);
                    inst.x = slot;
                    inst.offset = op.offset;
                    inst.width = op.width;
                    inst.imm_a = slots_[slot].words;
                    return;
                }
                if (op.source < register_count && width[op.source] > 64U) {
                    if (op.width == 0U || op.width > 64U
                        || op.offset > width[op.source]
                        || op.width > width[op.source] - op.offset) {
                        throw reject("extract_range");
                    }
                    inst.op = KOp::extract;
                    inst.sub = 2U;
                    inst.d = narrow(op.destination);
                    inst.x = op.source;
                    inst.offset = op.offset;
                    inst.width = op.width;
                    return;
                }
                if (op.width == 0U || op.offset > w(op.source)
                    || op.width > w(op.source) - op.offset) {
                    throw reject("extract_range");
                }
                inst.op = KOp::extract;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
                inst.offset = op.offset;
                inst.width = op.width;
            } else if constexpr (std::is_same_v<T, Insert>) {
                if (op.offset > w(op.target)
                    || w(op.source) > w(op.target) - op.offset) {
                    throw reject("insert_range");
                }
                inst.op = KOp::insert;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.target);
                inst.y = narrow(op.source);
                inst.offset = op.offset;
                inst.width = w(op.source);
            } else if constexpr (std::is_same_v<T, Concatenate>) {
                std::uint32_t total = 0U;
                inst.aux = static_cast<std::uint32_t>(body.concat.size());
                for (const auto operand : op.operands) {
                    total += w(operand);
                    body.concat.push_back({ operand, w(operand) });
                }
                if (total != op.width || op.operands.empty()) {
                    throw reject("concat_width");
                }
                inst.op = KOp::concat;
                inst.d = narrow(op.destination);
                inst.x = static_cast<std::uint32_t>(op.operands.size());
                inst.width = op.width;
            } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                if (w(op.condition) != 1U || w(op.when_true) != w(op.when_false)) {
                    throw reject("conditional_widths");
                }
                inst.op = KOp::conditional;
                inst.d = narrow(op.destination);
                inst.x = op.condition;
                inst.y = op.when_true;
                inst.z = op.when_false;
                inst.width = w(op.when_true);
            } else if constexpr (std::is_same_v<T, ConvertToTwoState>) {
                inst.op = KOp::two_state;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
            } else if constexpr (std::is_same_v<T, DynamicExtract>) {
                if (w(op.selection.index) != 32U) {
                    throw reject("dynamic_index_width");
                }
                inst.op = KOp::dynamic_extract;
                inst.d = narrow(op.destination);
                inst.y = op.selection.index;
                if (op.source < register_count && field_slot[op.source] != no_slot) {
                    inst.sub = 1U;
                    inst.x = field_slot[op.source];
                    inst.width = slots_[field_slot[op.source]].width;
                    // Logic9 slot read into a Logic4 register: coerce.
                    inst.imm_b = slots_[inst.x].planes == 4U
                            && kind_of(op.source) != ValueKind::logic9
                        ? 1U : 0U;
                } else if (op.source < register_count && width[op.source] > 64U) {
                    inst.sub = 2U;
                    inst.x = op.source;
                    inst.width = width[op.source];
                } else {
                    inst.x = narrow(op.source);
                    inst.width = w(op.source);
                }
                inst.aux = static_cast<std::uint32_t>(body.indices.size());
                inst.flags = op.selection.strict ? flag_strict : 0U;
                body.indices.push_back(op.selection);
            } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
                if (w(op.base) != 32U || op.width == 0U || op.width > 64U) {
                    throw reject("part_select");
                }
                inst.op = KOp::dynamic_part_select;
                inst.d = narrow(op.destination);
                inst.y = op.base;
                if (op.source < register_count && field_slot[op.source] != no_slot) {
                    inst.sub = 1U;
                    inst.x = field_slot[op.source];
                    inst.offset = slots_[field_slot[op.source]].width;
                    inst.imm_b = slots_[inst.x].planes == 4U
                            && kind_of(op.source) != ValueKind::logic9
                        ? 1U : 0U;
                    // The slot's plane count, for generated code.
                    inst.z = slots_[inst.x].planes;
                } else if (op.source < register_count && width[op.source] > 64U) {
                    inst.sub = 2U;
                    inst.x = op.source;
                    inst.offset = width[op.source];
                } else {
                    inst.x = narrow(op.source);
                    inst.offset = w(op.source);
                }
                inst.width = op.width;
                inst.aux = static_cast<std::uint32_t>(body.parts.size());
                inst.flags = static_cast<std::uint8_t>(
                    (op.increasing ? flag_increasing : 0U)
                    | (op.source_descending ? flag_descending : 0U)
                    | (op.two_state ? flag_two_state : 0U));
                inst.imm_a = op.base_offset;
                DynamicPartIndex part;
                part.left = op.left;
                part.right = op.right;
                part.base_offset = op.base_offset;
                part.width = op.width;
                part.increasing = op.increasing;
                part.source_descending = op.source_descending;
                body.parts.push_back(part);
            } else if constexpr (std::is_same_v<T, DynamicInsert>) {
                if (w(op.selection.index) != 32U) {
                    throw reject("dynamic_index_width");
                }
                inst.op = KOp::dynamic_insert;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.target);
                inst.y = narrow(op.source);
                inst.z = op.selection.index;
                inst.width = w(op.target);
                inst.offset = w(op.source);
                inst.aux = static_cast<std::uint32_t>(body.indices.size());
                inst.flags = op.selection.strict ? flag_strict : 0U;
                body.indices.push_back(op.selection);
            } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
                if (w(op.selection.base) != 32U || op.selection.width == 0U
                    || w(op.source) != op.selection.width) {
                    throw reject("part_insert");
                }
                inst.op = KOp::dynamic_part_insert;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.target);
                inst.y = narrow(op.source);
                inst.z = op.selection.base;
                inst.width = w(op.target);
                inst.aux = static_cast<std::uint32_t>(body.parts.size());
                body.parts.push_back(op.selection);
            } else if constexpr (std::is_same_v<T, Jump>) {
                inst.op = KOp::jump;
                inst.d = map_target(op.target);
            } else if constexpr (std::is_same_v<T, Branch>) {
                inst.op = KOp::branch;
                if (op.condition < register_count && width[op.condition] > 64U) {
                    // sub 2: the condition is a wide or polymorphic register.
                    inst.sub = 2U;
                } else if (w(op.condition) != 1U) {
                    throw reject("branch_width");
                }
                inst.x = op.condition;
                inst.y = map_target(op.when_true);
                inst.z = map_target(op.when_false);
                inst.flags = op.unknown_policy == UnknownBranchPolicy::when_false
                    ? flag_linear : 0U;
            } else if constexpr (std::is_same_v<T, WriteBlocking>
                || std::is_same_v<T, WriteBlockingSlice>
                || std::is_same_v<T, WriteUpdate>
                || std::is_same_v<T, WriteUpdateSlice>) {
                constexpr bool blocking = std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>;
                constexpr bool slice = std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdateSlice>;
                SignalUpdateDomain domain = SignalUpdateDomain::systemverilog_active;
                if constexpr (!blocking) {
                    domain = op.domain;
                }
                std::uint32_t offset = 0U;
                if constexpr (slice) {
                    offset = op.offset;
                }
                const auto value_width = w(op.source);
                inst.x = op.source;
                inst.offset = offset;
                inst.width = value_width;
                if (const auto* leaf = proxy_leaf(op.signal, offset, value_width)) {
                    // The whole leaf (see the generic decision).
                    inst.d = leaf->slot;
                    inst.offset = 0U;
                    inst.sub = slots_[leaf->slot].words != 1U ? 1U : 0U;
                    inst.imm_a = slots_[leaf->slot].words;
                    inst.op = !blocking
                            && domain == SignalUpdateDomain::systemverilog_nba
                        ? KOp::store_slot_nba : KOp::store_slot;
                    inst.flags = 0U;
                } else if (const auto slot = owned_slot(op.signal)) {
                    // Sub 1: a narrow slice of a wide slot; imm_a is the
                    // slot's word count.
                    inst.sub = slots_[*slot].words != 1U ? 1U : 0U;
                    inst.imm_a = slots_[*slot].words;
                    if (offset > slots_[*slot].width
                        || value_width > slots_[*slot].width - offset
                        || (!slice && value_width != slots_[*slot].width)) {
                        throw reject("write_width");
                    }
                    inst.d = *slot;
                    inst.op = !blocking
                            && domain == SignalUpdateDomain::systemverilog_nba
                        ? KOp::store_slot_nba : KOp::store_slot;
                    inst.flags = slice ? flag_linear : 0U;
                } else {
                    inst.op = KOp::store_host;
                    inst.d = op.signal;
                    inst.sub = static_cast<std::uint8_t>(domain);
                    inst.flags = static_cast<std::uint8_t>(
                        (blocking ? flag_blocking : 0U)
                        | (slice ? flag_linear : 0U));
                }
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                || std::is_same_v<T, WriteUpdateDynamicSlice>) {
                constexpr bool blocking
                    = std::is_same_v<T, WriteBlockingDynamicSlice>;
                const auto slot = owned_slot(op.signal);
                if (!slot || w(op.selection.index) != 32U) {
                    throw reject("dynamic_write");
                }
                inst.op = KOp::store_slot_dynamic;
                inst.d = *slot;
                inst.x = op.source;
                inst.y = op.selection.index;
                inst.width = w(op.source);
                inst.aux = static_cast<std::uint32_t>(body.indices.size());
                body.indices.push_back(op.selection);
                if constexpr (!blocking) {
                    inst.flags = op.domain == SignalUpdateDomain::systemverilog_nba
                        ? flag_nba : 0U;
                }
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>
                || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                constexpr bool blocking
                    = std::is_same_v<T, WriteBlockingDynamicPartSlice>;
                const auto slot = owned_slot(op.signal);
                if (!slot || w(op.selection.base) != 32U
                    || w(op.source) != op.selection.width) {
                    throw reject("dynamic_part_write");
                }
                inst.op = KOp::store_slot_part;
                inst.d = *slot;
                inst.x = op.source;
                inst.y = op.selection.base;
                inst.width = w(op.source);
                inst.aux = static_cast<std::uint32_t>(body.parts.size());
                body.parts.push_back(op.selection);
                if constexpr (!blocking) {
                    inst.flags = op.domain == SignalUpdateDomain::systemverilog_nba
                        ? flag_nba : 0U;
                }
            } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                inst.op = KOp::nop;
            } else if constexpr (std::is_same_v<T, ContainerRead>) {
                if (op.source >= container_of_register.size()
                    || container_of_register[op.source] == no_container
                    || op.string_index) {
                    throw reject("container_read");
                }
                const auto container = container_of_register[op.source];
                if (containers_[container].type.element_width > 64U
                    || containers_[container].type.associative
                    || !containers_[container].type.fixed) {
                    throw reject("container_shape");
                }
                inst.op = KOp::mem_read;
                inst.d = narrow(op.destination);
                inst.x = container;
                inst.y = narrow(op.index);
                inst.width = w(op.index);
                inst.flags = op.linear_index ? flag_linear : 0U;
            } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                // A blocking element write of a private array.
                if (op.target >= container_of_register.size()
                    || container_of_register[op.target] == no_container
                    || containers_[container_of_register[op.target]].object
                        != no_container_object
                    || op.string_index) {
                    throw reject("container_write");
                }
                inst.op = KOp::mem_write;
                inst.d = container_of_register[op.target];
                inst.x = narrow(op.source);
                inst.y = narrow(op.index);
                inst.width = w(op.source);
                inst.offset = w(op.index);
                inst.flags = static_cast<std::uint8_t>(
                    (op.linear_index ? flag_linear : 0U)
                    | (op.signed_index ? flag_signed : 0U));
            } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
                if (op.object >= container_of_object_.size()
                    || container_of_object_[op.object] == no_container
                    || op.transaction_signal) {
                    throw reject("container_write");
                }
                inst.op = KOp::mem_write;
                inst.d = container_of_object_[op.object];
                inst.x = narrow(op.source);
                inst.y = narrow(op.index);
                inst.width = w(op.source);
                inst.offset = w(op.index);
                inst.flags = static_cast<std::uint8_t>(
                    (op.nonblocking ? flag_nba : 0U)
                    | (op.linear_index ? flag_linear : 0U)
                    | (op.signed_index ? flag_signed : 0U));
                if (op.dynamic_part) {
                    inst.z = narrow(op.dynamic_part->base);
                    inst.aux = static_cast<std::uint32_t>(body.parts.size())
                        + 1U;
                    body.parts.push_back(*op.dynamic_part);
                }
            } else {
                throw reject("operation");
            }
        }, member.operations[pc]);
        body.code.push_back(inst);
    }
    // Every register an instruction reads must have a known narrow width.
    for (const auto& inst : body.code) {
        const auto check = [&](const std::uint32_t reg) { (void)narrow(reg); };
        switch (inst.op) {
        case KOp::binary:
            check(inst.x);
            check(inst.y);
            break;
        case KOp::conditional:
            check(inst.x);
            check(inst.y);
            check(inst.z);
            break;
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            check(inst.y);
            break;
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
            check(inst.z);
            break;
        case KOp::branch:
            if (inst.sub != 2U) {
                check(inst.x);
            }
            break;
        case KOp::store_slot:
        case KOp::store_slot_nba:
        case KOp::store_host:
            check(inst.x);
            break;
        case KOp::store_slot_dynamic:
        case KOp::store_slot_part:
            check(inst.x);
            check(inst.y);
            break;
        case KOp::store_vhdl:
            check(inst.x);
            if (inst.sub == 1U) {
                check(inst.y);
            }
            break;
        case KOp::integer_binary:
            check(inst.x);
            check(inst.y);
            break;
        case KOp::integer_unary:
        case KOp::integer_check:
        case KOp::assert_check:
            check(inst.x);
            break;
        case KOp::call:
        case KOp::ret:
            if ((inst.flags & flag_linear) == 0U) {
                check(inst.x);
            }
            break;
        default:
            break;
        }
    }
    if (behavioral) {
        // A thread starts at operation 0, resumes after each suspension, and
        // fork children start at their branches.
        body.entry = 0U;
        body.resume_entries.push_back(0U);
        for (std::uint32_t pc = 0U; pc < end; ++pc) {
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Fork>) {
                    body.resume_entries.insert(body.resume_entries.end(),
                        op.branches.begin(), op.branches.end());
                }
                if constexpr (behavioral_suspension<T>
                    || std::is_same_v<T, WaitSensitivity>) {
                    body.resume_entries.push_back(pc + 1U);
                }
            }, member.operations[pc]);
        }
        std::ranges::sort(body.resume_entries);
        body.resume_entries.erase(std::unique(body.resume_entries.begin(),
                                      body.resume_entries.end()),
            body.resume_entries.end());
        std::erase_if(body.resume_entries,
            [&](const std::uint32_t entry) { return entry >= end; });
        std::ranges::sort(body.return_targets);
        body.return_targets.erase(std::unique(body.return_targets.begin(),
                                      body.return_targets.end()),
            body.return_targets.end());
    }
    if (vhdl) {
        eliminate_dead_constants(body, member_index);
        track_unknowns(body, member_index);
        body.entry = member.body_begin;
        body.live_at_entry = std::move(live_at_entry);
        std::ranges::sort(body.return_targets);
        body.return_targets.erase(std::unique(body.return_targets.begin(),
                                      body.return_targets.end()),
            body.return_targets.end());
    }
    if (has_wide) {
        body.wide_registers.assign(register_count, PackedLogic4 { });
    }
    return body;
}

} // namespace fsim::runtime::simir
