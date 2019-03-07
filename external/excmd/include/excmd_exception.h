#pragma once
#include <exception>
#include <string>

namespace excmd
{

class exception : public std::exception
{
public:
   exception(const std::string& message)
      : message(message)
   {
   }

   virtual const char*
   what() const noexcept
   {
      return message.c_str();
   }

private:
   std::string message;
};

class spec_exception : public exception
{
public:
   spec_exception(const std::string& message)
      : exception(message)
   {
   }
};

class parse_exception : public exception
{
public:
   parse_exception(const std::string& message)
      : exception(message)
   {
   }
};

class invalid_option_name_exception : public spec_exception
{
public:
   invalid_option_name_exception(const std::string &name) :
      spec_exception("Invalid option name: " + name)
   {
   }
};

class invalid_option_get_type_exception : public spec_exception
{
public:
   invalid_option_get_type_exception() :
      spec_exception("Invalid type for option.get<Type>")
   {
   }
};

class option_not_exists_exception : public parse_exception
{
public:
   option_not_exists_exception(const std::string &name) :
      parse_exception("Option " + name + " does not exist")
   {
   }
};

class missing_value_exception : public parse_exception
{
public:
   missing_value_exception(const std::string &name) :
      parse_exception("Option " + name + " is missing a value")
   {
   }
};

class not_expecting_value_exception : public parse_exception
{
public:
   not_expecting_value_exception(const std::string &name) :
      parse_exception("Option " + name + " was not expecting a value")
   {
   }
};

class command_missing_argument_exception : public parse_exception
{
public:
   command_missing_argument_exception(const std::string &command, const std::string &argument) :
      parse_exception("Command " + command + " expected argument " + argument)
   {
   }
};

class unexpected_option_value : public parse_exception
{
public:
   unexpected_option_value(const std::string &found, const std::string &allowed) :
      parse_exception("Unexpected value, found: " + found + " expected one of: " + allowed)
   {
   }
};

} // namespace excmd
