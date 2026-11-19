#include <cxxrelay/json.h>
#include <cxxrelay/json_schema.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace CxxRelay;

struct Point3D
{
   double x{};
   double y{};
   double z{};
};
struct Node
{
   std::uint64_t id{};
   Point3D position{};
   std::optional<std::string> comment{};
};
struct NodeService
{
   Node find(std::uint64_t id) const { return {id, {1.0, 2.0, 3.0}, "example"}; }

   std::vector<Node> find_many(std::vector<std::uint64_t> ids) const
   {
      std::vector<Node> result;
      for (auto id : ids)
         result.push_back(find(id));
      return result;
   }
};

int main()
{
   TypeRegistry registry;
   registry.type<Point3D>("Point3D").field("x", &Point3D::x).field("y", &Point3D::y).field("z", &Point3D::z);
   registry.type<Node>("Node")
      .field("id", &Node::id)
      .field("position", &Node::position)
      .field("comment", &Node::comment);
   registry.type<NodeService>("NodeService")
      .method("find", &NodeService::find, {.parameter_names = {"id"}, .description = "Find a node by its numeric ID"})
      .method("find_many", &NodeService::find_many, {.parameter_names = {"ids"}});

   JsonSchemaGenerator schemas(registry);
   const Value arguments_schema = schemas.method_arguments<NodeService>("find");
   std::cout << "Tool arguments JSON Schema:\n" << to_json(arguments_schema) << "\n\n";
   std::cout << "Result JSON Schema:\n" << to_json(schemas.method_result<NodeService>("find")) << "\n\n";

   NodeService service;
   const Value args = parse_json(R"({"id": 42})");
   const Value result = registry.invoke(service, "find", args);
   const std::string response_json = to_json(result);
   std::cout << "Result JSON:\n" << response_json << "\n\n";
   const Node restored = registry.from_value<Node>(parse_json(response_json));
   std::cout << "Decoded node ID: " << restored.id << "\n";
}
