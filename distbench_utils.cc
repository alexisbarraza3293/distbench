// Copyright 2023 Google LLC
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

#include <fcntl.h>
#include <sys/resource.h>

#include <cerrno>
#include <fstream>
#include <streambuf>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"
#include "interface_lookup.h"

namespace std {
ostream& operator<<(ostream& out, grpc::Status const& c) {
  return out << "(grpc::status: " << c.error_message() << ")";
}
}  // namespace std

namespace distbench {

std::string Hostname() {
  char hostname[4096] = {};
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    LOG(ERROR) << errno;
  }
  return hostname;
}

grpc::ChannelArguments DistbenchCustomChannelArguments() {
  grpc::ChannelArguments args;
  args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
              std::numeric_limits<int32_t>::max());
  return args;
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

void InitLibs(const char* argv0) {
  // Extra library initialization can go here
  ::google::InitGoogleLogging(argv0);
  GOOGLE_PROTOBUF_VERIFY_VERSION;
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

absl::StatusOr<ServiceSpec> GetServiceSpec(
    std::string_view name, const DistributedSystemDescription& config) {
  for (const auto& service : config.services()) {
    if (service.name() == name) {
      return service;
    }
  }
  return absl::NotFoundError(absl::StrCat("Service '", name, "' not found"));
}

grpc::Status Annotate(const grpc::Status& status, std::string_view context) {
  return grpc::Status(status.error_code(),
                      absl::StrCat(context, status.error_message()));
}

grpc::Status abslStatusToGrpcStatus(const absl::Status& status) {
  if (status.ok()) return grpc::Status::OK;

  std::string message = std::string(status.message());
  // GRPC and ABSL (currently) share the same error codes
  grpc::StatusCode code = (grpc::StatusCode)status.code();
  return grpc::Status(code, message);
}

absl::Status grpcStatusToAbslStatus(const grpc::Status& status) {
  if (status.ok()) return absl::OkStatus();

  std::string message = status.error_message();
  // GRPC and ABSL (currently) share the same error codes
  absl::StatusCode code = (absl::StatusCode)status.error_code();
  return absl::Status(code, message);
}

void SetGrpcClientContextDeadline(grpc::ClientContext* context,
                                  int max_time_s) {
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(max_time_s);
  context->set_deadline(deadline);
}

absl::StatusOr<std::string> ReadFileToString(const std::string& filename) {
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (!in) {
    std::string error_message{"Error reading input file:" + filename + "; "};
    error_message += std::strerror(errno);
    return absl::InvalidArgumentError(error_message);
  }

  std::istreambuf_iterator<char> it(in);
  std::istreambuf_iterator<char> end;
  std::string str(it, end);

  in.close();
  return str;
}

void ApplyServerSettingsToGrpcBuilder(grpc::ServerBuilder* builder,
                                      const ProtocolDriverOptions& pd_opts) {
  for (const auto& setting : pd_opts.server_settings()) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto& name = setting.name();
    if (setting.has_string_value()) {
      builder->AddChannelArgument(name, setting.string_value());
      continue;
    }
    if (setting.has_int64_value()) {
      builder->AddChannelArgument(name, setting.int64_value());
      continue;
    }

    LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name << "]"
               << " no setting found (str or int)!";
  }
}

// RUsage functions
namespace {
double TimevalToDouble(const struct timeval& t) {
  return (double)t.tv_usec / 1'000'000.0 + t.tv_sec;
}
}  // Anonymous namespace

RUsage StructRUsageToMessage(const struct rusage& s_rusage) {
  RUsage rusage;

  rusage.set_user_cpu_time_seconds(TimevalToDouble(s_rusage.ru_utime));
  rusage.set_system_cpu_time_seconds(TimevalToDouble(s_rusage.ru_stime));
  rusage.set_max_resident_set_size(s_rusage.ru_maxrss);
  rusage.set_integral_shared_memory_size(s_rusage.ru_ixrss);
  rusage.set_integral_unshared_data_size(s_rusage.ru_idrss);
  rusage.set_integral_unshared_stack_size(s_rusage.ru_isrss);
  rusage.set_page_reclaims_soft_page_faults(s_rusage.ru_minflt);
  rusage.set_page_faults_hard_page_faults(s_rusage.ru_majflt);
  rusage.set_swaps(s_rusage.ru_nswap);
  rusage.set_block_input_operations(s_rusage.ru_inblock);
  rusage.set_block_output_operations(s_rusage.ru_oublock);
  rusage.set_ipc_messages_sent(s_rusage.ru_msgsnd);
  rusage.set_ipc_messages_received(s_rusage.ru_msgrcv);
  rusage.set_signals_received(s_rusage.ru_nsignals);
  rusage.set_voluntary_context_switches(s_rusage.ru_nvcsw);
  rusage.set_involuntary_context_switches(s_rusage.ru_nivcsw);

  return rusage;
}

RUsage DiffStructRUsageToMessage(const struct rusage& start,
                                 const struct rusage& end) {
  RUsage rusage;

  rusage.set_user_cpu_time_seconds(TimevalToDouble(end.ru_utime) -
                                   TimevalToDouble(start.ru_utime));
  rusage.set_system_cpu_time_seconds(TimevalToDouble(end.ru_stime) -
                                     TimevalToDouble(start.ru_stime));
  rusage.set_max_resident_set_size(end.ru_maxrss - start.ru_maxrss);
  rusage.set_integral_shared_memory_size(end.ru_ixrss - start.ru_ixrss);
  rusage.set_integral_unshared_data_size(end.ru_idrss - start.ru_idrss);
  rusage.set_integral_unshared_stack_size(end.ru_isrss - start.ru_isrss);
  rusage.set_page_reclaims_soft_page_faults(end.ru_minflt - start.ru_minflt);
  rusage.set_page_faults_hard_page_faults(end.ru_majflt - start.ru_majflt);
  rusage.set_swaps(end.ru_nswap - start.ru_nswap);
  rusage.set_block_input_operations(end.ru_inblock - start.ru_inblock);
  rusage.set_block_output_operations(end.ru_oublock - start.ru_oublock);
  rusage.set_ipc_messages_sent(end.ru_msgsnd - start.ru_msgsnd);
  rusage.set_ipc_messages_received(end.ru_msgrcv - start.ru_msgrcv);
  rusage.set_signals_received(end.ru_nsignals - start.ru_nsignals);
  rusage.set_voluntary_context_switches(end.ru_nvcsw - start.ru_nvcsw);
  rusage.set_involuntary_context_switches(end.ru_nivcsw - start.ru_nivcsw);

  return rusage;
}

RUsageStats GetRUsageStatsFromStructs(const struct rusage& start,
                                      const struct rusage& end) {
  RUsage* rusage_start = new RUsage();
  RUsage* rusage_diff = new RUsage();
  *rusage_start = StructRUsageToMessage(start);
  *rusage_diff = DiffStructRUsageToMessage(start, end);
  RUsageStats rusage_stats;
  rusage_stats.set_allocated_rusage_start(rusage_start);
  rusage_stats.set_allocated_rusage_diff(rusage_diff);
  return rusage_stats;
}

struct rusage DoGetRusage() {
  struct rusage rusage;
  int ret = getrusage(RUSAGE_SELF, &rusage);
  if (ret != 0) {
    LOG(WARNING) << "getrusage failed !";
  }
  return rusage;
}

std::string GetNamedSettingString(
    const ::google::protobuf::RepeatedPtrField<distbench::NamedSetting>&
        settings,
    absl::string_view setting_name, std::string default_value) {
  for (const auto& setting : settings) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto& name = setting.name();
    if (name != setting_name) continue;
    if (setting.has_int64_value()) {
      LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name
                 << "] should be a string !";
      continue;
    }
    if (setting.has_string_value()) {
      return setting.string_value();
    }
  }

  return default_value;
}

std::string GetNamedServerSettingString(
    const distbench::ProtocolDriverOptions& opts, absl::string_view name,
    std::string default_value) {
  return GetNamedSettingString(opts.server_settings(), name, default_value);
}

std::string GetNamedClientSettingString(
    const distbench::ProtocolDriverOptions& opts, absl::string_view name,
    std::string default_value) {
  return GetNamedSettingString(opts.client_settings(), name, default_value);
}

int64_t GetNamedSettingInt64(
    const ::google::protobuf::RepeatedPtrField<distbench::NamedSetting>&
        settings,
    absl::string_view setting_name, int64_t default_value) {
  for (const auto& setting : settings) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto& name = setting.name();
    if (name != setting_name) continue;
    if (setting.has_string_value()) {
      LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name
                 << "] should be an int !";
      continue;
    }
    if (setting.has_int64_value()) {
      return setting.int64_value();
    }
  }

  return default_value;
}

int64_t GetNamedServerSettingInt64(const distbench::ProtocolDriverOptions& opts,
                                   absl::string_view name,
                                   int64_t default_value) {
  return GetNamedSettingInt64(opts.server_settings(), name, default_value);
}

int64_t GetNamedClientSettingInt64(const distbench::ProtocolDriverOptions& opts,
                                   absl::string_view name,
                                   int64_t default_value) {
  return GetNamedSettingInt64(opts.client_settings(), name, default_value);
}

absl::StatusOr<int64_t> GetNamedAttributeInt64(
    const distbench::DistributedSystemDescription& test, absl::string_view name,
    int64_t default_value) {
  auto attributes = test.attributes();
  auto it = attributes.find(name);
  if (it == attributes.end()) {
    return default_value;
  }
  int64_t value;
  bool success = absl::SimpleAtoi(it->second, &value);
  if (success) {
    return value;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot convert test attribute ", name, " value (",
                     it->second, ") to int."));
  }
}

// Parse/Read TestSequence protos.
absl::StatusOr<TestSequence> ParseTestSequenceTextProto(
    const std::string& text_proto) {
  TestSequence test_sequence;
  if (::google::protobuf::TextFormat::ParseFromString(text_proto,
                                                      &test_sequence)) {
    return test_sequence;
  }
  return absl::InvalidArgumentError("Error parsing the TestSequence proto");
}

absl::StatusOr<TestSequence> ParseTestSequenceProtoFromFile(
    const std::string& filename) {
  absl::StatusOr<std::string> proto_string = ReadFileToString(filename);
  if (!proto_string.ok()) return proto_string.status();

  // Attempt to parse, assuming it is binary.
  TestSequence test_sequence;
  if (test_sequence.ParseFromString(*proto_string)) return test_sequence;

  // Attempt to parse, assuming it is text.
  auto result = ParseTestSequenceTextProto(*proto_string);
  if (result.ok()) return result;

  return absl::InvalidArgumentError(
      "Error parsing the TestSequence proto file (both in binary and text "
      "modes");
}

// Write TestSequenceResults protos.
absl::Status SaveResultProtoToFile(
    const std::string& filename, const distbench::TestSequenceResults& result) {
  int fd_proto = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_proto < 0) {
    std::string error_message{
        "Error opening the output result proto file for writing: "};
    return absl::InvalidArgumentError(error_message + filename);
  }

  ::google::protobuf::io::FileOutputStream fos_resultproto(fd_proto);
  if (!::google::protobuf::TextFormat::Print(result, &fos_resultproto)) {
    return absl::InvalidArgumentError("Error writing the result proto file");
  }

  return absl::OkStatus();
}

absl::Status SaveResultProtoToFileBinary(
    const std::string& filename, const distbench::TestSequenceResults& result) {
  std::fstream output(filename,
                      std::ios::out | std::ios::trunc | std::ios::binary);
  if (!result.SerializeToOstream(&output)) {
    return absl::InvalidArgumentError(
        "Error writing the result proto file in binary mode");
  }

  return absl::OkStatus();
}

void AddServerInt64OptionTo(ProtocolDriverOptions& pdo, std::string option_name,
                            int64_t value) {
  auto* ns = pdo.add_server_settings();
  ns->set_name(option_name);
  ns->set_int64_value(value);
}

void AddServerStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value) {
  auto* ns = pdo.add_server_settings();
  ns->set_name(option_name);
  ns->set_string_value(value);
}

void AddClientStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value) {
  auto* ns = pdo.add_client_settings();
  ns->set_name(option_name);
  ns->set_string_value(value);
}

void AddActivitySettingIntTo(ActivityConfig* ac, std::string option_name,
                             int value) {
  auto* ns = ac->add_activity_settings();
  ns->set_name(option_name);
  ns->set_int64_value(value);
}

void AddActivitySettingStringTo(ActivityConfig* ac, std::string option_name,
                                std::string value) {
  auto* ns = ac->add_activity_settings();
  ns->set_name(option_name);
  ns->set_string_value(value);
}

}  // namespace distbench
