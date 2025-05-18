#ifndef RDMA_H
#define RDMA_H

#define ENABLE_QP 1

namespace ns3 {

// Global flag to completely disable RDMA processing
extern bool g_disableRdmaProcessing;

// Functions to control RDMA processing
static inline void SetRdmaProcessingEnabled(bool enabled) {
    g_disableRdmaProcessing = !enabled;
}

static inline bool IsRdmaProcessingEnabled() {
    return !g_disableRdmaProcessing;
}

} // namespace ns3

#endif
