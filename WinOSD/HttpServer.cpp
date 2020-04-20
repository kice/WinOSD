#define CPPHTTPLIB_ZLIB_SUPPORT

#include <httplib.h>
#include <Windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#include "HttpServer.h"
#include "logging.h"
#include "strings.h"
#include "base64.h"

#include <thread>

#include "ActionCenter.h"

#pragma comment(lib, "Winhttp.lib")

static httplib::Server *server;
static std::thread httpWorker;

extern ActionCenter actionCenter;

#define USER_AGENT L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/80.0.3987.163 Safari/537.36"

std::vector<uint8_t> HttpClient(const char *verb, const std::wstring &url)
{
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    std::vector<uint8_t> buffer;

    try {
        std::wstring hostname;
        std::wstring path;

        hostname.resize(MAX_PATH);
        path.resize(MAX_PATH * 5);

        URL_COMPONENTS urlComp;
        memset(&urlComp, 0, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.lpszHostName = hostname.data();
        urlComp.dwHostNameLength = (DWORD)hostname.size();
        urlComp.lpszUrlPath = path.data();
        urlComp.dwUrlPathLength = (DWORD)path.size();
        urlComp.dwSchemeLength = 1;

        if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &urlComp)) {
            throw std::runtime_error("WinHttpCrackUrl Failed");
        }

        // Use WinHttpOpen to obtain a session handle.
        hSession = WinHttpOpen(USER_AGENT,
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);

        if (!hSession) {
            throw std::runtime_error("WinHttpOpen failed");
        }

        WinHttpSetTimeouts(hSession, 0, 60000, 30000, 30000);

        // Specify an HTTP server.
        hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            throw std::runtime_error("WinHttpConnect failed");
        }

        auto wverb = std::atow(verb);

        // Create an HTTP request handle.
        DWORD dwOpenRequestFlag = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, wverb.c_str(), urlComp.lpszUrlPath,
                                      NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      dwOpenRequestFlag);
        if (!hRequest) {
            throw std::runtime_error("WinHttpOpenRequest failed");
        }

        // Send a request.
        if (!WinHttpSendRequest(hRequest,
                                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0,
                                0, 0)) {
            throw std::runtime_error("WinHttpSendRequest failed");
        }

        // End the request.
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            throw std::runtime_error("WinHttpReceiveResponse failed");
        }

        DWORD size = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, NULL,
                            &size, WINHTTP_NO_HEADER_INDEX);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0) {
            std::wstring headers;
            headers.resize(size);

            // Now, use WinHttpQueryHeaders to retrieve the header.
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    headers.data(), &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                buffer.resize(_wtoi(headers.c_str()));
            }
        }

        size_t ptr = 0;
        DWORD  downloaded = 0;
        do {
            size = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &size)) {
                throw std::runtime_error("WinHttpQueryDataAvailable failed");
            }

            if (size == 0) {
                break;
            }

            if (buffer.size() < ptr + size) {
                buffer.resize(ptr + size);
            }

            if (!WinHttpReadData(hRequest, buffer.data() + ptr, size, &downloaded)) {
                throw std::runtime_error("WinHttpReadData failed");
            }

            ptr += downloaded;
        } while (size > 0);
    } catch (const std::exception & ex) {
        DBG << "error: HttpClient: " << ex.what();
    }

    // Close any open handles.
    if (hRequest)
        WinHttpCloseHandle(hRequest);
    if (hConnect)
        WinHttpCloseHandle(hConnect);
    if (hSession)
        WinHttpCloseHandle(hSession);

    return buffer;
}

Image DownloadImage(const std::wstring &url)
{
    auto buffer = HttpClient("GET", url);
    return Image::open(buffer.data(), buffer.size());
}

static void HttpMain()
{
    using namespace httplib;
    using json = nlohmann::json;

    auto &r = *server;

    r.Get("/toast", [](const Request &req, Response &res) {
        if (!req.has_param("title") && !req.has_param("text")) {
            res.set_content(R"({"status": "error", "msg": "invalid parameters"})", "application/json");
            return;
        }

        Toast toast;

        toast.title = std::u8tow(req.get_param_value("title"));
        toast.text = std::u8tow(req.get_param_value("text"));
        if (req.has_param("imageurl")) {
            toast.image = DownloadImage(std::u8tow(req.get_param_value("imageurl")));
        }

        int toastId = actionCenter.AddToast(std::move(toast));
        if (toastId == -1) {
            res.set_content(R"({"status": "error", "msg": "unable to add toast"})", "application/json");
            return;
        }

        char json[256];
        sprintf(json, R"({"status": "ok", "id": "%d"})", toastId);
        res.set_content(json, "application/json");
          });

    r.Post("/toast", [&](const Request &req, Response &res, const ContentReader &content_reader) {
        std::string body;
        if (req.is_multipart_form_data()) {
            MultipartFormDataItems files;
            content_reader(
                [&](const MultipartFormData &file) {
                    files.push_back(file);
                    return true;
                },
                [&](const char *data, size_t data_length) {
                    files.back().content.append(data, data_length);
                    return true;
                });
            body = files[0].content;
        } else {
            content_reader([&](const char *data, size_t data_length) {
                body.append(data, data_length);
                return true;
            });
        }

        auto data = json::parse(body);

        auto title = std::encoding(data["title"].get<std::string>(), CP_THREAD_ACP, CP_UTF8);
        auto text = std::encoding(data["text"].get<std::string>(), CP_THREAD_ACP, CP_UTF8);
        auto encoded = data["image"].get<std::string>();

        Toast toast = Toast{
            0,
            std::u8tow(title),
            std::u8tow(text),
        };

        if (encoded.size()) {
            uint8_t *image = (uint8_t *)_aligned_malloc(base64::decoded_size(encoded.length()), 512);
            if (image == nullptr) {
                res.set_content(R"({"status": "error", "msg": "unable allocate memory"})", "application/json");
                return;
            }

            auto [written, read] = base64::decode(image, encoded.data(), encoded.size());
            if (read == 0) {
                res.set_content(R"({"status": "error", "msg": "unable to decode base64"})", "application/json");
                _aligned_free(image);
                return;
            }

            toast.image = Image::open(image, written);
            _aligned_free(image);

            if (toast.image) {
                res.set_content(R"({"status": "error", "msg": "unable to decode image"})", "application/json");
                return;
            }
        }

        int toastId = actionCenter.AddToast(std::move(toast));
        if (toastId == -1) {
            res.set_content(R"({"status": "error", "msg": "unable to add toast"})", "application/json");
            return;
        }

        char json[256];
        sprintf(json, R"({"status": "ok", "id": "%d"})", toastId);
        res.set_content(json, "application/json");
           });

    r.listen("localhost", 8520);
}

void StartHttpServer()
{
    DBG << "Starting HTTP Server...";

    httpWorker = std::thread([]() {
        server = new httplib::Server();
        HttpMain();
                             });
}

void StopHttpServer()
{
    if (!server) {
        return;
    }

    DBG << "Stopping HTTP Server...";

    server->stop();
    if (httpWorker.joinable()) {
        httpWorker.join();
    }

    DBG << "HTTP Server exited.";
    delete server;
}
