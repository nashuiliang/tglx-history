/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  It is mainly used for debugging and
 *		statistics.
 *
 * Version:	$Id: proc.c,v 1.45 2001/05/16 16:45:35 davem Exp $
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Gerald J. Heim, <heim@peanuts.informatik.uni-tuebingen.de>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *		Erik Schoenfelder, <schoenfr@ibr.cs.tu-bs.de>
 *
 * Fixes:
 *		Alan Cox	:	UDP sockets show the rxqueue/txqueue
 *					using hint flag for the netinfo.
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Make /proc safer.
 *	Erik Schoenfelder	:	/proc/net/snmp
 *		Alan Cox	:	Handle dead sockets properly.
 *	Gerhard Koerting	:	Show both timers
 *		Alan Cox	:	Allow inode to be NULL (kernel socket)
 *	Andi Kleen		:	Add support for open_requests and
 *					split functions for more readibility.
 *	Andi Kleen		:	Add support for /proc/net/netstat
 *	Arnaldo C. Melo		:	Convert to seq_file
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/sock.h>
#include <net/raw.h>

static int fold_prot_inuse(struct proto *proto)
{
	int res = 0;
	int cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		res += proto->stats[cpu].inuse;

	return res;
}

/*
 *	Report socket allocation statistics [mea@utu.fi]
 */
static int sockstat_seq_show(struct seq_file *seq, void *v)
{
	/* From net/socket.c */
	extern void socket_seq_show(struct seq_file *seq);

	socket_seq_show(seq);
	seq_printf(seq, "TCP: inuse %d orphan %d tw %d alloc %d mem %d\n",
		   fold_prot_inuse(&tcp_prot), atomic_read(&tcp_orphan_count),
		   tcp_tw_count, atomic_read(&tcp_sockets_allocated),
		   atomic_read(&tcp_memory_allocated));
	seq_printf(seq, "UDP: inuse %d\n", fold_prot_inuse(&udp_prot));
	seq_printf(seq, "RAW: inuse %d\n", fold_prot_inuse(&raw_prot));
	seq_printf(seq,  "FRAG: inuse %d memory %d\n", ip_frag_nqueues,
		   atomic_read(&ip_frag_mem));
	return 0;
}

static int sockstat_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sockstat_seq_show, NULL);
}

static struct file_operations sockstat_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = sockstat_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

static unsigned long
fold_field(void *mib[], int offt)
{
	unsigned long res = 0;
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		res += *(((unsigned long *) per_cpu_ptr(mib[0], i)) + offt);
		res += *(((unsigned long *) per_cpu_ptr(mib[1], i)) + offt);
	}
	return res;
}

/* snmp items */
static struct snmp_mib snmp4_ipstats_list[] = {
#define SNMP_GEN(n,e)	SNMP_MIB_ITEM(n,e)
	SNMP_GEN("InReceives", IPSTATS_MIB_INRECEIVES),
	SNMP_GEN("InHdrErrors", IPSTATS_MIB_INHDRERRORS),
	SNMP_GEN("InAddrErrors", IPSTATS_MIB_INADDRERRORS),
	SNMP_GEN("ForwDatagrams", IPSTATS_MIB_OUTFORWDATAGRAMS),
	SNMP_GEN("InUnknownProtos", IPSTATS_MIB_INUNKNOWNPROTOS),
	SNMP_GEN("InDiscards", IPSTATS_MIB_INDISCARDS),
	SNMP_GEN("InDelivers", IPSTATS_MIB_INDELIVERS),
	SNMP_GEN("OutRequests", IPSTATS_MIB_OUTREQUESTS),
	SNMP_GEN("OutDiscards", IPSTATS_MIB_OUTDISCARDS),
	SNMP_GEN("OutNoRoutes", IPSTATS_MIB_OUTNOROUTES),
	SNMP_GEN("ReasmTimeout", IPSTATS_MIB_REASMTIMEOUT),
	SNMP_GEN("ReasmReqds", IPSTATS_MIB_REASMREQDS),
	SNMP_GEN("ReasmOKs", IPSTATS_MIB_REASMOKS),
	SNMP_GEN("ReasmFails", IPSTATS_MIB_REASMFAILS),
	SNMP_GEN("FragOKs", IPSTATS_MIB_FRAGOKS),
	SNMP_GEN("FragFails", IPSTATS_MIB_FRAGFAILS),
	SNMP_GEN("FragCreates", IPSTATS_MIB_FRAGCREATES),
	SNMP_MIB_SENTINEL
#undef SNMP_GEN
};

/*
 *	Called from the PROCfs module. This outputs /proc/net/snmp.
 */
static int snmp_seq_show(struct seq_file *seq, void *v)
{
	int i;

	seq_printf(seq, "Ip: Forwarding DefaultTTL");

	for (i = 0; snmp4_ipstats_list[i].name != NULL; i++)
		seq_printf(seq, " %s", snmp4_ipstats_list[i].name);

	seq_printf(seq, "\nIp: %d %d",
			ipv4_devconf.forwarding ? 1 : 2, sysctl_ip_default_ttl);

	for (i = 0; snmp4_ipstats_list[i].name != NULL; i++)
		seq_printf(seq, " %lu",
			   fold_field((void **) ip_statistics, 
				      snmp4_ipstats_list[i].entry));

	seq_printf(seq, "\nIcmp: InMsgs InErrors InDestUnreachs InTimeExcds "
			"InParmProbs InSrcQuenchs InRedirects InEchos "
			"InEchoReps InTimestamps InTimestampReps InAddrMasks "
			"InAddrMaskReps OutMsgs OutErrors OutDestUnreachs "
			"OutTimeExcds OutParmProbs OutSrcQuenchs OutRedirects "
			"OutEchos OutEchoReps OutTimestamps OutTimestampReps "
			"OutAddrMasks OutAddrMaskReps\nIcmp:");

	for (i = 1; i < __ICMP_MIB_MAX; i++)
		seq_printf(seq, " %lu",
			   fold_field((void **) icmp_statistics, i)); 

	seq_printf(seq, "\nTcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens "
			"PassiveOpens AttemptFails EstabResets CurrEstab "
			"InSegs OutSegs RetransSegs InErrs OutRsts\nTcp:");

	for (i = 1; i < __TCP_MIB_MAX; i++) {
		if (i == TCP_MIB_MAXCONN)
			/* MaxConn field is negative, RFC 2012 */
			seq_printf(seq, " %ld", 
				   fold_field((void **) tcp_statistics, i));
		else
			seq_printf(seq, " %lu", 
				   fold_field((void **) tcp_statistics, i));
	}

	seq_printf(seq, "\nUdp: InDatagrams NoPorts InErrors OutDatagrams\n"
			"Udp:");

	for (i = 1; i < __UDP_MIB_MAX; i++)
		seq_printf(seq, " %lu", 
				fold_field((void **) udp_statistics, i));

	seq_putc(seq, '\n');
	return 0;
}

static int snmp_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, snmp_seq_show, NULL);
}

static struct file_operations snmp_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = snmp_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

/*
 *	Output /proc/net/netstat
 */
static int netstat_seq_show(struct seq_file *seq, void *v)
{
	int i;

	seq_puts(seq, "TcpExt: SyncookiesSent SyncookiesRecv SyncookiesFailed"
		      " EmbryonicRsts PruneCalled RcvPruned OfoPruned"
		      " OutOfWindowIcmps LockDroppedIcmps ArpFilter"
		      " TW TWRecycled TWKilled"
		      " PAWSPassive PAWSActive PAWSEstab"
		      " DelayedACKs DelayedACKLocked DelayedACKLost"
		      " ListenOverflows ListenDrops"
		      " TCPPrequeued TCPDirectCopyFromBacklog"
		      " TCPDirectCopyFromPrequeue TCPPrequeueDropped"
		      " TCPHPHits TCPHPHitsToUser"
		      " TCPPureAcks TCPHPAcks"
		      " TCPRenoRecovery TCPSackRecovery"
		      " TCPSACKReneging"
		      " TCPFACKReorder TCPSACKReorder TCPRenoReorder"
		      " TCPTSReorder"
		      " TCPFullUndo TCPPartialUndo TCPDSACKUndo TCPLossUndo"
		      " TCPLoss TCPLostRetransmit"
		      " TCPRenoFailures TCPSackFailures TCPLossFailures"
		      " TCPFastRetrans TCPForwardRetrans TCPSlowStartRetrans"
		      " TCPTimeouts"
		      " TCPRenoRecoveryFail TCPSackRecoveryFail"
		      " TCPSchedulerFailed TCPRcvCollapsed"
		      " TCPDSACKOldSent TCPDSACKOfoSent TCPDSACKRecv"
		      " TCPDSACKOfoRecv"
		      " TCPAbortOnSyn TCPAbortOnData TCPAbortOnClose"
		      " TCPAbortOnMemory TCPAbortOnTimeout TCPAbortOnLinger"
		      " TCPAbortFailed TCPMemoryPressures\n"
		      "TcpExt:");
	for (i = 1; i < __LINUX_MIB_MAX; i++)
		seq_printf(seq, " %lu", 
		 	   fold_field((void **) net_statistics, i)); 
	seq_putc(seq, '\n');
	return 0;
}

static int netstat_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, netstat_seq_show, NULL);
}

static struct file_operations netstat_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = netstat_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

int __init ip_misc_proc_init(void)
{
	int rc = 0;

	if (!proc_net_fops_create("netstat", S_IRUGO, &netstat_seq_fops))
		goto out_netstat;

	if (!proc_net_fops_create("snmp", S_IRUGO, &snmp_seq_fops))
		goto out_snmp;

	if (!proc_net_fops_create("sockstat", S_IRUGO, &sockstat_seq_fops))
		goto out_sockstat;
out:
	return rc;
out_sockstat:
	proc_net_remove("snmp");
out_snmp:
	proc_net_remove("netstat");
out_netstat:
	rc = -ENOMEM;
	goto out;
}

