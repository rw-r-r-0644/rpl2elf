#pragma once
#include <regex>
#include <map>
#include <memory>
#include <sstream>
#include "excmd_exception.h"
#include "excmd_meta.h"
#include "excmd_str.h"
#include "excmd_value_parser.h"

namespace excmd
{

struct option
{
   bool requires_value()
   {
      return parser != nullptr;
   }

   bool get_value(void *ptr, size_t size)
   {
      return parser->get_value(ptr, size);
   }

   bool get_default_value(std::string &str)
   {
      if (!parser) {
         return false;
      } else {
         return parser->get_default_value(str);
      }
   }

   bool optional = false;
   std::string name;
   std::string shortName;
   std::string longName;
   std::string description;
   std::unique_ptr<internal::value_parser> parser;
};

struct option_group
{
   std::string name;
   std::vector<std::unique_ptr<option>> options;
};

struct command
{
   std::string name;
   std::vector<std::unique_ptr<option>> arguments;
   std::vector<option_group *> groups; // non-owning
};

struct option_group_adder
{
   template<typename... Types>
   option_group_adder &add_option(const std::string &name, Types... args)
   {
      auto opt = new option {};
      opt->description = internal::get_description<Types...>::get(args...);
      opt->parser.reset(internal::get_value_parser<Types...>::get(args...));

      auto cpos = name.find_first_of(',');
      if (cpos != std::string::npos) {
         auto left = name.substr(0, cpos);
         auto right = name.substr(cpos + 1);

         if (left.size() == 1) {
            opt->shortName = left;
            opt->longName = right;
         } else if (right.size() == 1) {
            opt->shortName = right;
            opt->longName = left;
         } else {
            throw invalid_option_name_exception(name);
         }
      } else if (name.size() == 1) {
         opt->shortName = name;
      } else {
         opt->longName = name;
      }

      opt->name = opt->longName.empty() ? opt->shortName : opt->longName;
      group->options.emplace_back(opt);
      return *this;
   }

   option_group *group;
};

struct command_adder
{
   command_adder &add_option_group(option_group *group)
   {
      cmd->groups.emplace_back(group);
      return *this;
   }

   command_adder &add_option_group(const option_group_adder &adder)
   {
      return add_option_group(adder.group);
   }

   template<typename... Types>
   command_adder &add_argument(const std::string &name, Types... args)
   {
      auto opt = new option {};
      opt->name = name;
      opt->description = internal::get_description<Types...>::get(args...);
      opt->parser.reset(internal::get_value_parser<Types...>::get(args...));
      opt->optional = internal::get_optional<Types...>::value;
      cmd->arguments.emplace_back(opt);
      return *this;
   }

   command *cmd;
};

struct option_state
{
   bool empty()
   {
      return !cmd && set_options.empty() && extra_arguments.empty();
   }

   bool has(const std::string &name)
   {
      if (cmd && cmd->name == name) {
         return true;
      } else {
         return set_options.find(name) != set_options.end();
      }
   }

   template<typename Type>
   Type get(const std::string &name)
   {
      Type type {};
      auto itr = set_options.find(name);

      if (itr == set_options.end()) {
         return type;
      } else {
         itr->second->get_value(&type, sizeof(Type));
         return type;
      }
   }

   std::size_t args_set = 0;
   command *cmd = nullptr;
   std::map<std::string, option *> set_options;
   std::vector<std::string> extra_arguments;
};

class parser
{
public:
   option_group_adder
   add_option_group(const std::string &name)
   {
      auto group = new option_group {};
      group->name = name;
      mGroups.emplace_back(group);
      return option_group_adder { group };
   }

   command_adder
   add_command(const std::string &name)
   {
      auto cmd = new command {};
      cmd->name = name;
      mCommands.emplace_back(cmd);
      return command_adder { cmd };
   }

   command_adder
   default_command()
   {
      auto cmd = new command {};
      mDefaultCommand.reset(cmd);
      return command_adder { cmd };
   }

   option_group_adder
   global_options()
   {
      return option_group_adder { &mGlobal };
   }

   option_state
   parse(const std::string &str) const
   {
      return parse(splitCommandString(str.cbegin(), str.cend()));
   }

   option_state
   parse(const std::wstring &str) const
   {
      return parse(splitCommandString(str.cbegin(), str.cend()));
   }

   option_state
   parse(int argc, char **argv) const
   {
      std::vector<std::string> args;

      for (auto i = 1; i < argc; ++i) {
         args.push_back(argv[i]);
      }

      return parse(args);
   }

   option_state
   parse(const std::vector<std::string> &argv) const
   {
      option_state state;
      auto option_matcher = std::regex { "--([[:alnum:]][-_[:alnum:]]+)(=(.*))?|-([a-zA-Z]+)" };
      auto argc = argv.size();

      auto is_valid_value = [](auto &argv, auto i) {
         return i < argv.size() && argv[i][0] != '-';
      };

      auto set_option = [](auto &state, auto opt, const std::string &value = {}) {
         if (opt->parser && !opt->parser->parse(value)) {
            return false;
         }

         state.set_options[opt->name] = opt;
         return true;
      };

      for (auto pos = 0u; pos != argc; ++pos) {
         std::smatch result;
         std::regex_match(argv[pos].cbegin(), argv[pos].cend(), result, option_matcher);

         if (result.empty()) {
            auto &positional = argv[pos];

            if (!state.cmd && mCommands.size() == 0 && mDefaultCommand) {
               state.cmd = mDefaultCommand.get();
            }

            if (!state.cmd) {
               state.cmd = find_command(positional);

               if (!state.cmd) {
                  if (mCommands.size() == 0 && mDefaultCommand) {
                     state.cmd = mDefaultCommand.get();
                  } else {
                     throw option_not_exists_exception(positional);
                  }
               }

               if (state.cmd && state.cmd != mDefaultCommand.get()) {
                  continue;
               }
            }

            if (!state.cmd || state.args_set >= state.cmd->arguments.size()) {
               state.extra_arguments.emplace_back(positional);
            } else {
               auto &arg = state.cmd->arguments[state.args_set++];
               set_option(state, arg.get(), positional);
            }
         } else if (result[4].length()) {
            // Short option(s)
            // -sfoo or -abcdef
            auto short_options = result[4].str();

            for (auto i = 0u; i < short_options.size(); ++i) {
               auto name = short_options.substr(i, 1);
               auto opt = find_option(name, state.cmd);

               if (!opt) {
                  throw option_not_exists_exception(name);
               }

               if (!opt->requires_value()) {
                  set_option(state, opt);
               } else if (i == short_options.size() - 1) {
                  // -s value
                  if (!is_valid_value(argv, pos + 1)) {
                     throw missing_value_exception(opt->name);
                  }

                  set_option(state, opt, argv[pos + 1]);
                  pos++;
               } else if (i == 0) {
                  // -svalue
                  auto value = short_options.substr(i + 1);
                  set_option(state, opt, value);
                  break;
               } else {
                  // -abcvalue is not valid syntax
                  throw missing_value_exception(opt->name);
               }
            }
         } else if (result[1].length()) {
            // Long option
            auto name = result[1].str();
            auto opt = find_option(name, state.cmd);

            if (!opt) {
               throw option_not_exists_exception(name);
            }

            if (result[3].length()) {
               // --long=value
               if (!opt->requires_value()) {
                  throw not_expecting_value_exception(opt->name);
               }

               auto value = result[3].str();
               set_option(state, opt, value);
            } else {
               if (!opt->requires_value()) {
                  // --long
                  set_option(state, opt);
               } else {
                  // --long value
                  if (!is_valid_value(argv, pos + 1)) {
                     throw missing_value_exception(opt->name);
                  }

                  set_option(state, opt, argv[pos + 1]);
                  pos++;
               }
            }
         }
      }

      // Check that we have read all required arguments for a command
      if (state.cmd && state.args_set < state.cmd->arguments.size()) {
         for (auto i = state.args_set; i < state.cmd->arguments.size(); ++i) {
            if (!state.cmd->arguments[i]->optional) {
               throw command_missing_argument_exception(state.cmd->name, state.cmd->arguments[i]->name);
            }
         }
      }

      return state;
   }

   std::string
   format_help(const std::string &name) const
   {
      std::ostringstream os;

      // Print commands
      if (mCommands.size()) {
         os << "Usage:" << std::endl;

         for (auto &cmd : mCommands) {
            os << "  " << name << " " << format_command(*cmd) << std::endl;
         }

         os << std::endl;
      }

      // Print global options
      os << format_option_group(mGlobal) << std::endl;

      // Print every option group
      for (auto &group : mGroups) {
         os << format_option_group(*group) << std::endl;
      }

      return os.str();
   }

   std::string
   format_help(const std::string &name, const std::string &cmd_name) const
   {
      std::ostringstream os;
      auto cmd = find_command(cmd_name);

      // If command doesn't exist print the full help
      if (!cmd) {
         os << "Command " << cmd_name << " not found." << std::endl;
         os << format_help(name);
         return os.str();
      }

      // Print just the selected command
      os << "Usage:" << std::endl;
      os << "  " << name << " " << format_command(*cmd) << std::endl;

      // Print global options
      os << format_option_group(mGlobal) << std::endl;

      // Print just the selected command's options
      for (auto group : cmd->groups) {
         os << format_option_group(*group) << std::endl;
      }

      return os.str();
   }

private:
   command *
   find_command(const std::string &name) const
   {
      for (auto &command : mCommands) {
         if (command->name == name) {
            return command.get();
         }
      }

      return nullptr;
   }

   option *
   find_option(const std::string &name,
               const command *activeCommand) const
   {
      auto option = find_option(name, mGlobal);

      if (option) {
         return option;
      }

      if (activeCommand) {
         for (auto group : activeCommand->groups) {
            auto option = find_option(name, *group);

            if (option) {
               return option;
            }
         }
      }

      return nullptr;
   }

   option *
   find_option(const std::string &name,
               const option_group &group) const
   {
      for (auto &option : group.options) {
         if (option->shortName == name || option->longName == name) {
            return option.get();
         }
      }

      return nullptr;
   }

   std::string
   format_option_group(const option_group &group) const
   {
      std::ostringstream os;
      os << group.name << ":" << std::endl;

      for (auto &option : group.options) {
         std::string default_value;
         os << "  ";

         if (option->shortName.size()) {
            os << "-" << option->shortName << " ";
         }

         if (option->longName.size()) {
            os << "--" << option->longName;
         }

         if (option->requires_value()) {
            os << "=<" << (option->longName.empty() ? option->shortName : option->longName) << ">";
         }

         if (option->get_default_value(default_value)) {
            os << " [default=" << default_value << "]";
         }

         os << std::endl;
         os << "    " << option->description << std::endl;
      }

      return os.str();
   }

   std::string
   format_command(const command &cmd) const
   {
      std::ostringstream os;
      os << cmd.name;

      for (auto group : cmd.groups) {
         for (auto &option : group->options) {
            os << " [";

            if (option->name.length() == 1) {
               os << "-";
            } else {
               os << "--";
            }

            os << option->name;

            if (option->requires_value()) {
               os << "=<" << option->name << ">";
            }

            os << "]";
         }
      }

      for (auto &argument : cmd.arguments) {
         os << " <" << argument->name << ">";
      }

      return os.str();
   }

private:
   option_group mGlobal = { "Global Options", {} };
   std::vector<std::unique_ptr<option_group>> mGroups;
   std::vector<std::unique_ptr<command>> mCommands;
   std::unique_ptr<command> mDefaultCommand;
};

} // namespace excmd
