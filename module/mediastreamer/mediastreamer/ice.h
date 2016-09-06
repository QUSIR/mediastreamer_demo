/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2006  Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef ice_hh
#define ice_hh

#include "msfilter.h"
#include "ortp/stun_udp.h"
#include "ortp/stun.h"
#include "ortp/ortp.h"
#ifdef  __cplusplus
extern "C" {
#endif
/* list of state for STUN connectivity check */
#define ICE_PRUNED -1
#define ICE_FROZEN 0
#define ICE_WAITING 1
#define ICE_IN_PROGRESS 2 /* STUN request was sent */
#define ICE_SUCCEEDED 3
#define ICE_FAILED 4 /* no answer or unrecoverable failure */


struct SdpCandidate {
	/* mandatory attributes: draft 19 */
	int foundation;
	int component_id;
	char transport[20];
	int priority;
	char conn_addr[64];
	int conn_port;
	char cand_type[20];
	char rel_addr[64];
	int rel_port;

	/* optionnal attributes: draft 19 */
	char extension_attr[512]; /* *(SP extension-att-name SP extension-att-value) */
};

struct CandidatePair {

    struct SdpCandidate local_candidate;
    struct SdpCandidate remote_candidate;
    long long pair_priority;
    /* additionnal information */
    int rem_controlling;
    UInt96 tid;
    int connectivity_check;
	int retransmission_number;
	uint64_t retransmission_time;
	int nominated_pair;
};

#define MAX_ICE_CANDIDATES 10

struct IceCheckList {
    struct CandidatePair cand_pairs[MAX_ICE_CANDIDATES];
	int nominated_pair_index;

    char loc_ice_ufrag[256];
    char loc_ice_pwd[256];
    char rem_ice_ufrag[256];
    char rem_ice_pwd[256];

    int rem_controlling;
    uint64_t tiebreak_value;

#define ICE_CL_RUNNING 0
#define ICE_CL_COMPLETED 1
#define ICE_CL_FAILED 2
    int state;

    int RTO;
    int Ta;
	uint64_t keepalive_time;
};

#define MS_ICE_SET_SESSION			MS_FILTER_METHOD(MS_ICE_ID,0,RtpSession*)
#define MS_ICE_SET_CANDIDATEPAIRS	MS_FILTER_METHOD(MS_ICE_ID,1,struct CandidatePair*)

extern MSFilterDesc ms_ice_desc;


#define MAXNATSESSION  4

#define NAT_MSG_SAVE   0x3810
#define NAT_MSG_LOAD   0x3811

#define	NAT_TYPE_ERROR					(-1)	//获取nat类型失败
#define	NAT_TYPE_UNKNOW				(0)		//未知NAT类型
#define	NAT_TYPE_OPEN					(1)		//开放NAT，无NAT
#define	NAT_TYPE_CONE					(2)		//完全圆锥型		
#define	NAT_TYPE_ADDR_RESTRICTED	(3)		//地址受限型
#define	NAT_TYPE_PORT_RESTRICTED	(4)		//端口受限型
#define	NAT_TPYE_SYMMETRY				(5)		//对称型
#define	NAT_TYPE_SYMMETRY_FIREWALL	(6)		//对称防火墙型


typedef	struct
{
	StunMsgHdr    hdr;
	StunAddress4  src;
	StunAddress4  dst;
}NatMsg;


typedef struct
{
	int foundation;
	int component_id;
	int transport;         // 0 udp, 1 tcp
	int priority;
	int cand_type;         // 0 host, 1 frlx, 2 relay
	StunAddress4  map;
	StunAddress4  src;
}NatCandidate;

typedef struct
{
    char ufrag[4];
    char pwd[22];
	NatCandidate  host;
	NatCandidate  rflx;
	NatCandidate  relay;
	StunAddress4  link;
}NatChannel;

typedef struct
{
	int  nc;
	NatChannel  rtp;
	NatChannel  rtcp;
}NatSession;

typedef struct
{
	int  magic;
	int  type;
	int  number;
	NatSession session[MAXNATSESSION];
} NatCheckList;

typedef struct
{
	char      		magic[8];
	char      		ltid[4];
	char      		rtid[4];
	char      		userid[4];
	uint32_t		timestamp;
	uint8_t			isrtcp;
	uint8_t			state;
	uint16_t		bitrate;
	uint32_t		reserved;
}IceMessage;


int ice_select_network_interface(int index);
int ice_get_nat_type(const char *server_name, int host_addr, uint16_t port);
int ice_gather_candidates(uint32_t stun_addr, uint16_t stun_port, int host_addr, int nat_type, int session, int start_port, int magic, NatCheckList *checkList);
int ice_exchange_check_list(const char *server_name, NatCheckList *checkList, int host_addr, uint16_t port, int action);
int ice_connectivity_check(NatCheckList *local, NatCheckList *remote, int host_addr, int magic);


#ifdef  __cplusplus
}
#endif
#endif
