/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/commands/cvd/parser/fetch_config_parser.h"

#include <string>
#include <string_view>
#include <vector>

#include <android-base/strings.h>
#include "json/json.h"

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"
#include "host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/web/android_build_api.h"

namespace cuttlefish {

using cvd::config::Fetch;
using cvd::config::Instance;
using cvd::config::Launch;

namespace {

constexpr std::string_view kFetchPrefix = "@ab/";

Result<void> InitFetchInstanceConfigs(Json::Value& instance) {
  CF_EXPECT(
      InitConfig(instance, kDefaultBuildString, {"disk", "default_build"}));
  CF_EXPECT(
      InitConfig(instance, kDefaultBuildString, {"disk", "super", "system"}));
  CF_EXPECT(
      InitConfig(instance, kDefaultBuildString, {"boot", "kernel", "build"}));
  CF_EXPECT(InitConfig(instance, kDefaultBuildString, {"boot", "build"}));
  CF_EXPECT(InitConfig(instance, kDefaultBuildString,
                       {"boot", "bootloader", "build"}));
  CF_EXPECT(InitConfig(instance, kDefaultBuildString, {"disk", "otatools"}));
  CF_EXPECT(InitConfig(instance, kDefaultDownloadImgZip,
                       {"disk", "download_img_zip"}));
  CF_EXPECT(InitConfig(instance, kDefaultDownloadTargetFilesZip,
                       {"disk", "download_target_files_zip"}));
  return {};
}

Result<void> InitFetchCvdConfigs(Json::Value& root) {
  CF_EXPECT(InitConfig(root, kDefaultApiKey, {"fetch", "api_key"}));
  CF_EXPECT(InitConfig(root, kDefaultCredentialSource,
                       {"fetch", "credential_source"}));
  CF_EXPECT(InitConfig(root, static_cast<int>(kDefaultWaitRetryPeriod.count()),
                       {"fetch", "wait_retry_period"}));
  CF_EXPECT(InitConfig(root, kDefaultExternalDnsResolver,
                       {"fetch", "external_dns_resolver"}));
  CF_EXPECT(InitConfig(root, kDefaultKeepDownloadedArchives,
                       {"fetch", "keep_downloaded_archives"}));
  CF_EXPECT(
      InitConfig(root, kAndroidBuildServiceUrl, {"fetch", "api_base_url"}));
  CF_EXPECT(InitConfig(root, kDefaultBuildString, {"common", "host_package"}));
  for (auto& instance : root["instances"]) {
    CF_EXPECT(InitFetchInstanceConfigs(instance));
  }
  return {};
}

bool ShouldFetch(const Instance& instance) {
  const auto& boot = instance.boot();
  const auto& disk = instance.disk();

  for (const auto& value :
       {disk.default_build(), disk.super().system(), boot.kernel().build(),
        boot.kernel().build(), boot.build(), boot.bootloader().build(),
        disk.otatools()}) {
    // expects non-prefixed build strings already converted to empty strings
    if (!value.empty()) {
      return true;
    }
  }
  return false;
}

Result<std::string> GetFetchBuildString(const Json::Value& value) {
  std::string strVal = value.asString();
  std::string_view view = strVal;
  if (!android::base::ConsumePrefix(&view, kFetchPrefix)) {
    // intentionally return an empty string when there are local, non-prefixed
    // paths.  Fetch does not process the local paths
    return "";
  }
  CF_EXPECTF(!view.empty(),
             "\"{}\" prefixed build string was not followed by a value",
             kFetchPrefix);
  return std::string(view);
}

Result<Instance> RemoveNonPrefixedBuildStrings(const Instance& instance) {
  Instance result = instance;

  auto& disk = *result.mutable_disk();
  disk.set_default_build(CF_EXPECT(GetFetchBuildString(disk.default_build())));
  disk.set_otatools(CF_EXPECT(GetFetchBuildString(disk.otatools())));

  auto& system = *disk.mutable_super()->mutable_system();
  system = CF_EXPECT(GetFetchBuildString(system));

  auto& boot = *result.mutable_boot();
  boot.set_build(CF_EXPECT(GetFetchBuildString(boot.build())));

  auto& kernel = *boot.mutable_kernel()->mutable_build();
  kernel = CF_EXPECT(GetFetchBuildString(kernel));

  auto& bootloader = *boot.mutable_bootloader()->mutable_build();
  bootloader = CF_EXPECT(GetFetchBuildString(bootloader));

  return result;
}

static std::string DefaultBuild(const Instance& instance) {
  return instance.disk().default_build();
}

static std::string SystemBuild(const Instance& instance) {
  return instance.disk().super().system();
}

static std::string KernelBuild(const Instance& instance) {
  return instance.boot().kernel().build();
}

static std::string BootBuild(const Instance& instance) {
  return instance.boot().build();
}

static std::string BootloaderBuild(const Instance& instance) {
  return instance.boot().bootloader().build();
}

static std::string OtaToolsBuild(const Instance& instance) {
  return instance.disk().otatools();
}

static bool DownloadImgZip(const Instance& instance) {
  return instance.disk().download_img_zip();
}

static bool DownloadTargetFilesZip(const Instance& instance) {
  return instance.disk().download_target_files_zip();
}

Result<std::vector<std::string>> GenerateFetchFlags(
    const Json::Value& root, const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  Launch config;
  CF_EXPECT(Validate(root, config));

  Launch fetch_instances;
  std::vector<std::string> fetch_subdirectories;
  CF_EXPECT_EQ(config.instances().size(), (int) target_subdirectories.size(),
               "Mismatched sizes between number of subdirectories and number "
               "of instances");
  for (int i = 0; i < config.instances().size(); i++) {
    const auto prefix_filtered =
        CF_EXPECT(RemoveNonPrefixedBuildStrings(config.instances()[i]));
    if (ShouldFetch(prefix_filtered)) {
      fetch_instances.add_instances()->CopyFrom(prefix_filtered);
      fetch_subdirectories.emplace_back(target_subdirectories[i]);
    }
  }

  const std::string host_package_build =
      CF_EXPECT(GetFetchBuildString(config.common().host_package()));
  if (fetch_subdirectories.empty() && host_package_build.empty()) {
    return {};
  }

  std::vector<std::string> result;
  const Fetch& fetch_config = config.fetch();
  result.emplace_back(GenerateFlag("target_directory", target_directory));
  if (fetch_config.has_api_key()) {
    result.emplace_back(GenerateFlag("api_key", fetch_config.api_key()));
  }
  if (fetch_config.has_credential_source()) {
    auto value = fetch_config.credential_source();
    result.emplace_back(GenerateFlag("credential_source", std::move(value)));
  }
  if (fetch_config.has_wait_retry_period()) {
    auto value = fetch_config.wait_retry_period();
    result.emplace_back(GenerateFlag("wait_retry_period", std::move(value)));
  }
  if (fetch_config.has_external_dns_resolver()) {
    auto value = fetch_config.external_dns_resolver();
    result.emplace_back(GenerateFlag("external_dns_resolver", std::move(value)));
  }
  if (fetch_config.has_keep_downloaded_archives()) {
    auto value = fetch_config.keep_downloaded_archives();
    result.emplace_back(GenerateFlag("keep_downloaded_archives", std::move(value)));
  }
  if (fetch_config.has_api_base_url()) {
    auto value = fetch_config.api_base_url();
    result.emplace_back(GenerateFlag("api_base_url", std::move(value)));
  }
  result.emplace_back(GenerateFlag("host_package_build", host_package_build));

  result.emplace_back(
      GenerateVecFlag("target_subdirectory", fetch_subdirectories));
  result.emplace_back(
      GenerateInstanceFlag("default_build", fetch_instances, DefaultBuild));
  result.emplace_back(
      GenerateInstanceFlag("system_build", fetch_instances, SystemBuild));
  result.emplace_back(
      GenerateInstanceFlag("kernel_build", fetch_instances, KernelBuild));
  result.emplace_back(
      GenerateInstanceFlag("boot_build", fetch_instances, BootBuild));
  result.emplace_back(GenerateInstanceFlag("bootloader_build", fetch_instances,
                                           BootloaderBuild));
  // TODO: schuffelen - should android_efi_loader_build come from a separate
  // setting?
  result.emplace_back(GenerateInstanceFlag("android_efi_loader_build",
                                           fetch_instances, BootloaderBuild));
  result.emplace_back(
      GenerateInstanceFlag("otatools_build", fetch_instances, OtaToolsBuild));
  result.emplace_back(GenerateInstanceFlag("download_img_zip", fetch_instances,
                                           DownloadImgZip));
  result.emplace_back(GenerateInstanceFlag(
      "download_target_files_zip", fetch_instances, DownloadTargetFilesZip));

  return result;
}

}  // namespace

Result<std::vector<std::string>> ParseFetchCvdConfigs(
    Json::Value& root, const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  CF_EXPECT(InitFetchCvdConfigs(root));
  return CF_EXPECT(
      GenerateFetchFlags(root, target_directory, target_subdirectories));
}

}  // namespace cuttlefish
