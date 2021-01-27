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

#ifndef _HOTSTUFF_ENT_H
#define _HOTSTUFF_ENT_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstddef>
#include <ios>
#include <sstream>
#include <sys/time.h>
#include <algorithm>

#include "salticidae/netaddr.h"
#include "salticidae/ref.h"
#include "hotstuff/type.h"
#include "hotstuff/util.h"
#include "hotstuff/crypto.h"


namespace hotstuff {

enum EntityType {
    ENT_TYPE_CMD = 0x0,
    ENT_TYPE_BLK = 0x1
};

struct ReplicaInfo {
    ReplicaID id;
    salticidae::PeerId peer_id;
    pubkey_bt pubkey;

    ReplicaInfo(ReplicaID id,
                const salticidae::PeerId &peer_id,
                pubkey_bt &&pubkey):
        id(id), peer_id(peer_id), pubkey(std::move(pubkey)) {}

    ReplicaInfo(const ReplicaInfo &other):
        id(other.id), peer_id(other.peer_id),
        pubkey(other.pubkey->clone()) {}

    ReplicaInfo(ReplicaInfo &&other):
        id(other.id), peer_id(other.peer_id),
        pubkey(std::move(other.pubkey)) {}
};

class ReplicaConfig {
    std::unordered_map<ReplicaID, ReplicaInfo> replica_map;

    public:
    size_t nreplicas;
    size_t nmajority;

    ReplicaConfig(): nreplicas(0), nmajority(0) {}

    void add_replica(ReplicaID rid, const ReplicaInfo &info) {
        replica_map.insert(std::make_pair(rid, info));
        nreplicas++;
    }

    const ReplicaInfo &get_info(ReplicaID rid) const {
        auto it = replica_map.find(rid);
        if (it == replica_map.end())
            throw HotStuffError("rid %s not found",
                    get_hex(rid).c_str());
        return it->second;
    }

    const PubKey &get_pubkey(ReplicaID rid) const {
        return *(get_info(rid).pubkey);
    }

    const salticidae::PeerId &get_peer_id(ReplicaID rid) const {
        return get_info(rid).peer_id;
    }
};

class Block;
class HotStuffCore;

using block_t = salticidae::ArcObj<Block>;

class Command: public Serializable {
    friend HotStuffCore;
    public:
    virtual ~Command() = default;
    virtual const uint256_t &get_hash() const = 0;
    virtual bool verify() const = 0;
    virtual operator std::string () const {
        DataStream s;
        s << "<cmd id=" << get_hex10(get_hash()) << ">";
        return s;
    }
};

using command_t = ArcObj<Command>;

template<typename Hashable>
inline static std::vector<uint256_t>
get_hashes(const std::vector<Hashable> &plist) {
    std::vector<uint256_t> hashes;
    for (const auto &p: plist)
        hashes.push_back(p->get_hash());
    return hashes;
}

class OrderedList;
using orderedlist_t = salticidae::ArcObj<OrderedList>;




/** Abstraction for containing OrderedList. */
/** assuming different commands have different content of "cmds". */
class OrderedList
{
    friend HotStuffCore;
    public:
    /** cmds contain the commands and timestamps contain their corresponding timestamps.*/
    std::vector<uint256_t> cmds;
    std::vector<uint64_t> timestamps;

    bool cmp(const int& i, const int& j)
    {
        return timestamps[i] < timestamps[j];
    }

public:
    OrderedList() = default;
    OrderedList(const std::vector<uint256_t> &cmds,
                const std::vector<uint64_t> &timestamps) : cmds(cmds), timestamps(timestamps) {}

    void serialize(DataStream &s) const;
    void unserialize(DataStream &s, HotStuffCore *hsc);
    const std::vector<uint256_t> &extract_cmds() const 
    { 
        HOTSTUFF_LOG_PROTO("Extracting commands!");
        return cmds; 
    }


    const std::vector<uint64_t> &extract_timestamps() const { return timestamps; }


    void sort_cmds()
    {
        std::vector<int> id_for_sort;
        int n_cmds = cmds.size();
        for(int i = 0; i < n_cmds; i++) id_for_sort.push_back(i);
        std::sort(id_for_sort.begin(), id_for_sort.end(), cmp);
        std::vector<uint256_t> new_cmds;
        for(int i = 0; i < n_cmds; i++) new_cmds.push_back(cmds[id_for_sort[i]]);
        cmds.assign(new_cmds.begin(), new_cmds.end());
        std::sort(timestamps.begin(), timestamps.end());
    }

    void printout()
    {
        int n_cmds = cmds.size();
        for(int i = 0; i < n_cmds; i++)
            std::cout << cmds[i] << " ";
        std::cout << "\n";
    }
};



class Block {
    friend HotStuffCore;
    std::vector<uint256_t> parent_hashes;
    std::vector<uint256_t> cmds;
    quorum_cert_bt qc;
    bytearray_t extra;

    /* the following fields can be derived from above */
    uint256_t hash;
    std::vector<block_t> parents;
    block_t qc_ref;
    quorum_cert_bt self_qc;
    uint32_t height;
    bool delivered;
    int8_t decision;

    std::unordered_set<ReplicaID> voted;

    public:
    Block():
        qc(nullptr),
        qc_ref(nullptr),
        self_qc(nullptr), height(0),
        delivered(false), decision(0) {}

    Block(bool delivered, int8_t decision):
        qc(new QuorumCertDummy()),
        hash(salticidae::get_hash(*this)),
        qc_ref(nullptr),
        self_qc(nullptr), height(0),
        delivered(delivered), decision(decision) {}

    Block(const std::vector<block_t> &parents,
        const std::vector<uint256_t> &cmds,
        quorum_cert_bt &&qc,
        bytearray_t &&extra,
        uint32_t height,
        const block_t &qc_ref,
        quorum_cert_bt &&self_qc,
        int8_t decision = 0):
            parent_hashes(get_hashes(parents)),
            cmds(cmds),
            qc(std::move(qc)),
            extra(std::move(extra)),
            hash(salticidae::get_hash(*this)),
            parents(parents),
            qc_ref(qc_ref),
            self_qc(std::move(self_qc)),
            height(height),
            delivered(0),
            decision(decision) {}

    void serialize(DataStream &s) const;

    void unserialize(DataStream &s, HotStuffCore *hsc);

    const std::vector<uint256_t> &get_cmds() const {
        return cmds;
    }

    const std::vector<block_t> &get_parents() const {
        return parents;
    }

    const std::vector<uint256_t> &get_parent_hashes() const {
        return parent_hashes;
    }

    const uint256_t &get_hash() const { return hash; }

    bool verify(const HotStuffCore *hsc) const;

    promise_t verify(const HotStuffCore *hsc, VeriPool &vpool) const;

    int8_t get_decision() const { return decision; }

    bool is_delivered() const { return delivered; }

    uint32_t get_height() const { return height; }

    const quorum_cert_bt &get_qc() const { return qc; }

    const block_t &get_qc_ref() const { return qc_ref; }

    const bytearray_t &get_extra() const { return extra; }

    operator std::string () const {
        DataStream s;
        s << "<block "
          << "id="  << get_hex10(hash) << " "
          << "height=" << std::to_string(height) << " "
          << "parent=" << get_hex10(parent_hashes[0]) << " "
          << "qc_ref=" << (qc_ref ? get_hex10(qc_ref->get_hash()) : "null") << ">";
        return s;
    }
};

struct BlockHeightCmp {
    bool operator()(const block_t &a, const block_t &b) const {
        return a->get_height() < b->get_height();
    }
};




/** 
 * 1. CommandTimeStorage will store all command hashes that has been received and the 
 * corresponding timestamps.
 * 2. It will also contain commands in ascending order of timestamps that heven't been 
 * included in any previous block. 
*/
class CommandTimestampStorage
{
    /* for active checking while doing acceptable fairness check of the proposal */
    std::unordered_map<const uint256_t, const uint64_t> cmd_ts_storage;

    /** Available commands to be included in the ordered list of the vote.
     * New commands are inserted in add_command_to_storage() function, everything 
     * in ascending order of timestamps. Whenever a new proposal is received, check whether
     * the proposal is acceptable under approximately fair. Only if it is acceptable, remove 
     * those commands and timestamps from it.*/
    // WARN - the updating procedure for these two is not applicable for rotating leader
    std::vector<uint256_t> available_cmd_hashes;
    std::vector<uint64_t> available_timestamps;

    /** for print later on */
    std::vector<uint256_t> cmd_hashes; // the corresponding hashes
    std::vector<uint64_t> timestamps;  // the time stamps when corresponding commands are received

    /** storing all replica preferred orderedlist that will be sent with the vote.
     * the key is the block hash for which the vote is being sent.*/
    std::unordered_map<const uint256_t, orderedlist_t> replica_preferred_ordering_cache;

public:
    void add_command_to_storage(const uint256_t cmd_hash);
    bool is_new_command(const uint256_t &cmd_hash) const;
    void refresh_available_cmds(const std::vector<uint256_t> cmds);
    const std::vector<uint256_t> &get_all_cmd_hashes() const { return cmd_hashes; }
    const std::vector<uint64_t> &get_all_timestamps() const { return timestamps; }
    std::vector<uint64_t> get_timestamps(const std::vector<uint256_t> &cmd_hashes_inquired) const;
    orderedlist_t get_orderedlist(const uint256_t &blk_hash);
    
};



/** It stores all the orderedlists  that the leader ever received from other replicas
 * sent along with the votes.*/
class OrderedListStorage {
    std::unordered_map<uint256_t, std::vector<OrderedList>> ordered_list_cache;
    //std::vector<uint256_t> list_block_hashes;
    //std::vector<std::vector<OrderedList>> ordered_list_cache;

public:
    void add_ordered_list(const uint256_t block_hash, const OrderedList preferred_orderedlist, bool leader);
    std::vector<OrderedList> get_set_of_orderedlists(const uint256_t block_hash) const;
    std::vector<uint256_t> get_all_block_hashes() const;
    std::vector<uint256_t> get_cmds_for_first_one(const uint256_t block_hash) const;
    std::vector<uint64_t> get_timestamps_for_first_one(const uint256_t block_hash) const;
    std::vector<uint256_t> get_cmds_for_second_one(const uint256_t block_hash) const;
    std::vector<uint64_t> get_timestamps_for_second_one(const uint256_t block_hash) const;


};







class EntityStorage {
    std::unordered_map<const uint256_t, block_t> blk_cache;
    std::unordered_map<const uint256_t, command_t> cmd_cache;
    public:
    bool is_blk_delivered(const uint256_t &blk_hash) {
        auto it = blk_cache.find(blk_hash);
        if (it == blk_cache.end()) return false;
        return it->second->is_delivered();
    }

    bool is_blk_fetched(const uint256_t &blk_hash) {
        return blk_cache.count(blk_hash);
    }

    block_t add_blk(Block &&_blk, const ReplicaConfig &/*config*/) {
        //if (!_blk.verify(config))
        //{
        //    HOTSTUFF_LOG_WARN("invalid %s", std::string(_blk).c_str());
        //    return nullptr;
        //}
        block_t blk = new Block(std::move(_blk));
        return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
    }

    const block_t &add_blk(const block_t &blk) {
        return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
    }

    block_t find_blk(const uint256_t &blk_hash) {
        auto it = blk_cache.find(blk_hash);
        return it == blk_cache.end() ? nullptr : it->second;
    }

    bool is_cmd_fetched(const uint256_t &cmd_hash) {
        return cmd_cache.count(cmd_hash);
    }

    const command_t &add_cmd(const command_t &cmd) {
        return cmd_cache.insert(std::make_pair(cmd->get_hash(), cmd)).first->second;
    }

    command_t find_cmd(const uint256_t &cmd_hash) {
        auto it = cmd_cache.find(cmd_hash);
        return it == cmd_cache.end() ? nullptr: it->second;
    }

    size_t get_cmd_cache_size() {
        return cmd_cache.size();
    }
    size_t get_blk_cache_size() {
        return blk_cache.size();
    }

    bool try_release_cmd(const command_t &cmd) {
        if (cmd.get_cnt() == 2) /* only referred by cmd and the storage */
        {
            const auto &cmd_hash = cmd->get_hash();
            cmd_cache.erase(cmd_hash);
            return true;
        }
        return false;
    }

    bool try_release_blk(const block_t &blk) {
        if (blk.get_cnt() == 2) /* only referred by blk and the storage */
        {
            const auto &blk_hash = blk->get_hash();
#ifdef HOTSTUFF_PROTO_LOG
            HOTSTUFF_LOG_INFO("releasing blk %.10s", get_hex(blk_hash).c_str());
#endif
//            for (const auto &cmd: blk->get_cmds())
//                try_release_cmd(cmd);
            blk_cache.erase(blk_hash);
            return true;
        }
#ifdef HOTSTUFF_PROTO_LOG
        else
            HOTSTUFF_LOG_INFO("cannot release (%lu)", blk.get_cnt());
#endif
        return false;
    }
};

}

#endif
