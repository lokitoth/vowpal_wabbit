/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */

#include <math.h>
#include <ctype.h>
#include "parse_example.h"
#include "hash.h"
#include "unique_sort.h"
#include "global_data.h"
#include "constant.h"

#include "parse_example_backend.h"

using namespace std;

size_t read_features(vw* all, char*& line, size_t& num_chars)
{
  line = nullptr;
  size_t num_chars_initial = readto(*(all->p->input), line, '\n');
  if (num_chars_initial < 1)
    return num_chars_initial;
  num_chars = num_chars_initial;
  if (line[0] == '\xef' && num_chars >= 3 && line[1] == '\xbb' && line[2] == '\xbf') // Skip the UTF-8 BOM
  {
    line += 3;
    num_chars -= 3;
  }
  if (num_chars > 0 && line[num_chars - 1] == '\n')
    num_chars--;
  if (num_chars > 0 && line[num_chars - 1] == '\r')
    num_chars--;
  return num_chars_initial;
}

int read_features_string(vw* all, v_array<example*>& examples)
{
  char* line;
  size_t num_chars;
  size_t num_chars_initial = read_features(all, line, num_chars);
  if (num_chars_initial < 1)
    return (int)num_chars_initial;

  substring example = {line, line + num_chars};
  substring_to_example(all, examples[0], example);

  return (int)num_chars_initial;
}

template <typename parser_backend_t>
class TC_parser
{
 public:
  char* beginLine;
  char* reading_head;
  char* endLine;
  float cur_channel_v;
  float v;
  parser* p;
  uint64_t parse_mask;

  parser_backend_t* backend;

  ~TC_parser() {}

  inline void parserWarning(const char* message, char* begin, char* pos, const char* message2)
  {
    std::stringstream ss;
    ss << message << std::string(begin, pos - begin).c_str() << message2 << "in Example #"
       << this->p->end_parsed_examples << ": \"" << std::string(this->beginLine, this->endLine).c_str() << "\"" << endl;
    if (p->strict_parse)
    {
      THROW_EX(VW::strict_parse_exception, ss.str());
    }
    else
    {
      cerr << ss.str();
    }
  }

  inline float featureValue()
  {
    if (*reading_head == ' ' || *reading_head == '\t' || *reading_head == '|' || reading_head == endLine ||
        *reading_head == '\r')
      return 1.;
    else if (*reading_head == ':')
    {
      // featureValue --> ':' 'Float'
      ++reading_head;
      char* end_read = nullptr;
      v = parseFloat(reading_head, &end_read, endLine);
      if (end_read == reading_head)
      {
        parserWarning("malformed example! Float expected after : \"", beginLine, reading_head, "\"");
      }
      if (nanpattern(v))
      {
        v = 0.f;
        parserWarning("warning: invalid feature value:\"", reading_head, end_read, "\" read as NaN. Replacing with 0.");
      }
      reading_head = end_read;
      return v;
    }
    else
    {
      // syntax error
      parserWarning("malformed example! '|', ':', space, or EOL expected after : \"", beginLine, reading_head, "\"");
      return 0.f;
    }
  }

  inline substring read_name()
  {
    substring ret;
    ret.begin = reading_head;
    while (!(*reading_head == ' ' || *reading_head == ':' || *reading_head == '\t' || *reading_head == '|' ||
        reading_head == endLine || *reading_head == '\r'))
      ++reading_head;
    ret.end = reading_head;

    return ret;
  }

  inline void maybeFeature()
  {
    if (*reading_head == ' ' || *reading_head == '\t' || *reading_head == '|' || reading_head == endLine ||
        *reading_head == '\r')
    {
      // maybeFeature --> ø
    }
    else
    {
      // maybeFeature --> 'String' FeatureValue
      substring feature_name = read_name();
      v = cur_channel_v * featureValue();
      
      backend->emit_feature(feature_name, v);
    }
  }

  inline void nameSpaceInfoValue()
  {
    if (*reading_head == ' ' || *reading_head == '\t' || reading_head == endLine || *reading_head == '|' ||
        *reading_head == '\r')
    {
      // nameSpaceInfoValue -->  ø
    }
    else if (*reading_head == ':')
    {
      // nameSpaceInfoValue --> ':' 'Float'
      ++reading_head;
      char* end_read = nullptr;
      cur_channel_v = parseFloat(reading_head, &end_read);
      if (end_read == reading_head)
      {
        parserWarning("malformed example! Float expected after : \"", beginLine, reading_head, "\"");
      }
      if (nanpattern(cur_channel_v))
      {
        cur_channel_v = 1.f;
        parserWarning(
            "warning: invalid namespace value:\"", reading_head, end_read, "\" read as NaN. Replacing with 1.");
      }
      reading_head = end_read;
    }
    else
    {
      // syntax error
      parserWarning("malformed example! '|',':', space, or EOL expected after : \"", beginLine, reading_head, "\"");
    }
  }

  inline void nameSpaceInfo()
  {
    if (reading_head == endLine || *reading_head == '|' || *reading_head == ' ' || *reading_head == '\t' ||
        *reading_head == ':' || *reading_head == '\r')
    {
      // syntax error
      parserWarning("malformed example! String expected after : \"", beginLine, reading_head, "\"");
    }
    else
    {
      // NameSpaceInfo --> 'String' NameSpaceInfoValue
      substring name = read_name();
      backend->begin_namespace(name);
      
      nameSpaceInfoValue();
    }
  }

  inline void listFeatures()
  {
    while ((*reading_head == ' ' || *reading_head == '\t') && (reading_head < endLine))
    {
      // listFeatures --> ' ' MaybeFeature ListFeatures
      ++reading_head;
      maybeFeature();
    }
    if (!(*reading_head == '|' || reading_head == endLine || *reading_head == '\r'))
    {
      // syntax error
      parserWarning("malformed example! '|',space, or EOL expected after : \"", beginLine, reading_head, "\"");
    }
  }

  inline void nameSpace()
  {
    cur_channel_v = 1.0;
    if (*reading_head == ' ' || *reading_head == '\t' || reading_head == endLine || *reading_head == '|' ||
        *reading_head == '\r')
    {
      // NameSpace --> ListFeatures
      backend->begin_default_namespace();
      listFeatures();
    }
    else if (*reading_head != ':')
    {
      // NameSpace --> NameSpaceInfo ListFeatures
      nameSpaceInfo();
      listFeatures();
    }
    else
    {
      // syntax error
      parserWarning("malformed example! '|',String,space, or EOL expected after : \"", beginLine, reading_head, "\"");
    }

    backend->complete_namespace();
  }

  inline void listNameSpace()
  {
    while ((*reading_head == '|') && (reading_head < endLine))  // ListNameSpace --> '|' NameSpace ListNameSpace
    {
      ++reading_head;
      nameSpace();
    }
    if (reading_head != endLine && *reading_head != '\r')
    {
      // syntax error
      parserWarning("malformed example! '|' or EOL expected after : \"", beginLine, reading_head, "\"");
    }
  }

  TC_parser(char* reading_head, char* endLine, parser* p, uint64_t parse_mask, parser_backend_t& backend)
  {
    if (endLine != reading_head)
    {
      this->beginLine = reading_head;
      this->reading_head = reading_head;
      this->endLine = endLine;
      this->p = p;
      this->parse_mask = parse_mask;
      this->backend = &backend;
      listNameSpace();
    }
  }
};

template <typename parser_backend_t>
inline void substring_to_backend(vw* all, parser_backend_t& backend, substring example)
{
  all->p->lp.default_label(backend.label());
  char* bar_location = safe_index(example.begin, '|', example.end);
  char* tab_location = safe_index(example.begin, '\t', bar_location);
  substring label_space;
  if (tab_location != bar_location)
  {
    label_space.begin = tab_location + 1;
  }
  else
  {
    label_space.begin = example.begin;
  }
  label_space.end = bar_location;

  if (*example.begin == '|')
  {
    all->p->words.clear();
  }
  else
  {
    tokenize(' ', label_space, all->p->words);
    if (all->p->words.size() > 0 &&
        (all->p->words.last().end == label_space.end ||
            *(all->p->words.last().begin) == '\''))  // The last field is a tag, so record and strip it off
    {
      substring tag = all->p->words.pop();
      if (*tag.begin == '\'')
        tag.begin++;
      
      backend.push_tag(tag);
    }
  }

  if (all->p->words.size() > 0)
    all->p->lp.parse_label(all->p, all->sd, backend.label(), all->p->words);

  TC_parser<parser_backend_t> parser_line(bar_location, example.end, all->p, all->parse_mask, backend);

  backend.complete_example();
}

template <bool audit_or_hash_inv>
void substring_to_example(vw* all, example* ae, substring example)
{
  typedef VW::PARSER::example_parser_backend<audit_or_hash_inv> parser_backend_t;
  VW::PARSER::parser_context context = VW::PARSER::init_parser_context(all);

  parser_backend_t backend(ae, context);
  substring_to_backend<parser_backend_t>(all, backend, example);
}

void substring_to_example(vw* all, example* ae, substring example)
{
  if (all->audit || all->hash_inv)
  {
    substring_to_example<true>(all, ae, example);
  }
  else
  {
    substring_to_example<false>(all, ae, example);
  }
}

std::vector<std::string> split(char* phrase, std::string delimiter)
{
  std::vector<std::string> list;
  std::string s = std::string(phrase);
  size_t pos = 0;
  std::string token;
  while ((pos = s.find(delimiter)) != std::string::npos)
  {
    token = s.substr(0, pos);
    list.push_back(token);
    s.erase(0, pos + delimiter.length());
  }
  list.push_back(s);
  return list;
}

namespace VW
{
void read_line(vw& all, example* ex, char* line)
{
  substring ss = {line, line + strlen(line)};
  while ((ss.end >= ss.begin) && (*(ss.end - 1) == '\n')) ss.end--;
  substring_to_example(&all, ex, ss);
}

void read_lines(vw* all, char* line, size_t /*len*/, v_array<example*>& examples)
{
  auto lines = split(line, "\n");
  for (size_t i = 0; i < lines.size(); i++)
  {
    // Check if a new empty example needs to be added.
    if (examples.size() < i + 1)
    {
      examples.push_back(&VW::get_unused_example(all));
    }
    read_line(*all, examples[i], const_cast<char*>(lines[i].c_str()));
  }
}

}  // namespace VW
