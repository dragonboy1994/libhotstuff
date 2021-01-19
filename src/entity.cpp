/**
 * Copyright 2018 VMware
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hotstuff/entity.h"
#include "hotstuff/hotstuff.h"

namespace hotstuff {

void OrderedList::serialize(DataStream &s) const {
    HOTSTUFF_LOG_INFO("Stop 0");
    HOTSTUFF_LOG_PROTO("Date : %lu", cmds.size());
    s << htole((uint32_t)cmds.size());
    HOTSTUFF_LOG_INFO("Stop 1");
    for (const auto &cmd : cmds)
        s << cmd;
    HOTSTUFF_LOG_INFO("Stop 2");
    for (const auto &timestamp : timestamps)
        s << timestamp;
    HOTSTUFF_LOG_INFO("Stop 3");
}

void OrderedList::unserialize(DataStream &s, HotStuffCore *hsc) {
    HOTSTUFF_LOG_INFO("Orderedlist being deserialized 0");
    uint32_t n;
    s >> n;
    HOTSTUFF_LOG_INFO("Orderedlist being deserialized 1");
    cmds.resize(n);
    for (auto &cmd : cmds)
        s >> cmd;
    timestamps.resize(n);
    for (auto timestamp : timestamps)
        s >> timestamp;
}

void Block::serialize(DataStream &s) const {
    s << htole((uint32_t)parent_hashes.size());
    for (const auto &hash: parent_hashes)
        s << hash;
    s << htole((uint32_t)cmds.size());
    for (auto cmd: cmds)
        s << cmd;
    s << *qc << htole((uint32_t)extra.size()) << extra;
}

void Block::unserialize(DataStream &s, HotStuffCore *hsc) {
    uint32_t n;
    s >> n;
    n = letoh(n);
    parent_hashes.resize(n);
    for (auto &hash: parent_hashes)
        s >> hash;
    s >> n;
    n = letoh(n);
    cmds.resize(n);
    for (auto &cmd: cmds)
        s >> cmd;
//    for (auto &cmd: cmds)
//        cmd = hsc->parse_cmd(s);
    qc = hsc->parse_quorum_cert(s);
    s >> n;
    n = letoh(n);
    if (n == 0)
        extra.clear();
    else
    {
        auto base = s.get_data_inplace(n);
        extra = bytearray_t(base, base + n);
    }
    this->hash = salticidae::get_hash(*this);
}

bool Block::verify(const HotStuffCore *hsc) const {
    if (qc->get_obj_hash() == hsc->get_genesis()->get_hash())
        return true;
    return qc->verify(hsc->get_config());
}

promise_t Block::verify(const HotStuffCore *hsc, VeriPool &vpool) const {
    if (qc->get_obj_hash() == hsc->get_genesis()->get_hash())
        return promise_t([](promise_t &pm) { pm.resolve(true); });
    return qc->verify(hsc->get_config(), vpool);
}





void CommandTimestampStorage::add_command_to_storage(const uint256_t cmd_hash)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t timestamp_us = tv.tv_sec;
    timestamp_us *= 1000 * 1000;
    timestamp_us += tv.tv_usec;
    cmd_ts_storage.insert(std::make_pair(cmd_hash, timestamp_us));
    available_cmd_hashes.push_back(cmd_hash);
    available_timestamps.push_back(timestamp_us);
    cmd_hashes.push_back(cmd_hash);
    timestamps.push_back(timestamp_us);
}

/** return true if it is a new command */
bool CommandTimestampStorage::is_new_command(const uint256_t &cmd_hash) const
{
    if (std::find(cmd_hashes.begin(), cmd_hashes.end(), cmd_hash) == cmd_hashes.end())
    {
        return true;
    }
    else
    {
        return false;
    }
}

/** Updating the available cmds and timestamps on receiving acceptable proposals.
  * This must be called after ensuring acceptable_fairness_check() outputs True.
  * The input to this function will be block->cmds. */
void CommandTimestampStorage::refresh_available_cmds(const std::vector<uint256_t> cmds)
{
    for (auto& cmd : cmds)
    {
        auto it = std::find(available_cmd_hashes.begin(), available_cmd_hashes.end(), cmd);
        if (it != available_cmd_hashes.end())
        {
            auto index = std::distance(available_cmd_hashes.begin(), it);
            available_cmd_hashes.erase(available_cmd_hashes.begin() + index);
            available_timestamps.erase(available_timestamps.begin() + index);
        }
    }
}

/** Get a orderedlist on giving a vector of commands as input.
     * Called just before calling acceptable fairness check in order to get the 
     * corresponding timestamps of the commands in the proposed block.
     * Must be called after add_command_to_storage().*/
std::vector<uint64_t> CommandTimestampStorage::get_timestamps(const std::vector<uint256_t> &cmd_hashes_inquired) const
{
    std::vector<uint64_t> timestamps_list;
    for (auto& cmd_hash : cmd_hashes_inquired)
    {
        auto it = std::find(cmd_hashes.begin(), cmd_hashes.end(), cmd_hash);
        timestamps_list.push_back(timestamps[std::distance(cmd_hashes.begin(), it)]);
    }
    return timestamps_list;
}

/** Get a new ordered list to send as part of the vote.
  * Basically has to repackage the available cmds and timestamps into ordered list
*/
/*
const OrderedList get_orderedlist() const
{
    // should there be size requirements on how big the ordered list should be?
    while (available_cmd_hashes.size() < 3)
    {
        HOTSTUFF_LOG_PROTO("Waiting!");
        HOTSTUFF_LOG_PROTO("Number of available cmd hashes is: %d", available_cmd_hashes.size());
    }
    HOTSTUFF_LOG_PROTO("Number of available cmd hashes is: %d", available_cmd_hashes.size());

    std::vector<uint256_t> proposed_available_cmd_hashes;
    std::vector<uint64_t> proposed_available_timestamps;
    for (auto i = 0; i < 3; i++)
    {
        proposed_available_cmd_hashes.push_back(available_cmd_hashes[i]);
        proposed_available_timestamps.push_back(available_timestamps[i]);
        //HOTSTUFF_LOG_PROTO("cmd is: %s", get_hex10(available_cmd_hashes[i]).c_str());
        //HOTSTUFF_LOG_PROTO("cmd is: %s", boost::lexical_cast<std::string>(available_timestamps[i]).c_str());
    }

    // remove the above while not testing and update the following to send all available cmds and ts
    OrderedList replica_proposed_orderedlist = OrderedList(proposed_available_cmd_hashes, proposed_available_timestamps);
    HOTSTUFF_LOG_PROTO("Obtained Ordered list!");
    return replica_proposed_orderedlist;
}
*/
}