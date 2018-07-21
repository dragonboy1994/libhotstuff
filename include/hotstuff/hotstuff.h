#ifndef _HOTSTUFF_CORE_H
#define _HOTSTUFF_CORE_H

#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"
#include "hotstuff/util.h"
#include "hotstuff/consensus.h"
#include "hotstuff/liveness.h"

namespace hotstuff {

using salticidae::MsgNetwork;
using salticidae::PeerNetwork;
using salticidae::ElapsedTime;
using salticidae::_1;
using salticidae::_2;

const double ent_waiting_timeout = 10;
const double double_inf = 1e10;

enum {
    PROPOSE = 0x0,
    VOTE = 0x1,
    QUERY_FETCH_BLK = 0x2,
    RESP_FETCH_BLK = 0x3,
};

/** Network message format for HotStuff. */
struct MsgPropose {
    static const opcode_t opcode = 0x0;
    DataStream serialized;
    Proposal proposal;
    MsgPropose(const Proposal &);
    /** Only move the data to serialized, do not parse immediately. */
    MsgPropose(DataStream &&s): serialized(std::move(s)) {}
    /** Parse the serialized data to blks now, with `hsc->storage`. */
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgVote {
    static const opcode_t opcode = 0x1;
    DataStream serialized;
    Vote vote;
    MsgVote(const Vote &);
    MsgVote(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgReqBlock {
    static const opcode_t opcode = 0x2;
    DataStream serialized;
    std::vector<uint256_t> blk_hashes;
    MsgReqBlock() = default;
    MsgReqBlock(const std::vector<uint256_t> &blk_hashes);
    MsgReqBlock(DataStream &&s);
};


struct MsgRespBlock {
    static const opcode_t opcode = 0x3;
    DataStream serialized;
    std::vector<block_t> blks;
    MsgRespBlock(const std::vector<block_t> &blks);
    MsgRespBlock(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

using promise::promise_t;

class HotStuffBase;

template<EntityType ent_type>
class FetchContext: public promise_t {
    Event timeout;
    HotStuffBase *hs;
    MsgReqBlock fetch_msg;
    const uint256_t ent_hash;
    std::unordered_set<NetAddr> replica_ids;
    inline void timeout_cb(evutil_socket_t, short);
    public:
    FetchContext(const FetchContext &) = delete;
    FetchContext &operator=(const FetchContext &) = delete;
    FetchContext(FetchContext &&other);

    FetchContext(const uint256_t &ent_hash, HotStuffBase *hs);
    ~FetchContext() {}

    inline void send(const NetAddr &replica_id);
    inline void reset_timeout();
    inline void add_replica(const NetAddr &replica_id, bool fetch_now = true);
};

class BlockDeliveryContext: public promise_t {
    public:
    ElapsedTime elapsed;
    BlockDeliveryContext &operator=(const BlockDeliveryContext &) = delete;
    BlockDeliveryContext(const BlockDeliveryContext &other):
        promise_t(static_cast<const promise_t &>(other)),
        elapsed(other.elapsed) {}
    BlockDeliveryContext(BlockDeliveryContext &&other):
        promise_t(static_cast<const promise_t &>(other)),
        elapsed(std::move(other.elapsed)) {}
    template<typename Func>
    BlockDeliveryContext(Func callback): promise_t(callback) {
        elapsed.start();
    }
};


/** HotStuff protocol (with network implementation). */
class HotStuffBase: public HotStuffCore {
    using BlockFetchContext = FetchContext<ENT_TYPE_BLK>;
    using CmdFetchContext = FetchContext<ENT_TYPE_CMD>;
    using conn_t = PeerNetwork<opcode_t>::conn_t;

    friend BlockFetchContext;
    friend CmdFetchContext;

    protected:
    /** the binding address in replica network */
    NetAddr listen_addr;
    /** the block size */
    size_t blk_size;
    /** libevent handle */
    EventContext eb;
    pacemaker_bt pmaker;

    private:
    /** whether libevent handle is owned by itself */
    bool eb_loop;
    /** network stack */
    PeerNetwork<opcode_t> pn;
#ifdef HOTSTUFF_ENABLE_BLK_PROFILE
    BlockProfiler blk_profiler;
#endif
    /* queues for async tasks */
    std::unordered_map<const uint256_t, BlockFetchContext> blk_fetch_waiting;
    std::unordered_map<const uint256_t, BlockDeliveryContext> blk_delivery_waiting;
    std::unordered_map<const uint256_t, CmdFetchContext> cmd_fetch_waiting;
    std::unordered_map<const uint256_t, promise_t> decision_waiting;
    std::queue<command_t> cmd_pending;

    /* statistics */
    uint64_t fetched;
    uint64_t delivered;
    mutable uint64_t nsent;
    mutable uint64_t nrecv;

    mutable uint32_t part_parent_size;
    mutable uint32_t part_fetched;
    mutable uint32_t part_delivered;
    mutable uint32_t part_decided;
    mutable uint32_t part_gened;
    mutable double part_delivery_time;
    mutable double part_delivery_time_min;
    mutable double part_delivery_time_max;
    mutable std::unordered_map<const NetAddr, uint32_t> part_fetched_replica;

    void on_fetch_cmd(const command_t &cmd);
    void on_fetch_blk(const block_t &blk);
    void on_deliver_blk(const block_t &blk);

    /** deliver consensus message: <propose> */
    inline void propose_handler(MsgPropose &&, conn_t);
    /** deliver consensus message: <vote> */
    inline void vote_handler(MsgVote &&, conn_t);
    /** fetches full block data */
    inline void req_blk_handler(MsgReqBlock &&, conn_t);
    /** receives a block */
    inline void resp_blk_handler(MsgRespBlock &&, conn_t);

    void do_broadcast_proposal(const Proposal &) override;
    void do_vote(ReplicaID, const Vote &) override;
    void do_decide(Finality &&) override;

    protected:

    /** Called to replicate the execution of a command, the application should
     * implement this to make transition for the application state. */
    virtual void state_machine_execute(const Finality &) = 0;

    public:
    HotStuffBase(uint32_t blk_size,
            ReplicaID rid,
            privkey_bt &&priv_key,
            NetAddr listen_addr,
            pacemaker_bt pmaker,
            EventContext eb);

    ~HotStuffBase();

    /* the API for HotStuffBase */

    /* Submit the command to be decided. */
    promise_t exec_command(command_t cmd);
    void add_replica(ReplicaID idx, const NetAddr &addr, pubkey_bt &&pub_key);
    void start(bool eb_loop = false);

    size_t size() const { return pn.all_peers().size(); }
    void print_stat() const;

    /* Helper functions */
    /** Returns a promise resolved (with command_t cmd) when Command is fetched. */
    promise_t async_fetch_cmd(const uint256_t &cmd_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is fetched. */
    promise_t async_fetch_blk(const uint256_t &blk_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is delivered (i.e. prefix is fetched). */
    promise_t async_deliver_blk(const uint256_t &blk_hash,  const NetAddr &replica_id);
};

/** HotStuff protocol (templated by cryptographic implementation). */
template<typename PrivKeyType = PrivKeyDummy,
        typename PubKeyType = PubKeyDummy,
        typename PartCertType = PartCertDummy,
        typename QuorumCertType = QuorumCertDummy>
class HotStuff: public HotStuffBase {
    using HotStuffBase::HotStuffBase;
    protected:

    part_cert_bt create_part_cert(const PrivKey &priv_key, const uint256_t &blk_hash) override {
        return new PartCertType(
                    static_cast<const PrivKeyType &>(priv_key),
                    blk_hash);
    }

    part_cert_bt parse_part_cert(DataStream &s) override {
        PartCert *pc = new PartCertType();
        s >> *pc;
        return pc;
    }

    quorum_cert_bt create_quorum_cert(const uint256_t &blk_hash) override {
        return new QuorumCertType(get_config(), blk_hash);
    }

    quorum_cert_bt parse_quorum_cert(DataStream &s) override {
        QuorumCert *qc = new QuorumCertType();
        s >> *qc;
        return qc;
    }

    public:
    HotStuff(uint32_t blk_size,
            ReplicaID rid,
            const bytearray_t &raw_privkey,
            NetAddr listen_addr,
            pacemaker_bt pmaker,
            EventContext eb = EventContext()):
        HotStuffBase(blk_size,
                    rid,
                    new PrivKeyType(raw_privkey),
                    listen_addr,
                    std::move(pmaker),
                    eb) {}

    void add_replica(ReplicaID idx, const NetAddr &addr, const bytearray_t &pubkey_raw) {
        DataStream s(pubkey_raw);
        HotStuffBase::add_replica(idx, addr, new PubKeyType(pubkey_raw));
    }
};

using HotStuffNoSig = HotStuff<>;
using HotStuffSecp256k1 = HotStuff<PrivKeySecp256k1, PubKeySecp256k1,
                                    PartCertSecp256k1, QuorumCertSecp256k1>;

template<EntityType ent_type>
FetchContext<ent_type>::FetchContext(FetchContext && other):
        promise_t(static_cast<const promise_t &>(other)),
        hs(other.hs),
        fetch_msg(std::move(other.fetch_msg)),
        ent_hash(other.ent_hash),
        replica_ids(std::move(other.replica_ids)) {
    other.timeout.del();
    timeout = Event(hs->eb, -1, 0,
            std::bind(&FetchContext::timeout_cb, this, _1, _2));
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_CMD>::timeout_cb(evutil_socket_t, short) {
    HOTSTUFF_LOG_WARN("cmd fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_BLK>::timeout_cb(evutil_socket_t, short) {
    HOTSTUFF_LOG_WARN("block fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<EntityType ent_type>
FetchContext<ent_type>::FetchContext(
                                const uint256_t &ent_hash, HotStuffBase *hs):
            promise_t([](promise_t){}),
            hs(hs), ent_hash(ent_hash) {
    fetch_msg = std::vector<uint256_t>{ent_hash};

    timeout = Event(hs->eb, -1, 0,
            std::bind(&FetchContext::timeout_cb, this, _1, _2));
    reset_timeout();
}

template<EntityType ent_type>
void FetchContext<ent_type>::send(const NetAddr &replica_id) {
    hs->part_fetched_replica[replica_id]++;
    hs->pn.send_msg(fetch_msg, replica_id);
}

template<EntityType ent_type>
void FetchContext<ent_type>::reset_timeout() {
    timeout.add_with_timeout(salticidae::gen_rand_timeout(ent_waiting_timeout));
}

template<EntityType ent_type>
void FetchContext<ent_type>::add_replica(const NetAddr &replica_id, bool fetch_now) {
    if (replica_ids.empty() && fetch_now)
        send(replica_id);
    replica_ids.insert(replica_id);
}

}

#endif