#pragma once

#include <string>
#include <vector>

namespace CxxRelay
{
   enum class MethodAccess { ReadOnly, Write };

   struct MethodOptions
   {
      std::vector<std::string> parameter_names{};
      std::string description{};
      // When absent, const methods default to ReadOnly and mutable methods
      // conservatively default to Write. Access is descriptive metadata only.
      std::optional<MethodAccess> access{};
   };
} // namespace CxxRelay