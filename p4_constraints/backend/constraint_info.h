/*
 * Copyright 2020 The P4-Constraints Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Data structure containing all info required for constraint checking.
//
// ConstraintInfo is a data structure containing all information required for
// checking that a table entry satisfies the constraints specified in the P4
// program. ConstraintInfo can be parsed from a P4Info protobuf.

#ifndef P4_CONSTRAINTS_BACKEND_CONSTRAINT_INFO_H_
#define P4_CONSTRAINTS_BACKEND_CONSTRAINT_INFO_H_

#include <stdint.h>

#include <ostream>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_constraints/ast.pb.h"
#include "p4_constraints/constraint_source.h"

namespace p4_constraints {

struct KeyInfo {
  uint32_t id;       // Same as MatchField.id in p4info.proto.
  std::string name;  // Same as MatchField.name in p4info.proto.

  // Key type specifying how many bits to match on and how, e.g. Ternary<16>.
  // Derives from MatchField.match_type and MatchField.bitwidth in p4info.proto.
  ast::Type type;
};

struct ParamInfo {
  uint32_t id;       // Same as Action.Param.id in p4info.proto.
  std::string name;  // Same as Action.Param.name in p4info.proto.

  // Param type specified by a combination of Action.Param.bitwidth and
  // Action.Param.P4NamedType in p4info.proto.
  ast::Type type;
};

template <typename Sink>
void AbslStringify(Sink& sink, const KeyInfo& info) {
  absl::Format(&sink, "KeyInfo{ id: %d; name: \"%s\"; type: { %s }; }", info.id,
               info.name, info.type.ShortDebugString());
}

struct TableInfo {
  uint32_t id;       // Same as Table.preamble.id in p4info.proto.
  std::string name;  // Same as Table.preamble.name in p4info.proto.

  // An optional constraint (aka entry_restriction) on table entries.
  absl::optional<ast::Expression> constraint;
  // If member `constraint` is present, this captures its source. Arbitrary
  // otherwise.
  ConstraintSource constraint_source;

  // Maps from key IDs/names to KeyInfo.
  // Derives from Table.match_fields in p4info.proto.
  absl::flat_hash_map<uint32_t, KeyInfo> keys_by_id;
  absl::flat_hash_map<std::string, KeyInfo> keys_by_name;
};

struct ActionInfo {
  uint32_t id;       // Same as Action.preamble.id in p4info.proto.
  std::string name;  // Same as Action.preamble.name in p4info.proto.

  // An optional constraint (aka action_restriction) on actions.
  absl::optional<ast::Expression> constraint;
  // If member `constraint` is present, this captures its source. Arbitrary
  // otherwise.
  ConstraintSource constraint_source;

  // Maps from param IDs to ParamInfo.
  absl::flat_hash_map<uint32_t, ParamInfo> params_by_id;
  // Maps from param names to ParamInfo.
  absl::flat_hash_map<std::string, ParamInfo> params_by_name;
};

// Contains all information required for constraint checking.
struct ConstraintInfo {
  // Maps from action IDs to ActionInfo.
  absl::flat_hash_map<uint32_t, ActionInfo> action_info_by_id;
  // Maps from table IDs to TableInfo.
  absl::flat_hash_map<uint32_t, TableInfo> table_info_by_id;
};

// Translates `P4Info` to `ConstraintInfo`.
//
// Parses all tables and actions and their p4-constraints annotations into an
// in-memory representation suitable for constraint checking. Returns parsed
// representation, or an error status if parsing fails.
absl::StatusOr<ConstraintInfo> P4ToConstraintInfo(
    const p4::config::v1::P4Info& p4info);

// Returns a pointer to the TableInfo associated with a given table_id
// or std::nullptr if the table_id cannot be found.
const TableInfo* GetTableInfoOrNull(const ConstraintInfo& constraint_info,
                                    uint32_t table_id);

// Returns a pointer to the ActionInfo associated with a given action_id
// or std::nullptr if the action_id cannot be found.
const ActionInfo* GetActionInfoOrNull(const ConstraintInfo& constraint_info,
                                      uint32_t action_id);

// Table entry attribute accessible in the constraint language, e.g. priority.
struct AttributeInfo {
  std::string name;
  ast::Type type;
};

// Returns information for a given attribute name, std::nullopt for unknown
// attribute.
std::optional<AttributeInfo> GetAttributeInfo(absl::string_view attribute_name);

// -- Pretty Printers ----------------------------------------------------------

inline std::ostream& operator<<(std::ostream& os, const KeyInfo& info) {
  return os << absl::StrCat(info);
}

}  // namespace p4_constraints

#endif  // P4_CONSTRAINTS_BACKEND_CONSTRAINT_INFO_H_
