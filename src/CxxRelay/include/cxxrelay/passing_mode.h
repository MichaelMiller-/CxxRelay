#pragma once

namespace CxxRelay
{
   // Classification of parameters and return values. The underlying type is
   // always reported without top-level cv/reference qualifiers; PassingMode
   // retains the qualifier information needed by future invocation adapters.
   enum class PassingMode { Value, LValueReference, ConstLValueReference, RValueReference, ConstRValueReference };
} // namespace CxxRelay