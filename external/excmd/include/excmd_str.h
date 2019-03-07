#pragma once
#include <codecvt>
#include <locale>
#include <vector>

namespace excmd
{

template<typename IteratorType>
static std::vector<std::string> splitCommandString(IteratorType begin, IteratorType end)
{
   std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
   std::vector<std::string> args;
   std::wstring arg;
   auto locale = std::locale("");
   auto inQuotes = false;

   for (auto itr = begin; itr != end; ++itr) {
      auto cur = *itr;

      if (cur == '\\') {
         if (itr + 1 != end) {
            auto next = *(itr + 1);

            if (next == '\\') {
               arg.push_back('\\');
               itr++;
               continue;
            } else if (next == '"') {
               arg.push_back('"');
               itr++;
               continue;
            } else if (std::isspace(next, locale)) {
               arg.push_back(next);
               itr++;
               continue;
            }
         }

         arg.push_back(cur);
      } else if (cur == '"') {
         inQuotes = !inQuotes;
      } else if (!inQuotes && std::isspace(cur, locale)) {
         if (arg.size()) {
            args.push_back(converter.to_bytes(arg));
            arg.clear();
         }
      } else {
         arg.push_back(cur);
      }
   }

   if (arg.size()) {
      args.push_back(converter.to_bytes(arg));
   }

   return args;
}

} // namespace excmd
