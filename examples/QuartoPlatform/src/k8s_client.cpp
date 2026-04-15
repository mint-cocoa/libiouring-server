// examples/QuartoPlatform/src/k8s_client.cpp
#include "k8s_client.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

#if HAS_CURL
#include <curl/curl.h>
#endif

namespace quarto {

#if HAS_CURL
static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}
#endif

K8sClient::K8sClient(const std::string& namespace_name,
                     const std::string& editor_image,
                     const std::string& editor_config)
    : namespace_(namespace_name)
    , editor_image_(editor_image)
    , editor_config_(editor_config)
    , token_(LoadToken()) {}

std::string K8sClient::LoadToken() {
    std::ifstream f("/var/run/secrets/kubernetes.io/serviceaccount/token");
    if (!f.is_open()) {
        spdlog::warn("[K8s] ServiceAccount token not found (not in cluster?)");
        return "";
    }
    std::string token;
    std::getline(f, token);
    return token;
}

std::string K8sClient::ApiCall(const std::string& method,
                                const std::string& path,
                                const std::string& body) {
#if HAS_CURL
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string url = api_server_ + path;
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + token_).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        spdlog::error("[K8s] {} {} failed: {}", method, path, curl_easy_strerror(res));
        return "";
    }
    return response;
#else
    spdlog::warn("[K8s] ApiCall not implemented (libcurl not available): {} {}", method, path);
    return "";
#endif
}

std::string K8sClient::PodManifest(const std::string& user_id) {
    nlohmann::json manifest = {
        {"apiVersion", "v1"},
        {"kind", "Pod"},
        {"metadata", {
            {"name", "editor-" + user_id},
            {"namespace", namespace_},
            {"labels", {{"app", "editor"}, {"user", user_id}}}
        }},
        {"spec", {
            {"containers", {{
                {"name", "editor"},
                {"image", editor_image_},
                {"env", {
                    {{"name", "USER_ID"}, {"value", user_id}},
                    {{"name", "WORKSPACE"}, {"value", "/workspace"}},
                    {{"name", "PUBLISHED"}, {"value", "/published"}}
                }},
                {"ports", {
                    {{"containerPort", 8080}},
                    {{"containerPort", 4000}}
                }},
                {"resources", {
                    {"requests", {{"cpu", "200m"}, {"memory", "256Mi"}}},
                    {"limits", {{"cpu", "500m"}, {"memory", "512Mi"}}}
                }},
                {"volumeMounts", {
                    {{"name", "workspace"}, {"mountPath", "/workspace"}},
                    {{"name", "published"}, {"mountPath", "/published"}}
                }},
                {"readinessProbe", {
                    {"httpGet", {{"path", "/health"}, {"port", 8080}}},
                    {"initialDelaySeconds", 10},
                    {"periodSeconds", 5}
                }}
            }}},
            {"volumes", {
                {{"name", "workspace"}, {"persistentVolumeClaim", {{"claimName", "ws-" + user_id}}}},
                {{"name", "published"}, {"persistentVolumeClaim", {{"claimName", "published-shared"}}}}
            }}
        }}
    };
    return manifest.dump();
}

std::string K8sClient::PvcManifest(const std::string& user_id) {
    nlohmann::json manifest = {
        {"apiVersion", "v1"},
        {"kind", "PersistentVolumeClaim"},
        {"metadata", {
            {"name", "ws-" + user_id},
            {"namespace", namespace_}
        }},
        {"spec", {
            {"accessModes", {"ReadWriteOnce"}},
            {"resources", {{"requests", {{"storage", "1Gi"}}}}}
        }}
    };
    return manifest.dump();
}

bool K8sClient::CreateUserPod(const std::string& user_id) {
    // Check if pod already exists
    auto check = ApiCall("GET",
        "/api/v1/namespaces/" + namespace_ + "/pods/editor-" + user_id);
    if (!check.empty()) {
        auto json = nlohmann::json::parse(check, nullptr, false);
        if (!json.is_discarded() && json.contains("metadata")) {
            spdlog::info("[K8s] Pod editor-{} already exists", user_id);
            return true;
        }
    }

    EnsureUserPvc(user_id);

    auto resp = ApiCall("POST",
        "/api/v1/namespaces/" + namespace_ + "/pods",
        PodManifest(user_id));

    auto json = nlohmann::json::parse(resp, nullptr, false);
    if (json.is_discarded()) return false;
    bool ok = json.contains("metadata");
    if (ok) spdlog::info("[K8s] Created pod editor-{}", user_id);
    return ok;
}

bool K8sClient::DeleteUserPod(const std::string& user_id) {
    auto resp = ApiCall("DELETE",
        "/api/v1/namespaces/" + namespace_ + "/pods/editor-" + user_id);
    spdlog::info("[K8s] Deleted pod editor-{}", user_id);
    return !resp.empty();
}

std::optional<std::string> K8sClient::GetPodIp(const std::string& user_id) {
    auto resp = ApiCall("GET",
        "/api/v1/namespaces/" + namespace_ + "/pods/editor-" + user_id);
    if (resp.empty()) return std::nullopt;

    auto json = nlohmann::json::parse(resp, nullptr, false);
    if (json.is_discarded()) return std::nullopt;
    if (json.contains("status") && json["status"].contains("podIP")) {
        auto ip = json["status"]["podIP"].get<std::string>();
        return ip.empty() ? std::nullopt : std::optional(ip);
    }
    return std::nullopt;
}

std::vector<PodInfo> K8sClient::ListEditorPods() {
    auto resp = ApiCall("GET",
        "/api/v1/namespaces/" + namespace_ + "/pods?labelSelector=app=editor");
    if (resp.empty()) return {};

    auto json = nlohmann::json::parse(resp, nullptr, false);
    if (json.is_discarded() || !json.contains("items")) return {};

    std::vector<PodInfo> pods;
    for (const auto& item : json["items"]) {
        PodInfo info;
        info.name = item["metadata"]["name"].get<std::string>();
        info.user_id = item["metadata"]["labels"].value("user", "");
        info.ip = item["status"].value("podIP", "");
        info.phase = item["status"].value("phase", "");
        pods.push_back(std::move(info));
    }
    return pods;
}

bool K8sClient::EnsureUserPvc(const std::string& user_id) {
    auto check = ApiCall("GET",
        "/api/v1/namespaces/" + namespace_ + "/persistentvolumeclaims/ws-" + user_id);
    if (!check.empty()) {
        auto json = nlohmann::json::parse(check, nullptr, false);
        if (!json.is_discarded() && json.contains("metadata")) return true;
    }

    auto resp = ApiCall("POST",
        "/api/v1/namespaces/" + namespace_ + "/persistentvolumeclaims",
        PvcManifest(user_id));
    return !resp.empty();
}

} // namespace quarto
