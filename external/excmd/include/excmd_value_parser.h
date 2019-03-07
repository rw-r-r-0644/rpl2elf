#pragma once
#include <string>

namespace excmd
{

namespace internal
{

struct value_parser
{
   virtual bool parse(const std::string &value) = 0;

   virtual bool has_multiple_values() const = 0;
   virtual bool get_value(void *ptr, std::size_t size) const = 0;
   virtual bool get_default_value(std::string &str) const = 0;

   template<typename Type>
   static bool parse_value(const std::string &text, Type &value)
   {
      std::istringstream is(text);

      if (!(is >> value)) {
         return false;
      }

      return true;
   }

   static bool parse_value(const std::string &text, std::string &value)
   {
      value = text;
      return true;
   }

   template<typename Type>
   static bool parse_value(const std::string &text, std::vector<Type> &value)
   {
      Type tmp;

      if (!parse_value(text, tmp)) {
         return false;
      }

      value.push_back(tmp);
      return true;
   }
};

template<typename Type>
struct type_value_parser : public value_parser
{
   virtual bool has_multiple_values() const override
   {
      return is_vector<Type>::value;
   }

   virtual bool parse(const std::string &text) override
   {
      value_set = value_parser::parse_value(text, value);

      // Check if value is in allowed value list
      if (value_set && allowed_values.size()) {
         for (auto &allowed : allowed_values) {
            if (allowed == value) {
               return true;
            }
         }

         // Throw error
         std::ostringstream os;

         for (auto i = 0u; i < allowed_values.size(); ++i) {
            if (i != 0) {
               os << ", ";
            }

            os << allowed_values[i];
         }

         throw unexpected_option_value(text, os.str());
      }

      return value_set;
   }

   virtual bool get_value(void *ptr, std::size_t size) const override
   {
      if (size != sizeof(Type)) {
         throw invalid_option_get_type_exception();
      }

      if (value_set) {
         *reinterpret_cast<Type *>(ptr) = value;
      } else if (default_value_set) {
         *reinterpret_cast<Type *>(ptr) = default_value;
      } else {
         return false;
      }

      return true;
   }

   virtual bool get_default_value(std::string &str) const override
   {
      if (!default_value_set) {
         return false;
      }

      std::ostringstream os;
      os << default_value;
      str = os.str();
      return true;
   }

   bool optional = false;
   Type value;
   bool value_set = false;
   std::vector<Type> allowed_values;
   Type default_value;
   bool default_value_set = false;
};

template<typename ValueType, typename... Types>
struct get_value_parser_2
{
   static value_parser *get(Types... args)
   {
      auto parser = new type_value_parser<ValueType>();
      get_allowed<ValueType, Types...>::get(parser->allowed_values, args...);
      parser->default_value_set = get_default_value<ValueType, Types...>::get(parser->default_value, args...);
      return parser;
   }
};

template<typename... Types>
struct get_value_parser_2<no_value_type, Types...>
{
   static value_parser *get(Types... args)
   {
      return nullptr;
   }
};

template<typename... Types>
struct get_value_parser
{
   using ValueType = typename get_value_type<Types...>::ValueType;

   static value_parser *get(Types... args)
   {
      return get_value_parser_2<ValueType, Types...>::get(args...);
   }
};

} // namespace internal

} // namespace excmd
