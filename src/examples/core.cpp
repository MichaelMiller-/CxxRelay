#include <cxxrelay/core.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

struct Point3D
{
   double x{};
   double y{};
   double z{};
   friend bool operator==(const Point3D&, const Point3D&) = default;
};

struct Node
{
   std::uint64_t id{};
   Point3D position{};
   std::vector<std::string> tags{};
   std::optional<std::string> comment{};
   friend bool operator==(const Node&, const Node&) = default;
};

int main()
{
   using namespace CxxRelay;

   TypeRegistry registry;
   registry.type<Point3D>("Point3D").field("x", &Point3D::x).field("y", &Point3D::y).field("z", &Point3D::z);

   registry.type<Node>("Node")
      .field("id", &Node::id)
      .field("position", &Node::position)
      .field("tags", &Node::tags)
      .field("comment", &Node::comment);

   const Node original{42, {1, 2, 3}, {"active", "visible"}, std::nullopt};
   Value data = registry.to_value(original);
   Node recovered = registry.from_value<Node>(data);

   std::cout << "Node.id = " << recovered.id << '\n';
   std::cout << "Node.position.x = " << recovered.position.x << '\n';
   std::cout << "Registered fields = " << registry.find<Node>()->fields().size() << '\n';
   return original == recovered ? 0 : 1;
}
