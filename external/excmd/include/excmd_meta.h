#pragma once
#include <string>
#include <vector>

namespace excmd
{

// Sets the allowed values for an option
template<typename Type>
struct allowed
{
   allowed(const std::vector<Type> &values) :
      values(values)
   {
   }

   std::vector<Type> values;
};

// Sets the default value for an option
template<typename Type>
struct default_value
{
   default_value(Type value) :
      value(value)
   {
   }

   Type value;
};

template<typename Type>
default_value<Type> make_default_value(const Type &value)
{
   return default_value<Type>(value);
}

// Sets the description for an option
struct description
{
   description(const std::string &desc) :
      desc(desc)
   {
   }

   std::string desc;
};

// Defines if a command argument is optional
struct optional
{
};

// Define the type of value for an option
template<typename Type>
struct value
{
};

namespace internal
{

// Returns true if Type is a std::vector
template<typename Type>
struct is_vector
{
   static constexpr bool value = false;
};

template<typename Type>
struct is_vector<std::vector<Type>>
{
   static constexpr bool value = true;
};

// allowed
template<typename ValueType, typename... Types>
struct get_allowed;

template<typename ValueType>
struct get_allowed<ValueType>
{
   static bool get(std::vector<ValueType> &values)
   {
      return false;
   }
};

template<typename ValueType, typename... Remaining>
struct get_allowed<ValueType, allowed<std::string>, Remaining...>
{
   static bool get(std::vector<ValueType> &values, allowed<ValueType> allowed, Remaining... args)
   {
      values = allowed.values;
      return true;
   }
};

template<typename ValueType, typename First, typename... Remaining>
struct get_allowed<ValueType, First, Remaining...>
{
   static bool get(std::vector<ValueType> &values, First first, Remaining... args)
   {
      return get_allowed<ValueType, Remaining...>::get(values, args...);
   };
};

// default_value
template<typename ValueType, typename... Types>
struct get_default_value;

template<typename ValueType>
struct get_default_value<ValueType>
{
   static bool get(ValueType &value)
   {
      return false;
   }
};

template<typename ValueType, typename... Remaining>
struct get_default_value<ValueType, default_value<std::string>, Remaining...>
{
   static bool get(ValueType &value, default_value<ValueType> def, Remaining... args)
   {
      value = def.value;
      return true;
   }
};

template<typename ValueType, typename First, typename... Remaining>
struct get_default_value<ValueType, First, Remaining...>
{
   static bool get(ValueType &value, First first, Remaining... args)
   {
      return get_default_value<ValueType, Remaining...>::get(value, args...);
   };
};

// description
template<typename... Types>
struct get_description;

template<>
struct get_description<>
{
   static std::string get()
   {
      return {};
   }
};

template<typename First, typename... Remaining>
struct get_description<First, Remaining...>
{
   static std::string get(First first, Remaining... args)
   {
      return get_description<Remaining...>::get(args...);
   };
};

template<typename... Remaining>
struct get_description<description, Remaining...>
{
   static std::string get(description desc, Remaining... args)
   {
      return desc.desc;
   }
};

// optional
template<typename... Types>
struct get_optional;

template<>
struct get_optional<>
{
   static constexpr bool value = false;
};

template<typename... Remaining>
struct get_optional<optional, Remaining...>
{
   static constexpr bool value = true;
};

template<typename First, typename... Remaining>
struct get_optional<First, Remaining...>
{
   static constexpr bool value = get_optional<Remaining...>::value;
};

// value<Type> / default_value<Type>
struct no_value_type;

template<typename... Types>
struct get_value_type;

template<>
struct get_value_type<>
{
   using ValueType = no_value_type;
};

template<typename First, typename... Remaining>
struct get_value_type<First, Remaining...>
{
   using ValueType = typename get_value_type<Remaining...>::ValueType;
};

template<typename Type, typename... Remaining>
struct get_value_type<value<Type>, Remaining...>
{
   using ValueType = Type;
};

template<typename Type, typename... Remaining>
struct get_value_type<default_value<Type>, Remaining...>
{
   using ValueType = Type;
};

} // namespace internal

} // namespace excmd
