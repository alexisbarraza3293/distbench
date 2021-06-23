// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "distbench_utils.h"

#include "interface_lookup.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "glog/logging.h"

namespace std {
ostream& operator<< (ostream &out, grpc::Status const& c)
{
    return out << "(grpc::status" << c.error_message() << ")";
}
}

namespace distbench {

static bool use_ipv4_first = false;

void set_use_ipv4_first(bool _use_ipv4_first){
  use_ipv4_first = _use_ipv4_first;
}

std::string Hostname() {
  char hostname[4096] = {};
  if (gethostname(hostname, sizeof(hostname))) {
    LOG(ERROR) << errno;
  }
  return hostname;
}

std::shared_ptr<grpc::ChannelCredentials> MakeChannelCredentials() {
  // grpc::SslCredentialsOptions sec_ops;
  // return grpc::SslCredentials(sec_ops);
  return grpc::InsecureChannelCredentials();
}

std::shared_ptr<grpc::ServerCredentials> MakeServerCredentials() {
  // grpc::SslServerCredentialsOptions sec_ops;
  // return grpc::SslServerCredentials(sec_ops);
  return grpc::InsecureServerCredentials();
}

std::thread RunRegisteredThread(const std::string& thread_name,
                                std::function<void()> f) {
  return std::thread([=]() {
    f();
  });
}

void InitLibs(const char* argv0) {
  // Extra library initialization can go here
  ::google::InitGoogleLogging(argv0);

}

std::string IpAddressForDevice(std::string_view netdev) {
  net_base::IPAddress ip;
  CHECK(net_base::InterfaceLookup::MyIPv6Address(&ip) ||
        net_base::InterfaceLookup::MyIPv4Address(&ip));
  return ip.ToString();
}

std::string SocketAddressForDevice(std::string_view netdev, int port) {
  net_base::IPAddress ip;

  if (use_ipv4_first &&
      net_base::InterfaceLookup::MyIPv4Address(&ip))
    return absl::StrCat(ip.ToString(), ":", port);

  if (net_base::InterfaceLookup::MyIPv6Address(&ip))
    return absl::StrCat("[", ip.ToString(), "]:", port);

  if (net_base::InterfaceLookup::MyIPv4Address(&ip))
    return absl::StrCat(ip.ToString(), ":", port);

  LOG(FATAL) << "Could not get ip v4/v6 address";
  exit(1);
}

std::string ServiceInstanceName(std::string_view service_type, int instance) {
  CHECK(!service_type.empty());
  CHECK_GE(instance, 0);
  return absl::StrCat(service_type, "/", instance);
}

std::map<std::string, int> EnumerateServiceTypes(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    // LOG(INFO) << "service " << service.name() << " = " << ret.size();
    ret[service.name()] = ret.size();
  }
  return ret;
}

std::map<std::string, int> EnumerateServiceSizes(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    // LOG(INFO) << "service " << service.name() << " = " << ret.size();
    ret[service.name()] = service.count();
  }
  return ret;
}

std::map<std::string, int> EnumerateRpcs(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& rpc : config.rpc_descriptions()) {
    ret[rpc.name()] = ret.size();
  }
  return ret;
}

std::map<std::string, int> EnumerateServiceInstanceIds(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    for (int i = 0; i < service.count(); ++i) {
      std::string instance = ServiceInstanceName(service.name(), i);
      // LOG(INFO) << "service " << instance << " = " << ret.size();
      ret[instance] = ret.size();
    }
  }
  return ret;
}

ServiceSpec GetServiceSpec(std::string_view name,
                           const DistributedSystemDescription& config) {
  for (const auto& service : config.services()) {
    if (service.name() == name) {
      return service;
    }
  }
  LOG(FATAL) << "Service not found: " << name;
  exit(1);
}

namespace {

std::string LatencySummary(std::vector<int64_t> latencies) {
  std::string ret;
  CHECK(!latencies.empty());
  size_t N =  latencies.size();
  absl::StrAppendFormat(&ret, "N: %ld", N);
  absl::StrAppendFormat(&ret, " min: %ldns", *latencies.begin());
  absl::StrAppendFormat(&ret, " median: %ldns", latencies[N * 0.5]);
  absl::StrAppendFormat(&ret, " 90%%: %ldns", latencies[N * 0.9]);
  absl::StrAppendFormat(&ret, " 99%%: %ldns", latencies[N * 0.99]);
  absl::StrAppendFormat(&ret, " 99.9%%: %ldns", latencies[N * 0.999]);
  absl::StrAppendFormat(&ret, " max: %ldns", *latencies.rbegin());
  return ret;
}

}  // anonymous namespace

std::string SummarizeTestResult(const TestResult& test_result) {
  std::string ret = "RPC latency summary:\n";
  std::map<std::string, std::vector<int64_t>> latency_map;
  for (const auto& instance_log : test_result.service_logs().instance_logs()) {
    for (const auto& peer_log : instance_log.second.peer_logs()) {
      for (const auto& rpc_log : peer_log.second.rpc_logs()) {
        std::string rpc_name  =
          test_result.traffic_config().rpc_descriptions(rpc_log.first).name();
        std::vector<int64_t>& latencies = latency_map[rpc_name];
        for (const auto& sample : rpc_log.second.successful_rpc_samples()) {
          latencies.push_back(sample.latency_ns());
        }
      }
    }
  }

  for (auto& latencies : latency_map) {
    std::sort(latencies.second.begin(), latencies.second.end());
    absl::StrAppendFormat(
        &ret, "%s: %s\n", latencies.first, LatencySummary(latencies.second));
  }

  return ret;
}

grpc::Status Annotate(const grpc::Status& status, std::string_view context) {
  return grpc::Status(
      status.error_code(), absl::StrCat(context, status.error_message()));
}

grpc::Status abslStatusToGrpcStatus(const absl::Status &status){
  if (status.ok())
    return grpc::Status::OK;

  std::string message = std::string(status.message());
  // GRPC and ABSL (currently) share the same error codes
  grpc::StatusCode code = (grpc::StatusCode)status.code();
  return grpc::Status(code, message);
}

absl::Status grpcStatusToAbslStatus(const grpc::Status &status){
  if (status.ok())
    return absl::OkStatus();

  std::string message = status.error_message();
  // GRPC and ABSL (currently) share the same error codes
  absl::StatusCode code = (absl::StatusCode)status.error_code();
  return absl::Status(code, message);
}

}  // namespace distbench
