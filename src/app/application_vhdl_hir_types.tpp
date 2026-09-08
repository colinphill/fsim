// SPDX-License-Identifier: Apache-2.0

        [[nodiscard]] semantic::DeclarationId add_mode_view_declaration(
            const frontend::TypeAliasDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::mode_view,
                semantic::DeclarationKind::mode_view,
                input.name,
                input.span,
                parent);
            auto& output = declaration(id);
            vh::ModeViewProfile profile;
            if (!input.vhdl_mode_view_converse_of.empty()) {
                profile.converse_of = name(
                    input.vhdl_mode_view_converse_of,
                    input.span,
                    scope);
                output.mode_view = std::move(profile);
                return id;
            }
            profile.record_subtype = subtype(
                input.type, input.span, scope, output.origin);
            profile.elements.reserve(input.vhdl_mode_view_elements.size());
            for (const auto& input_element : input.vhdl_mode_view_elements) {
                vh::ModeViewElement element;
                element.element = vh::Name {
                    input_element.name,
                    canonical_vhdl_name(input_element.name),
                    source(input_element.span),
                    std::nullopt,
                    { }
                };
                switch (input_element.kind) {
                case frontend::VhdlModeViewElementKind::direction:
                    element.form = vh::ModeViewElementForm::direction;
                    element.direction = vhdl_direction(
                        input_element.direction);
                    break;
                case frontend::VhdlModeViewElementKind::record_view:
                    element.form = vh::ModeViewElementForm::record_view;
                    break;
                case frontend::VhdlModeViewElementKind::array_view:
                    element.form = vh::ModeViewElementForm::array_view;
                    break;
                }
                if (!input_element.referenced_view.empty()) {
                    element.referenced_view = name(
                        input_element.referenced_view,
                        input_element.span,
                        scope);
                }
                profile.elements.push_back(std::move(element));
            }
            output.mode_view = std::move(profile);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_type_declaration(
            const frontend::TypeAliasDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (input.declaration_kind
                == frontend::TypeDeclarationKind::VhdlModeView) {
                return add_mode_view_declaration(input, scope, parent);
            }
            const auto subtype_form = input.declaration_kind == frontend::TypeDeclarationKind::VhdlSubtype;
            const auto id = add_declaration_record(
                scope,
                subtype_form ? vh::DeclarationForm::subtype
                             : vh::DeclarationForm::type,
                subtype_form ? semantic::DeclarationKind::subtype
                             : semantic::DeclarationKind::type,
                input.name,
                input.span,
                parent);
            auto& output = declaration(id);
            auto declared_subtype = input.type;
            if (subtype_form) {
                // The declaration identity names this new subtype; its HIR
                // base must continue to reference the subtype indication
                // written after `is`.
                declared_subtype.vhdl_type_declaration.clear();
            }
            output.subtype = subtype(
                declared_subtype, input.span, scope, output.origin);
            const auto type_id = ensure_type(
                input.name,
                scope,
                subtype_form ? semantic::TypeKind::subtype
                             : semantic::TypeKind::declaration,
                *output.subtype,
                output.source,
                output.origin);
            output.declared_type = type_id;

            vh::TypeDefinition definition;
            definition.id = type_id;
            definition.declaration = id;
            definition.form = type_form(input.declaration_kind);
            definition.name = input.name;
            definition.base = *output.subtype;
            definition.source = output.source;
            definition.origin = output.origin;
            if (input.type.vhdl_access) {
                definition.deallocate_releases_storage =
                    input.type.vhdl_access->deallocate_releases_storage;
                definition.reclaim_when_unreachable =
                    input.type.vhdl_access->reclaim_when_unreachable;
            }
            const auto declaration_origin = output.origin;
            add_type_payload(input, definition, scope, declaration_origin);
            add_protected_payload(
                input.type, definition, id, scope, declaration_origin);
            add_predefined_attributes(input.type, definition);
            hir_.mutable_types().push_back(std::move(definition));
            return id;
        }

        void add_type_payload(
            const frontend::TypeAliasDeclaration& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            for (std::size_t index = 0; index < input.enum_literals.size(); ++index) {
                const auto& literal = input.enum_literals[index];
                const auto literal_id = add_declaration_record(
                    scope,
                    vh::DeclarationForm::enumeration_literal,
                    semantic::DeclarationKind::enumeration_literal,
                    literal.name,
                    literal.span,
                    parent);
                auto& literal_declaration = declaration(literal_id);
                literal_declaration.subtype = output.base;
                literal_declaration.declared_value = ensure_value(
                    literal.name,
                    scope,
                    semantic::ValueKind::enumeration_literal,
                    output.base,
                    literal_declaration.source,
                    literal_declaration.origin);
                output.enumeration_literals.push_back({ literal_id,
                    literal.name,
                    static_cast<std::uint32_t>(index),
                    literal_declaration.source });
            }
            if (input.type.integer_range) {
                output.scalar_range = concrete_range(
                    *input.type.integer_range,
                    vh::RangeKind::integer,
                    output.source);
            } else if (input.type.integer_range_expression) {
                output.scalar_range = expression_range(
                    *input.type.integer_range_expression,
                    vh::RangeKind::integer,
                    scope,
                    parent);
            } else if (input.type.enumeration_range) {
                output.scalar_range = concrete_range(
                    *input.type.enumeration_range, output.source);
            } else if (input.type.enumeration_range_expression) {
                output.scalar_range = expression_range(
                    *input.type.enumeration_range_expression,
                    vh::RangeKind::enumeration,
                    scope,
                    parent);
            }
            add_array_payload(input.type, output, scope, parent);
            add_record_payload(input.type, output, scope, parent);
            add_indirect_type_payload(input.type, output, scope, parent);
        }

        void add_predefined_attributes(
            const frontend::Type& input,
            vh::TypeDefinition& output) const
        {
            const bool vhdl_2019 = current_vhdl_standard_
                >= frontend::VhdlStandard::Vhdl2019;
            if (input.vhdl_array || !input.vhdl_array_constraints.empty()) {
                output.attributes = {
                    vh::PredefinedAttribute::left,
                    vh::PredefinedAttribute::right,
                    vh::PredefinedAttribute::high,
                    vh::PredefinedAttribute::low,
                    vh::PredefinedAttribute::range,
                    vh::PredefinedAttribute::length
                };
                if (current_vhdl_standard_
                    >= frontend::VhdlStandard::Vhdl1993) {
                    output.attributes.push_back(
                        vh::PredefinedAttribute::reverse_range);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::ascending);
                }
                if (vhdl_2019) {
                    output.attributes.push_back(
                        vh::PredefinedAttribute::index);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::image);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::value);
                }
            }
            if (!input.enumeration_literals.empty()) {
                output.attributes.insert(
                    output.attributes.end(),
                    { vh::PredefinedAttribute::left,
                        vh::PredefinedAttribute::right,
                        vh::PredefinedAttribute::high,
                        vh::PredefinedAttribute::low,
                        vh::PredefinedAttribute::pos,
                        vh::PredefinedAttribute::val,
                        vh::PredefinedAttribute::succ,
                        vh::PredefinedAttribute::pred,
                        vh::PredefinedAttribute::leftof,
                        vh::PredefinedAttribute::rightof });
                if (current_vhdl_standard_
                    >= frontend::VhdlStandard::Vhdl1993) {
                    output.attributes.push_back(
                        vh::PredefinedAttribute::ascending);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::image);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::value);
                }
                if (vhdl_2019) {
                    output.attributes.push_back(
                        vh::PredefinedAttribute::length);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::range);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::reverse_range);
                }
            }
            if (vhdl_2019 && input.vhdl_access) {
                output.attributes.push_back(
                    vh::PredefinedAttribute::designated_subtype);
            }
            if (vhdl_2019 && input.vhdl_file) {
                output.attributes.push_back(
                    vh::PredefinedAttribute::designated_subtype);
            }
            if (vhdl_2019 && !input.packed_members.empty()) {
                output.attributes.push_back(
                    vh::PredefinedAttribute::image);
                output.attributes.push_back(
                    vh::PredefinedAttribute::value);
            }
            if (vhdl_2019) {
                output.attributes.push_back(
                    vh::PredefinedAttribute::reflect);
            }
        }

        void add_array_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (!input.vhdl_array) {
                return;
            }
            const auto& array = *input.vhdl_array;
            if (array.dimensions.empty() && !array.index_subtype.empty()) {
                vh::ArrayDimension converted;
                converted.index_subtype = name(
                    array.index_subtype, array.index_span, scope);
                converted.unconstrained = array.unconstrained;
                converted.source = source(array.index_span);
                if (array.index_base_range) {
                    converted.constraint = concrete_range(
                        *array.index_base_range,
                        vh::RangeKind::array_index,
                        converted.source);
                }
                output.array_dimensions.push_back(std::move(converted));
            }
            for (const auto& dimension : array.dimensions) {
                vh::ArrayDimension converted;
                converted.index_subtype = name(
                    dimension.index_subtype, dimension.index_span, scope);
                converted.unconstrained = dimension.unconstrained;
                converted.source = source(dimension.index_span);
                if (dimension.range) {
                    converted.constraint = concrete_range(
                        *dimension.range,
                        vh::RangeKind::array_index,
                        converted.source);
                    converted.constraint->null = dimension.null;
                } else if (dimension.constraint) {
                    converted.constraint = expression_range(
                        *dimension.constraint,
                        vh::RangeKind::array_index,
                        scope,
                        parent);
                }
                output.array_dimensions.push_back(std::move(converted));
            }
            if (!array.element_types.empty()) {
                output.element_subtype = subtype(
                    array.element_types.front(), array.element_span, scope, parent);
            }
        }

        void add_record_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            for (const auto& member : input.packed_members) {
                frontend::Type member_type;
                member_type.domain = member.domain;
                member_type.spelling = member.spelling;
                member_type.packed_range = member.packed_range;
                member_type.is_signed = member.is_signed;
                member_type.packed_range_expression = member.packed_range_expression;
                if (!member.nested_types.empty()) {
                    member_type = member.nested_types.front();
                }
                output.record_elements.push_back({ member.name,
                    subtype(member_type, member.span, scope, parent),
                    source(member.span) });
            }
        }

        void add_indirect_type_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (input.vhdl_access && !input.vhdl_access->designated_types.empty()) {
                output.designated_subtype = subtype(
                    input.vhdl_access->designated_types.front(),
                    input.vhdl_access->designated_span,
                    scope,
                    parent);
            }
            if (input.vhdl_file && !input.vhdl_file->element_types.empty()) {
                output.element_subtype = subtype(
                    input.vhdl_file->element_types.front(),
                    input.vhdl_file->element_span,
                    scope,
                    parent);
            }
            if (input.vhdl_physical) {
                if (input.vhdl_physical->resolved_range) {
                    output.scalar_range = concrete_range(
                        *input.vhdl_physical->resolved_range,
                        vh::RangeKind::integer,
                        output.source);
                } else if (input.vhdl_physical->range) {
                    output.scalar_range = expression_range(
                        *input.vhdl_physical->range,
                        vh::RangeKind::integer,
                        scope,
                        parent);
                }
                for (const auto& unit : input.vhdl_physical->units) {
                    const auto unit_id = add_declaration_record(
                        scope,
                        vh::DeclarationForm::constant,
                        semantic::DeclarationKind::constant,
                        unit.name,
                        unit.span,
                        parent);
                    const auto& unit_declaration = declaration(unit_id);
                    output.physical_units.push_back({ unit_id,
                        unit.name,
                        unit.scale ? expression(*unit.scale, scope, parent) : std::nullopt,
                        unit.scale_factor,
                        unit_declaration.source });
                }
            }
        }

        void add_protected_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::DeclarationId owner,
            const semantic::ScopeId parent_scope,
            const semantic::OriginId parent_origin)
        {
            if (!input.vhdl_protected) {
                return;
            }
            const auto member_scope = nested_scope(
                parent_scope, output.name, output.source, parent_origin);
            declaration(owner).nested_scope = member_scope;
            add_children(
                input.vhdl_protected->generic_parameters,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_parameter);
            add_children(
                input.vhdl_protected->variables,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.vhdl_protected->functions,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_function);
            add_children(
                input.vhdl_protected->procedures,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_procedure);
            add_children(
                input.vhdl_protected->method_aliases,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_alias);
            declaration(owner).children = output.protected_members;
        }
