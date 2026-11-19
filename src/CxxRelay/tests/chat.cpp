#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/chat.h>
#include <cxxrelay/openai_responses.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

using namespace CxxRelay;
namespace
{
   struct Doc
   {
      std::string title = "draft";
      int counter = 0;
      std::string search(std::string term) const { return "found:" + term; }
      void rename(std::string name)
      {
         title = std::move(name);
         ++counter;
      }
   };

   struct Fixture
   {
      TypeRegistry registry;
      std::shared_ptr<Doc> service = std::make_shared<Doc>();
      ToolDispatcher tools{registry};
      Fixture()
      {
         registry.type<Doc>("Doc")
            .method("search", &Doc::search, {.parameter_names = {"term"}, .description = "Search for a document"})
            .method("rename", &Doc::rename, {.parameter_names = {"name"}, .description = "Rename a document"});
         tools.expose("search", service, "search").expose("rename", service, "rename");
      }
   };
   struct Script : ILLMProvider
   {
      std::vector<LlmResponse> answers;
      std::vector<LlmRequest> requests;
      std::string delta;
      bool throw_error = false;
      LlmResponse complete(const LlmRequest& request, std::stop_token stop, TextDeltaCallback on_delta) override
      {
         if (stop.stop_requested())
            throw ChatCancelled{};
         requests.push_back(request);
         if (throw_error)
            throw LlmError("transport unavailable");
         if (!delta.empty() && on_delta)
            on_delta(delta);
         if (answers.empty())
            throw LlmError("script exhausted");
         auto answer = std::move(answers.front());
         answers.erase(answers.begin());
         return answer;
      }
   };
   ToolContext viewer() { return {.principal = "alice", .allow_read = true}; }
   ToolContext editor() { return {.principal = "alice", .allow_read = true, .allow_write = true}; }
   LlmResponse read_response(std::string id = "resp1")
   {
      return {std::move(id), "", {{"c1", "search", R"({"term":"abc"})"}}};
   }
   LlmResponse write_response(std::string id = "resp1")
   {
      return {std::move(id), "", {{"c2", "rename", R"({"name":"Final"})"}}};
   }

   struct FakeHttp : IHttpTransport
   {
      long status = 200;
      std::string response_body;
      std::vector<std::string> chunks;
      std::string sent_json;
      std::string sent_url;
      std::vector<HttpHeader> sent_headers;
      HttpResponse post(std::string_view url, std::string_view json, const std::vector<HttpHeader>& headers,
                        const HttpChunkCallback& chunk, std::stop_token stop) override
      {
         if (stop.stop_requested())
            throw ChatCancelled{};
         sent_json = json;
         sent_url = url;
         sent_headers = headers;
         if (chunk)
            for (const auto& part : chunks)
               chunk(part);
         return {status, response_body};
      }
   };
   std::shared_ptr<OpenAIResponsesProvider> openai(std::shared_ptr<FakeHttp> http, bool stream = true)
   {
      return std::make_shared<OpenAIResponsesProvider>(
         http,
         OpenAIResponsesOptions{.api_key = "secret-for-testing", .model = "test-model", .enable_streaming = stream});
   }
   const std::string ok_json =
      R"({"id":"resp_3","status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"Hello!"}]}]})";
} // namespace

TEST_CASE("7.1 - Simple user request and text response", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "Hello", {}}};
   ChatOrchestrator chat(p, f.tools);
   auto result = chat.send("Hi", viewer());
   REQUIRE(result.status == ChatTurnResult::Status::Completed);
   REQUIRE(result.answer == "Hello");
   REQUIRE(chat.history().size() == 2);
   REQUIRE(p.requests[0].input[0].kind == LlmInputItem::Kind::UserMessage);
   REQUIRE_FALSE(p.requests[0].previous_response_id.has_value());
}
TEST_CASE("7.2 - Second turn continues via previous_response_id", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "A", {}}, {"r2", "B", {}}};
   ChatOrchestrator chat(p, f.tools);
   (void)chat.send("first", viewer());
   (void)chat.send("second", viewer());
   REQUIRE(p.requests[1].previous_response_id.value() == "r1");
   REQUIRE(p.requests[1].input[0].content == "second");
   REQUIRE(chat.history().size() == 4);
}
TEST_CASE("7.3 - Read tool executes and returns output with the same call id", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {read_response(), {"resp2", "Done", {}}};
   ChatOrchestrator chat(p, f.tools);
   auto outcome = chat.send("Find it", viewer());
   REQUIRE(outcome.answer == "Done");
   REQUIRE(p.requests.size() == 2);
   REQUIRE(p.requests[1].previous_response_id.value() == "resp1");
   REQUIRE(p.requests[1].input[0].call_id == "c1");
   REQUIRE(parse_json(p.requests[1].input[0].content).at("ok").get<bool>());
}
TEST_CASE("7.4 - Multiple independent tool outputs preserved in order", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "", {{"c1", "search", R"({"term":"x"})"}, {"c2", "search", R"({"term":"y"})"}}},
                {"r2", "Both", {}}};
   ChatOrchestrator chat(p, f.tools);
   REQUIRE(chat.send("two", viewer()).answer == "Both");
   REQUIRE(p.requests[1].input.size() == 2);
   REQUIRE(p.requests[1].input[0].call_id == "c1");
   REQUIRE(p.requests[1].input[1].call_id == "c2");
}
TEST_CASE("7.5 - Write call pauses and does not mutate before approval", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response(), {"r2", "Renamed", {}}};
   ChatOrchestrator chat(p, f.tools);
   auto result = chat.send("rename", editor());
   REQUIRE(result.status == ChatTurnResult::Status::AwaitingConfirmation);
   REQUIRE(result.approval->tool_name == "rename");
   REQUIRE(result.approval->arguments.at("name").get<std::string>() == "Final");
   REQUIRE(f.service->counter == 0);
   REQUIRE(p.requests.size() == 1);
}
TEST_CASE("7.6 - Approval from host executes exactly once", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response(), {"r2", "Renamed", {}}};
   ChatOrchestrator chat(p, f.tools);
   auto result = chat.send("rename", editor());
   result = chat.resume(result.approval->approval_id, true, editor());
   REQUIRE(result.answer == "Renamed");
   REQUIRE(f.service->counter == 1);
   REQUIRE(f.service->title == "Final");
   REQUIRE(chat.history().size() == 3);
   REQUIRE(parse_json(p.requests[1].input[0].content).at("ok").get<bool>());
}
TEST_CASE("7.7 - Denial produces a tool result without side effects", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response(), {"r2", "Denied", {}}};
   ChatOrchestrator chat(p, f.tools);
   auto first = chat.send("rename", editor());
   auto result = chat.resume(first.approval->approval_id, false, editor());
   REQUIRE(result.answer == "Denied");
   REQUIRE(f.service->counter == 0);
   REQUIRE_FALSE(parse_json(p.requests[1].input[0].content).at("ok").get<bool>());
}
TEST_CASE("7.8 - LLM cannot supply write confirmation via context", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response()};
   ChatOrchestrator chat(p, f.tools);
   auto context = editor();
   context.confirm_write = [](auto&) { return true; };
   auto result = chat.send("rename", context);
   REQUIRE(result.status == ChatTurnResult::Status::AwaitingConfirmation);
   REQUIRE(f.service->counter == 0);
}
TEST_CASE("7.9 - Stale approval id rejected", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response()};
   ChatOrchestrator chat(p, f.tools);
   auto first = chat.send("rename", editor());
   REQUIRE_THROWS_AS(chat.resume("fake", true, editor()), LlmError);
   REQUIRE(chat.awaiting_confirmation());
   REQUIRE(f.service->counter == 0);
}
TEST_CASE("7.10 - Cross-principal approval denied", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response()};
   ChatOrchestrator chat(p, f.tools);
   auto first = chat.send("rename", editor());
   auto another = editor();
   another.principal = "bob";
   REQUIRE_THROWS_AS(chat.resume(first.approval->approval_id, true, another), LlmError);
   REQUIRE(f.service->counter == 0);
}
TEST_CASE("7.11 - Permission change is rechecked when approving", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {write_response(), {"r2", "No write", {}}};
   ChatOrchestrator chat(p, f.tools);
   auto first = chat.send("rename", editor());
   auto response = chat.resume(first.approval->approval_id, true, viewer());
   REQUIRE(response.answer == "No write");
   REQUIRE(f.service->counter == 0);
   REQUIRE_FALSE(parse_json(p.requests[1].input[0].content).at("ok").get<bool>());
}
TEST_CASE("7.12 - Unavailable tool returns sanitized tool result", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "", {{"c1", "not_exposed", "{}"}}}, {"r2", "Unavailable", {}}};
   ChatOrchestrator chat(p, f.tools);
   REQUIRE(chat.send("try", viewer()).answer == "Unavailable");
   REQUIRE(parse_json(p.requests[1].input[0].content).at("error").at("code").get<std::string>() == "unknown_tool");
}
TEST_CASE("7.13 - Bad arguments fail without calling C++ service", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "", {{"c1", "rename", R"({"name":123})"}}}, {"r2", "Invalid", {}}};
   ChatOrchestrator chat(p, f.tools);
   REQUIRE(chat.send("rename", editor()).answer == "Invalid");
   REQUIRE(f.service->counter == 0);
}
TEST_CASE("7.14 - Duplicate call IDs are rejected before side effects", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "", {{"same", "search", R"({"term":"x"})"}, {"same", "rename", R"({"name":"Y"})"}}}};
   ChatOrchestrator chat(p, f.tools);
   REQUIRE_THROWS_AS(chat.send("call", editor()), LlmError);
   REQUIRE(chat.failed());
   REQUIRE(f.service->counter == 0);
}
TEST_CASE("7.15 - Maximum model responses enforced", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {read_response(), read_response("r2")};
   ChatOrchestrator chat(p, f.tools, {.max_model_responses = 1});
   REQUIRE_THROWS_AS(chat.send("loop", viewer()), LlmError);
   REQUIRE(chat.failed());
}
TEST_CASE("7.16 - Maximum tool calls enforced", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "", {{"c1", "search", R"({"term":"x"})"}, {"c2", "search", R"({"term":"y"})"}}}};
   ChatOrchestrator chat(p, f.tools, {.max_tool_calls = 1});
   REQUIRE_THROWS_AS(chat.send("loop", viewer()), LlmError);
   REQUIRE(chat.failed());
}
TEST_CASE("7.17 - Cancellation marks conversation for reset", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "yes", {}}};
   ChatOrchestrator chat(p, f.tools);
   std::stop_source source;
   source.request_stop();
   REQUIRE_THROWS_AS(chat.send("stop", viewer(), source.get_token()), ChatCancelled);
   REQUIRE(chat.failed());
   REQUIRE_THROWS_AS(chat.send("still stopped", viewer()), LlmError);
   chat.reset();
   REQUIRE_FALSE(chat.failed());
   REQUIRE(chat.history().empty());
}
TEST_CASE("7.18 - Text deltas are delivered to worker's callback", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "Hello", {}}};
   p.delta = "Hel";
   ChatOrchestrator chat(p, f.tools);
   std::string observed;
   REQUIRE(chat.send("hi", viewer(), {}, [&](std::string_view s) { observed += s; }).answer == "Hello");
   REQUIRE(observed == "Hel");
}
TEST_CASE("7.19 - Provider failure requires an explicit reset", "[chat]")
{
   Fixture f;
   Script p;
   p.throw_error = true;
   ChatOrchestrator chat(p, f.tools);
   REQUIRE_THROWS_AS(chat.send("hi", viewer()), LlmError);
   REQUIRE(chat.failed());
   chat.reset();
   p.throw_error = false;
   p.answers = {{"r1", "Back", {}}};
   REQUIRE(chat.send("retry", viewer()).answer == "Back");
}
TEST_CASE("7.20 - User message bounds and principal isolation", "[chat]")
{
   Fixture f;
   Script p;
   p.answers = {{"r1", "OK", {}}};
   ChatOrchestrator chat(p, f.tools, {.max_user_message_bytes = 5});
   REQUIRE_THROWS_AS(chat.send("long message", viewer()), std::invalid_argument);
   REQUIRE(chat.send("short", viewer()).answer == "OK");
   auto other = viewer();
   other.principal = "other";
   REQUIRE_THROWS_AS(chat.send("again", other), LlmError);
}
TEST_CASE("7.21 - OpenAI nonstreaming payload includes function schema and model", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->response_body = ok_json;
   auto provider = openai(http, false);
   Fixture f;
   LlmRequest request{{LlmInputItem::user("Hello")}, {}, f.tools.definitions(viewer())};
   auto result = provider->complete(request);
   REQUIRE(result.text == "Hello!");
   auto payload = parse_json(http->sent_json);
   REQUIRE(payload.at("model").get<std::string>() == "test-model");
   REQUIRE(payload.at("store").get<bool>());
   REQUIRE_FALSE(payload.at("stream").get<bool>());
   REQUIRE(payload.at("tools").size() == 1);
   REQUIRE(payload.at("tools").at(0).at("type").get<std::string>() == "function");
   REQUIRE_FALSE(payload.at("tools").at(0).at("strict").get<bool>());
   REQUIRE(http->sent_url == "https://api.openai.com/v1/responses");
}
TEST_CASE("7.22 - OpenAI function call and call_id parsed", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->response_body =
      R"({"id":"r1","status":"completed","output":[{"type":"reasoning","id":"rs1"},{"type":"function_call","call_id":"call_x","name":"search","arguments":"{\"term\":\"x\"}"}]})";
   auto provider = openai(http, false);
   auto response = provider->complete({{LlmInputItem::user("find")}, {}, Value::array()});
   REQUIRE(response.tool_calls.size() == 1);
   REQUIRE(response.tool_calls[0].call_id == "call_x");
   REQUIRE(response.tool_calls[0].arguments_json == R"({"term":"x"})");
}
TEST_CASE("7.23 - OpenAI tool output resumes previous response", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->response_body = ok_json;
   auto provider = openai(http, false);
   (void)provider->complete({{LlmInputItem::tool_result("c1", R"({"ok":true})")}, "old-response", Value::array()});
   auto payload = parse_json(http->sent_json);
   REQUIRE(payload.at("previous_response_id").get<std::string>() == "old-response");
   REQUIRE(payload.at("input").at(0).at("type").get<std::string>() == "function_call_output");
   REQUIRE(payload.at("input").at(0).at("call_id").get<std::string>() == "c1");
}
TEST_CASE("7.24 - OpenAI stream parses fragmented SSE and forwards deltas", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->chunks = {"data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hel\"}\n\n",
                   "data: {\"type\":\"response.output_text.delta\",\"delta\":\"lo\"}\n",
                   "\ndata: {\"type\":\"response.completed\",\"response\":" + ok_json + "}\r\n\r\n"};
   auto provider = openai(http);
   std::string observed;
   auto result = provider->complete({{LlmInputItem::user("Hi")}, {}, Value::array()}, {},
                                    [&](std::string_view delta) { observed += delta; });
   REQUIRE(observed == "Hello");
   REQUIRE(result.text == "Hello!");
   REQUIRE(parse_json(http->sent_json).at("stream").get<bool>());
}
TEST_CASE("7.25 - OpenAI HTTP errors do not expose response body", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->status = 401;
   http->response_body = "secret error";
   auto provider = openai(http, false);
   bool threw = false;
   try {
      (void)provider->complete({{LlmInputItem::user("Hi")}, {}, Value::array()});
   } catch (const LlmError& e) {
      threw = true;
      REQUIRE(std::string(e.what()).find("secret error") == std::string::npos);
      REQUIRE(std::string(e.what()).find("401") != std::string::npos);
   }
   REQUIRE(threw);
}
TEST_CASE("7.26 - Incomplete and malformed model output rejected", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   auto provider = openai(http, false);
   http->response_body = R"({"id":"r1","status":"incomplete","output":[]})";
   REQUIRE_THROWS_AS(provider->complete({{LlmInputItem::user("Hi")}, {}, Value::array()}), LlmError);
   http->response_body = "garbled";
   REQUIRE_THROWS_AS(provider->complete({{LlmInputItem::user("Hi")}, {}, Value::array()}), LlmError);
}
TEST_CASE("7.27 - Streaming response must include completed event", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->chunks = {"data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hello\"}\n\n"};
   auto provider = openai(http);
   REQUIRE_THROWS_AS(provider->complete({{LlmInputItem::user("Hi")}, {}, Value::array()}, {}, [](std::string_view) {}),
                     LlmError);
}
TEST_CASE("7.28 - OpenAI provider validates credentials and model", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   REQUIRE_THROWS_AS(OpenAIResponsesProvider(http, {.api_key = "", .model = "m"}), std::invalid_argument);
   REQUIRE_THROWS_AS(OpenAIResponsesProvider(http, {.api_key = "k", .model = ""}), std::invalid_argument);
   REQUIRE_THROWS_AS(OpenAIResponsesProvider(http, {.api_key = "k\nBad", .model = "m"}), std::invalid_argument);
}
TEST_CASE("7.29 - Tool result request without prior response is rejected", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   auto provider = openai(http, false);
   REQUIRE_THROWS_AS(provider->complete({{LlmInputItem::tool_result("c1", "{}")}, {}, Value::array()}), LlmError);
}
TEST_CASE("7.30 - OpenAI refusal content can be represented", "[openai]")
{
   auto http = std::make_shared<FakeHttp>();
   http->response_body =
      R"({"id":"r1","status":"completed","output":[{"type":"message","content":[{"type":"refusal","refusal":"Cannot help"}]}]})";
   auto provider = openai(http, false);
   REQUIRE(provider->complete({{LlmInputItem::user("hello")}, {}, Value::array()}).text == "Cannot help");
}
