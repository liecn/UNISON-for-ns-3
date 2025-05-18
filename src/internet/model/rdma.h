#ifndef RDMA_H
#define RDMA_H

#define ENABLE_QP 1

namespace ns3 {

/**
 * Global flag to completely disable RDMA processing
 * 
 * When set to true, RDMA components will be skipped during initialization
 * and runtime processing, improving performance for simulations that don't
 * need RDMA functionality.
 * 
 * By default, RDMA processing is disabled for better performance.
 */
extern bool g_disableRdmaProcessing;

/**
 * Enable or disable RDMA processing globally
 * 
 * This function should be called early in the simulation setup,
 * ideally at the beginning of main(), before any RDMA components
 * are initialized.
 * 
 * @param enabled true to enable RDMA processing, false to disable it
 */
static inline void SetRdmaProcessingEnabled(bool enabled) {
    g_disableRdmaProcessing = !enabled;
}

/**
 * Check if RDMA processing is currently enabled
 * 
 * @return true if RDMA processing is enabled, false otherwise
 */
static inline bool IsRdmaProcessingEnabled() {
    return !g_disableRdmaProcessing;
}

/**
 * Configure RDMA for optimal performance in MTP simulations
 * 
 * This function should be called at the beginning of your simulation
 * script to properly configure RDMA for use with MTP.
 * 
 * @param enableRdma true to enable RDMA functionality, false to disable it
 *                   for better performance when RDMA is not needed
 */
static inline void ConfigureRdma(bool enableRdma) {
    SetRdmaProcessingEnabled(enableRdma);
}

} // namespace ns3

#endif
