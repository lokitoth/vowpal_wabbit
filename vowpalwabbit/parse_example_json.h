/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD
license as described in the file LICENSE.
*/

#pragma once

#include "parse_primitives.h"
#include "v_array.h"

#include "cb.h"
#include "conditional_contextual_bandit.h"

#include "best_constant.h"

#include <algorithm>
#include <vector>

struct vw;

// Decision Service JSON header information - required to construct final label
struct DecisionServiceInteraction
{
  std::string eventId;
  std::vector<unsigned> actions;
  std::vector<float> probabilities;
  float probabilityOfDrop = 0.f;
  bool skipLearn{false};
};

namespace VW
{

namespace JSON_PARSER
{
  void read_line_json_internal(vw& all, v_array<example*>& examples, char* line, example_factory_t example_factory, void* ex_factory_context);
  void read_line_json_internal_audit(vw& all, v_array<example*>& examples, char* line, example_factory_t example_factory, void* ex_factory_context);

  void read_line_decision_service_json_internal(vw& all, v_array<example*>& examples, char* line, size_t length, bool copy_line,
    example_factory_t example_factory, void* ex_factory_context, DecisionServiceInteraction* data);
  void read_line_decision_service_json_internal_audit(vw& all, v_array<example*>& examples, char* line, size_t length, bool copy_line,
    example_factory_t example_factory, void* ex_factory_context, DecisionServiceInteraction* data);
} // namespace JSON_PARSER

// This odd structure (template, to explicit, to template internally) is needed to avoid changing the interface
// TODO: Clean this up when we define a proper explicit public interface
template <bool audit>
inline void read_line_json(
    vw& all, v_array<example*>& examples, char* line, example_factory_t example_factory, void* ex_factory_context);

template <>
inline void read_line_json<false>(
    vw& all, v_array<example*>& examples, char* line, example_factory_t example_factory, void* ex_factory_context)
{
  VW::JSON_PARSER::read_line_json_internal(all, examples, line, example_factory, ex_factory_context);
}

template <>
inline void read_line_json<true>(
    vw& all, v_array<example*>& examples, char* line, example_factory_t example_factory, void* ex_factory_context)
{
  VW::JSON_PARSER::read_line_json_internal_audit(all, examples, line, example_factory, ex_factory_context);
}

template <bool audit>
inline void read_line_decision_service_json(vw& all, v_array<example*>& examples, char* line, size_t length, bool copy_line,
    example_factory_t example_factory, void* ex_factory_context, DecisionServiceInteraction* data);

template <>
inline void read_line_decision_service_json<false>(vw& all, v_array<example*>& examples, char* line, size_t length, bool copy_line,
    example_factory_t example_factory, void* ex_factory_context, DecisionServiceInteraction* data)
{
  VW::JSON_PARSER::read_line_decision_service_json_internal(all, examples, line, length, copy_line, example_factory, ex_factory_context, data);
}

template <>
inline void read_line_decision_service_json<true>(vw& all, v_array<example*>& examples, char* line, size_t length, bool copy_line,
    example_factory_t example_factory, void* ex_factory_context, DecisionServiceInteraction* data)
{
  VW::JSON_PARSER::read_line_decision_service_json_internal_audit(all, examples, line, length, copy_line, example_factory, ex_factory_context, data);
}
}  // namespace VW

template <bool audit>
bool parse_line_json(vw* all, char* line, size_t num_chars, v_array<example*>& examples)
{
  if (all->p->decision_service_json)
  {
    // Skip lines that do not start with "{"
    if (line[0] != '{')
    {
      return false;
    }

    DecisionServiceInteraction interaction;
    VW::template read_line_decision_service_json<audit>(*all, examples, line, num_chars, false,
        reinterpret_cast<VW::example_factory_t>(&VW::get_unused_example), all, &interaction);

    // TODO: In refactoring the parser to be usable standalone, we need to ensure that we
    // stop suppressing "skipLearn" interactions. Also, not sure if this is the right logic
    // for counterfactual. (@marco)
    if (interaction.skipLearn)
    {
      VW::return_multiple_example(*all, examples);
      examples.push_back(&VW::get_unused_example(all));
      return false;
    }

    // let's ask to continue reading data until we find a line with actions provided
    if (interaction.actions.size() == 0)
    {
      // VW::return_multiple_example(*all, examples);
      // examples.push_back(&VW::get_unused_example(all));
      return false;
    }
  }
  else
    VW::template read_line_json<audit>(
        *all, examples, line, reinterpret_cast<VW::example_factory_t>(&VW::get_unused_example), all);

  return true;
}

template <bool audit>
inline void prepare_for_learner(vw* all, v_array<example*>& examples)
{
  // note: the json parser does single pass parsing and cannot determine if a shared example is needed.
  // since the communication between the parsing thread the main learner expects examples to be requested in order (as
  // they're layed out in memory) there is no way to determine upfront if a shared example exists thus even if there are
  // no features for the shared example, still an empty example is returned.

  // insert new line example at the end
  if (examples.size() > 1)
  {
    example& ae = VW::get_unused_example(all);
    char empty = '\0';
    substring example = {&empty, &empty};

    substring_to_example(all, &ae, example);

    examples.push_back(&ae);
  }
}

// This is used by the python parser
template <bool audit>
void line_to_examples_json(vw* all, char* line, size_t num_chars, v_array<example*>& examples)
{
  bool good_example = parse_line_json<audit>(all, line, num_chars, examples);
  if (!good_example)
  {
    VW::return_multiple_example(*all, examples);
    examples.push_back(&VW::get_unused_example(all));
    return;
  }

  prepare_for_learner<audit>(all, examples);
}

template <bool audit>
int read_features_json(vw* all, v_array<example*>& examples)
{
  // Keep reading lines until a valid set of examples is produced.
  bool reread;
  do
  {
    reread = false;

    char* line;
    size_t num_chars;
    size_t num_chars_initial = read_features(all, line, num_chars);
    if (num_chars_initial < 1)
      return (int)num_chars_initial;

    // Ensure there is a null terminator.
    line[num_chars] = '\0';

    reread = !parse_line_json<audit>(all, line, num_chars, examples);
  } while (reread);

  prepare_for_learner<audit>(all, examples);

  return 1;
}
