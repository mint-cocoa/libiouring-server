// examples/QuartoPlatform/src/k8s_client.h
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace quarto {

struct PodInfo {
    std::string name;
    std::string user_id;
    std::string ip;
    std::string phase;
};

class K8sClient {
public:
    K8sClient(const std::string& namespace_name,
              const std::string& editor_image,
              const std::string& editor_config);

    bool CreateUserPod(const std::string& user_id);
    bool DeleteUserPod(const std::string& user_id);
    std::optional<std::string> GetPodIp(const std::string& user_id);
    std::vector<PodInfo> ListEditorPods();
    bool EnsureUserPvc(const std::string& user_id);

private:
    std::string ApiCall(const std::string& method,
                        const std::string& path,
                        const std::string& body = "");
    std::string LoadToken();
    std::string PodManifest(const std::string& user_id);
    std::string PvcManifest(const std::string& user_id);

    std::string namespace_;
    std::string editor_image_;
    std::string editor_config_;
    std::string api_server_ = "https://kubernetes.default.svc";
    std::string ca_cert_ = "/var/run/secrets/kubernetes.io/serviceaccount/ca.crt";
    std::string token_;
};

} // namespace quarto
