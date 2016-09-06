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

//#include <jni.h>
//#include <android/log.h>
//#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "JNIMsg", __VA_ARGS__))

#define TURN_PORT 10302

#if !defined(WIN32) && !defined(_WIN32_WCE)
#ifdef __APPLE__
#include <sys/types.h>
#endif
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "msticker.h"
#include "ice.h"

#ifdef __APPLE__
#include "IPAdress.h"
#endif
#ifdef ANDROID
#include <linux/if.h>
#endif

static void 
ice_sendtest( struct IceCheckList *checklist, struct CandidatePair *remote_candidate, Socket myFd, StunAddress4 *dest, 
              const StunAtrString *username, const StunAtrString *password, 
              UInt96 *tid)
{
   StunMessage req;
   char buf[STUN_MAX_MESSAGE_SIZE];
   int len = STUN_MAX_MESSAGE_SIZE;
   
   memset(&req, 0, sizeof(StunMessage));

   stunBuildReqSimple( &req, username, FALSE, FALSE, 1);
   req.hasMessageIntegrity=TRUE;

   /* 7.1.1.1
   The attribute MUST be set equal to the priority that would be
   assigned, based on the algorithm in Section 4.1.2, to a peer
   reflexive candidate, should one be learned as a consequence of this
   check */
   req.hasPriority = TRUE;

   req.priority.priority = (110 << 24) | (255 << 16) | (255 << 8)
	   | (256 - remote_candidate->remote_candidate.component_id);

   /* TODO: put this parameter only for the candidate selected */
   if (remote_candidate->nominated_pair==1)
	   req.hasUseCandidate = TRUE;

   if (remote_candidate->rem_controlling==1)
	   {
		   req.hasIceControlled = TRUE;
		   req.iceControlled.value = checklist->tiebreak_value;
	   }
   else
	   {
		   req.hasIceControlling = TRUE;
		   req.iceControlling.value	= checklist->tiebreak_value;
	   }

   /* TODO: not yet implemented? */
   req.hasFingerprint = TRUE;
   
   len = stunEncodeMessage( &req, buf, len, password );

   memcpy(tid , &(req.msgHdr.tr_id), sizeof(req.msgHdr.tr_id));

   sendMessage( myFd, buf, len, dest->addr, dest->port );	
}

static int ice_restart(struct IceCheckList *checklist)
{
	struct CandidatePair *remote_candidates = NULL;
	int pos;

	int count_waiting=0;
	int count=0;

	if (checklist==NULL)
		return 0;
	remote_candidates = checklist->cand_pairs;
	if (remote_candidates==NULL)
		return 0;

	for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
	{
		if (strcasecmp(remote_candidates[pos].local_candidate.cand_type, "srflx")==0)
		{
			/* search for a highest priority "equivalent" pair */
			int pos2;
			for (pos2=0;pos2<pos && remote_candidates[pos2].remote_candidate.conn_addr[0]!='\0';pos2++)
			{
				/* same "base" address (origin of STUN connectivity check to the remote candidate */
				if (strcasecmp(remote_candidates[pos].local_candidate.rel_addr, /* base address for "reflexive" address */
					remote_candidates[pos2].local_candidate.conn_addr)==0) /* base address for "host" address */
				{
					/* if same target remote candidate: -> remove the one with lowest priority */
					if (strcasecmp(remote_candidates[pos].remote_candidate.conn_addr,
						remote_candidates[pos2].remote_candidate.conn_addr)==0)
					{    
						/* useless cpair */                 
						ms_message("ice.c: Removing useless pair (idx=%i)", pos);
						remote_candidates[pos].connectivity_check = ICE_PRUNED;

					}

				}

			}
		}
	}

	/* no currently nominated pair */
	checklist->nominated_pair_index = -1;

	for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
	{
		if (remote_candidates[pos].connectivity_check == ICE_PRUNED)
			continue;
		if (remote_candidates[pos].connectivity_check == ICE_FROZEN)
			remote_candidates[pos].connectivity_check = ICE_WAITING;
	}

	checklist->Ta = 40;
	for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
	{
		if (remote_candidates[pos].connectivity_check == ICE_PRUNED)
			continue;
		if (remote_candidates[pos].connectivity_check == ICE_WAITING)
			count_waiting++;
		count++;
	}
	checklist->RTO = MAX(200, count*checklist->Ta*count_waiting);
	return 0;
}

static int ice_sound_send_stun_request(RtpSession *session, struct IceCheckList *checklist, uint64_t ctime)
{
	struct CandidatePair *remote_candidates = NULL;

	if (checklist==NULL)
		return 0;
	remote_candidates = checklist->cand_pairs;
	if (remote_candidates==NULL)
		return 0;

	{
		struct CandidatePair *cand_pair;
		int media_socket = rtp_session_get_rtp_socket(session);
		StunAddress4 stunServerAddr;
		StunAtrString username;
		StunAtrString password;
		bool_t res;
		int pos;

		/* prepare ONCE tie-break value */
		if (checklist->tiebreak_value==0) {
			checklist->tiebreak_value = random() * (0x7fffffffffffffffLL /0x7fff);
		}

		cand_pair=NULL;
		for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
		{
			cand_pair = &remote_candidates[pos];
			if (cand_pair->connectivity_check == ICE_PRUNED)
			{
				cand_pair=NULL;
				continue;
			}
			if (cand_pair->connectivity_check == ICE_WAITING)
				break;
			if (cand_pair->connectivity_check == ICE_IN_PROGRESS)
				break;
			if (cand_pair->connectivity_check == ICE_SUCCEEDED)
				break;
			cand_pair=NULL;
		}

		if (cand_pair==NULL)
			return 0; /* nothing to do: every pair is FAILED, FROZEN or PRUNED */

		/* start first WAITING pair */
		cand_pair=NULL;
		for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
		{
			cand_pair = &remote_candidates[pos];
			if (cand_pair->connectivity_check == ICE_PRUNED)
			{
				cand_pair=NULL;
				continue;
			}
			if (cand_pair->connectivity_check == ICE_WAITING)
				break;
			cand_pair=NULL;
		}

		if (cand_pair!=NULL)
		{
			cand_pair->connectivity_check = ICE_IN_PROGRESS;
			cand_pair->retransmission_number=0;
			cand_pair->retransmission_time=ctime+checklist->RTO;
			/* keep same rem_controlling for retransmission */
			cand_pair->rem_controlling = checklist->rem_controlling;
		}

		/* try no nominate a pair if we are ready */
		if (cand_pair==NULL && checklist->nominated_pair_index<0)
		{
			for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
			{
				cand_pair = &remote_candidates[pos];
				if (cand_pair->connectivity_check == ICE_PRUNED)
				{
					cand_pair=NULL;
					continue;
				}
				if (cand_pair->connectivity_check == ICE_SUCCEEDED)
				{
					break;
				}
				cand_pair=NULL;
			}

			/* ALWAYS accept "host" candidate that have succeeded */
			if (cand_pair!=NULL
				&& (strcasecmp(cand_pair->remote_candidate.cand_type, "host")==0))
			{
				checklist->nominated_pair_index = pos;
				cand_pair->nominated_pair = 1;
				cand_pair->connectivity_check = ICE_IN_PROGRESS;
				cand_pair->retransmission_number=0;
				cand_pair->retransmission_time=ctime+checklist->RTO;
				/* keep same rem_controlling for retransmission */
				cand_pair->rem_controlling = checklist->rem_controlling;
				/* send a new STUN with USE-CANDIDATE */
				ms_message("ice.c: nominating pair -> %i (%s:%i:%s -> %s:%i:%s) nominated=%s",
					pos,
					cand_pair->local_candidate.conn_addr,
					cand_pair->local_candidate.conn_port,
					cand_pair->local_candidate.cand_type,
					cand_pair->remote_candidate.conn_addr,
					cand_pair->remote_candidate.conn_port,
					cand_pair->remote_candidate.cand_type,
					cand_pair->nominated_pair==0?"FALSE":"TRUE");
				checklist->keepalive_time=ctime+15*1000;
			}
			else if (cand_pair!=NULL)
			{
				struct CandidatePair *cand_pair2=NULL;
				int pos2;
				for (pos2=0;pos2<pos && remote_candidates[pos2].remote_candidate.conn_addr[0]!='\0';pos2++)
				{
					cand_pair2 = &remote_candidates[pos2];
					if (cand_pair2->connectivity_check == ICE_PRUNED)
					{
						cand_pair2=NULL;
						continue;
					}
					if (cand_pair2->connectivity_check == ICE_IN_PROGRESS
						||cand_pair2->connectivity_check == ICE_WAITING)
					{
						break;
					}
					cand_pair2=NULL;
				}

				if (cand_pair2!=NULL)
				{
					/* a better candidate is still tested */
					cand_pair=NULL;
				}
				else
				{
					checklist->nominated_pair_index = pos;
					cand_pair->nominated_pair = 1;
					cand_pair->connectivity_check = ICE_IN_PROGRESS;
					cand_pair->retransmission_number=0;
					cand_pair->retransmission_time=ctime+checklist->RTO;
					/* keep same rem_controlling for retransmission */
					cand_pair->rem_controlling = checklist->rem_controlling;
					/* send a new STUN with USE-CANDIDATE */
					ms_message("ice.c: nominating pair -> %i (%s:%i:%s -> %s:%i:%s) nominated=%s",
						pos,
						cand_pair->local_candidate.conn_addr,
						cand_pair->local_candidate.conn_port,
						cand_pair->local_candidate.cand_type,
						cand_pair->remote_candidate.conn_addr,
						cand_pair->remote_candidate.conn_port,
						cand_pair->remote_candidate.cand_type,
						cand_pair->nominated_pair==0?"FALSE":"TRUE");
					checklist->keepalive_time=ctime+15*1000;
				}
			}
		}

		if (cand_pair==NULL)
		{
			/* no WAITING pair: retransmit after RTO */
			for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
			{
				cand_pair = &remote_candidates[pos];
				if (cand_pair->connectivity_check == ICE_PRUNED)
				{
					cand_pair=NULL;
					continue;
				}
				if (cand_pair->connectivity_check == ICE_IN_PROGRESS
					&& ctime > cand_pair->retransmission_time)
				{
					if (cand_pair->retransmission_number>7)
					{
						ms_message("ice.c: ICE_FAILED for candidate pair! %s:%i -> %s:%i",
							cand_pair->local_candidate.conn_addr,
							cand_pair->local_candidate.conn_port,
							cand_pair->remote_candidate.conn_addr,
							cand_pair->remote_candidate.conn_port);

						cand_pair->connectivity_check = ICE_FAILED;
						cand_pair=NULL;
						continue;
					}

					cand_pair->retransmission_number++;
					cand_pair->retransmission_time=ctime+checklist->RTO;
					break;
				}
				cand_pair=NULL;
			}
		}

		if (cand_pair==NULL)
		{
			if (checklist->nominated_pair_index<0)
				return 0;

			/* send STUN indication each 15 seconds: keepalive */
			if (ctime>checklist->keepalive_time)
			{
				checklist->keepalive_time=ctime+15*1000;
				for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
				{
					cand_pair = &remote_candidates[pos];
					if (cand_pair->connectivity_check == ICE_SUCCEEDED)
					{
						res = stunParseServerName(cand_pair->remote_candidate.conn_addr,
							&stunServerAddr);
						if ( res == TRUE )
						{
							StunMessage req;
							char buf[STUN_MAX_MESSAGE_SIZE];
							int len = STUN_MAX_MESSAGE_SIZE;
							stunServerAddr.port = cand_pair->remote_candidate.conn_port;
							memset(&req, 0, sizeof(StunMessage));
							stunBuildReqSimple( &req, NULL, FALSE, FALSE, 1);
							req.msgHdr.msgType = (STUN_METHOD_BINDING|STUN_INDICATION);
							req.hasFingerprint = TRUE;
							len = stunEncodeMessage( &req, buf, len, NULL);
							sendMessage( media_socket, buf, len, stunServerAddr.addr, stunServerAddr.port );
						}
					}
				}
			}

			return 0;
		}

		username.sizeValue = 0;
		password.sizeValue = 0;

		/* username comes from "ice-ufrag" (rfrag:lfrag) */
		/* ufrag and pwd are in first row only */
		snprintf(username.value, sizeof(username.value), "%s:%s",
			checklist->rem_ice_ufrag,
			checklist->loc_ice_ufrag);
		username.sizeValue = (uint16_t)strlen(username.value);


		snprintf(password.value, sizeof(password.value), "%s",
			checklist->rem_ice_pwd);
		password.sizeValue = (uint16_t)strlen(password.value);


		res = stunParseServerName(cand_pair->remote_candidate.conn_addr,
			&stunServerAddr);
		if ( res == TRUE )
		{
			ms_message("ice.c: STUN REQ (%s) -> %i (%s:%i:%s -> %s:%i:%s) nominated=%s",
				cand_pair->nominated_pair==0?"":"USE-CANDIDATE",
				pos,
				cand_pair->local_candidate.conn_addr,
				cand_pair->local_candidate.conn_port,
				cand_pair->local_candidate.cand_type,
				cand_pair->remote_candidate.conn_addr,
				cand_pair->remote_candidate.conn_port,
				cand_pair->remote_candidate.cand_type,
				cand_pair->nominated_pair==0?"FALSE":"TRUE");
			stunServerAddr.port = cand_pair->remote_candidate.conn_port;
			ice_sendtest(checklist, cand_pair, media_socket, &stunServerAddr, &username, &password,
				&(cand_pair->tid));
		}
	}

	return 0;
}


static int SetSocketBlock (ortp_socket_t sock, bool_t yesno)
{
#if	!defined(_WIN32) && !defined(_WIN32_WCE)
	int flags = fcntl (sock, F_GETFL, 0);
	flags = yesno ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	return fcntl (sock, F_SETFL, flags);
#else
	unsigned long nonBlock = yesno;
	return ioctlsocket(sock, FIONBIO , &nonBlock);
#endif
}

static int MyConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout, bool_t block)
{
	if (sockfd < 0 || addr == NULL || addrlen <= 0 || timeout < 0)
		return -1;

	if (SetSocketBlock(sockfd, TRUE) < 0)
		return -1;

	if (connect(sockfd, addr, addrlen) != 0)
	{   
#if	!defined(_WIN32) && !defined(_WIN32_WCE)
		if (errno == EINPROGRESS)
#else
		if (WSAGetLastError() == WSAEWOULDBLOCK)
#endif	
		{
			struct timeval  waittime;
			fd_set  writefds;
			int  fdnum;

			waittime.tv_sec  = timeout/1000;
			waittime.tv_usec = (timeout%1000)*1000;

			FD_ZERO(&writefds);
			FD_SET(sockfd, &writefds);

			fdnum = select (sockfd + 1, NULL, &writefds, NULL, &waittime);
			if (fdnum < 0)
			{
				// error
				SetSocketBlock(sockfd, block);
				return -1;
			}
			else if (fdnum == 0)
			{
				// time out
				SetSocketBlock(sockfd, block);
				return -1;
			}
		}
		else
		{
			SetSocketBlock(sockfd, block);
			return -1;
		}
	}

	SetSocketBlock(sockfd, block);
	return 0;
}

#if 1
static int
_ice_get_localip_for (struct sockaddr_storage *saddr, size_t saddr_len, char *loc, int size)
{
	int err, tmp;
	int sock;
	struct sockaddr_storage addr;
	socklen_t addr_len;

	strcpy (loc, "127.0.0.1");    /* always fallback to local loopback */

	sock = socket (saddr->ss_family, SOCK_STREAM, IPPROTO_TCP);
	tmp = 1;
	err = setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &tmp, sizeof (int));
	if (err < 0)
	{
		ms_error("ice.c: Error in setsockopt");
		closesocket (sock);
		return -1;
	}
	err = MyConnect (sock, (struct sockaddr*)saddr, saddr_len, 2000, FALSE);
	if (err < 0)
	{
		ms_error("ice.c: Error in connect");
		closesocket (sock);
		return -1;
	}
	addr_len = sizeof (addr);
	err = getsockname (sock, (struct sockaddr *) &addr, (socklen_t*)&addr_len);
	if (err != 0)
	{
		ms_error("ice.c: Error in getsockname");
		closesocket (sock);
		return -1;
	}
	else
	{
		struct sockaddr_in *p = (struct sockaddr_in*) &addr;
		ms_warning("_ice_get_localip_for : [%d] %s:%d", p->sin_family, inet_ntoa(p->sin_addr), p->sin_port);
	}

	err = getnameinfo ((struct sockaddr *) &addr, addr_len, loc, size, NULL, 0, NI_NUMERICHOST);
	if (err != 0)
	{
		ms_error("ice.c: Error in getnameinfo");
		closesocket (sock);
		return -1;
	}
	closesocket (sock);
	/* ms_message("ice.c: Outgoing interface for sending STUN answer is %s", loc); */
	return 0;
}

#endif

static void
_ice_createErrorResponse(StunMessage *response, int cl, int number, const char* msg)
{
	response->msgHdr.msgType = (STUN_METHOD_BINDING | STUN_ERR_RESP);
	response->hasErrorCode = TRUE;
	response->errorCode.errorClass = cl;
	response->errorCode.number = number;
	strcpy(response->errorCode.reason, msg);
	response->errorCode.sizeReason = strlen(msg);
	response->hasFingerprint = TRUE;
}

static int ice_process_stun_message(RtpSession *session, struct IceCheckList *checklist, OrtpEvent *evt)
{
	struct CandidatePair *remote_candidates = NULL;
	StunMessage msg;
	bool_t res;
	int highest_priority_success=-1;
	OrtpEventData *evt_data = ortp_event_get_data(evt);
	mblk_t *mp = evt_data->packet;
	struct sockaddr_in *udp_remote;
	char src6host[NI_MAXHOST];
	int recvport = 0;
	int i;

	udp_remote = (struct sockaddr_in*)&evt_data->ep->addr;

	memset( &msg, 0 , sizeof(msg) );
	res = stunParseMessage((char*)mp->b_rptr, mp->b_wptr-mp->b_rptr, &msg);
	if (!res)
	{
		ms_error("ice.c: Malformed STUN packet.");
		return -1;
	}

	if (checklist==NULL)
	{
		ms_error("ice.c: dropping STUN packet: ice is not configured");
		return -1;
	}

	remote_candidates = checklist->cand_pairs;
	if (remote_candidates==NULL)
	{
		ms_error("ice.c: dropping STUN packet: ice is not configured");
		return -1;
	}

	/* prepare ONCE tie-break value */
	if (checklist->tiebreak_value==0) {
		checklist->tiebreak_value = random() * (0x7fffffffffffffffLL/0x7fff);
	}

	memset (src6host, 0, sizeof (src6host));

	{
		struct sockaddr_storage *aaddr = (struct sockaddr_storage *)&evt_data->ep->addr;
		if (aaddr->ss_family==AF_INET)
			recvport = ntohs (((struct sockaddr_in *) udp_remote)->sin_port);
		else
			recvport = ntohs (((struct sockaddr_in6 *) &evt_data->ep->addr)->sin6_port);
	}
	i = getnameinfo ((struct sockaddr*)&evt_data->ep->addr, evt_data->ep->addrlen,
		src6host, NI_MAXHOST,
		NULL, 0, NI_NUMERICHOST);
	if (i != 0)
	{
		ms_error("ice.c: Error with getnameinfo");
		return -1;
	}

	if (STUN_IS_REQUEST(msg.msgHdr.msgType))
		ms_message("ice.c: STUN_CONNECTIVITYCHECK: Request received from: %s:%i",
		src6host, recvport);
	else if (STUN_IS_INDICATION(msg.msgHdr.msgType))
		ms_message("ice.c: SUN_INDICATION: Request Indication received from: %s:%i",
		src6host, recvport);
	else
		ms_message("ice.c: STUN_ANSWER: Answer received from: %s:%i",
		src6host, recvport);

	{
		int pos;
		for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
		{
			struct CandidatePair *cand_pair = &remote_candidates[pos];

			if (cand_pair->connectivity_check == ICE_SUCCEEDED)
			{
				highest_priority_success=pos;
				break;
			}
		}
	}

	if (STUN_IS_INDICATION(msg.msgHdr.msgType))
	{
		ms_message("ice.c: STUN INDICATION <- (?:?:? <- %s:%i:?)", src6host, recvport);
		return 0;
	}
	else if (STUN_IS_REQUEST(msg.msgHdr.msgType))
	{
		StunMessage resp;
		StunAtrString hmacPassword;
		StunAddress4 remote_addr;
		int rtp_socket;

		memset( &resp, 0 , sizeof(resp));
		remote_addr.addr = ntohl(udp_remote->sin_addr.s_addr);
		remote_addr.port = ntohs(udp_remote->sin_port);

		rtp_socket = rtp_session_get_rtp_socket(session);

		resp.msgHdr.magic_cookie = ntohl(msg.msgHdr.magic_cookie);
		for (i=0; i<12; i++ )
		{
			resp.msgHdr.tr_id.octet[i] = msg.msgHdr.tr_id.octet[i];
		}

		/* check mandatory params */

		if (!msg.hasUsername)
		{
			char buf[STUN_MAX_MESSAGE_SIZE];
			int len = sizeof(buf);
			ms_error("ice.c: STUN REQ <- Missing USERNAME attribute in connectivity check");
			_ice_createErrorResponse(&resp, 4, 32, "Missing USERNAME attribute");
			len = stunEncodeMessage(&resp, buf, len, &hmacPassword );
			if (len)
				sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
			return -1;
		}
		if (!msg.hasMessageIntegrity)
		{
			char buf[STUN_MAX_MESSAGE_SIZE];
			int len = sizeof(buf);
			ms_error("ice.c: STUN REQ <- Missing MESSAGEINTEGRITY attribute in connectivity check");
			_ice_createErrorResponse(&resp, 4, 1, "Missing MESSAGEINTEGRITY attribute");
			len = stunEncodeMessage(&resp, buf, len, &hmacPassword );
			if (len)
				sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
			return -1;
		}

		/*
		The password associated with that transport address ID is used to verify
		the MESSAGE-INTEGRITY attribute, if one was present in the request.
		*/
		{
			char hmac[20];
			/* remove length of fingerprint if present */
			if (msg.hasFingerprint==TRUE)
			{
				char *lenpos = (char *)mp->b_rptr + sizeof(uint16_t);
				uint16_t newlen = htons(msg.msgHdr.msgLength-8); /* remove fingerprint size */
				memcpy(lenpos, &newlen, sizeof(uint16_t));
				stunCalculateIntegrity_shortterm(hmac, (char*)mp->b_rptr, mp->b_wptr-mp->b_rptr-24-8, checklist->loc_ice_pwd);
			}
			else
				stunCalculateIntegrity_shortterm(hmac, (char*)mp->b_rptr, mp->b_wptr-mp->b_rptr-24, checklist->loc_ice_pwd);
			if (memcmp(msg.messageIntegrity.hash, hmac, 20)!=0)
			{
				char buf[STUN_MAX_MESSAGE_SIZE];
				int len = sizeof(buf);
				ms_error("ice.c: STUN REQ <- Wrong MESSAGEINTEGRITY attribute in connectivity check");
				_ice_createErrorResponse(&resp, 4, 1, "Wrong MESSAGEINTEGRITY attribute");
				len = stunEncodeMessage(&resp, buf, len, &hmacPassword );
				if (len)
					sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
				return -1;
			}
			if (msg.hasFingerprint==TRUE)
			{
				char *lenpos = (char *)mp->b_rptr + sizeof(uint16_t);
				uint16_t newlen = htons(msg.msgHdr.msgLength); /* add back fingerprint size */
				memcpy(lenpos, &newlen, sizeof(uint16_t));
			}
		}


		/* 7.2.1.1. Detecting and Repairing Role Conflicts */
		/* TODO */
		if (!msg.hasIceControlling && !msg.hasIceControlled)
		{
			char buf[STUN_MAX_MESSAGE_SIZE];
			int len = sizeof(buf);
			ms_error("ice.c: STUN REQ <- Missing either ICE-CONTROLLING or ICE-CONTROLLED attribute");
			_ice_createErrorResponse(&resp, 4, 87, "Missing either ICE-CONTROLLING or ICE-CONTROLLED attribute");
			len = stunEncodeMessage(&resp, buf, len, &hmacPassword );
			if (len)
				sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
			return -1;
		}

		if (checklist->rem_controlling==0 && msg.hasIceControlling) {
			/* If the agent's tie-breaker is larger than or equal
			to the contents of the ICE-CONTROLLING attribute
			-> send 487, and do not change ROLE */
			if (checklist->tiebreak_value >= msg.iceControlling.value) {
				char buf[STUN_MAX_MESSAGE_SIZE];
				int len = sizeof(buf);
				ms_error("ice.c: STUN REQ <- 487 Role Conflict");
				_ice_createErrorResponse(&resp, 4, 87, "Role Conflict");
				len = stunEncodeMessage(&resp, buf, len, &hmacPassword );
				if (len)
					sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
				return -1;
			}
			else {
				int pos;
				for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
				{
					/* controller agent */
					uint64_t G = remote_candidates[pos].remote_candidate.priority;
					/* controlled agent */	
					uint64_t D = remote_candidates[pos].local_candidate.priority;
					remote_candidates[pos].pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
				}
				checklist->rem_controlling = 1;
				/* reset all to initial WAITING state? */
				ms_message("ice.c: STUN REQ <- tiebreaker -> reset all to ICE_WAITING state");
				for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
				{
					if (remote_candidates[pos].connectivity_check == ICE_PRUNED)
						continue;
					remote_candidates[pos].connectivity_check = ICE_WAITING;
					memset(&remote_candidates[pos].tid , 0, sizeof(remote_candidates[pos].tid));
					remote_candidates[pos].retransmission_time = 0;
					remote_candidates[pos].retransmission_number = 0;
				}
			}
		}

		if (checklist->rem_controlling==1 && msg.hasIceControlled) {

			/* If the agent's tie-breaker is larger than or equal
			to the contents of the ICE-CONTROLLED attribute
			-> change ROLE */
			if (checklist->tiebreak_value >= msg.iceControlled.value) {
				int pos;
				for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
				{
					/* controller agent */
					uint64_t G = remote_candidates[pos].local_candidate.priority;
					/* controlled agent */	
					uint64_t D = remote_candidates[pos].remote_candidate.priority;
					remote_candidates[pos].pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
				}
				checklist->rem_controlling = 0;
				/* reset all to initial WAITING state? */
				ms_message("ice.c: STUN REQ <- tiebreaker -> reset all to ICE_WAITING state");
				for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
				{
					if (remote_candidates[pos].connectivity_check == ICE_PRUNED)
						continue;
					remote_candidates[pos].connectivity_check = ICE_WAITING;
					memset(&remote_candidates[pos].tid , 0, sizeof(remote_candidates[pos].tid));
					remote_candidates[pos].retransmission_time = 0;
					remote_candidates[pos].retransmission_number = 0;
				}
			}
			else {
				char buf[STUN_MAX_MESSAGE_SIZE];
				int len = sizeof(buf);
				ms_error("ice.c: STUN REQ <- 487 Role Conflict");
				_ice_createErrorResponse(&resp, 4, 87, "Role Conflict");
				len = stunEncodeMessage(&resp, buf, len, &hmacPassword );
				if (len)
					sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
				return -1;
			}
		}

		{
			struct CandidatePair *cand_pair;
			int pos;
			cand_pair=NULL;
			for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
			{
				cand_pair = &remote_candidates[pos]; 
				/* connectivity check is coming from a known remote candidate?
				we should also check the port...
				*/
				if (strcmp(cand_pair->remote_candidate.conn_addr, src6host)==0
					&& cand_pair->remote_candidate.conn_port==recvport)
				{
					ms_message("ice.c: STUN REQ (%s) <- %i (%s:%i:%s <- %s:%i:%s) from known peer",
						msg.hasUseCandidate==0?"":"USE-CANDIDATE",
						pos,
						cand_pair->local_candidate.conn_addr,
						cand_pair->local_candidate.conn_port,
						cand_pair->local_candidate.cand_type,
						cand_pair->remote_candidate.conn_addr,
						cand_pair->remote_candidate.conn_port,
						cand_pair->remote_candidate.cand_type);
					if (cand_pair->connectivity_check==ICE_FROZEN
						|| cand_pair->connectivity_check==ICE_IN_PROGRESS
						|| cand_pair->connectivity_check==ICE_FAILED)
					{
						cand_pair->connectivity_check = ICE_WAITING;
						if (msg.hasUseCandidate==TRUE && checklist->rem_controlling==0)
							cand_pair->nominated_pair = 1;
					}
					else if (cand_pair->connectivity_check==ICE_SUCCEEDED)
					{
						if (msg.hasUseCandidate==TRUE && checklist->rem_controlling==0)
						{
							cand_pair->nominated_pair = 1;

							/* USE-CANDIDATE is in STUN request and we already succeeded on that link */
							ms_message("ice.c: ICE CONCLUDED == %i (%s:%i:%s <- %s:%i:%s nominated=%s)",
								pos,
								cand_pair->local_candidate.conn_addr,
								cand_pair->local_candidate.conn_port,
								cand_pair->local_candidate.cand_type,
								cand_pair->remote_candidate.conn_addr,
								cand_pair->remote_candidate.conn_port,
								cand_pair->remote_candidate.cand_type,
								cand_pair->nominated_pair==0?"FALSE":"TRUE");
							memcpy(&session->rtp.rem_addr, &evt_data->ep->addr, evt_data->ep->addrlen);
							session->rtp.rem_addrlen=evt_data->ep->addrlen;
						}
					}
					break;
				}
				cand_pair=NULL;
			}
			if (cand_pair==NULL)
			{
				struct CandidatePair new_pair;
				memset(&new_pair, 0, sizeof(struct CandidatePair));

				ms_message("ice.c: STUN REQ <- connectivity check received from an unknow candidate (%s:%i)", src6host, recvport);
				/* TODO: add the peer-reflexive candidate */

				memcpy(&new_pair.local_candidate, &remote_candidates[0].local_candidate, sizeof(new_pair.local_candidate));

				new_pair.remote_candidate.foundation = 6;
				new_pair.remote_candidate.component_id = remote_candidates[0].remote_candidate.component_id;

				/* -> no known base address for peer */

				new_pair.remote_candidate.conn_port = recvport;
				snprintf(new_pair.remote_candidate.conn_addr, sizeof(new_pair.remote_candidate.conn_addr),
					"%s", src6host);

				/* take it from PRIORITY STUN attr */
				new_pair.remote_candidate.priority = msg.priority.priority;
				if (new_pair.remote_candidate.priority==0)
				{
					uint32_t type_preference = 110;
					uint32_t interface_preference = 255;
					uint32_t stun_priority=255;
					new_pair.remote_candidate.priority = (type_preference << 24) | (interface_preference << 16) | (stun_priority << 8)
						| (256 - new_pair.remote_candidate.component_id);
				}

				snprintf(new_pair.remote_candidate.cand_type, sizeof(cand_pair->remote_candidate.cand_type),
					"prflx");
				snprintf (new_pair.remote_candidate.transport,
								sizeof (new_pair.remote_candidate.transport),
								"UDP");

				if (checklist->rem_controlling==0)
				{
					uint64_t G = new_pair.local_candidate.priority;
					/* controlled agent */	
					uint64_t D = new_pair.remote_candidate.priority;
					new_pair.pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
				}
				else
				{
					uint64_t G = new_pair.remote_candidate.priority;
					/* controlled agent */	
					uint64_t D = new_pair.local_candidate.priority;
					new_pair.pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
				}
				new_pair.connectivity_check = ICE_WAITING;
				/* insert new pair candidate */
				if (msg.hasUseCandidate==TRUE && checklist->rem_controlling==0)
				{
					new_pair.nominated_pair = 1;
				}

				for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
				{
					if (pos==9)
					{
						ms_message("ice.c: STUN REQ (%s) <- X (%s:%i:%s <- %s:%i:%s) no room for new remote reflexive candidate",
							msg.hasUseCandidate==0?"":"USE-CANDIDATE",
							new_pair.local_candidate.conn_addr,
							new_pair.local_candidate.conn_port,
							new_pair.local_candidate.cand_type,
							new_pair.remote_candidate.conn_addr,
							new_pair.remote_candidate.conn_port,
							new_pair.remote_candidate.cand_type);
						break;
					}
					if (new_pair.pair_priority > remote_candidates[pos].pair_priority)
					{
						/* move upper data */
						memmove(&remote_candidates[pos+1], &remote_candidates[pos], sizeof(struct CandidatePair)*(10-pos-1));
						memcpy(&remote_candidates[pos], &new_pair, sizeof(struct CandidatePair));

						if (checklist->nominated_pair_index>=pos)
							checklist->nominated_pair_index++;
						ms_message("ice.c: STUN REQ (%s) <- %i (%s:%i:%s <- %s:%i:%s) new learned remote reflexive candidate",
							msg.hasUseCandidate==0?"":"USE-CANDIDATE",
							pos,
							new_pair.local_candidate.conn_addr,
							new_pair.local_candidate.conn_port,
							new_pair.local_candidate.cand_type,
							new_pair.remote_candidate.conn_addr,
							new_pair.remote_candidate.conn_port,
							new_pair.remote_candidate.cand_type);
						break;
					}
				}
			}
		}

		{
			uint32_t cookie = 0x2112A442;
			resp.hasXorMappedAddress = TRUE;
			resp.xorMappedAddress.ipv4.port = remote_addr.port^(cookie>>16);
			resp.xorMappedAddress.ipv4.addr = remote_addr.addr^cookie;
		}

		resp.msgHdr.msgType = (STUN_METHOD_BINDING | STUN_SUCCESS_RESP);

		resp.hasUsername = TRUE;
		memcpy(resp.username.value, msg.username.value, msg.username.sizeValue );
		resp.username.sizeValue = msg.username.sizeValue;

		/* ? any messageintegrity in response? */
		resp.hasMessageIntegrity = TRUE;

		{
			const char serverName[] = "mediastreamer2 " STUN_VERSION;
			resp.hasSoftware = TRUE;
			memcpy( resp.softwareName.value, serverName, sizeof(serverName));
			resp.softwareName.sizeValue = sizeof(serverName);
		}

		resp.hasFingerprint = TRUE;

		{
			char buf[STUN_MAX_MESSAGE_SIZE];
			int len = sizeof(buf);
			len = stunEncodeMessage( &resp, buf, len, &hmacPassword );
			if (len)
				sendMessage( rtp_socket, buf, len, remote_addr.addr, remote_addr.port);
		}
	}
	else if (STUN_IS_SUCCESS_RESP(msg.msgHdr.msgType))
	{
		/* set state to RECV-VALID or VALID */
		StunMessage resp;
		StunAddress4 mappedAddr;
		memset(&resp, 0, sizeof(StunMessage));
		res = stunParseMessage((char*)mp->b_rptr, mp->b_wptr-mp->b_rptr,
			&resp );
		if (!res)
		{
			ms_error("ice.c: STUN RESP <- Bad format for STUN answer.");
			return -1;
		}

		if (resp.hasXorMappedAddress!=TRUE)
		{
			ms_error("ice.c: STUN RESP <- Missing XOR-MAPPED-ADDRESS in STUN answer.");		  
			return -1;
		}

		{
			uint32_t cookie = 0x2112A442;
			uint16_t cookie16 = 0x2112A442 >> 16;
			mappedAddr.port = resp.xorMappedAddress.ipv4.port^cookie16;
			mappedAddr.addr = resp.xorMappedAddress.ipv4.addr^cookie;
		}

		{
			struct in_addr inaddr;
			char mapped_addr[64];
			struct CandidatePair *cand_pair=NULL;
			int pos;
			inaddr.s_addr = htonl (mappedAddr.addr);
			snprintf(mapped_addr, sizeof(mapped_addr),
				"%s", inet_ntoa (inaddr));

			for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
			{
				cand_pair = &remote_candidates[pos];

				if (memcmp(&(cand_pair->tid), &(resp.msgHdr.tr_id), sizeof(resp.msgHdr.tr_id))==0)
				{
					break;
				}
				cand_pair = NULL;
			}

			if (cand_pair==NULL)
			{
				ms_message("ice.c: STUN RESP (%s) <- no transaction for STUN answer?",
					msg.hasUseCandidate==0?"":"USE-CANDIDATE");
			}
			else if (strcmp(src6host, cand_pair->remote_candidate.conn_addr)!=0
				|| recvport!=cand_pair->remote_candidate.conn_port)
			{
				/* 7.1.2.2.  Success Cases
				-> must be a security issue: refuse non-symmetric answer */
				ms_message("ice.c: STUN RESP (%s) <- %i (%s:%i:%s <- %s:%i:%s nominated=%s) refused because non-symmetric",
					msg.hasUseCandidate==0?"":"USE-CANDIDATE",
					pos,
					cand_pair->local_candidate.conn_addr,
					cand_pair->local_candidate.conn_port,
					cand_pair->local_candidate.cand_type,
					cand_pair->remote_candidate.conn_addr,
					cand_pair->remote_candidate.conn_port,
					cand_pair->remote_candidate.cand_type,
					cand_pair->nominated_pair==0?"FALSE":"TRUE");
				cand_pair->connectivity_check = ICE_FAILED;
			}
			else
			{
				/* Youhouhouhou */
				ms_message("ice.c: STUN RESP (%s) <- %i (%s:%i:%s <- %s:%i:%s nominated=%s)",
					msg.hasUseCandidate==0?"":"USE-CANDIDATE",
					pos,
					cand_pair->local_candidate.conn_addr,
					cand_pair->local_candidate.conn_port,
					cand_pair->local_candidate.cand_type,
					cand_pair->remote_candidate.conn_addr,
					cand_pair->remote_candidate.conn_port,
					cand_pair->remote_candidate.cand_type,
					cand_pair->nominated_pair==0?"FALSE":"TRUE");
				if (cand_pair->connectivity_check != ICE_SUCCEEDED)
				{
					if (checklist->rem_controlling==1 && cand_pair->nominated_pair>0)
					{
						/* USE-CANDIDATE was in previous STUN request sent */
						ms_message("ice.c: ICE CONCLUDED == %i (%s:%i:%s <- %s:%i:%s nominated=%s)",
							pos,
							cand_pair->local_candidate.conn_addr,
							cand_pair->local_candidate.conn_port,
							cand_pair->local_candidate.cand_type,
							cand_pair->remote_candidate.conn_addr,
							cand_pair->remote_candidate.conn_port,
							cand_pair->remote_candidate.cand_type,
							cand_pair->nominated_pair==0?"FALSE":"TRUE");
						memcpy(&session->rtp.rem_addr, &evt_data->ep->addr, evt_data->ep->addrlen);
						session->rtp.rem_addrlen=evt_data->ep->addrlen;
					}

					if (cand_pair->nominated_pair>0 && checklist->rem_controlling==0)
					{
						/* USE-CANDIDATE is in STUN request and we already succeeded on that link */
						ms_message("ice.c: ICE CONCLUDED == %i (%s:%i:%s <- %s:%i:%s nominated=%s)",
							pos,
							cand_pair->local_candidate.conn_addr,
							cand_pair->local_candidate.conn_port,
							cand_pair->local_candidate.cand_type,
							cand_pair->remote_candidate.conn_addr,
							cand_pair->remote_candidate.conn_port,
							cand_pair->remote_candidate.cand_type,
							cand_pair->nominated_pair==0?"FALSE":"TRUE");
						memcpy(&session->rtp.rem_addr, &evt_data->ep->addr, evt_data->ep->addrlen);
						session->rtp.rem_addrlen=evt_data->ep->addrlen;
					}

					cand_pair->connectivity_check = ICE_FAILED;
					if (mappedAddr.port == cand_pair->local_candidate.conn_port
						&& strcmp(mapped_addr, cand_pair->local_candidate.conn_addr)==0)
					{
						/* no peer-reflexive candidate was discovered */
						cand_pair->connectivity_check = ICE_SUCCEEDED;
					}
					else
					{
						int pos2;
						for (pos2=0;pos2<10 && remote_candidates[pos2].remote_candidate.conn_addr[0]!='\0';pos2++)
						{
							if (mappedAddr.port == remote_candidates[pos2].local_candidate.conn_port
								&& strcmp(mapped_addr, remote_candidates[pos2].local_candidate.conn_addr)==0
								&& cand_pair->remote_candidate.conn_port == remote_candidates[pos2].remote_candidate.conn_port
								&& strcmp(cand_pair->remote_candidate.conn_addr, remote_candidates[pos2].remote_candidate.conn_addr)==0)
							{
								if (remote_candidates[pos2].connectivity_check==ICE_PRUNED
									||remote_candidates[pos2].connectivity_check==ICE_FROZEN
									||remote_candidates[pos2].connectivity_check==ICE_FAILED
									|| remote_candidates[pos2].connectivity_check==ICE_IN_PROGRESS)
									remote_candidates[pos2].connectivity_check = ICE_WAITING; /* trigger check */
								/*
								ms_message("ice.c: STUN RESP (%s) <- %i (%s:%i:%s <- %s:%i:%s) found candidate pair matching XOR-MAPPED-ADDRESS",
									msg.hasUseCandidate==0?"":"USE-CANDIDATE",
									pos,
									cand_pair->local_candidate.conn_addr,
									cand_pair->local_candidate.conn_port,
									cand_pair->local_candidate.cand_type,
									cand_pair->remote_candidate.conn_addr,
									cand_pair->remote_candidate.conn_port,
									cand_pair->remote_candidate.cand_type);
									*/
								break;
							}
						}
						if (pos2==10 || remote_candidates[pos2].remote_candidate.conn_addr[0]=='\0')
						{
							struct CandidatePair new_pair;
							memset(&new_pair, 0, sizeof(struct CandidatePair));

							/* 7.1.2.2.1.  Discovering Peer Reflexive Candidates */
							/* If IP & port were different than mappedAddr, there was A NAT
							between me and remote destination. */
							memcpy(&new_pair.remote_candidate, &cand_pair->remote_candidate, sizeof(new_pair.remote_candidate));

							new_pair.local_candidate.foundation = 6;
							new_pair.local_candidate.component_id = cand_pair->local_candidate.component_id;

							/* what is my base address? */
							new_pair.local_candidate.rel_port = cand_pair->local_candidate.conn_port;
							snprintf(new_pair.local_candidate.rel_addr, sizeof(new_pair.local_candidate.rel_addr),
								"%s", cand_pair->local_candidate.conn_addr);

							new_pair.local_candidate.conn_port = mappedAddr.port;
							snprintf(new_pair.local_candidate.conn_addr, sizeof(new_pair.local_candidate.conn_addr),
								"%s", mapped_addr);

							new_pair.remote_candidate.priority = (110 << 24) | (255 << 16) | (255 << 8)
								| (256 - new_pair.remote_candidate.component_id);

							snprintf(new_pair.local_candidate.cand_type, sizeof(cand_pair->local_candidate.cand_type),
								"prflx");
							snprintf (new_pair.local_candidate.transport,
											sizeof (new_pair.local_candidate.transport),
											"UDP");

							if (checklist->rem_controlling==0)
							{
								uint64_t G = new_pair.local_candidate.priority;
								/* controlled agent */	
								uint64_t D = new_pair.remote_candidate.priority;
								new_pair.pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
							}
							else
							{
								uint64_t G = new_pair.remote_candidate.priority;
								/* controlled agent */	
								uint64_t D = new_pair.local_candidate.priority;
								new_pair.pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
							}
							new_pair.connectivity_check = ICE_WAITING;
							/* insert new pair candidate */
							for (pos2=0;pos2<10 && remote_candidates[pos2].remote_candidate.conn_addr[0]!='\0';pos2++)
							{
								if (pos2==9)
								{
									ms_message("ice.c: STUN RESP (%s) <- %i (%s:%i:%s <- %s:%i:%s) no room for new local peer-reflexive candidate",
										msg.hasUseCandidate==0?"":"USE-CANDIDATE",
										pos2,
										new_pair.local_candidate.conn_addr,
										new_pair.local_candidate.conn_port,
										new_pair.local_candidate.cand_type,
										new_pair.remote_candidate.conn_addr,
										new_pair.remote_candidate.conn_port,
										new_pair.remote_candidate.cand_type);
									break;
								}
								if (new_pair.pair_priority > remote_candidates[pos2].pair_priority)
								{
									/* move upper data */
									memmove(&remote_candidates[pos2+1], &remote_candidates[pos2], sizeof(struct CandidatePair)*(10-pos2-1));
									memcpy(&remote_candidates[pos2], &new_pair, sizeof(struct CandidatePair));

									if (checklist->nominated_pair_index>=pos2)
										checklist->nominated_pair_index++;
									ms_message("ice.c: STUN RESP (%s) <- %i (%s:%i:%s <- %s:%i:%s) new discovered local peer-reflexive candidate",
										msg.hasUseCandidate==0?"":"USE-CANDIDATE",
										pos2,
										new_pair.local_candidate.conn_addr,
										new_pair.local_candidate.conn_port,
										new_pair.local_candidate.cand_type,
										new_pair.remote_candidate.conn_addr,
										new_pair.remote_candidate.conn_port,
										new_pair.remote_candidate.cand_type);
									break;
								}
							}
						}
					}
				}
			}
		}
	}
	else if (STUN_IS_ERR_RESP(msg.msgHdr.msgType))
	{
		int pos;
		StunMessage resp;
		memset(&resp, 0, sizeof(StunMessage));
		res = stunParseMessage((char*)mp->b_rptr, mp->b_wptr-mp->b_rptr,
			&resp );
		if (!res)
		{
			ms_error("ice.c: ERROR_RESPONSE: Bad format for STUN answer.");
			return -1;
		}

		for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
		{
			struct CandidatePair *cand_pair = &remote_candidates[pos];

			if (memcmp(&(cand_pair->tid), &(resp.msgHdr.tr_id), sizeof(resp.msgHdr.tr_id))==0)
			{
				cand_pair->connectivity_check = ICE_FAILED;
				ms_message("ice.c: ERROR_RESPONSE: ICE_FAILED for candidate pair! %s:%i -> %s:%i",
					cand_pair->local_candidate.conn_addr,
					cand_pair->local_candidate.conn_port,
					cand_pair->remote_candidate.conn_addr,
					cand_pair->remote_candidate.conn_port);
				if (resp.hasErrorCode==TRUE && resp.errorCode.errorClass==4 && resp.errorCode.number==87)
				{
					if (remote_candidates[pos].rem_controlling==1)
					{
						int pos2;
						for (pos2=0;pos2<10 && remote_candidates[pos2].remote_candidate.conn_addr[0]!='\0';pos2++)
						{
							/* controller agent */
							uint64_t G = remote_candidates[pos2].local_candidate.priority;
							/* controlled agent */	
							uint64_t D = remote_candidates[pos2].remote_candidate.priority;
							remote_candidates[pos2].pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
						}
						checklist->rem_controlling=0;
					}
					else
					{
						int pos2;
						for (pos2=0;pos2<10 && remote_candidates[pos2].remote_candidate.conn_addr[0]!='\0';pos2++)
						{
							/* controller agent */
							uint64_t G = remote_candidates[pos2].remote_candidate.priority;
							/* controlled agent */	
							uint64_t D = remote_candidates[pos2].local_candidate.priority;
							remote_candidates[pos2].pair_priority = (MIN(G, D))<<32 | (MAX(G, D))<<1 | (G>D?1:0);
						}
						checklist->rem_controlling=1;
					}
					/* reset all to initial WAITING state? */
					ms_message("ice.c: ERROR_RESPONSE: 487 -> reset all to ICE_WAITING state");
					for (pos=0;pos<10 && remote_candidates[pos].remote_candidate.conn_addr[0]!='\0';pos++)
					{
						if (remote_candidates[pos].connectivity_check == ICE_PRUNED)
							continue;
						remote_candidates[pos].connectivity_check = ICE_WAITING;
						memset(&remote_candidates[pos].tid , 0, sizeof(remote_candidates[pos].tid));
						remote_candidates[pos].retransmission_time = 0;
						remote_candidates[pos].retransmission_number = 0;
					}
				}
			}
		}
	}

	return 0;
}

struct IceData {
	RtpSession *session;
	OrtpEvQueue *ortp_event;
	struct IceCheckList *check_lists;	/* table of 10 cpair */
	int rate;
};

typedef struct IceData IceData;

static void ice_init(MSFilter * f)
{
	IceData *d = (IceData *)ms_new(IceData, 1);

	d->ortp_event = ortp_ev_queue_new();
	d->session = NULL;
	d->check_lists = NULL;
	d->rate = 8000;
	f->data = d;
}

static void ice_postprocess(MSFilter * f)
{
	IceData *d = (IceData *) f->data;
	if (d->session!=NULL && d->ortp_event!=NULL)
	  rtp_session_unregister_event_queue(d->session, d->ortp_event);
}

static void ice_uninit(MSFilter * f)
{
	IceData *d = (IceData *) f->data;
	if (d->ortp_event!=NULL)
	  ortp_ev_queue_destroy(d->ortp_event);
	ms_free(f->data);
}

static int ice_set_session(MSFilter * f, void *arg)
{
	IceData *d = (IceData *) f->data;
	RtpSession *s = (RtpSession *) arg;
	PayloadType *pt = rtp_profile_get_payload(rtp_session_get_profile(s),
											  rtp_session_get_recv_payload_type
											  (s));
	if (pt != NULL) {
		if (strcasecmp("g722", pt->mime_type)==0 )
			d->rate=8000;
		else d->rate = pt->clock_rate;
	} else {
		ms_warning("Receiving undefined payload type ?");
	}
	d->session = s;

	return 0;
}

static int ice_set_sdpcandidates(MSFilter * f, void *arg)
{
	IceData *d = (IceData *) f->data;
	struct IceCheckList *scs = NULL;

	if (d == NULL)
		return -1;

	scs = (struct IceCheckList *) arg;
	d->check_lists = scs;
	ice_restart(d->check_lists);
	return 0;
}

static void ice_preprocess(MSFilter * f){
	IceData *d = (IceData *) f->data;
	if (d->session!=NULL && d->ortp_event!=NULL)
		rtp_session_register_event_queue(d->session, d->ortp_event);
}

static void ice_process(MSFilter * f)
{
	IceData *d = (IceData *) f->data;

	if (d->session == NULL)
		return;

	/* check received STUN request */
	if (d->ortp_event!=NULL)
	{
		OrtpEvent *evt = ortp_ev_queue_get(d->ortp_event);

		while (evt != NULL) {
			if (ortp_event_get_type(evt) ==
				ORTP_EVENT_STUN_PACKET_RECEIVED) {
				ice_process_stun_message(d->session, d->check_lists, evt);
			}
			if (ortp_event_get_type(evt) ==
				ORTP_EVENT_TELEPHONE_EVENT) {
			}

			ortp_event_destroy(evt);
			evt = ortp_ev_queue_get(d->ortp_event);
		}
	}

	ice_sound_send_stun_request(d->session, d->check_lists, f->ticker->time);
}

static MSFilterMethod ice_methods[] = {
	{MS_ICE_SET_SESSION, ice_set_session},
	{MS_ICE_SET_CANDIDATEPAIRS, ice_set_sdpcandidates},
	{0, NULL}
};

#ifdef _MSC_VER

MSFilterDesc ms_ice_desc = {
	MS_ICE_ID,
	"MSIce",
	N_("ICE filter"),
	MS_FILTER_OTHER,
	NULL,
	0,
	0,
	ice_init,
	ice_preprocess,
	ice_process,
	ice_postprocess,
	ice_uninit,
	ice_methods
};

#else

MSFilterDesc ms_ice_desc = {
	.id = MS_ICE_ID,
	.name = "MSIce",
	.text = N_("ICE filter"),
	.category = MS_FILTER_OTHER,
	.ninputs = 0,
	.noutputs = 0,
	.init = ice_init,
	.preprocess = ice_preprocess,
	.process = ice_process,
	.postprocess=ice_postprocess,
	.uninit = ice_uninit,
	.methods = ice_methods
};

#endif

MS_FILTER_DESC_EXPORT(ms_ice_desc)


static char user_id[4];

static uint32_t get_time (int initflag);
static int random_port();
static void random_buffer(char *buf, int len);
static Socket open_port(uint16_t port, uint32_t interface_addr);
static int recv_message(Socket fd, char* buf, int buflen, int timeout, uint32_t* src_ip, uint16_t* src_port);
static int send_turn_request(Socket sock, NatChannel *lc, NatChannel *rc, char *userid, int isrtcp);
static int send_stun_request(Socket sock, StunAddress4 dest, char *tid);
static int recv_stun_response(Socket sock, StunAddress4 *from, StunAddress4 *rflx, char *tid, int timeout);
static int send_stun_message(Socket sock, StunAddress4 dest, NatChannel *lc, NatChannel *rc, char *userid, int isrtcp, int state);
static int proc_stun_message(Socket sock, StunAddress4 *from, StunAddress4 *rflx, char *ltid, char *rtid, int timeout);
static void init_nat_session(NatSession *session, StunAddress4 *host, uint16_t port);
static int ice_get_network_interface(uint32_t *addr, int m);

static uint32_t get_time (int init_flag)
{
	static struct timeval start, stop;
	if(init_flag)
	{
		gettimeofday(&start, NULL);
		return 0;
	}
	gettimeofday(&stop, NULL);
	return (uint32_t)((stop.tv_sec-start.tv_sec)*1000 + (stop.tv_usec-start.tv_usec)/1000000LL);
}


static int random_port()
{
   int min=0x4000;
   int max=0x7FFF;
   int ret = 0;
	while(ret == 0)
	{
   	ret = random();
   	ret = ret|min;
   	ret = ret&max;
	}
	
   return ret;
}

static void random_buffer(char *buf, int len)
{
	int min = 0x40;
	int max = 0x7F;
	int i;
	
	if(buf == NULL)
		return;
	for(i = 0; i < len; i++)
	{
		int ret = random();
		while(ret == 0) ret = random();
		ret = ret | min;
		ret = ret & max;
		buf[i] = (char)ret;
	}
}


static Socket open_port(uint16_t port, uint32_t interface_addr)
{
   struct sockaddr_in addr;
   Socket fd;
	
   fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if(fd == INVALID_SOCKET)
   {
	  printf("stun_udp: Could not create a UDP socket");
	  return INVALID_SOCKET;
   }
   
   memset((char*) &(addr),0, sizeof((addr)));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   addr.sin_addr.s_addr = ((interface_addr != 0) && (interface_addr != 0x100007f )) ? htonl(interface_addr) : htonl(INADDR_ANY);
	
   if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
   {
		perror("bind socket failed");
		close(fd);
		return INVALID_SOCKET;
   }	
   return fd;
}

static int recv_message(Socket fd, char* buf, int buflen, int timeout, uint32_t* src_ip, uint16_t* src_port)
{
	int original_size = buflen;
	struct sockaddr_in from;
	int from_len = sizeof(from);
	int len;
	int err;
	struct timeval tv;
	fd_set set; 
	int set_size;

	if(fd==INVALID_SOCKET || buf==NULL || buflen<=0 || timeout<0 || src_ip==NULL || src_port==NULL)
	{
		return -1;
	}

	tv.tv_sec=timeout/1000;
	tv.tv_usec=(timeout%1000)*1000;
	FD_ZERO(&set); set_size=0;
	FD_SET(fd,&set); set_size = fd + 1;

	err = select(set_size, &set, NULL, NULL, &tv);
	if(err == SOCKET_ERROR)
	{
		return -2;
	}
	if(err == 0)
	{
		return 0;
	}

	if(FD_ISSET(fd, &set))
	{
		len  = recvfrom(fd, buf, original_size,	0, (struct sockaddr *)&from, (socklen_t*)&from_len);
		if(len <= 0 )
		{
			return -3;
		}

		*src_port = ntohs(from.sin_port);
		*src_ip = ntohl(from.sin_addr.s_addr);
		return len;
	}
	return 0;
}

static int send_turn_request(Socket sock, NatChannel *lc, NatChannel *rc, char *userid, int isrtcp)
{
	char buf[30];
	int len = 30;
	StunAddress4 dest;

	if(sock == INVALID_SOCKET || rc == NULL || lc == NULL || userid == NULL)
	{
		return -1;
	}

	buf[0] = 'T';
	buf[1] = 'U';
	buf[2] = 'R';
	buf[3] = 'N';
	buf[4] = 'P';
	buf[5] = 'E';
	buf[6] = 'R';
	buf[7] = 'M';
	memcpy(&buf[8],  lc->ufrag, 4);
	memcpy(&buf[12], rc->ufrag, 4);
	memcpy(&buf[16], userid, 4);
//	LOGI("ken debug - lc->ufrag=%04s rc->ufrag=%04s socket=%d",lc->ufrag,rc->ufrag,sock);
	buf[20] = 0;
	buf[21] = 0;
	buf[22] = isrtcp;
	buf[23] = 0;
	buf[24] = (lc->host.src.port >>  8) & 0x00ff;
	buf[25] = (lc->host.src.port >>  0) & 0x00ff;
	buf[26] = (lc->host.src.addr >> 24) & 0x00ff;
	buf[27] = (lc->host.src.addr >> 16) & 0x00ff;
	buf[28] = (lc->host.src.addr >>  8) & 0x00ff;
	buf[29] = (lc->host.src.addr >>  0) & 0x00ff;
	
	dest = rc->relay.map;

	sendMessage(sock, buf, len, dest.addr, dest.port);
	return len;
}

static int send_stun_request(Socket sock, StunAddress4 dest, char *tid)
{
	char buf[STUN_MAX_MESSAGE_SIZE];
	int len = STUN_MAX_MESSAGE_SIZE;
	StunMessage req;

	if(sock == INVALID_SOCKET || dest.addr == 0 || tid == NULL)
	{
		return -1;
	}

	memset(&req, 0, sizeof(StunMessage));
	req.msgHdr.msgType = (STUN_METHOD_BINDING|STUN_REQUEST);
	req.msgHdr.magic_cookie = 0x2112A442;
	memcpy(&req.msgHdr.tr_id.octet[0], tid, 12);

	len = stunEncodeMessage(&req, buf, len, NULL);
	if (len <= 0)
	{
		return -2;
	}

	sendMessage(sock, buf, len, dest.addr, dest.port);
	return len;
}

static int recv_stun_response(Socket sock, StunAddress4 *from, StunAddress4 *rflx, char *tid, int timeout)
{
	char buf[STUN_MAX_MESSAGE_SIZE];
	int len = STUN_MAX_MESSAGE_SIZE;
	StunMessage resp;

	if(sock == INVALID_SOCKET || from == NULL || rflx == NULL || tid == NULL || timeout < 0)
	{
		return -1;
	}

	len = recv_message(sock, buf, len, timeout, &from->addr, &from->port);
	if(len <= 0)
	{
		return -2;
	}

	memset(&resp, 0, sizeof(resp));
	if(len < sizeof(StunMsgHdr) || buf[0] & 0xC0 || FALSE == stunParseMessage(buf, len, &resp))
	{
		return -3;
	}

	if(memcmp(&resp.msgHdr.tr_id.octet[0], tid, 12))
	{
		return -4;
	}

	if(resp.hasXorMappedAddress)
	{
		*rflx = resp.xorMappedAddress.ipv4;
	}
	else if(resp.hasMappedAddress)
	{
		*rflx = resp.mappedAddress.ipv4;
	}
	else
	{
		return -5;
	}
	
	
	return len;
}

static int send_stun_message(Socket sock, StunAddress4 dest, NatChannel *lc, NatChannel *rc, char *userid, int isrtcp, int state)
{
	char buf[STUN_MAX_MESSAGE_SIZE];
	int len = STUN_MAX_MESSAGE_SIZE;
	StunMessage req;
	IceMessage msg;
	int msglen = sizeof(msg);
	uint32_t currtime = get_time(0);

	if(sock == INVALID_SOCKET || dest.addr == 0 || rc == NULL || lc == NULL || userid == NULL)
	{
		return -1;
	}

	memset(&req, 0, sizeof(StunMessage));
	req.msgHdr.msgType = (STUN_METHOD_BINDING|STUN_REQUEST);
	req.msgHdr.magic_cookie = 0x2112A442;
	memcpy(&req.msgHdr.tr_id.octet[0], &lc->pwd[0], 6);
	memcpy(&req.msgHdr.tr_id.octet[6], &rc->pwd[6], 6);

	memcpy(&msg.magic, " ICEMSG ", 8);
	memcpy(&msg.ltid, lc->ufrag, 4);
	memcpy(&msg.rtid, rc->ufrag, 4);
	memcpy(&msg.userid, userid, 4);
	msg.timestamp = htonl(currtime);
	msg.isrtcp	  = (uint8_t) isrtcp;
	msg.state	  = (uint8_t) state;
	msg.bitrate   = htons(lc->host.src.port);
	msg.reserved  = htonl(lc->host.src.addr);

	req.hasData = TRUE;
	req.data.sizeValue = msglen;
	memcpy(req.data.value, &msg, msglen);

	len = stunEncodeMessage(&req, buf, len, NULL);
	if (len <= 0)
	{
		return -2;
	}

	sendMessage(sock, buf, len, dest.addr, dest.port);
	return len;
}

//static int proc_stun_message(Socket sock, StunAddress4 *from, StunAddress4 *rflx, char *ltid, char *rtid, int timeout)
//{
//	char buf[STUN_MAX_MESSAGE_SIZE];
//	int len = STUN_MAX_MESSAGE_SIZE;
//	StunMessage resp;
//
//	if(sock == INVALID_SOCKET || from == NULL || rflx == NULL || ltid == NULL || rtid == NULL || timeout < 0)
//	{
//		return -1;
//	}
//
//	len = recv_message(sock, buf, len, timeout, &from->addr, &from->port);
//
//	if(len <= 0)
//	{
//		return -2;
//	}
//
//	memset(&resp, 0, sizeof(resp));
//	if(len < sizeof(StunMsgHdr) || buf[0] & 0xC0 || FALSE == stunParseMessage(buf, len, &resp))
//	{
//		return -3;
//	}
//
//	if(STUN_IS_SUCCESS_RESP(resp.msgHdr.msgType))
//	{
//		if(memcmp(&resp.msgHdr.tr_id.octet[0], &ltid[0], 6) ||
//			memcmp(&resp.msgHdr.tr_id.octet[6], &rtid[6], 6) )
//		{
//			return -4;
//		}
//
//		if(resp.hasXorMappedAddress)
//			*rflx = resp.xorMappedAddress.ipv4;
//		else if(resp.hasMappedAddress)
//			*rflx = resp.mappedAddress.ipv4;
//		else
//		{
//			return -5;
//		}
//		return 1;
//	}
//	else if(STUN_IS_REQUEST(resp.msgHdr.msgType))
//	{
//		StunAddress4 src;
//		StunAddress4 dest;
//		bool_t changePort = FALSE;
//		bool_t changeIP = FALSE;
//		bool_t ok;
//
//		if(memcmp(&resp.msgHdr.tr_id.octet[0], &rtid[0], 6) || memcmp(&resp.msgHdr.tr_id.octet[6], &ltid[6], 6) )
//		{
//			return -6;
//		}
//		if(resp.hasData)
//		{
//			if(resp.data.sizeValue >= sizeof(IceMessage) && memcmp(resp.data.value, " ICEMSG ", 8) == 0)
//			{
//				IceMessage msg;
//				memcpy(&msg, resp.data.value, sizeof(IceMessage));
//				return (msg.state==0) ? 0 : 3;
//			}
//		}
//		memset(&resp, 0, sizeof(StunMessage));
//		ok = stunServerProcessMsg(buf, len, from, &src, &src, &resp, &dest, NULL, &changePort, &changeIP);
//		if(ok == FALSE)
//		{
//			return -7;
//		}
//		len = stunEncodeMessage(&resp, buf, sizeof(buf), NULL);
//		if(len <= 0)
//		{
//			return -8;
//		}
//		sendMessage(sock, buf, len, dest.addr, dest.port);
//		return 0;
//	}
//	else
//	{
//		return -9;
//	}
//}
static int proc_stun_message(Socket sock, StunAddress4 *from, StunAddress4 *rflx, char *ltid, char *rtid, int timeout)
{
	char buf[STUN_MAX_MESSAGE_SIZE];
	int len = STUN_MAX_MESSAGE_SIZE;
	StunMessage resp;

	if(sock == INVALID_SOCKET || from == NULL || rflx == NULL || ltid == NULL || rtid == NULL || timeout < 0)
	{
		return -1;
	}

	len = recv_message(sock, buf, len, timeout, &from->addr, &from->port);

	if(len <= 0)
	{
		return -2;
	}

	memset(&resp, 0, sizeof(resp));
	if(len < sizeof(StunMsgHdr) || buf[0] & 0xC0 || FALSE == stunParseMessage(buf, len, &resp))
	{
		return -3;
	}

	if(memcmp(&resp.msgHdr.tr_id.octet[0], &rtid[0], 6) || memcmp(&resp.msgHdr.tr_id.octet[6], &ltid[6], 6) )
	{
		return -6;
	}
	if(resp.hasData)
	{
		if(resp.data.sizeValue >= sizeof(IceMessage) && memcmp(resp.data.value, " ICEMSG ", 8) == 0)
		{
			IceMessage msg;
			memcpy(&msg, resp.data.value, sizeof(IceMessage));
			return msg.state;
		}
		return -7;
	}
	return -8;
}

static void init_nat_session(NatSession *session, StunAddress4 *host, uint16_t port)
{
	random_buffer(session->rtp.ufrag,	sizeof(session->rtp.ufrag));
	random_buffer(session->rtp.pwd,		sizeof(session->rtp.pwd));
	random_buffer(session->rtcp.ufrag,	sizeof(session->rtcp.ufrag));
	random_buffer(session->rtcp.pwd,	sizeof(session->rtcp.pwd));
	
	session->rtp.host.foundation	= 6;
	session->rtp.host.component_id	= 1;
	session->rtp.host.transport		= 0;
	session->rtp.host.priority		= 126;
	session->rtp.host.cand_type		= 0;
	session->rtp.host.map.addr		= host->addr;
	session->rtp.host.map.port		= port;
	
	session->rtcp.host.foundation	= 6;
	session->rtcp.host.component_id	= 1;
	session->rtcp.host.transport	= 0;
	session->rtcp.host.priority		= 120;
	session->rtcp.host.cand_type	= 0;
	session->rtcp.host.map.addr		= host->addr;
	session->rtcp.host.map.port		= port + 1;
	
	session->rtp.rflx.foundation	= 6;
	session->rtp.rflx.component_id	= 2;
	session->rtp.rflx.transport		= 0;
	session->rtp.rflx.priority		= 100;
	session->rtp.rflx.cand_type		= 1;
	session->rtp.rflx.src.addr		= host->addr;
	session->rtp.rflx.src.port		= port;
	
	session->rtcp.rflx.foundation	= 6;
	session->rtcp.rflx.component_id	= 2;
	session->rtcp.rflx.transport	= 0;
	session->rtcp.rflx.priority		= 90;
	session->rtcp.rflx.cand_type	= 1;
	session->rtcp.rflx.src.addr		= host->addr;
	session->rtcp.rflx.src.port		= port + 1;
	
	session->rtp.relay.foundation	= 6;
	session->rtp.relay.component_id	= 3;
	session->rtp.relay.transport	= 0;
	session->rtp.relay.priority		= 20;
	session->rtp.relay.cand_type	= 2;
	
	session->rtcp.relay.foundation		= 6;
	session->rtcp.relay.component_id	= 3;
	session->rtcp.relay.transport		= 0;
	session->rtcp.relay.priority		= 10;
	session->rtcp.relay.cand_type		= 2;
	
	session->nc = 0;
}

static uint32_t ice_get_local_ip(const char *serverName)
{
	uint32_t local_ip = 0;
	StunAddress4  dest;
	struct sockaddr_storage addr;
	socklen_t addr_len;
	struct sockaddr_in *p = (struct sockaddr_in*) &addr;
	char loc[128];
	int size = sizeof(loc);
	addr_len = sizeof(addr);

	if (serverName == NULL)
		return 0;
	if (stunParseServerName(serverName, &dest) == FALSE)
		return 0;

	if (dest.port == 3478)
		dest.port = 80;

	p->sin_family = AF_INET;
	p->sin_addr.s_addr = htonl(dest.addr);
	p->sin_port = htons(dest.port);
	if (0 == _ice_get_localip_for(&addr, addr_len, loc, size))
	{
		local_ip = ntohl(inet_addr(loc));
	}
	return local_ip;
}

static int ice_get_network_interface(uint32_t *addr, int m)
{
	int i = 0;
	int n = 0;
	char hostname[128];
	struct hostent *hptr = NULL;
	
	if(addr == NULL || m <= 0 || gethostname(hostname, sizeof(hostname)) != 0 || (hptr = gethostbyname(hostname)) == NULL)
		return 0;
	switch(hptr->h_addrtype)
	{
		case AF_INET:
			for(i=0; hptr->h_addr_list[i] != NULL; i++)
			{
				struct in_addr inaddr;
				inaddr.s_addr = *(u_long *) hptr->h_addr_list[i];
				if (n < m) addr[n++] = ntohl(inaddr.s_addr);
			}
			break;
		case AF_INET6:
			break;
		default:
			break;
	}
	return n;
}


int ice_select_network_interface(int index)
{
	int n;
	uint32_t addr[16];
	uint32_t host_ip = 0;

	n = ice_get_network_interface(addr, 16);

	if(index > 0 && index <= n)
	{
		return addr[index - 1];
	}
	if(n == 1)
		return addr[0];
	if(n > 1)
	{
		host_ip = ice_get_local_ip("www.baidu.com");
		if(host_ip == 0)
			host_ip = ice_get_local_ip("www.intel.com");
		if(host_ip == 0)
			host_ip = ice_get_local_ip("www.yahoo.com");
	}
	return host_ip;
}

int ice_get_nat_type(const char *server_name, int host_addr, uint16_t port)
{
	int				type;
	StunAddress4	host;
	StunAddress4	dest;
	bool_t			preservePort = FALSE;
	bool_t			hairpin = FALSE;

	if(server_name == NULL)
	{
		return -1;
	}

	if(stunParseServerName(server_name, &dest) == FALSE)
	{
		return -2;
	}
	host.addr = host_addr;
	host.port = (port <= 0) ? random_port() : port;
	type = stunNatType(&dest, &preservePort, &hairpin, host.port, &host);

	if(type == StunTypeBlocked) return -3;
	if(type == StunTypeFailure) return -4;
	return type;
}

int ice_gather_candidates(uint32_t stun_addr, uint16_t stun_port, int host_addr, int nat_type, int session, int start_port, int magic, NatCheckList *checkList)
{
	Socket			fd[MAXNATSESSION][2];
	int				state[MAXNATSESSION][2];
	StunAddress4	dest;
	StunAddress4	host;
	int i, n;
	struct in_addr struct_stun_addr;
	char server_name[128];
	memset(server_name, 0, sizeof(server_name));
	struct_stun_addr.s_addr = stun_addr;
	sprintf(server_name, "%s:%d", inet_ntoa(struct_stun_addr), ntohs(stun_port));
	
	if(server_name == NULL || session <= 0 || checkList == NULL)
	{
		return -1;
	}
	if(stunParseServerName(server_name, &dest) == FALSE)
	{
		return -2;
	}
	if(session > MAXNATSESSION)
	{
		session = MAXNATSESSION;
	}

	if(start_port < 0 || start_port > 65535)
	{
		start_port = 0;
	}

	if(nat_type <= 0)
	{
		nat_type  = StunTypeSymNat;
	}
	memset(checkList, 0, sizeof(NatCheckList));
	checkList->magic = magic;
	checkList->type  = nat_type;

	host.addr = host_addr;
	host.port = stun_port;

	for(n = 0; n < session; n++)
	{
		NatSession *s = &checkList->session[n];
		int port;
		for(i = 0; i < 10; i++)
		{
			port = (start_port > 0) ? (start_port + n*2) : random_port();
			fd[n][0] = open_port(port+0, host_addr);
			fd[n][1] = open_port(port+1, host_addr);
			if(fd[n][0] != INVALID_SOCKET && fd[n][1] != INVALID_SOCKET)
			{
				break;
			}
			closesocket(fd[n][0]);
			closesocket(fd[n][1]);
		}
		if(i==10) break;
		
//		LOGI("open_port is ok, n=%d port=%d",n,port);
		state[n][0] = 0;
		state[n][1] = 0;
		
		init_nat_session(s, &host, port);
		get_time(1);

		for(i = 0; i < 50; i++)
		{
			StunAddress4  from, rflx;
			if(state[n][0] == 0)
			{
				send_stun_request(fd[n][0], dest, s->rtp.pwd);
				ms_usleep(20*1000);
			}
			if(state[n][1] == 0)
			{
				send_stun_request(fd[n][1], dest, s->rtcp.pwd);
				ms_usleep(20*1000);
			}
			if(state[n][0] == 0 && recv_stun_response(fd[n][0], &from, &rflx, s->rtp.pwd,  50) > 0)
			{
//				LOGI("recv message, rtp");
				state[n][0] = 1;
				s->rtp.rflx.map.addr  = rflx.addr;
				s->rtp.rflx.map.port  = rflx.port;
				s->rtp.relay.src.addr = rflx.addr;
				s->rtp.relay.src.port = rflx.port;
			}
			if(state[n][1] == 0 && recv_stun_response(fd[n][1], &from, &rflx, s->rtcp.pwd, 50) > 0)
			{
//				LOGI("recv message, rtcp");
				state[n][1] = 1;
				s->rtcp.rflx.map.addr  = rflx.addr;
				s->rtcp.rflx.map.port  = rflx.port;
				s->rtcp.relay.src.addr = rflx.addr;
				s->rtcp.relay.src.port = rflx.port;
			}
			if (state[n][0]==1 && state[n][1]==1)
			{
				s->nc = 2;
				break;
			}
			ms_usleep(20*1000);
			if(get_time(0) > 6400) break;
		}
		closesocket(fd[n][0]);
		closesocket(fd[n][1]);

		if(s->nc == 0) break;
	}
	for(i = 0; i < session; i++)
	{
		NatSession *s = &checkList->session[i];
		StunAddress4  relay1, relay2;
		relay1.addr = dest.addr;
		relay2.addr = dest.addr;
		relay1.port = TURN_PORT+i*2+0;//3482 + i*2 + 0;
		relay2.port = TURN_PORT+i*2+1;//3482 + i*2 + 1;
		s->rtp.relay.map.addr  = relay1.addr;
		s->rtp.relay.map.port  = relay1.port;
		s->rtcp.relay.map.addr = relay2.addr;
		s->rtcp.relay.map.port = relay2.port;
	}
	checkList->number = n;

	return (n == session) ? session : 0;
}

int ice_exchange_check_list(const char *server_name, NatCheckList *checkList, int host_addr, uint16_t port, int action)
{
	StunAddress4  dest;
	StunAddress4  host;
	Socket myFd;
	NatMsg *natmsg;
	char msgbuf[sizeof(NatMsg) + sizeof(NatCheckList)];
	int  msglen = sizeof(msgbuf);

	if(server_name == NULL || checkList == NULL)
		return -1;
	if(stunParseServerName(server_name, &dest) == FALSE)
	{
		printf("stunParseServerName: %s failed\n", server_name);
		return -2;
	}
	host.addr = host_addr;
	host.port = (port == 0) ? random_port() : port;

	if((myFd = open_port(host.port, host_addr)) == INVALID_SOCKET)
	{
		printf("Open myFd failed\n");
		return -3;
	}

	natmsg = (NatMsg*) msgbuf;
	natmsg->src = host;
	natmsg->dst = dest;
	natmsg->hdr.magic_cookie = 0x2112A442;
	if(action == NAT_MSG_SAVE)
	{
		natmsg->hdr.msgType = NAT_MSG_SAVE;
		natmsg->hdr.msgLength = sizeof(NatMsg) + sizeof(NatCheckList);
		memcpy(msgbuf+sizeof(NatMsg), checkList, sizeof(NatCheckList));
		sendMessage(myFd, msgbuf, natmsg->hdr.msgLength, dest.addr, dest.port);
	}
	else
	{
		natmsg->hdr.msgType = NAT_MSG_LOAD;
		natmsg->hdr.msgLength = sizeof(NatMsg);
		sendMessage(myFd, msgbuf, natmsg->hdr.msgLength, dest.addr, dest.port);
	}
	
	if (action != NAT_MSG_SAVE)
	{
		StunAddress4 from;
		int n = recv_message(myFd, msgbuf, msglen, 1000, &from.addr, &from.port);
		if(n != msglen || natmsg->hdr.magic_cookie != 0x2112A442)
		{
			memset(checkList, 0, sizeof(NatCheckList));
			closesocket(myFd);
			return -4;
		}
		else
		{
			memcpy(checkList, msgbuf+sizeof(NatMsg), sizeof(NatCheckList));
			port = natmsg->dst.port;
		}
	}
	closesocket(myFd);
	return port;
}

//int ice_connectivity_check(NatCheckList *local, NatCheckList *remote, int host_addr, int magic)
//{
//	static int n_check_times = 0;
//	Socket fd[MAXNATSESSION][2];
//	int state[MAXNATSESSION][2];
//	int i, j, n;
//	int session = local->number;
//	bool_t same_nat = FALSE;
//	bool_t same_lan = FALSE;
//	uint32_t time_out;
//
//	if (local == NULL || remote == NULL)
//	{
//		return -1;
//	}
//
//	if (local->magic != magic)
//	{
//		return -2;
//	}
//	if(remote->magic != local->magic)
//	{
//		return -3;
//	}
//
//	if (session != remote->number || session <= 0 || session > MAXNATSESSION)
//	{
//		return -4;
//	}
//	for (i=0; i < session; i++)
//	{
//		NatSession *ls = &local->session[i];
//		NatSession *rs = &remote->session[i];
//		same_nat =	(ls->rtp.rflx.map.addr  != 0 && ls->rtp.rflx.map.addr  == rs->rtp.rflx.map.addr) ||
//						(ls->rtcp.rflx.map.addr != 0 && ls->rtcp.rflx.map.addr == rs->rtcp.rflx.map.addr);
//		same_lan =	same_nat &&
//						 ((ls->rtp.host.map.addr != 0 && (ls->rtp.host.map.addr  & 0xFFFFFF00) == (rs->rtp.host.map.addr  & 0xFFFFFF00)) ||
//						 (ls->rtcp.host.map.addr != 0 && (ls->rtcp.host.map.addr & 0xFFFFFF00) == (rs->rtcp.host.map.addr & 0xFFFFFF00)));
//	}
//	for (i = 0; i < session; i++)
//	{
//		NatSession *ls = &local->session[i];
//		fd[i][0] = open_port(ls->rtp.host.map.port,	host_addr);
//		fd[i][1] = open_port(ls->rtcp.host.map.port, host_addr);
//	}
//
//	n = 0;
//	memset(state, 0, sizeof(state));
//	random_buffer(user_id, sizeof(user_id));
//	n_check_times = 240;
//	time_out = 0;
//	get_time(1);
//
//
//	if(same_lan == TRUE)
//	{
//		time_out += 3200;
//		for (j = 0; j < n_check_times; j++)
//		{
//			for(i = 0; i < session; i++)
//			{
//				NatSession *ls = &local->session[i];
//				NatSession *rs = &remote->session[i];
//				StunAddress4 dest0, dest1;
//				StunAddress4 from, rflx;
//				int ret;
//				while(0x3 != state[i][0])
//				{
//					ret = proc_stun_message(fd[i][0], &from, &rflx, ls->rtp.pwd,	rs->rtp.pwd,  0);
//					if(ret >= 0)
//					{
//						int oldstate = state[i][0];
//
//						state[i][0] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//						ls->rtp.link = ls->rtp.host.map;
//						rs->rtp.link = from;
//
//						if (oldstate != state[i][0]){
//							dest0 = (rs->rtp.link.addr	== 0) ? rs->rtp.host.map  : rs->rtp.link;
//							send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						}
//					}
//					else
//					{
//						dest0 = (rs->rtp.link.addr	== 0) ? rs->rtp.host.map  : rs->rtp.link;
//						send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//				while(0x3 != state[i][1])
//				{
//					ret = proc_stun_message(fd[i][1], &from, &rflx, ls->rtcp.pwd, rs->rtcp.pwd, 0);
//					if (ret >= 0)
//					{
//						int oldstate = state[i][1];
//
//						state[i][1] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//						ls->rtcp.link = ls->rtcp.host.map;
//						rs->rtcp.link = from;
//
//						if (oldstate != state[i][1]){
//							dest1 = (rs->rtcp.link.addr == 0) ? rs->rtcp.host.map : rs->rtcp.link;
//							send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						}
//					}
//					else
//					{
//						dest1 = (rs->rtcp.link.addr == 0) ? rs->rtcp.host.map : rs->rtcp.link;
//						send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//			}
//
//			n = 0;
//			for(i = 0; i < session; i++)
//			{
//				if(state[i][0] == 0x3 && state[i][1] == 0x3)
//					n++;
//			}
//			if (j < (n_check_times - 4) && n == session)
//				j =  n_check_times - 4;
//			if (get_time(0) > time_out)
//				break;
//		}
//	}
//
//	//lgf, 2015-04-27 11:39:39
//	//内网判断，如果是内网则，尝试直连，如果直连成功，则不通过服务器转发
//	if(same_nat == TRUE &&
//			local->type == StunTypeSymNat && remote->type == StunTypeSymNat)
//	{
//		time_out += 3200;
//		for (j = 0; j < n_check_times; j++)
//		{
//			for(i = 0; i < session; i++)
//			{
//				NatSession *ls = &local->session[i];
//				NatSession *rs = &remote->session[i];
//				StunAddress4 dest0, dest1;
//				StunAddress4 from, rflx;
//				int ret;
//				while(0x3 != state[i][0])
//				{
//					ret = proc_stun_message(fd[i][0], &from, &rflx, ls->rtp.pwd,	rs->rtp.pwd,  0);
//					if(ret >= 0)
//					{
//						int oldstate = state[i][0];
//
//						state[i][0] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//						ls->rtp.link = ls->rtp.host.map;
//						rs->rtp.link = from;
//
//						if (oldstate != state[i][0]){
//							dest0 = (rs->rtp.link.addr	== 0) ? rs->rtp.host.map  : rs->rtp.link;
//							send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						}
//					}
//					else
//					{
//						dest0 = (rs->rtp.link.addr	== 0) ? rs->rtp.host.map  : rs->rtp.link;
//						send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//				while(0x3 != state[i][1])
//				{
//					ret = proc_stun_message(fd[i][1], &from, &rflx, ls->rtcp.pwd, rs->rtcp.pwd, 0);
//					if (ret >= 0)
//					{
//						int oldstate = state[i][1];
//
//						state[i][1] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//						ls->rtcp.link = ls->rtcp.host.map;
//						rs->rtcp.link = from;
//
//						if (oldstate != state[i][1]){
//							dest1 = (rs->rtcp.link.addr == 0) ? rs->rtcp.host.map : rs->rtcp.link;
//							send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						}
//					}
//					else
//					{
//						dest1 = (rs->rtcp.link.addr == 0) ? rs->rtcp.host.map : rs->rtcp.link;
//						send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//			}
//
//			n = 0;
//			for(i = 0; i < session; i++)
//			{
//				if(state[i][0] == 0x3 && state[i][1] == 0x3)
//					n++;
//			}
//			if (n == session){
//				break;
//			}
//
//			if (j < (n_check_times - 4) && n == session)
//				j =  n_check_times - 4;
//			if (get_time(0) > time_out)
//				break;
//		}
//	}
//	//endlgf
//
//	if (n < session && same_nat != TRUE &&
//		((local->type == StunTypePortRestrictedNat && remote->type == StunTypePortRestrictedNat) ||
//		  local->type <  StunTypePortRestrictedNat || remote->type <  StunTypePortRestrictedNat ))
//	{
//		time_out += 3200;
//		for (j = 0; j < n_check_times; j++)
//		{
//			for (i = 0; i < session; i++)
//			{
//				NatSession *ls = &local->session[i];
//				NatSession *rs = &remote->session[i];
//				StunAddress4 dest0, dest1;
//				StunAddress4 from, rflx;
//				int ret;
//				while(0x3 != state[i][0])
//				{
//					ret = proc_stun_message(fd[i][0], &from, &rflx, ls->rtp.pwd,	rs->rtp.pwd,  0);
//					if (ret >= 0)
//					{
//						int oldstate = state[i][0];
//
//						state[i][0] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//						ls->rtp.link = ls->rtp.host.map;
//						rs->rtp.link = from;
//
//						if (oldstate != state[i][0]){
//							dest0 = (rs->rtp.link.addr	== 0) ? rs->rtp.rflx.map  : rs->rtp.link;
//							send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						}
//					}
//					else
//					{
//						dest0 = (rs->rtp.link.addr	== 0) ? rs->rtp.rflx.map  : rs->rtp.link;
//						send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//				while(0x3 != state[i][1])
//				{
//					ret = proc_stun_message(fd[i][1], &from, &rflx, ls->rtcp.pwd, rs->rtcp.pwd, 0);
//					if (ret >= 0)
//					{
//						int oldstate = state[i][1];
//
//						state[i][1] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//						ls->rtcp.link = ls->rtcp.host.map;
//						rs->rtcp.link = from;
//
//						if(oldstate != state[i][1]){
//							dest1 = (rs->rtcp.link.addr == 0) ? rs->rtcp.rflx.map : rs->rtcp.link;
//							send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						}
//					}
//					else
//					{
//						dest1 = (rs->rtcp.link.addr == 0) ? rs->rtcp.rflx.map : rs->rtcp.link;
//						send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//			}
//
//			n = 0;
//			for (i = 0; i < session; i++)
//			{
//				if(state[i][0] == 0x3 && state[i][1] == 0x3)
//					n ++;
//			}
//			if (j < (n_check_times - 4) && n == session)
//				j =  n_check_times - 4;
//			if (get_time(0) > time_out)
//				break;
//		}
//	}
//
//	if (n < session)
//	{
//		time_out += 6400;
//		memset(state, 0, sizeof(state));
//		for(i = 0; i < session; i++)
//		{
//			NatSession *rs = &remote->session[i];
//			rs->rtp.link.addr  = 0;
//			rs->rtcp.link.addr = 0;
//		}
//		for(j = 0; j < n_check_times; j++)
//		{
//			for(i = 0; i < session; i++)
//			{
//				NatSession *ls = &local->session[i];
//				NatSession *rs = &remote->session[i];
//				StunAddress4 dest0, dest1;
//				StunAddress4 from, rflx;
//				int ret;
//
//				if (state[i][0] == 0x0)
//				{
//					send_turn_request(fd[i][0], &ls->rtp,	&rs->rtp,  user_id, 0);
//					ms_usleep(20*1000);
//				}
//
//				if (state[i][1] == 0x0)
//				{
//					send_turn_request(fd[i][1], &ls->rtcp, &rs->rtcp, user_id, 1);
//					ms_usleep(20*1000);
//				}
//
//				while(state[i][0] != 0x3)
//				{
//					ret = proc_stun_message(fd[i][0], &from, &rflx, ls->rtp.pwd,	rs->rtp.pwd,  0);
//					if(ret >= 0)
//					{
//						if(from.addr == ls->rtp.relay.map.addr)
//						{
//							int oldstate = state[i][0];
//
//							state[i][0] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//							ls->rtp.link = ls->rtp.host.map;
//							rs->rtp.link = rs->rtp.relay.map;
//
//							if (oldstate != state[i][0]){
//								dest0 = rs->rtp.relay.map;
//								send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//							}
//						}
//					}
//					else
//					{
//						dest0 = rs->rtp.relay.map;
//						send_stun_message(fd[i][0], dest0, &ls->rtp,	&rs->rtp,  user_id, 0, state[i][0]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//
//				while(state[i][1] != 0x3)
//				{
//					ret = proc_stun_message(fd[i][1], &from, &rflx, ls->rtcp.pwd, rs->rtcp.pwd, 0);
//					if (ret >= 0)
//					{
//						if(from.addr == ls->rtcp.relay.map.addr)
//						{
//							int oldstate = state[i][1];
//
//							state[i][1] |= (ret == 3) ? 0x3 : ((ret == 0) ? 0x1 : 0x2);
//							ls->rtcp.link = ls->rtcp.host.map;
//							rs->rtcp.link = rs->rtcp.relay.map;
//
//							if (oldstate != state[i][1]){
//								dest1 = rs->rtcp.relay.map;
//								send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//							}
//						}
//					}
//					else
//					{
//						dest1 = rs->rtcp.relay.map;
//						send_stun_message(fd[i][1], dest1, &ls->rtcp, &rs->rtcp, user_id, 1, state[i][1]);
//						ms_usleep(20*1000);
//						break;
//					}
//				}
//			}
//
//			n = 0;
//			for(i = 0; i < session; i++)
//			{
//				if(state[i][0] == 0x3 && state[i][1] == 0x3)
//					n ++;
//			}
//			//lgf, 2015-5-6 10:24:57
//			if (n == session)
//			{
//				break;
//			}
//			//endlgf
//
//			if (j < (n_check_times- 4) && n == session)
//				j =  n_check_times - 4;
//			if (get_time(0) > time_out)
//				break;
//		}
//	}
//
//	for (i = 0; i < session; i++)
//	{
//		closesocket(fd[i][0]);
//		closesocket(fd[i][1]);
//	}
//	return (n == session) ? session : 0;
//}

static int ice_check_p2p_connectiviy(NatCheckList *local, NatCheckList *remote, Socket fd[][2], bool_t isSameNat)
{
	int i,j,n=0,state=0;
	int session=local->number;
	StunAddress4 from, rflx;
	bool_t connectable[MAXNATSESSION][2]={{FALSE,FALSE}};
	bool_t isEnd[MAXNATSESSION][2]={{FALSE,FALSE}};
	uint32_t time_out=200;
	StunAddress4 dest[MAXNATSESSION][2];

	/* send 0 */
	for(i=0;i<session;++i)
	{
		if(isSameNat==TRUE)
		{
			dest[i][0]=(remote->session[i].rtp.link.addr==0) ? remote->session[i].rtp.host.map  : remote->session[i].rtp.link;
			dest[i][1]=(remote->session[i].rtcp.link.addr==0) ? remote->session[i].rtcp.host.map  : remote->session[i].rtcp.link;
		}
		else
		{
			dest[i][0]=remote->session[i].rtp.rflx.map;
			dest[i][1]=remote->session[i].rtcp.rflx.map;
		}
		send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 0);
		send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 0);
	}
	/* recv */
	get_time(1);
	while(TRUE)
	{
		/* recv and process */
		for(i=0;i<session;++i)
		{
			if(!isEnd[i][0])
			{
				state=proc_stun_message(fd[i][0], &from, &rflx, local->session[i].rtp.pwd,remote->session[i].rtp.pwd,0);
				switch(state)
				{
					case 0:
						send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 1);
						break;
					case 1:
						send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 3);
						connectable[i][0]=TRUE;
						local->session[i].rtp.link = local->session[i].rtp.host.map;
						remote->session[i].rtp.link = from;
						break;
					case 3:
						send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 3);
						connectable[i][0]=TRUE;
						local->session[i].rtp.link = local->session[i].rtp.host.map;
						remote->session[i].rtp.link = from;
						isEnd[i][0]=TRUE;
						break;
					default:
						break;
				}
			}
			if(isEnd[i][1]==FALSE)
			{
				state=proc_stun_message(fd[i][1], &from, &rflx, local->session[i].rtcp.pwd,remote->session[i].rtcp.pwd,0);
				switch(state)
				{
					case 0:
						send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 1);
						break;
					case 1:
						send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 3);
						connectable[i][1]=TRUE;
						local->session[i].rtcp.link = local->session[i].rtcp.host.map;
						remote->session[i].rtcp.link = from;
						break;
					case 3:
						send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 3);
						connectable[i][1]=TRUE;
						local->session[i].rtcp.link = local->session[i].rtcp.host.map;
						remote->session[i].rtcp.link = from;
						isEnd[i][1]=TRUE;
						break;
					default:
						break;
				}
			}
		}
		n=0;
		for(i=0;i<session;++i)
		{
			if(isEnd[i][0]==TRUE && isEnd[i][1]==TRUE) ++n;
		}
		if(n==session) break;
		/* check timeout */
		if(get_time(0) > time_out)
		{
			for(i=0;i<session;++i)
			{
				if(connectable[i][0]==FALSE)
				{
					send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 0);
				}
				if(connectable[i][1]==FALSE)
				{
					send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 0);
				}
			}
			time_out+=200;
		}
		if(get_time(0)>1000) break;
		ms_usleep(50*1000);
	}
	return n;
}

static int ice_check_turn_connectiviy(NatCheckList *local, NatCheckList *remote, Socket fd[][2])
{
	int i,j,n=0,state=0;
	int session=local->number;
	StunAddress4 from, rflx;
	bool_t connectable[MAXNATSESSION][2]={{FALSE,FALSE}};
	bool_t isEnd[MAXNATSESSION][2]={{FALSE,FALSE}};
	uint32_t time_out=200;
	StunAddress4 dest[MAXNATSESSION][2];
	/* send turn request */
	for(i=0;i<session;i++)
	{
		send_turn_request(fd[i][0], &local->session[i].rtp,	&remote->session[i].rtp,  user_id, 0);
		ms_usleep(20*1000);
		send_turn_request(fd[i][1], &local->session[i].rtcp, &remote->session[i].rtcp, user_id, 1);
		ms_usleep(20*1000);
	}
	/* send 0 */
	for(i=0;i<session;++i)
	{
		dest[i][0]=remote->session[i].rtp.relay.map;
		dest[i][1]=remote->session[i].rtcp.relay.map;
		send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 0);
		send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 0);
	}
	/* recv */
	get_time(1);
	while(TRUE)
	{
		/* recv and process */
		for(i=0;i<session;++i)
		{
			if(!isEnd[i][0])
			{
				state=proc_stun_message(fd[i][0], &from, &rflx, local->session[i].rtp.pwd,remote->session[i].rtp.pwd,0);
				switch(state)
				{
					case 0:
						send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 1);
						break;
					case 1:
						send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 3);
						connectable[i][0]=TRUE;
						local->session[i].rtp.link = local->session[i].rtp.host.map;
						remote->session[i].rtp.link = from;
						break;
					case 3:
						send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 3);
						connectable[i][0]=TRUE;
						local->session[i].rtp.link = local->session[i].rtp.host.map;
						remote->session[i].rtp.link = from;
						isEnd[i][0]=TRUE;
						break;
					default:
						break;
				}
			}
			if(isEnd[i][1]==FALSE)
			{
				state=proc_stun_message(fd[i][1], &from, &rflx, local->session[i].rtcp.pwd,remote->session[i].rtcp.pwd,0);
				switch(state)
				{
					case 0:
						send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 1);
						break;
					case 1:
						send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 3);
						connectable[i][1]=TRUE;
						local->session[i].rtcp.link = local->session[i].rtcp.host.map;
						remote->session[i].rtcp.link = from;
						break;
					case 3:
						send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 3);
						connectable[i][1]=TRUE;
						local->session[i].rtcp.link = local->session[i].rtcp.host.map;
						remote->session[i].rtcp.link = from;
						isEnd[i][1]=TRUE;
						break;
					default:
						break;
				}
			}
		}
		n=0;
		for(i=0;i<session;++i)
		{
			if(isEnd[i][0]==TRUE && isEnd[i][1]==TRUE) ++n;
		}
		if(n==session) break;
		/* check timeout */
		if(get_time(0) > time_out)
		{
			for(i=0;i<session;++i)
			{
				if(connectable[i][0]==FALSE)
				{
					send_stun_message(fd[i][0], dest[i][0], &local->session[i].rtp,&remote->session[i].rtp,user_id, 0, 0);
					send_turn_request(fd[i][0], &local->session[i].rtp,	&remote->session[i].rtp,  user_id, 0);
				}
				if(connectable[i][1]==FALSE)
				{
					send_stun_message(fd[i][1], dest[i][1], &local->session[i].rtcp,&remote->session[i].rtcp,user_id, 0, 0);
					send_turn_request(fd[i][1], &local->session[i].rtcp,	&remote->session[i].rtcp,  user_id, 1);
				}
			}
			time_out+=200;
		}
		if(get_time(0)>1000) break;
		ms_usleep(50*1000);
	}
	return n;
}

int ice_connectivity_check(NatCheckList *local, NatCheckList *remote, int host_addr, int magic)
{
	Socket fd[MAXNATSESSION][2];
	int i,j,n;
	int session = local->number;
	bool_t isSameNat=FALSE;

	/* check value */
	if (local == NULL || remote == NULL)
	{
		return -1;
	}

	if (local->magic != magic)
	{
		return -2;
	}
	if(remote->magic != local->magic)
	{
		return -3;
	}

	if (session != remote->number || session <= 0 || session > MAXNATSESSION)
	{
		return -4;
	}

	/* is the same nat? */
	{
		NatSession *ls = &local->session[0];
		NatSession *rs = &remote->session[0];
		isSameNat =(ls->rtp.rflx.map.addr  != 0 && ls->rtp.rflx.map.addr  == rs->rtp.rflx.map.addr) ||
						(ls->rtcp.rflx.map.addr != 0 && ls->rtcp.rflx.map.addr == rs->rtcp.rflx.map.addr);
	}
	/* open port for remote send */
	for (i = 0; i < session; ++i)
	{
		NatSession *ls = &local->session[i];
		fd[i][0] = open_port(ls->rtp.host.map.port,host_addr);
		fd[i][1] = open_port(ls->rtcp.host.map.port, host_addr);
		if(fd[i][0]==INVALID_SOCKET||fd[i][1]==INVALID_SOCKET)
		{
			for(j=0;j<=i;++j)
			{
				if(fd[j][0]!=INVALID_SOCKET) closesocket(fd[j][0]);
				if(fd[j][1]!=INVALID_SOCKET) closesocket(fd[j][1]);
			}
			return -5;
		}
	}

	/* set value */
	n=0;
	random_buffer(user_id, sizeof(user_id));

	/* try to p2p connect */
	if(isSameNat==TRUE)
	{
		n=ice_check_p2p_connectiviy(local, remote, fd, TRUE);
	}
	else if((local->type == StunTypePortRestrictedNat && remote->type == StunTypePortRestrictedNat) ||
			local->type <  StunTypePortRestrictedNat ||
			remote->type <  StunTypePortRestrictedNat)
	{
		n=ice_check_p2p_connectiviy(local, remote, fd, FALSE);
	}

	/* connect to turn server */
	if (n < session)
	{
		/* reset remote link.addr */
		for(i = 0; i < session; i++)
		{
			NatSession *rs = &remote->session[i];
			rs->rtp.link.addr  = 0;
			rs->rtcp.link.addr = 0;
		}
		/* ice_check_p2p_connectiviy */
		n=ice_check_turn_connectiviy(local, remote, fd);
//		LOGI("check turn , n=%d",n);
	}

	/* close port */
	for (i = 0; i < session; i++)
	{
		closesocket(fd[i][0]);
		closesocket(fd[i][1]);
	}
	return (n == session) ? session : 0;
}
