#include <infiniband/verbs.h>

#include "config.hpp"
#include "debug.hpp"

// The following info should be matched
// Some of them are constant or assumed to be the same at now
struct conn_xchg_info {
    // p_key
    // path mtu
    // max_rd_atomic & max_dest_rd_atomic
    uint32_t psn;  // Packet Sequence Number
    uint32_t qpn;  // QP Number
    uint16_t lid;  // Local IDentifier
    uint32_t rkey;
    uint64_t addr;

    friend std::ostream &operator<<(std::ostream &os,
                                    const conn_xchg_info &info) {
        return os << "[ psn: " << info.psn << " qpn: " << info.qpn
                  << " lid: " << info.lid << " rkey: " << info.rkey
                  << " addr:" << std::hex << info.addr << std::dec << " ]";
    }
};

// use RAII to manage resources
struct rdma_context {
    ibv_context *context;

    rdma_context() {
        int num_devices;
        ibv_device **dev_list = ibv_get_device_list(&num_devices);
        ASSERT(dev_list && num_devices > 0);
        ibv_device *device = dev_list[0];
        ibv_free_device_list(dev_list);

        // open device
        context = ibv_open_device(device);
        ASSERT(context);
    }

    ~rdma_context() { CHECK(ibv_close_device(context)); }
};

struct rdma_protection_domain {
    ibv_pd *protection_domain;

    explicit rdma_protection_domain(rdma_context &ctx) {
        protection_domain = ibv_alloc_pd(ctx.context);
        ASSERT(protection_domain);
    }

    ~rdma_protection_domain() { CHECK(ibv_dealloc_pd(protection_domain)); }
};

struct rdma_memory_region {
    ibv_mr *memory_region;

    rdma_memory_region(rdma_protection_domain &pd, void *buffer,
                       size_t buffer_size) {
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE;
        memory_region =
            ibv_reg_mr(pd.protection_domain, buffer, buffer_size, access_flags);
        assert(memory_region);
    }

    ~rdma_memory_region() { CHECK(ibv_dereg_mr(memory_region)); }
};

struct rdma_complete_queue {
    ibv_cq *complete_queue;

    rdma_complete_queue(rdma_context &ctx) {
        complete_queue =
            ibv_create_cq(ctx.context, CQ_ENTRIES, nullptr, nullptr, 0);
        assert(complete_queue);
    }

    ~rdma_complete_queue() { CHECK(ibv_destroy_cq(complete_queue)); }
};

struct rdma_queue_pair {
    ibv_qp *queue_pair;

    rdma_queue_pair(rdma_context &ctx, rdma_complete_queue &send_cq,
                    rdma_complete_queue &recv_cq, rdma_protection_domain &pd) {
        ibv_qp_cap qp_cap = {
            .max_send_wr = QP_SEND_CAP,
            .max_recv_wr = QP_RECV_CAP,
            .max_send_sge = QP_MAX_SEND_SGE,
            .max_recv_sge = QP_MAX_RECV_SGE,
            .max_inline_data = 0,
        };
        ibv_qp_init_attr qp_init_attr = {
            .qp_context = ctx.context,
            .send_cq = send_cq.complete_queue,
            .recv_cq = recv_cq.complete_queue,
            .srq = nullptr,
            .cap = qp_cap,
            .qp_type = IBV_QPT_RC,
            .sq_sig_all = 1,  // will submit work completion for all requests
        };
        queue_pair = ibv_create_qp(pd.protection_domain, &qp_init_attr);
    }

    ~rdma_queue_pair() { CHECK(ibv_destroy_qp(queue_pair)); }

    void init() {
        int attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                        IBV_QP_ACCESS_FLAGS;
        ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_INIT,
            .qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
                               IBV_ACCESS_REMOTE_READ |
                               IBV_ACCESS_REMOTE_ATOMIC,
            .pkey_index = 0,
            .port_num = IB_PORT,
        };
        CHECK(ibv_modify_qp(queue_pair, &qp_attr, attr_mask));
    }

    void ready_to_recv(conn_xchg_info &dest_info) {
        int attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                        IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                        IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
        ibv_ah_attr ah_attr = {
            .dlid = dest_info.lid,
            .sl = 0,  // service level
            .src_path_bits = 0,
            .is_global = 0,
            .port_num = IB_PORT,
        };
        ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTR,
            .path_mtu = QP_MTU,
            .rq_psn = dest_info.psn,
            .dest_qp_num = dest_info.qpn,
            .ah_attr = ah_attr,
            .max_dest_rd_atomic = QP_MAX_RD_ATOMIC,
            .min_rnr_timer = QP_MIN_RNR_TIMER,
        };
        CHECK(ibv_modify_qp(queue_pair, &qp_attr, attr_mask));
    }

    void ready_to_send(uint32_t psn) {
        int attr_mask = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
                        IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                        IBV_QP_MAX_QP_RD_ATOMIC;
        ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTS,
            .sq_psn = psn,
            .max_rd_atomic = QP_MAX_RD_ATOMIC,
            .timeout = QP_TIMEOUT,
            .retry_cnt = QP_RETRY_CNT,
            .rnr_retry = QP_RNR_RETRY,
        };
        CHECK(ibv_modify_qp(queue_pair, &qp_attr, attr_mask));
    }
};