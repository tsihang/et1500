#ifndef DP_DECODE_ARP_H
#define DP_DECODE_ARP_H

/**
 * \brief Function to decode GRE packets
 */

static __oryx_always_inline__
int DecodeARP0(threadvar_ctx_t *tv, decode_threadvar_ctx_t *dtv, packet_t *p, uint8_t *pkt, uint16_t len, pq_t *pq)
{
    uint16_t header_len = GRE_HDR_LEN;

	oryx_logd("ARP");

    oryx_counter_inc(&tv->perf_private_ctx0, dtv->counter_arp);

	return 0;
}

#endif
