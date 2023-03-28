#pragma once

#include <infiniband/verbs.h>

#include <cstddef>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t BUFFER_SIZE = 1024 * 1024;
constexpr size_t CQ_ENTRIES = 1024;
constexpr size_t QP_SEND_CAP = 1024;
constexpr size_t QP_RECV_CAP = 1024;
constexpr size_t QP_MAX_SEND_SGE = 1;
constexpr size_t QP_MAX_RECV_SGE = 1;
constexpr size_t QP_MAX_RD_ATOMIC = 1;
constexpr ibv_mtu QP_MTU = IBV_MTU_256;
constexpr int QP_MIN_RNR_TIMER = 12;  // 0.64 milliseconds delay
constexpr int QP_TIMEOUT = 8;         // 1048.576 usec (0.00104 sec)
constexpr int QP_RETRY_CNT = 7;
constexpr int QP_RNR_RETRY = 7;
constexpr size_t IB_PORT = 1;
const char *SERVER_ADDR = "127.0.0.1";
const char *SERVER_PORT = "1234";
