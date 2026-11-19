#include <cxxrelay/value.h>

#include <cstdint>
#include <iostream>

int main()
{
   using CxxRelay::Value;
   Value model = Value::object({{"id", std::uint64_t{42}},
                                {"position", Value::object({{"x", 1.0}, {"y", 2.0}, {"z", 3.0}})},
                                {"tags", Value::array({"mesh", "selected"})}});
   model.at("tags").push_back("active");

   std::cout << "id=" << model.at("id").as_uint64() << ", z=" << model.at("position").at("z").get<double>()
             << ", tags=" << model.at("tags").size() << '\n';
}
