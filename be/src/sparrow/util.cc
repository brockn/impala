// Copyright (c) 2012 Cloudera, Inc. All rights reserved.

#include "sparrow/util.h"

#include <utility>

#include <boost/foreach.hpp>

#include "gen-cpp/StateStoreSubscriberService_types.h"

using namespace boost;
using namespace std;
using impala::THostPort;

namespace sparrow {

const SubscriptionId INVALID_SUBSCRIPTION_ID = "";

bool ServiceState::operator==(const ServiceState& that) const {
  if (membership != that.membership) {
    return false;
  } else if (object_updates != that.object_updates) {
    return false;
  } else if (deleted_object_keys != that.deleted_object_keys) {
    return false;
  }
  return true;
}

void StateFromThrift(const TUpdateStateRequest& request, ServiceStateMap* state) {
  state->clear();
  BOOST_FOREACH(const TServiceMembership& membership, request.service_memberships) {
    pair<ServiceStateMap::iterator, bool> result =
        state->insert(make_pair(membership.service_id, ServiceState()));
    DCHECK(result.second);
    ServiceState& to_state = result.first->second;
    for (int instance_index = 0; instance_index < membership.service_instances.size();
         ++instance_index) {
      const TServiceInstance& instance = membership.service_instances[instance_index];
      to_state.membership.insert(make_pair(instance.subscriber_id, instance.host_port));
    }
  }
  // TODO: Copy object updates and deletions from the request as well.
}

void MembershipToThrift(const Membership& from_membership,
                        vector<TServiceInstance>* to_membership) {
  to_membership->clear();
  BOOST_FOREACH(const Membership::value_type& from_instance, from_membership) {
    TServiceInstance to_instance;
    to_instance.subscriber_id = from_instance.first;
    to_instance.host_port = from_instance.second;
    to_membership->push_back(to_instance);
  }
}


}
