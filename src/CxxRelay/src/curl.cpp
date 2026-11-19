#include <cxxrelay/openai_responses.h>

#include <curl/curl.h>

#include <exception>
#include <string>
#include <utility>

namespace CxxRelay
{
   namespace
   {
      struct CurlGlobal
      {
         CurlGlobal()
         {
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
               throw LlmError("Could not initialize libcurl");
         }
         ~CurlGlobal() { curl_global_cleanup(); }
      };
      struct ChunkState
      {
         HttpResponse response;
         const HttpChunkCallback* chunk;
         std::exception_ptr exception;
         std::size_t total_bytes{};
      };
      constexpr std::size_t kMaxBytes = 8 * 1024 * 1024;
      size_t write_callback(char* data, size_t size, size_t count, void* opaque) noexcept
      {
         auto& state = *static_cast<ChunkState*>(opaque);
         if (size != 0 && count > static_cast<size_t>(-1) / size)
            return 0;
         const auto bytes = size * count;
         if (bytes > kMaxBytes - state.total_bytes)
            return 0;
         state.total_bytes += bytes;
         try {
            if (*state.chunk)
               (*state.chunk)(std::string_view{data, bytes});
            else
               state.response.body.append(data, bytes);
            return bytes;
         } catch (...) {
            state.exception = std::current_exception();
            return 0;
         }
      }
      int progress_callback(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
      {
         return static_cast<std::stop_token*>(opaque)->stop_requested() ? 1 : 0;
      }
   } // namespace

   HttpResponse CurlHttpTransport::post(std::string_view url, std::string_view json,
                                        const std::vector<HttpHeader>& headers, const HttpChunkCallback& chunk,
                                        std::stop_token stop)
   {
      static CurlGlobal global;
      if (stop.stop_requested())
         throw ChatCancelled{};
      // The provider uses a fixed HTTPS endpoint, not user-supplied URLs.
      if (url != "https://api.openai.com/v1/responses")
         throw LlmError("HTTP endpoint not allowed");
      using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
      CurlHandle handle{curl_easy_init(), &curl_easy_cleanup};
      if (!handle)
         throw LlmError("Could not create libcurl handle");
      curl_slist* raw_headers = nullptr;
      for (const auto& header : headers) {
         if (header.name.find_first_of("\r\n") != std::string::npos ||
             header.value.find_first_of("\r\n") != std::string::npos) {
            curl_slist_free_all(raw_headers);
            throw LlmError("Invalid HTTP header");
         }
         auto* appended = curl_slist_append(raw_headers, (header.name + ": " + header.value).c_str());
         if (!appended) {
            curl_slist_free_all(raw_headers);
            throw LlmError("Could not allocate HTTP headers");
         }
         raw_headers = appended;
      }
      std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_guard{raw_headers, &curl_slist_free_all};
      const std::string endpoint{url};
      const std::string payload{json};
      ChunkState state{{}, &chunk, {}, 0};
      curl_easy_setopt(handle.get(), CURLOPT_URL, endpoint.c_str());
      curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
      curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, payload.c_str());
      curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(payload.size()));
      curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, header_guard.get());
      curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_callback);
      curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &state);
      curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, &progress_callback);
      curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &stop);
      curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 120L);
      curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 20L);
      curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
      const auto result = curl_easy_perform(handle.get());
      if (state.exception)
         std::rethrow_exception(state.exception);
      if (stop.stop_requested())
         throw ChatCancelled{};
      if (result != CURLE_OK)
         throw LlmError("OpenAI HTTP transport failed");
      curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &state.response.status_code);
      return std::move(state.response);
   }

} // namespace CxxRelay
