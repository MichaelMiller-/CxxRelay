#include <cxxrelay/chat.h>
#include <cxxrelay/core.h>
#include <cxxrelay/openai_responses.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "mock_provider.h"
#include "tracing_provider.h"

// User code
//
struct Document
{
   std::uint64_t id{};
   std::string title;
   std::string category;
};

class DocumentService
{
public:
   std::vector<Document> list() const { return documents_; }

   std::vector<Document> search(std::string query) const
   {
      std::ranges::transform(query, query.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::vector<Document> matching;
      for (const auto& document : documents_) {
         std::string title = document.title;
         std::ranges::transform(title, title.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
         if (title.find(query) != std::string::npos)
            matching.push_back(document);
      }
      return matching;
   }

   bool rename(std::uint64_t id, std::string title)
   {
      if (title.empty() || title.size() > 200)
         return false;
      for (auto& document : documents_) {
         if (document.id == id) {
            document.title = std::move(title);
            return true;
         }
      }
      return false;
   }

private:
   std::vector<Document> documents_ = {{1, "Projektbericht 2026", "Bericht"},
                                       {2, "Technische Spezifikation", "Technik"},
                                       {3, "Statusbericht", "Bericht"}};
};

// Worker
//
using namespace CxxRelay;

namespace
{
   std::mutex output_mutex;

   void emit(std::string_view id, std::string_view type, Value detail = Value::object())
   {
      auto event = Value::object({{"id", std::string(id)}, {"type", std::string(type)}});
      if (detail.is<Value::Object>()) {
         for (const auto& [key, value] : detail.get<Value::Object>())
            event.set(key, value);
      }
      std::lock_guard lock(output_mutex);
      std::cout << to_json(event) << '\n' << std::flush;
   }

   class Worker final
   {
   public:
      explicit Worker(bool openai) : tools_(registry_)
      {
         // Register existing classes
         //
         registry_.type<Document>("Document")
            .field("id", &Document::id)
            .field("title", &Document::title)
            .field("category", &Document::category);
         registry_.type<DocumentService>("DocumentService")
            .method("list", &DocumentService::list, {.description = "List all available documents"})
            .method("search", &DocumentService::search,
                    {.parameter_names = {"query"}, .description = "Search document titles"})
            .method("rename", &DocumentService::rename,
                    {.parameter_names = {"id", "title"}, .description = "Rename a document by ID"});
         tools_.expose("documents_list", documents_, "list")
            .expose("documents_search", documents_, "search")
            .expose("documents_rename", documents_, "rename");
         tools_.set_audit_sink([this](const ToolAuditEvent& event) {
            emit(current_id_, "tool",
                 Value::object({{"name", std::string(event.tool_name)},
                                {"code", event.code == ToolErrorCode::None                   ? "ok"
                                         : event.code == ToolErrorCode::ConfirmationRequired ? "confirmation_required"
                                                                                             : "failed"}}));
         });

         // Setup AI
         //
         if (openai) {
            auto const key = std::getenv("OPENAI_API_KEY");
            auto const model = std::getenv("OPENAI_MODEL");
            if (!key || !*key || !model || !*model)
               throw std::runtime_error("OPENAI_API_KEY and OPENAI_MODEL are required in OpenAI mode");

            auto transport = std::make_shared<CurlHttpTransport>();
            provider_ = std::make_unique<OpenAIResponsesProvider>(transport, OpenAIResponsesOptions{.api_key = key, .model = model});
         } else {
            // testing
            provider_ = std::make_unique<MockProvider>();
         }
         mode_ = openai ? "openai" : "mock";
         trace_ = std::make_unique<TracingProvider>(*provider_, [this](auto event, auto details) {
            emit(current_id_, event, std::move(details));
         });
         chat_ = std::make_unique<ChatOrchestrator>(*trace_, tools_);
      }

      void execute(const Value& command)
      {
         const auto id = command.at("id").get<std::string>();
         if (id.empty() || id.size() > 128)
            return;
         const auto action = command.at("action").get<std::string>();
         if (action == "cancel") {
            if (busy_.load() && task_.joinable())
               task_.request_stop();
            emit(id, "complete", Value::object({{"status", busy_.load() ? "cancel_requested" : "idle"}}));
            return;
         }
         if (busy_.load()) {
            emit(id, "error", Value::object({{"message", "Worker ist beschäftigt"}}));
            return;
         }
         if (task_.joinable())
            task_.join();
         if (action == "reset") {
            chat_->reset();
            emit(id, "complete", Value::object({{"status", "reset"}, {"snapshot", snapshot()}}));
            return;
         }
         if (action == "snapshot") {
            emit(id, "complete", Value::object({{"snapshot", snapshot()}}));
            return;
         }
         if (action != "send" && action != "resume") {
            emit(id, "error", Value::object({{"message", "Unbekannte Aktion"}}));
            return;
         }
         const std::string text = action == "send" ? command.at("text").get<std::string>() : "";
         const std::string approval_id = action == "resume" ? command.at("approval_id").get<std::string>() : "";
         const bool approved = action == "resume" && command.at("approved").get<bool>();
         busy_.store(true);
         task_ = std::jthread([this, id, action, text, approval_id, approved](std::stop_token stop) {
            current_id_ = id; // Written only by the single active task thread.
            try {
               const auto callback = [id](std::string_view delta) {
                  if (!delta.empty())
                     emit(id, "delta", Value::object({{"text", std::string(delta)}}));
               };
               auto result = action == "send" ? chat_->send(text, context_, stop, callback)
                                              : chat_->resume(approval_id, approved, context_, stop, callback);
               if (result.status == ChatTurnResult::Status::AwaitingConfirmation) {
                  const auto& request = *result.approval;
                  auto event = Value::object({{"approval_id", request.approval_id},
                                              {"tool_name", request.tool_name},
                                              {"arguments", request.arguments},
                                              {"snapshot", snapshot()}});
                  busy_.store(false);
                  emit(id, "approval", std::move(event));
               } else {
                  auto event = Value::object({{"answer", result.answer}, {"snapshot", snapshot()}});
                  busy_.store(false);
                  emit(id, "complete", std::move(event));
               }
            } catch (const ChatCancelled&) {
               busy_.store(false);
               emit(id, "cancelled", Value::object({{"message", "Anfrage abgebrochen. Chat zurücksetzen."}}));
            } catch (const std::exception& e) {
               // Keep exception details local; do not leak provider diagnostics to browser.
               std::cerr << "[worker] operation failed: " << e.what() << '\n';
               busy_.store(false);
               emit(id, "error",
                    Value::object(
                       {{"message", "Anfrage fehlgeschlagen; ggf. Chat zurücksetzen (Details im Worker-Log)."}}));
            }
         });
      }

   private:
      Value snapshot() const
      {
         Value descriptors = Value::array();
         for (const auto* type : {registry_.find<Document>(), registry_.find<DocumentService>()}) {
            if (!type)
               continue;
            Value fields = Value::array();
            for (const auto& field : type->fields())
               fields.push_back(std::string(field.name()));
            Value methods = Value::array();
            for (const auto& method : type->methods())
               methods.push_back(std::string(method.name()));
            descriptors.push_back(
               Value::object({{"type", std::string(type->name())}, {"fields", fields}, {"methods", methods}}));
         }
         return Value::object({{"mode", mode_},
                               {"documents", registry_.to_value(documents_->list())},
                               {"types", descriptors},
                               {"tools", tools_.definitions(context_)},
                               {"failed", chat_->failed()},
                               {"awaiting_confirmation", chat_->awaiting_confirmation()}});
      }

      TypeRegistry registry_;
      std::shared_ptr<DocumentService> documents_ = std::make_shared<DocumentService>();
      ToolDispatcher tools_;
      std::unique_ptr<ILLMProvider> provider_;
      std::unique_ptr<TracingProvider> trace_;
      std::unique_ptr<ChatOrchestrator> chat_;
      const ToolContext context_{.principal = "local-browser-user", .allow_read = true, .allow_write = true};
      std::string mode_;
      std::atomic<bool> busy_{false};
      std::jthread task_;
      std::string current_id_; // only accessed from task thread via audit callback
   };
} // namespace

int main(int argc, char** argv)
{
   try {
      bool openai = false;
      if (argc > 1) {
         if (std::string_view(argv[1]) == "--openai")
            openai = true;
         else if (std::string_view(argv[1]) != "--mock") {
            std::cerr << "Usage: prototype.worker [--mock | --openai]\n";
            return 2;
         }
      }

      Worker worker(openai);
      std::string line;
      while (std::getline(std::cin, line)) {
         if (line.size() > 128 * 1024)
            continue;
         std::string id;
         try {
            auto command = parse_json(line);
            id = command.at("id").get<std::string>();
            worker.execute(command);
         } catch (const std::exception&) {
            if (!id.empty())
               emit(id, "error", Value::object({{"message", "Ungültige Worker-Anfrage"}}));
         }
      }
   } catch (const std::exception& error) {
      std::cerr << "[worker] startup failed: " << error.what() << '\n';
      return 2;
   }
}
