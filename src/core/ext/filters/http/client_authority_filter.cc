//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/ext/filters/http/client_authority_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/filter/auth/auth_filters.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/util/latent_see.h"

namespace grpc_core {

absl::StatusOr<std::unique_ptr<ClientAuthorityFilter>>
ClientAuthorityFilter::Create(const ChannelArgs& args, ChannelFilter::Args) {
  std::optional<absl::string_view> default_authority =
      args.GetString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (!default_authority.has_value()) {
    return absl::InvalidArgumentError(
        "GRPC_ARG_DEFAULT_AUTHORITY string channel arg. not found. Note that "
        "direct channels must explicitly specify a value for this argument.");
  }
  return std::make_unique<ClientAuthorityFilter>(
      Slice::FromCopiedString(*default_authority));
}

void ClientAuthorityFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ClientAuthorityFilter* filter) {
  GRPC_LATENT_SEE_SCOPE("ClientAuthorityFilter::Call::OnClientInitialMetadata");
  // If no authority is set, set the default authority.
  if (md.get_pointer(HttpAuthorityMetadata()) == nullptr) {
    md.Set(HttpAuthorityMetadata(), filter->default_authority_.Ref());
  }
}

const grpc_channel_filter ClientAuthorityFilter::kFilter =
    MakePromiseBasedFilter<ClientAuthorityFilter, FilterEndpoint::kClient>();

namespace {
bool NeedsClientAuthorityFilter(const ChannelArgs& args) {
  return !args.GetBool(GRPC_ARG_DISABLE_CLIENT_AUTHORITY_FILTER)
              .value_or(false);
}
}  // namespace

void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()
      ->RegisterFilter<ClientAuthorityFilter>(GRPC_CLIENT_SUBCHANNEL)
      .If(NeedsClientAuthorityFilter)
      .Before<ClientAuthFilter>();
  builder->channel_init()
      ->RegisterFilter<ClientAuthorityFilter>(GRPC_CLIENT_DIRECT_CHANNEL)
      .If(NeedsClientAuthorityFilter)
      .Before<ClientAuthFilter>();
}

}  // namespace grpc_core
