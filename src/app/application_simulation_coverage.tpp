// SPDX-License-Identifier: Apache-2.0

    [[nodiscard]] static artifact::CoverageDatabaseIdentity
    coverage_identity_from_digest(
        const support::Sha256::Digest& digest) noexcept
    {
        artifact::CoverageDatabaseIdentity identity;
        for (std::size_t index = 0U; index < 8U; ++index) {
            identity.high = (identity.high << 8U) | digest[index];
            identity.low = (identity.low << 8U) | digest[index + 8U];
        }
        if (identity.high == 0U && identity.low == 0U) {
            identity.low = 1U;
        }
        return identity;
    }

    static void coverage_hash_u64(
        support::Sha256& hasher, const std::uint64_t value) noexcept
    {
        std::array<std::byte, sizeof(value)> encoded;
        for (std::size_t index = 0U; index < encoded.size(); ++index) {
            encoded[index] = static_cast<std::byte>(
                (value >> (index * 8U)) & UINT64_C(0xff));
        }
        hasher.update(encoded);
    }

    [[nodiscard]] static artifact::CoverageDatabaseDigest
    coverage_digest(const support::Sha256::Digest& digest) noexcept
    {
        artifact::CoverageDatabaseDigest result;
        std::ranges::transform(digest, result.begin(),
            [](const auto value) { return static_cast<std::byte>(value); });
        return result;
    }

    [[nodiscard]] artifact::CoverageDatabaseIdentity
    standard_run_identity()
    {
        if (!standard_coverage_run_identity) {
            static std::atomic<std::uint64_t> sequence { 1U };
            support::Sha256 hasher;
            hasher.update("fsim-v3-standard-coverage-run");
            hasher.update(built.cache_key);
            const auto now = std::chrono::system_clock::now()
                                 .time_since_epoch()
                                 .count();
            const auto serial = sequence.fetch_add(
                1U, std::memory_order_relaxed);
            coverage_hash_u64(hasher, static_cast<std::uint64_t>(now));
            coverage_hash_u64(hasher, serial);
            standard_coverage_run_identity
                = coverage_identity_from_digest(hasher.finish());
        }
        return *standard_coverage_run_identity;
    }

    [[nodiscard]] std::optional<artifact::CoverageDatabaseContents>
    fallback_standard_coverage_database()
    {
        const auto counters = interpreter->code_coverage_counters();
        if (counters.empty()) {
            return std::nullopt;
        }
        artifact::CoverageDatabaseContents contents;
        const auto source_digest = support::Sha256::digest(
            built.cache_key + "-coverage-source");
        contents.sources.push_back({
            coverage_identity_from_digest(source_digest),
            "design/coverage",
            0U,
            coverage_digest(support::Sha256::digest(built.cache_key)),
        });
        contents.fingerprint.digest = coverage_digest(
            support::Sha256::digest(
                "fsim-v3-code-coverage-model:" + built.cache_key));
        const auto run_identity = standard_run_identity();
        const auto& scheduler = interpreter->scheduler();
        contents.runs.push_back({
            run_identity,
            "standard-coverage-api",
            "fsim-v3",
            built.seed,
            scheduler.now(),
            scheduler.delta(),
            lifecycle == Lifecycle::finished
                ? artifact::CoverageDatabaseRunStatus::Complete
                : artifact::CoverageDatabaseRunStatus::Stopped,
        });
        std::set<std::uint32_t> emitted_counters;
        for (const auto& process_info : built.design_ir.processes()) {
            const auto& process = interpreter->process_program(
                process_info.runtime_index);
            support::Sha256 identity_hasher;
            identity_hasher.update("fsim-v3-code-coverage-process");
            identity_hasher.update(process.name);
            coverage_hash_u64(
                identity_hasher, process_info.runtime_index);
            const auto instance_identity = coverage_identity_from_digest(
                identity_hasher.finish());
            for (std::size_t instruction = 0U;
                instruction < process.operations.size(); ++instruction) {
                const auto* hit = runtime::simir::operation_get_if<
                    runtime::simir::CodeCoverageHit>(
                    &process.operations[instruction]);
                if (hit == nullptr) {
                    continue;
                }
                const auto counter = process.operations.code_coverage_counter(
                    instruction, hit->counter);
                if (counter.value >= counters.size()
                    || !emitted_counters.insert(counter.value).second) {
                    continue;
                }
                const auto family = hit->metric
                        == runtime::CodeCoverageMetric::Statement
                    ? artifact::CoverageDatabaseMetricFamily::Statement
                    : artifact::CoverageDatabaseMetricFamily::Branch;
                contents.metrics.push_back({
                    artifact::CoverageDatabaseNamespace::Code,
                    family,
                    artifact::CoverageDatabaseMetricScope::Instance,
                    { hit->point.high, hit->point.low },
                    contents.sources.front().identity,
                    instance_identity,
                    run_identity,
                    counters[counter.value],
                    0U,
                    interpreter->code_coverage_counter_overflowed(counter),
                    false,
                });
            }
        }
        auto model = artifact::make_coverage_database_contents(
            std::move(contents));
        return model.ok() ? std::move(model.contents) : std::nullopt;
    }

    [[nodiscard]] std::optional<artifact::CoverageDatabaseContents>
    current_standard_coverage_database()
    {
        if (!built.code_coverage_enabled) {
            return std::nullopt;
        }
        const auto& inventory = built.design.code_coverage_inventory();
        if (!inventory) {
            return fallback_standard_coverage_database();
        }
        const auto counters = interpreter->code_coverage_counters();
        if (counters.size() != inventory->total_points) {
            return std::nullopt;
        }

        artifact::CoverageDatabaseContents contents;
        support::Sha256 fingerprint;
        fingerprint.update("fsim-v3-code-coverage-model");
        for (const auto& source : inventory->sources) {
            fingerprint.update(std::as_bytes(std::span {
                source.identity.digest.data(),
                source.identity.digest.size() }));
            artifact::CoverageDatabaseSourceRecord record;
            record.identity = coverage_identity_from_digest(
                source.identity.digest);
            record.logical_path = source.identity.logical_path;
            record.content_bytes = source.identity.content_bytes;
            record.content_digest = coverage_digest(
                source.identity.content_digest);
            contents.sources.push_back(std::move(record));
        }
        for (const auto& instance : inventory->instances) {
            coverage_hash_u64(fingerprint, instance.identity.high);
            coverage_hash_u64(fingerprint, instance.identity.low);
            for (const auto& point : instance.points) {
                coverage_hash_u64(fingerprint, point.point.id.high);
                coverage_hash_u64(fingerprint, point.point.id.low);
                coverage_hash_u64(fingerprint,
                    static_cast<std::uint64_t>(point.point.metric));
            }
        }
        contents.fingerprint.digest = coverage_digest(fingerprint.finish());

        const auto run_identity = standard_run_identity();
        const auto& scheduler = interpreter->scheduler();
        contents.runs.push_back({
            run_identity,
            "standard-coverage-api",
            "fsim-v3",
            built.seed,
            scheduler.now(),
            scheduler.delta(),
            lifecycle == Lifecycle::finished
                ? artifact::CoverageDatabaseRunStatus::Complete
                : artifact::CoverageDatabaseRunStatus::Stopped,
        });
        for (const auto& instance : inventory->instances) {
            const artifact::CoverageDatabaseIdentity instance_identity {
                instance.identity.high, instance.identity.low };
            for (const auto& point : instance.points) {
                if (point.source_index >= contents.sources.size()
                    || point.point.counter.value >= counters.size()) {
                    return std::nullopt;
                }
                const auto family = point.point.metric
                        == runtime::CodeCoverageMetric::Statement
                    ? artifact::CoverageDatabaseMetricFamily::Statement
                    : point.point.metric
                            == runtime::CodeCoverageMetric::Branch
                    ? artifact::CoverageDatabaseMetricFamily::Branch
                    : artifact::CoverageDatabaseMetricFamily::Line;
                contents.metrics.push_back({
                    artifact::CoverageDatabaseNamespace::Code,
                    family,
                    artifact::CoverageDatabaseMetricScope::Instance,
                    { point.point.id.high, point.point.id.low },
                    contents.sources[point.source_index].identity,
                    instance_identity,
                    run_identity,
                    counters[point.point.counter.value],
                    0U,
                    interpreter->code_coverage_counter_overflowed(
                        point.point.counter),
                    false,
                });
            }
        }
        auto model = artifact::make_coverage_database_contents(
            std::move(contents));
        return model.ok() ? std::move(model.contents) : std::nullopt;
    }

    [[nodiscard]] std::optional<artifact::CoverageDatabaseContents>
    aggregate_standard_coverage_database()
    {
        auto current = current_standard_coverage_database();
        if (!current || !standard_coverage_history) {
            return current;
        }
        const std::array inputs {
            std::cref(*standard_coverage_history), std::cref(*current) };
        std::array<artifact::CoverageDatabaseContents, 2U> copies {
            inputs[0].get(), inputs[1].get() };
        auto merged = artifact::merge_coverage_databases(copies);
        return merged.ok() ? std::move(merged.contents) : std::nullopt;
    }

    struct StandardCoverageSelection {
        std::vector<std::string> instances;
        std::vector<runtime::CodeCoverageCounterId> counters;
        std::set<artifact::CoverageDatabaseIdentity> identities;
        std::size_t available_instances { };
    };

    [[nodiscard]] static bool standard_module_name_matches(
        const std::string_view target,
        const std::string_view selector) noexcept
    {
        if (target == selector) {
            return true;
        }
        const auto separator = target.find_last_of(".:/");
        return separator != std::string_view::npos
            && target.substr(separator + 1U) == selector;
    }

    [[nodiscard]] std::optional<StandardCoverageSelection>
    select_standard_coverage(
        const std::int32_t scope_value,
        std::string selector,
        const std::string_view instance_context,
        const bool selector_is_instance) const
    {
        using Scope = runtime::simir::SystemVerilogCoverageScope;
        const auto scope = static_cast<Scope>(scope_value);
        if ((scope != Scope::module && scope != Scope::hierarchy)
            || selector.empty()) {
            return std::nullopt;
        }

        const auto& occurrences = built.design_ir.instances();
        std::vector<std::string> seeds;
        if (selector_is_instance) {
            if (selector == "$root") {
                for (const auto& occurrence : occurrences) {
                    if (!occurrence.parent) {
                        seeds.push_back(occurrence.path);
                    }
                }
            } else {
                constexpr std::string_view root_prefix = "$root.";
                if (selector.starts_with(root_prefix)) {
                    selector.erase(0U, root_prefix.size());
                }
                const auto exact = std::ranges::find(
                    occurrences, selector, &semantic::design::InstanceOccurrence::path);
                if (exact != occurrences.end()) {
                    seeds.push_back(exact->path);
                } else if (!instance_context.empty()) {
                    auto relative = std::string { instance_context } + '.'
                        + selector;
                    const auto found = std::ranges::find(occurrences,
                        relative,
                        &semantic::design::InstanceOccurrence::path);
                    if (found != occurrences.end()) {
                        seeds.push_back(found->path);
                    }
                }
            }
        } else {
            for (const auto& occurrence : occurrences) {
                if (standard_module_name_matches(
                        occurrence.target, selector)) {
                    seeds.push_back(occurrence.path);
                }
            }
        }
        if (seeds.empty()) {
            return std::nullopt;
        }

        StandardCoverageSelection result;
        for (const auto& occurrence : occurrences) {
            const bool selected = std::ranges::any_of(
                seeds, [&](const std::string& seed) {
                    return occurrence.path == seed
                        || (scope == Scope::hierarchy
                            && occurrence.path.size() > seed.size()
                            && occurrence.path.starts_with(seed)
                            && occurrence.path[seed.size()] == '.');
                });
            if (selected) {
                result.instances.push_back(occurrence.path);
            }
        }
        std::ranges::sort(result.instances);
        result.instances.erase(
            std::unique(result.instances.begin(), result.instances.end()),
            result.instances.end());

        if (const auto& inventory = built.design.code_coverage_inventory()) {
            for (const auto& instance : inventory->instances) {
                if (!std::ranges::binary_search(
                        result.instances, instance.instance)) {
                    continue;
                }
                std::size_t before = result.counters.size();
                for (const auto& point : instance.points) {
                    if (point.point.metric
                        == runtime::CodeCoverageMetric::Statement) {
                        result.counters.push_back(point.point.counter);
                    }
                }
                if (result.counters.size() != before) {
                    ++result.available_instances;
                    result.identities.insert({
                        instance.identity.high, instance.identity.low });
                }
            }
        }
        if (result.counters.empty() && built.code_coverage_enabled) {
            std::set<std::string> available;
            const auto& specializations = built.design_ir.specializations();
            for (const auto& occurrence : built.design_ir.processes()) {
                if (occurrence.specialization.value()
                        >= specializations.size()) {
                    return std::nullopt;
                }
                const auto& specialization
                    = specializations[occurrence.specialization.value()];
                if (specialization.instance.value()
                        >= occurrences.size()) {
                    return std::nullopt;
                }
                const auto& path
                    = occurrences[specialization.instance.value()].path;
                if (!std::ranges::binary_search(result.instances, path)) {
                    continue;
                }
                const auto& process = interpreter->process_program(
                    occurrence.runtime_index);
                bool has_statement { };
                for (std::size_t instruction = 0U;
                    instruction < process.operations.size(); ++instruction) {
                    const auto* hit = runtime::simir::operation_get_if<
                        runtime::simir::CodeCoverageHit>(
                        &process.operations[instruction]);
                    if (hit == nullptr
                        || hit->metric
                            != runtime::CodeCoverageMetric::Statement) {
                        continue;
                    }
                    result.counters.push_back(
                        process.operations.code_coverage_counter(
                            instruction, hit->counter));
                    has_statement = true;
                }
                if (has_statement) {
                    available.insert(path);
                }
            }
            result.available_instances = available.size();
        }
        std::ranges::sort(result.counters, { },
            &runtime::CodeCoverageCounterId::value);
        result.counters.erase(
            std::unique(result.counters.begin(), result.counters.end()),
            result.counters.end());
        return result;
    }

    [[nodiscard]] static std::int32_t query_standard_coverage(
        const artifact::CoverageDatabaseContents& contents,
        const bool maximum,
        const std::set<artifact::CoverageDatabaseIdentity>* identities
        = nullptr) noexcept
    {
        using Key = std::tuple<artifact::CoverageDatabaseIdentity,
            artifact::CoverageDatabaseIdentity,
            artifact::CoverageDatabaseIdentity>;
        std::set<Key> bins;
        std::set<Key> covered;
        for (const auto& metric : contents.metrics) {
            if (metric.name_space
                    != artifact::CoverageDatabaseNamespace::Code
                || metric.family
                    != artifact::CoverageDatabaseMetricFamily::Statement) {
                continue;
            }
            if (identities != nullptr
                && !identities->contains(metric.instance_identity)) {
                continue;
            }
            const Key key { metric.source_identity,
                metric.instance_identity, metric.bin_identity };
            bins.insert(key);
            if (metric.hits != 0U) {
                covered.insert(key);
            }
        }
        const auto count = maximum ? bins.size() : covered.size();
        return count > static_cast<std::size_t>(
                           std::numeric_limits<std::int32_t>::max())
            ? static_cast<std::int32_t>(
                  runtime::simir::SystemVerilogCoverageStatus::overflow)
            : static_cast<std::int32_t>(count);
    }

    [[nodiscard]] std::int32_t access_standard_coverage(
        const runtime::simir::CoverageAccessEvent& event) noexcept
    {
        using Access = runtime::simir::SystemVerilogCoverageAccessKind;
        using Status = runtime::simir::SystemVerilogCoverageStatus;
        using Type = runtime::simir::SystemVerilogCoverageType;
        const auto status = [](const Status value) {
            return static_cast<std::int32_t>(value);
        };
        try {
            const auto type = static_cast<Type>(event.coverage_type);
            if (type != Type::assertion && type != Type::fsm_state
                && type != Type::statement && type != Type::toggle) {
                return status(Status::error);
            }
            if (type != Type::statement) {
                return status(Status::no_coverage);
            }
            if (event.kind == Access::get || event.kind == Access::get_max) {
                if (!event.scope) {
                    return status(Status::error);
                }
                const auto selection = select_standard_coverage(
                    *event.scope, event.selector, event.instance_context,
                    event.selector_is_instance);
                if (!selection) {
                    return status(Status::error);
                }
                if (selection->counters.empty()) {
                    return status(Status::no_coverage);
                }
                if (selection->identities.empty()) {
                    const auto counters = interpreter->code_coverage_counters();
                    std::size_t count { };
                    for (const auto counter : selection->counters) {
                        if (counter.value >= counters.size()) {
                            return status(Status::error);
                        }
                        count += event.kind == Access::get_max
                            || counters[counter.value] != 0U
                            ? 1U
                            : 0U;
                    }
                    return count > static_cast<std::size_t>(
                                       std::numeric_limits<std::int32_t>::max())
                        ? status(Status::overflow)
                        : static_cast<std::int32_t>(count);
                }
                const auto contents = aggregate_standard_coverage_database();
                return contents
                    ? query_standard_coverage(
                          *contents, event.kind == Access::get_max,
                          selection->identities.empty()
                              ? nullptr
                              : &selection->identities)
                    : status(Status::no_coverage);
            }
            if (event.filename.empty()) {
                return status(Status::error);
            }
            const auto path = coverage_database_file(event.filename);
            if (event.kind == Access::save) {
                auto contents = aggregate_standard_coverage_database();
                if (!contents) {
                    return status(Status::no_coverage);
                }
                return artifact::write_coverage_database_atomically(
                           path, std::move(*contents)).ok()
                    ? status(Status::ok)
                    : status(Status::error);
            }
            if (event.kind == Access::merge) {
                const auto loaded = artifact::read_coverage_database(path);
                const auto current = current_standard_coverage_database();
                if (!loaded.ok() || !current) {
                    return status(Status::error);
                }
                std::vector<artifact::CoverageDatabaseContents> inputs;
                if (standard_coverage_history) {
                    inputs.push_back(*standard_coverage_history);
                }
                inputs.push_back(*loaded.contents);
                inputs.push_back(*current);
                auto merged = artifact::merge_coverage_databases(inputs);
                if (!merged.ok()) {
                    return status(Status::error);
                }
                inputs.pop_back();
                auto history = artifact::merge_coverage_databases(inputs);
                if (!history.ok()) {
                    return status(Status::error);
                }
                standard_coverage_history = std::move(*history.contents);
                return status(Status::ok);
            }
        } catch (...) {
            return status(Status::error);
        }
        return status(Status::error);
    }

    [[nodiscard]] std::int32_t control_standard_coverage(
        const runtime::simir::CoverageControlEvent& event) noexcept
    {
        using Command = runtime::simir::SystemVerilogCoverageCommand;
        using Status = runtime::simir::SystemVerilogCoverageStatus;
        using Type = runtime::simir::SystemVerilogCoverageType;
        const auto status = [](const Status value) {
            return static_cast<std::int32_t>(value);
        };
        try {
            const auto type = static_cast<Type>(event.coverage_type);
            if (type != Type::assertion && type != Type::fsm_state
                && type != Type::statement && type != Type::toggle) {
                return status(Status::error);
            }
            if (type != Type::statement) {
                return status(Status::no_coverage);
            }
            const auto selection = select_standard_coverage(
                event.scope, event.selector, event.instance_context,
                event.selector_is_instance);
            if (!selection) {
                return status(Status::error);
            }
            if (selection->counters.empty()) {
                return status(Status::no_coverage);
            }
            const bool partial = selection->available_instances
                != selection->instances.size();
            switch (static_cast<Command>(event.command)) {
            case Command::start:
                if (!interpreter->set_code_coverage_collection_enabled(
                        selection->counters, true)) {
                    return status(Status::error);
                }
                return status(partial ? Status::partial : Status::ok);
            case Command::stop:
                if (!interpreter->set_code_coverage_collection_enabled(
                        selection->counters, false)) {
                    return status(Status::error);
                }
                return status(partial ? Status::partial : Status::ok);
            case Command::reset:
                if (!interpreter->reset_code_coverage_counters(
                        selection->counters)) {
                    return status(Status::error);
                }
                return status(partial ? Status::partial : Status::ok);
            case Command::check: {
                const auto overflows = interpreter->code_coverage_overflow_count(
                    selection->counters);
                if (!overflows) {
                    return status(Status::error);
                }
                return *overflows != 0U
                    ? status(Status::overflow)
                    : status(partial ? Status::partial : Status::ok);
            }
            }
        } catch (...) {
            return status(Status::error);
        }
        return status(Status::error);
    }

    [[nodiscard]] std::optional<
        runtime::SystemVerilogVpiCoverageStatistics>
    standard_vpi_coverage_statistics(
        const fsim_vpi_handle_v1 handle) const noexcept
    {
        runtime::SystemVerilogVpiCoverageStatistics result;
        if (const auto statements = vpi_statement_counters.find(handle);
            statements != vpi_statement_counters.end()) {
            const auto counters = interpreter->code_coverage_counters();
            result.coverable_items = statements->second.size();
            for (const auto counter : statements->second) {
                if (counter.value >= counters.size()) {
                    return std::nullopt;
                }
                const auto hits = counters[counter.value];
                result.covered_items += hits != 0U ? 1U : 0U;
                if (interpreter->code_coverage_counter_overflowed(counter)
                    || result.covered_count
                        > std::numeric_limits<std::uint64_t>::max() - hits) {
                    result.covered_count
                        = std::numeric_limits<std::uint64_t>::max();
                } else {
                    result.covered_count += hits;
                }
            }
            return result;
        }
        const auto keys = vpi_assertion_coverage_keys.find(handle);
        if (keys == vpi_assertion_coverage_keys.end()) {
            return std::nullopt;
        }
        const auto key_matches = [&](const ConcurrentAssertionCoverage& item) {
            return std::ranges::find(keys->second, item.process)
                    != keys->second.end()
                || std::ranges::find(keys->second, item.name)
                    != keys->second.end();
        };
        result.coverable_items = 1U;
        for (const auto& coverage : concurrent_assertion_coverage) {
            if (!key_matches(coverage)) {
                continue;
            }
            const auto add = [](std::uint64_t& destination,
                                 const std::uint64_t value) {
                destination = destination
                        > std::numeric_limits<std::uint64_t>::max() - value
                    ? std::numeric_limits<std::uint64_t>::max()
                    : destination + value;
            };
            add(result.assertion_attempts, coverage.attempts);
            add(result.assertion_successes, coverage.passes);
            add(result.assertion_failures, coverage.failures);
            add(result.assertion_vacuous_successes, coverage.vacuous);
            add(result.assertion_kills, coverage.aborted);
        }
        result.covered_count = result.assertion_successes;
        result.covered_items = result.assertion_attempts != 0U
                && result.assertion_successes != 0U
                && result.assertion_failures == 0U
            ? 1U
            : 0U;
        return result;
    }

    [[nodiscard]] std::int32_t control_standard_vpi_coverage(
        const runtime::SystemVerilogVpiCoverageControlRequest& request) noexcept
    {
        using Control = runtime::SystemVerilogVpiCoverageControl;
        using Status = runtime::simir::SystemVerilogCoverageStatus;
        using Type = runtime::SystemVerilogVpiCoverageType;
        const auto status = [](const Status value) {
            return static_cast<std::int32_t>(value);
        };
        try {
            if (request.type != Type::Statement) {
                return status(Status::no_coverage);
            }
            if (request.control == Control::Merge
                || request.control == Control::Save) {
                runtime::simir::CoverageAccessEvent event;
                event.kind = request.control == Control::Merge
                    ? runtime::simir::SystemVerilogCoverageAccessKind::merge
                    : runtime::simir::SystemVerilogCoverageAccessKind::save;
                event.coverage_type = static_cast<std::int32_t>(
                    runtime::simir::SystemVerilogCoverageType::statement);
                event.filename = request.filename;
                return access_standard_coverage(event);
            }
            if (!request.object) {
                return status(Status::error);
            }
            const auto selected = vpi_statement_counters.find(
                *request.object);
            if (selected == vpi_statement_counters.end()) {
                return status(Status::no_coverage);
            }
            switch (request.control) {
            case Control::Start:
                return interpreter->set_code_coverage_collection_enabled(
                           selected->second, true)
                    ? status(Status::ok)
                    : status(Status::error);
            case Control::Stop:
                return interpreter->set_code_coverage_collection_enabled(
                           selected->second, false)
                    ? status(Status::ok)
                    : status(Status::error);
            case Control::Reset:
                return interpreter->reset_code_coverage_counters(
                           selected->second)
                    ? status(Status::ok)
                    : status(Status::error);
            case Control::Check: {
                const auto overflows = interpreter->code_coverage_overflow_count(
                    selected->second);
                return !overflows
                    ? status(Status::error)
                    : *overflows == 0U ? status(Status::ok)
                                       : status(Status::overflow);
            }
            case Control::Merge:
            case Control::Save:
                break;
            }
        } catch (...) {
            return status(Status::error);
        }
        return status(Status::error);
    }

    void configure_standard_vpi_coverage()
    {
        vpi_coverage = std::make_unique<
            runtime::SystemVerilogVpiCoverageService>(
            *vpi_registry,
            [this](const fsim_vpi_handle_v1 handle) {
                return standard_vpi_coverage_statistics(handle);
            },
            [this](
                const runtime::SystemVerilogVpiCoverageControlRequest&
                    request) {
                return control_standard_vpi_coverage(request);
            });
        if (!vpi_coverage->valid()) {
            throw std::logic_error {
                "failed to construct the standard VPI coverage service"
            };
        }
        if (!vpi_runtime_updates_enabled) {
            return;
        }

        if (const auto& inventory = built.design.code_coverage_inventory()) {
            for (const auto& instance : inventory->instances) {
                const auto object = vpi_registry->find(instance.instance);
                if (!object) {
                    continue;
                }
                auto& counters = vpi_statement_counters[object.value->handle];
                for (const auto& point : instance.points) {
                    if (point.point.metric
                        == runtime::CodeCoverageMetric::Statement) {
                        counters.push_back(point.point.counter);
                    }
                }
                if (counters.empty()) {
                    vpi_statement_counters.erase(object.value->handle);
                }
            }
        } else if (built.code_coverage_enabled) {
            const auto roots = static_cast<std::size_t>(std::ranges::count_if(
                built.design_ir.instances(),
                [](const auto& instance) { return !instance.parent; }));
            if (roots == 1U) {
                const auto root = std::ranges::find_if(
                    built.design_ir.instances(),
                    [](const auto& instance) { return !instance.parent; });
                const auto object = vpi_registry->find(root->path);
                if (object) {
                    auto& counters
                        = vpi_statement_counters[object.value->handle];
                    for (std::size_t index = 0U;
                        index < interpreter->code_coverage_counters().size();
                        ++index) {
                        counters.push_back({
                            static_cast<std::uint32_t>(index) });
                    }
                    if (counters.empty()) {
                        vpi_statement_counters.erase(object.value->handle);
                    }
                }
            }
        }
        const std::array statement {
            runtime::SystemVerilogVpiCoverageType::Statement
        };
        for (auto& [object, counters] : vpi_statement_counters) {
            std::ranges::sort(counters, { },
                &runtime::CodeCoverageCounterId::value);
            counters.erase(std::unique(counters.begin(), counters.end()),
                counters.end());
            const auto published = vpi_coverage->publish_target(
                object, statement);
            if (published != runtime::SystemVerilogVpiCoverageError::None) {
                throw std::logic_error {
                    "failed to publish a standard VPI statement coverage target"
                };
            }
        }

        for (const auto& [key, object] : vpi_assertion_handles) {
            vpi_assertion_coverage_keys[object].push_back(key);
        }
        const std::array assertion {
            runtime::SystemVerilogVpiCoverageType::Assertion
        };
        for (auto& [object, keys] : vpi_assertion_coverage_keys) {
            std::ranges::sort(keys);
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            const auto published = vpi_coverage->publish_target(
                object, assertion);
            if (published != runtime::SystemVerilogVpiCoverageError::None) {
                throw std::logic_error {
                    "failed to publish a standard VPI assertion coverage target"
                };
            }
        }
    }
