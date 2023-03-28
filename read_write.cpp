#include <infiniband/verbs.h>
#include <netdb.h>
#include <sys/socket.h>

#include <cassert>
#include <cstdlib>
#include <iostream>

#include "config.hpp"
#include "debug.hpp"
#include "exchange_msg.hpp"
#include "rdma.hpp"

void write_and_read(ibv_qp *queue_pair, ibv_mr *memory_region,
                    const conn_xchg_info *dest_info) {
    uint64_t local_base_addr = reinterpret_cast<uint64_t>(memory_region->addr);

    // this is a signal for server
    ibv_send_wr end_wr = {
        .wr_id = 2,
        .next = nullptr,
        .sg_list = nullptr,
        .num_sge = 0,
        .opcode = IBV_WR_SEND,
        .send_flags = 0,
    };

    ibv_sge read_sge = {.addr = reinterpret_cast<uint64_t>(local_base_addr),
                        .length = PAGE_SIZE,
                        .lkey = memory_region->lkey};
    ibv_send_wr read_wr = {.wr_id = 1,
                           .next = &end_wr,
                           .sg_list = &read_sge,
                           .num_sge = 1,
                           .opcode = IBV_WR_RDMA_READ,
                           .send_flags = 0,
                           .wr = {.rdma = {
                                      .remote_addr = dest_info->addr,
                                      .rkey = dest_info->rkey,
                                  }}};

    ibv_sge write_sge = {.addr = local_base_addr,
                         .length = 4 * sizeof(int),
                         .lkey = memory_region->lkey};
    ibv_send_wr write_wr = {
        .wr_id = 0,
        .next = &read_wr,
        .sg_list = &write_sge,
        .num_sge = 1,
        .opcode = IBV_WR_RDMA_WRITE,
        .send_flags = 0,
        .wr = {.rdma = {
                   .remote_addr = dest_info->addr + 4 * sizeof(int),
                   .rkey = dest_info->rkey,
               }}};
    ibv_send_wr *bad_wr;
    int send_ret = ibv_post_send(queue_pair, &write_wr, &bad_wr);
    switch (send_ret) {
        case 0:
            break;
        case EINVAL:
            std::cerr << "ibv_post_send: Invalid value provided in wr"
                      << std::endl;
            break;
        case ENOMEM:
            std::cerr << "ibv_post_send: Send Queue is full or not enough "
                         "resources to complete this operation"
                      << std::endl;
            break;
        case EFAULT:
            std::cerr << "ibv_post_send: Invalid value provided in qp"
                      << std::endl;
            break;
        default:
            std::cerr << "ibv_post_send: failed, errno = " << errno
                      << std::endl;
            break;
    }
}

void wait_for_cq(ibv_cq *complete_queue, size_t count) {
    int completed_count = 0;
    while (completed_count < count) {
        ibv_wc work_completion;
        int poll_ret = ibv_poll_cq(complete_queue, 1, &work_completion);
        ASSERT(poll_ret >= 0 && poll_ret <= 1);
        if (poll_ret == 1) {
            ASSERT(work_completion.wr_id == completed_count);
            ASSERT(work_completion.status == IBV_WC_SUCCESS);
            completed_count++;
        }
    }
}

void print_buffer(int *buffer, size_t size) {
    std::cout << "data:";
    for (size_t i = 0; i < size; i++) {
        std::cout << " " << buffer[i];
    }
    std::cout << std::endl;
}

int main(int argc, const char *argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <server|client>" << std::endl;
        exit(-1);
    }
    bool is_server;
    if (strcmp(argv[1], "server") == 0) {
        is_server = true;
    } else if (strcmp(argv[1], "client") == 0) {
        is_server = false;
    } else {
        ASSERT(false);
    }

    std::cout << "Hello!" << std::endl;

    static_assert(PAGE_SIZE <= BUFFER_SIZE);
    void *buffer = std::aligned_alloc(PAGE_SIZE, BUFFER_SIZE);
    if (is_server) {
        for (size_t i = 0; i < PAGE_SIZE / sizeof(int); i++) {
            static_cast<int *>(buffer)[i] = i;
        }
    } else {
        memset(buffer, 0, PAGE_SIZE);
    }

    {
        /* ================ INITIALIZITION ================ */
        // open device
        rdma_context ctx;

        // allocate protection domain
        rdma_protection_domain pd(ctx);

        // register memory region
        rdma_memory_region mr(pd, buffer, BUFFER_SIZE);

        // create the complete queue
        rdma_complete_queue send_cq(ctx);
        rdma_complete_queue recv_cq(ctx);

        // create queue pair
        rdma_queue_pair qp(ctx, send_cq, recv_cq, pd);

        // modify state to INIT
        qp.init();

        // build connection
        // exchange infomation by tcp
        ibv_port_attr port_attr;
        ibv_query_port(ctx.context, IB_PORT, &port_attr);
        srand48(time(nullptr));
        uint32_t psn = lrand48() & 0xffffff;
        conn_xchg_info dest_info = {
            .psn = psn,
            .qpn = qp.queue_pair->qp_num,
            .lid = port_attr.lid,
            .rkey = mr.memory_region->rkey,
            .addr = reinterpret_cast<uint64_t>(buffer),
        };
        std::cout << "local info: " << dest_info << std::endl;
        exchange_msg(is_server, SERVER_ADDR, SERVER_PORT, &dest_info,
                     sizeof(dest_info));
        std::cout << "dest info: " << dest_info << std::endl;

        // modify state to RTR
        qp.ready_to_recv(dest_info);

        // for client, modify state to RTS
        if (!is_server) {
            qp.ready_to_send(psn);
        }

        /* ================ INITIALIZED ================ */

        print_buffer(static_cast<int *>(buffer), 16);
        if (!is_server) {
            write_and_read(qp.queue_pair, mr.memory_region, &dest_info);
            wait_for_cq(send_cq.complete_queue, 3);

            // should be 0 1 2 3 0 0 0 0 8 9 ... 15
            print_buffer(static_cast<int *>(buffer), 16);
        }
        if (is_server) {
            ibv_recv_wr recv_wr = {
                .wr_id = 0,
                .next = nullptr,
                .sg_list = nullptr,
                .num_sge = 0,
            };
            ibv_recv_wr *bad_wr;
            ibv_post_recv(qp.queue_pair, &recv_wr, &bad_wr);
            wait_for_cq(recv_cq.complete_queue, 1);
        }
    }

    // free buffer
    std::free(buffer);

    std::cout << "Bye!" << std::endl;

    return 0;
}