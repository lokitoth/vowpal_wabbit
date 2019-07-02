#include "global_data.h"
#include "example.h"

#include <vector>
#include <sstream>

namespace VW { namespace PARSER {
  class label_parser_backend {
    parser* p;
    shared_data* sd;

  public:
    label_parser_backend(vw* all)
    : p{all->p}, sd{all->sd}
    {}

    inline void default_label(polylabel* l)
    {
      p->lp.default_label(l);
    }

    inline void parse_label(polylabel* l, v_array<substring>& words)
    {
      p->lp.parse_label(p, sd, l, words);
    }
  };

  struct parser_context {
    // featurization configuration
    uint64_t* affix_features;
    bool* spelling_features;
    std::vector<feature_dict*>* namespace_dictionaries;

    // feature_group redefines
    bool redefine_some;
    unsigned char (*redefines)[256];

    // hashing configuration (functions should ideally come from template)
    hash_func_t hasher;
    hash_func_t stringhasher;
    uint32_t hash_seed;
    uint64_t hash_space;
  };

  inline parser_context init_parser_context(vw* all)
  {
    return {
      all->affix_features, 
      all->spelling_features, 
      all->namespace_dictionaries, 
      all->redefine_some,
      &all->redefine,
      all->p->hasher,
      hashstring,
      all->hash_seed,
      all->parse_mask};
  }

  // define the callbacks for getting/registering shared data
  // this will allow us to avoid taking a dependency on vw*

  template<bool audit>
  class example_parser_backend {
    example* ae;
    parser_context context;

    // ambient namespace/feature_group info
    v_array<char> base;
    bool new_index;
    unsigned char index;
    uint64_t channel_hash;

    size_t anon; // number of anonymous features in the namespace

    // slot to build up spelling features
    v_array<char> spelling_slot;

    inline void begin_namespace()
    {
      index = 0;
      new_index = false;
      anon = 0;
    }

  public:
    example_parser_backend(example* ae, parser_context context)
    : ae{ae}, 
      context{context},
      base{v_init<char>()},
      spelling_slot{v_init<char>()}
    {}

    inline void begin_default_namespace()
    {
      begin_namespace();
      index = (unsigned char)' ';
      
      if (ae->feature_space[index].size() == 0)
        new_index = true;
      
      if (audit)
      {
        base.clear();

        base.push_back(' ');
        base.push_back('\0');
      }
      channel_hash = this->context.hash_seed == 0 ? 0 : uniform_hash("", 0, this->context.hash_seed);
    }

    inline void begin_namespace(substring& name)
    {
      begin_namespace();
      index = (unsigned char)(*name.begin);
      
      if (context.redefine_some)
        index = (*context.redefines)[index];  // redefine index
      
      if (ae->feature_space[index].size() == 0)
        new_index = true;

      if (audit)
      {
        base.clear();
        push_many(base, name.begin, name.end - name.begin);
        base.push_back('\0');
      }

      channel_hash = context.hasher(name, this->context.hash_seed);
    }

    inline void complete_namespace()
    {
      // TODO: is index = 0 a legal namespace
      if (new_index && ae->feature_space[index].size() > 0)
        ae->indices.push_back(index);
    }

    inline void emit_feature(substring& feature_name, float v)
    {
      uint64_t word_hash;
      if (feature_name.end != feature_name.begin)
        word_hash = (context.hasher(feature_name, channel_hash) & context.hash_space);
      else
        word_hash = channel_hash + anon++;
      if (v == 0)
        return;  // dont add 0 valued features to list of features
      features& fs = ae->feature_space[index];
      fs.push_back(v, word_hash);
      if (audit)
      {
        v_array<char> feature_v = v_init<char>();
        push_many(feature_v, feature_name.begin, feature_name.end - feature_name.begin);
        feature_v.push_back('\0');
        fs.space_names.push_back(audit_strings_ptr(new audit_strings(base.begin(), feature_v.begin())));
        feature_v.delete_v();
      }
      if ((context.affix_features[index] > 0) && (feature_name.end != feature_name.begin))
      {
        features& affix_fs = ae->feature_space[affix_namespace];
        if (affix_fs.size() == 0)
          ae->indices.push_back(affix_namespace);
        uint64_t affix = context.affix_features[index];
        while (affix > 0)
        {
          bool is_prefix = affix & 0x1;
          uint64_t len = (affix >> 1) & 0x7;
          substring affix_name = {feature_name.begin, feature_name.end};
          if (affix_name.end > affix_name.begin + len)
          {
            if (is_prefix)
              affix_name.end = affix_name.begin + len;
            else
              affix_name.begin = affix_name.end - len;
          }
          word_hash =
              context.hasher(affix_name, (uint64_t)channel_hash) * (affix_constant + (affix & 0xF) * quadratic_constant);
          affix_fs.push_back(v, word_hash);
          if (audit)
          {
            v_array<char> affix_v = v_init<char>();
            if (index != ' ')
              affix_v.push_back(index);
            affix_v.push_back(is_prefix ? '+' : '-');
            affix_v.push_back('0' + (char)len);
            affix_v.push_back('=');
            push_many(affix_v, affix_name.begin, affix_name.end - affix_name.begin);
            affix_v.push_back('\0');
            affix_fs.space_names.push_back(audit_strings_ptr(new audit_strings("affix", affix_v.begin())));
          }
          affix >>= 4;
        }
      }

      if (context.spelling_features[index])
      {
        features& spell_fs = ae->feature_space[spelling_namespace];
        if (spell_fs.size() == 0)
          ae->indices.push_back(spelling_namespace);
        // v_array<char> spelling;
        spelling_slot.clear();
        for (char* c = feature_name.begin; c != feature_name.end; ++c)
        {
          char d = 0;
          if ((*c >= '0') && (*c <= '9'))
            d = '0';
          else if ((*c >= 'a') && (*c <= 'z'))
            d = 'a';
          else if ((*c >= 'A') && (*c <= 'Z'))
            d = 'A';
          else if (*c == '.')
            d = '.';
          else
            d = '#';
          // if ((spelling_slot.size() == 0) || (spelling_slot.last() != d))
          spelling_slot.push_back(d);
        }
        substring spelling_ss = {spelling_slot.begin(), spelling_slot.end()};
        uint64_t word_hash = context.stringhasher(spelling_ss, (uint64_t)channel_hash); //this is explicitly *not* hasher
        spell_fs.push_back(v, word_hash);
        if (audit)
        {
          v_array<char> spelling_v = v_init<char>();
          if (index != ' ')
          {
            spelling_v.push_back(index);
            spelling_v.push_back('_');
          }
          push_many(spelling_v, spelling_ss.begin, spelling_ss.end - spelling_ss.begin);
          spelling_v.push_back('\0');
          spell_fs.space_names.push_back(audit_strings_ptr(new audit_strings("spelling", spelling_v.begin())));
        }
      }

      if (context.namespace_dictionaries[index].size() > 0)
      {
        for (size_t dict = 0; dict < context.namespace_dictionaries[index].size(); dict++)
        {
          feature_dict* map = context.namespace_dictionaries[index][dict];
          uint64_t hash = uniform_hash(feature_name.begin, feature_name.end - feature_name.begin, quadratic_constant);
          features* feats = map->get(feature_name, hash);
          if ((feats != nullptr) && (feats->values.size() > 0))
          {
            features& dict_fs = ae->feature_space[dictionary_namespace];
            if (dict_fs.size() == 0)
              ae->indices.push_back(dictionary_namespace);
            push_many(dict_fs.values, feats->values.begin(), feats->values.size());
            push_many(dict_fs.indicies, feats->indicies.begin(), feats->indicies.size());
            dict_fs.sum_feat_sq += feats->sum_feat_sq;
            if (audit)
              for (size_t i = 0; i < feats->indicies.size(); ++i)
              {
                uint64_t id = feats->indicies[i];
                std::stringstream ss;
                ss << index << '_';
                for (char* fc = feature_name.begin; fc != feature_name.end; ++fc) ss << *fc;
                ss << '=' << id;
                dict_fs.space_names.push_back(audit_strings_ptr(new audit_strings("dictionary", ss.str())));
              }
          }
        }
      }
    }

    inline void push_tag(substring& tag)
    {
      push_many(ae->tag, tag.begin, tag.end - tag.begin);
    }

    inline polylabel* label()
    {
      return &ae->l;
    }

    inline void complete_example()
    {}
  };

}}