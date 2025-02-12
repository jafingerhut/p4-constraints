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

// The interpreter evaluates a constraint with respect to a table entry.

#ifndef P4_CONSTRAINTS_BACKEND_INTERPRETER_H_
#define P4_CONSTRAINTS_BACKEND_INTERPRETER_H_

#include <gmpxx.h>
#include <stdint.h>

#include <ostream>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/variant.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_constraints/ast.h"
#include "p4_constraints/ast.pb.h"
#include "p4_constraints/backend/constraint_info.h"

namespace p4_constraints {

// Checks if a given table entry satisfies the constraints attached to its
// associated table/action.
//
// Returns the empty string if this is the case, or a human-readable nonempty
// string explaining why it is not the case otherwise. Returns an
// `InvalidArgument` if the entry's table or action is not defined in
// `ConstraintInfo`, or if `entry` is inconsistent with these definitions.
absl::StatusOr<std::string> ReasonEntryViolatesConstraint(
    const p4::v1::TableEntry& entry, const ConstraintInfo& constraint_info);

// -- END OF PUBLIC INTERFACE --------------------------------------------------

// Exposed for testing only.
namespace internal_interpreter {

// -- Runtime representations --------------------------------------------------

// We use mpz_class to represent all integers, including arbitrary-precision
// and fixed-width signed/unsigned integers.
using Integer = mpz_class;

struct Exact {
  Integer value;
};

// Used to represent both ternary and optional keys at runtime, since an
// optional key is just a ternary key whose mask is all zeros or all ones.
struct Ternary {
  Integer value;
  Integer mask;
};

struct Lpm {
  Integer value;
  Integer prefix_length;
};

struct Range {
  Integer low;
  Integer high;
};

// Evaluation can result in a value of various types.
// We use a tagged union to ease debugging (see DynamicTypeCheck); an untagged
// union would work just fine assuming the type checker has no bugs.
using EvalResult = absl::variant<bool, Integer, Exact, Ternary, Lpm, Range>;

inline bool operator==(const Exact& left, const Exact& right) {
  return left.value == right.value;
}

inline bool operator==(const Ternary& left, const Ternary& right) {
  return left.value == right.value && left.mask == right.mask;
}

inline bool operator==(const Lpm& left, const Lpm& right) {
  return left.value == right.value && left.prefix_length == right.prefix_length;
}

inline bool operator==(const Range& left, const Range& right) {
  return left.low == right.low && left.high == right.high;
}

inline std::ostream& operator<<(std::ostream& os, const Integer& integer) {
  return os << integer.get_str();
}

inline std::ostream& operator<<(std::ostream& os, const Exact& exact) {
  return os << absl::StrFormat("Exact{.value = %s}", exact.value.get_str());
}

inline std::ostream& operator<<(std::ostream& os, const Ternary& ternary) {
  return os << absl::StrFormat("Ternary{.value = %s, .mask = %s}",
                               ternary.value.get_str(), ternary.mask.get_str());
}

inline std::ostream& operator<<(std::ostream& os, const Lpm& lpm) {
  return os << absl::StrFormat("Lpm{.value = %s, .prefix_length = %s}",
                               lpm.value.get_str(),
                               lpm.prefix_length.get_str());
}

inline std::ostream& operator<<(std::ostream& os, const Range& range) {
  return os << absl::StrFormat("Range{.low = %s, .high = %s}",
                               range.low.get_str(), range.high.get_str());
}

// Converts EvalResult to readable string.
std::string EvalResultToString(const EvalResult& result);

// TODO(smolkaj): The code below does not compile with C++11. Find workaround.
// inline std::ostream& operator<<(std::ostream& os, const EvalResult& result) {
//   absl::visit([&](const auto& result) { os << result; }, result);
//   return os;
// }

// Parsed representation of p4::v1::TableEntry.
struct TableEntry {
  std::string table_name;
  int32_t priority;
  // All table keys, by name.
  // In contrast to p4::v1::TableEntry, all keys must be present, i.e. this must
  // be a total map from key names to values.
  absl::flat_hash_map<std::string, EvalResult> keys;
};

// Parsed representation of p4::v1::Action.
struct ActionInvocation {
  uint32_t action_id;
  std::string action_name;
  // Map of param names to param values.
  absl::flat_hash_map<std::string, Integer> action_parameters;
};

// Context under which an `Expression` is evaluated. An `EvaluationContext` is
// "valid" for a given `Expression` iff the following holds:
// - If the `Expression` being evaluated is a Table constraint, then
// `constraint_context` is a TableEntry type. If the `Expression` being
// evaluated is an Action constraint, then `constraint_context` is an
// ActionInvocation type. `constraint_context` must contain all fields in the
// expression, with correct type. If not, an Error Status is returned.
//   -`source` must be the source from which the expression was parsed. If not,
//     behaviour is undefined (depending on the source, either an InternalError
//     will be given or a non-sense quote will be returned)
//
// ***WARNING***: This struct's members are references in order to avoid
// expensive copies. This leads to the possibility of dangling references, use
// with caution.
struct EvaluationContext {
  std::variant<ActionInvocation, TableEntry> constraint_context;
  const ConstraintSource& constraint_source;
};

// Parses p4::v1::TableEntry into an EvaluationContext using keys, table name,
// and constraint source from table_info. Returns InvalidArgument if it is not
// possible to parse entry fields into keys or if an exact match key is not
// present in the entry.
absl::StatusOr<EvaluationContext> ParseTableEntry(
    const p4::v1::TableEntry& entry, const TableInfo& table_info);

// Parses p4::v1::Action into an EvaluationContext using action parameters,
// action name, and constraint source from action_info. Returns InvalidArgument
// if an Action parameter cannot be found in action_info or if there are
// duplicate action parameters.
absl::StatusOr<EvaluationContext> ParseAction(const p4::v1::Action& action,
                                              const ActionInfo& action_info);

// Used to memoize evaluation results to avoid re-computation.
using EvaluationCache = absl::flat_hash_map<const ast::Expression*, bool>;

// Evaluates `expr` over `context.entry` to an `EvalResult`. Returns error
// status if AST is malformed and uses `context.constraint_source` to quote
// constraint. `eval_cache` holds boolean results, useful for avoiding
// recomputation when an explanation is desired. Passing a nullptr for
// `eval-cache` disables caching.
absl::StatusOr<EvalResult> Eval(const ast::Expression& expr,
                                const EvaluationContext& context,
                                EvaluationCache* eval_cache);

// Provides a minimal explanation for why `expression` resolved to true/false
// under `context.entry` as a pointer to a subexpression that implies the
// result. In the formal sense, finds the smallest subexpression `s` of `e` s.t.
//
//  eval(s, entry1) == eval(s, entry2)  =>  eval(e, entry1) == eval(e, entry2)
//
// In the special case where eval(e, entry1) == false, this is equivalent to
//
//           eval(e, entry2)  =>  eval(s, entry2) != eval(s, entry1)
//
// Uses `eval_cache` and `size_cache` to avoid recomputation, allowing it to run
// in linear time. Given current language specification, search only requires
// traversal of nodes with type boolean. Traversal of non-boolean nodes or an
// invalid AST will return InternalError Status. Uses
// `context.constraint_source` to quote constraint on error.
absl::StatusOr<const ast::Expression*> MinimalSubexpressionLeadingToEvalResult(
    const ast::Expression& expression, const EvaluationContext& context,
    EvaluationCache& eval_cache, ast::SizeCache& size_cache);

// Same as `Eval` except forces boolean result.
absl::StatusOr<bool> EvalToBool(const ast::Expression& expr,
                                const EvaluationContext& context,
                                EvaluationCache* eval_cache);

// Converts a P4 integer in binary string format to Integer format. For details
// on the conversion, see
// https://p4.org/p4-spec/docs/p4runtime-spec-working-draft-html-version.html#sec-bytestrings.
Integer ParseP4RTInteger(std::string int_str);

}  // namespace internal_interpreter
}  // namespace p4_constraints

#endif  // P4_CONSTRAINTS_BACKEND_INTERPRETER_H_
