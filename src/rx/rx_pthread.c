/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * An implementation of the rx socket listener for pthreads (not using select).
 * This assumes that multiple read system calls may be extant at any given
 * time. Also implements the pthread-specific event handling for rx.
 *
 * rx_pthread.c is used for the thread safe RX package.
 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header$");

#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#ifndef AFS_NT40_ENV
# include <sys/socket.h>
# include <sys/file.h>
# include <netdb.h>
# include <netinet/in.h>
# include <net/if.h>
# include <sys/ioctl.h>
# include <sys/time.h>
#endif
#include <sys/stat.h>
#include <rx/rx.h>
#include <rx/rx_globals.h>
#include <assert.h>
#include <rx/rx_pthread.h>
#include <rx/rx_clock.h>

/*
 * Number of times the event handling thread was signalled because a new
 * event was scheduled earlier than the lastest event.
 *
 * Protected by event_handler_mutex
 */
static long rx_pthread_n_event_wakeups;

/* Set rx_pthread_event_rescheduled if event_handler should just try
 * again instead of sleeping.
 *
 * Protected by event_handler_mutex
 */
static int rx_pthread_event_rescheduled = 0;

static void *rx_ListenerProc(void *);

/*
 * We supply an event handling thread for Rx's event processing.
 * The condition variable is used to wakeup the thread whenever a new
 * event is scheduled earlier than the previous earliest event.
 * This thread is also responsible for keeping time.
 */
static pthread_t event_handler_thread;
pthread_cond_t rx_event_handler_cond;
pthread_mutex_t event_handler_mutex;
pthread_cond_t rx_listener_cond;
pthread_mutex_t listener_mutex;
static int listeners_started = 0;
pthread_mutex_t rx_clock_mutex;
struct clock rxi_clockNow;

/*
 * Delay the current thread the specified number of seconds.
 */
void
rxi_Delay(int sec)
{
    sleep(sec);
}

/*
 * Called from rx_Init()
 */
void
rxi_InitializeThreadSupport(void)
{
    listeners_started = 0;
    clock_GetTime(&rxi_clockNow);
}

static void *
server_entry(void *argp)
{
    void (*server_proc) () = (void (*)())argp;
    server_proc();
    dpf(("rx_pthread.c: server_entry: Server proc returned unexpectedly\n"));
    exit(1);
    return (void *)0;
}

/*
 * Start an Rx server process.
 */
void
rxi_StartServerProc(void (*proc) (void), int stacksize)
{
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	dpf(("Unable to Create Rx server thread (pthread_attr_init)\n"));
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	dpf
	    (("Unable to Create Rx server thread (pthread_attr_setdetachstate)\n"));
	exit(1);
    }

    /*
     * NOTE: We are ignoring the stack size parameter, for now.
     */
    AFS_SIGSET_CLEAR();
    if (pthread_create(&thread, &tattr, server_entry, (void *)proc) != 0) {
	dpf(("Unable to Create Rx server thread\n"));
	exit(1);
    }
    AFS_SIGSET_RESTORE();
}

/*
 * The event handling process.
 */
static void *
event_handler(void *argp)
{
    struct clock rx_pthread_last_event_wait_time = { 0, 0 };
    unsigned long rx_pthread_n_event_expired = 0;
    unsigned long rx_pthread_n_event_waits = 0;
    long rx_pthread_n_event_woken = 0;
    struct timespec rx_pthread_next_event_time = { 0, 0 };

    assert(pthread_mutex_lock(&event_handler_mutex) == 0);

    for (;;) {
	struct clock cv;
	struct clock next;

	assert(pthread_mutex_unlock(&event_handler_mutex) == 0);

	next.sec = 30;		/* Time to sleep if there are no events scheduled */
	next.usec = 0;
	clock_GetTime(&cv);
	rxevent_RaiseEvents(&next);

	assert(pthread_mutex_lock(&event_handler_mutex) == 0);
	if (rx_pthread_event_rescheduled) {
	    rx_pthread_event_rescheduled = 0;
	    continue;
	}

	clock_Add(&cv, &next);
	rx_pthread_next_event_time.tv_sec = cv.sec;
	rx_pthread_next_event_time.tv_nsec = cv.usec * 1000;
	rx_pthread_n_event_waits++;
	if (pthread_cond_timedwait
	    (&rx_event_handler_cond, &event_handler_mutex,
	     &rx_pthread_next_event_time) == -1) {
#ifdef notdef
	    assert(errno == EAGAIN);
#endif
	    rx_pthread_n_event_expired++;
	} else {
	    rx_pthread_n_event_woken++;
	}
	rx_pthread_event_rescheduled = 0;
    }
}


/*
 * This routine will get called by the event package whenever a new,
 * earlier than others, event is posted. */
void
rxi_ReScheduleEvents(void)
{
    assert(pthread_mutex_lock(&event_handler_mutex) == 0);
    pthread_cond_signal(&rx_event_handler_cond);
    rx_pthread_event_rescheduled = 1;
    assert(pthread_mutex_unlock(&event_handler_mutex) == 0);
}


/* Loop to listen on a socket. Return setting *newcallp if this
 * thread should become a server thread.  */
static void
rxi_ListenerProc(int sock, int *tnop, struct rx_call **newcallp)
{
    struct sockaddr_storage saddr;
    int slen;
    register struct rx_packet *p = (struct rx_packet *)0;

    assert(pthread_mutex_lock(&listener_mutex) == 0);
    while (!listeners_started) {
	assert(pthread_cond_wait(&rx_listener_cond, &listener_mutex) == 0);
    }
    assert(pthread_mutex_unlock(&listener_mutex) == 0);

    for (;;) {
	/*
	 * Grab a new packet only if necessary (otherwise re-use the old one)
	 */
	if (p) {
	    rxi_RestoreDataBufs(p);
	} else {
	    if (!(p = rxi_AllocPacket(RX_PACKET_CLASS_RECEIVE))) {
		/* Could this happen with multiple socket listeners? */
		dpf(("rxi_Listener: no packets!"));	/* Shouldn't happen */
		exit(1);
	    }
	}

	slen = sizeof(saddr);
	if (rxi_ReadPacket(sock, p, &saddr, &slen)) {
	    clock_NewTime();
	    p = rxi_ReceivePacket(p, sock, &saddr, slen, tnop, newcallp);
	    if (newcallp && *newcallp) {
		if (p)
		    rxi_FreePacket(p);
		return;
	    }
	}
    }
    /* NOTREACHED */
}

/* This is the listener process request loop. The listener process loop
 * becomes a server thread when rxi_ListenerProc returns, and stays
 * server thread until rxi_ServerProc returns. */
static void *
rx_ListenerProc(void *argp)
{
    int threadID;
    int sock = (int)argp;
    struct rx_call *newcall;

    while (1) {
	newcall = NULL;
	threadID = -1;
	rxi_ListenerProc(sock, &threadID, &newcall);
	/* assert(threadID != -1); */
	/* assert(newcall != NULL); */
	sock = OSI_NULLSOCKET;
	assert(pthread_setspecific(rx_thread_id_key, (void *)threadID) == 0);
	rxi_ServerProc(threadID, newcall, &sock);
	/* assert(sock != OSI_NULLSOCKET); */
    }
    /* not reached */
}

/* This is the server process request loop. The server process loop
 * becomes a listener thread when rxi_ServerProc returns, and stays
 * listener thread until rxi_ListenerProc returns. */
void
rx_ServerProc(void)
{
    int sock;
    int threadID;
    struct rx_call *newcall = NULL;

    rxi_MorePackets(rx_maxReceiveWindow + 2);	/* alloc more packets */
    MUTEX_ENTER(&rx_stats_mutex);
    rxi_dataQuota += rx_initSendWindow;	/* Reserve some pkts for hard times */
    /* threadID is used for making decisions in GetCall.  Get it by bumping
     * number of threads handling incoming calls */
    /* Unique thread ID: used for scheduling purposes *and* as index into
     * the host hold table (fileserver). 
     * The previously used rxi_availProcs is unsuitable as it
     * will already go up and down as packets arrive while the server
     * threads are still initialising! The recently introduced
     * rxi_pthread_hinum does not necessarily lead to a server
     * thread with id 0, which is not allowed to hop through the
     * incoming call queue.
     * So either introduce yet another counter or flag the FCFS
     * thread... chose the latter.
     */
    threadID = ++rxi_pthread_hinum;
    if (rxi_fcfs_thread_num == 0 && rxi_fcfs_thread_num != threadID)
	rxi_fcfs_thread_num = threadID;
    ++rxi_availProcs;
    MUTEX_EXIT(&rx_stats_mutex);

    while (1) {
	sock = OSI_NULLSOCKET;
	assert(pthread_setspecific(rx_thread_id_key, (void *)threadID) == 0);
	rxi_ServerProc(threadID, newcall, &sock);
	/* assert(sock != OSI_NULLSOCKET); */
	newcall = NULL;
	rxi_ListenerProc(sock, &threadID, &newcall);
	/* assert(threadID != -1); */
	/* assert(newcall != NULL); */
    }
    /* not reached */
}

/*
 * Historically used to start the listener process. We now have multiple
 * listener processes (one for each socket); these are started by GetUdpSocket.
 *
 * The event handling process *is* started here (the old listener used
 * to also handle events). The listener threads can't actually start 
 * listening until rxi_StartListener is called because most of R may not
 * be initialized when rxi_Listen is called.
 */
void
rxi_StartListener(void)
{
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	dpf
	    (("Unable to create Rx event handling thread (pthread_attr_init)\n"));
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	dpf
	    (("Unable to create Rx event handling thread (pthread_attr_setdetachstate)\n"));
	exit(1);
    }

    AFS_SIGSET_CLEAR();
    if (pthread_create(&event_handler_thread, &tattr, event_handler, NULL) !=
	0) {
	dpf(("Unable to create Rx event handling thread\n"));
	exit(1);
    }
    MUTEX_ENTER(&rx_stats_mutex);
    ++rxi_pthread_hinum;
    MUTEX_EXIT(&rx_stats_mutex);
    AFS_SIGSET_RESTORE();

    assert(pthread_mutex_lock(&listener_mutex) == 0);
    assert(pthread_cond_broadcast(&rx_listener_cond) == 0);
    listeners_started = 1;
    assert(pthread_mutex_unlock(&listener_mutex) == 0);

}

/*
 * Listen on the specified socket.
 */
int
rxi_Listen(osi_socket sock)
{
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	dpf
	    (("Unable to create socket listener thread (pthread_attr_init)\n"));
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	dpf
	    (("Unable to create socket listener thread (pthread_attr_setdetachstate)\n"));
	exit(1);
    }

    AFS_SIGSET_CLEAR();
    if (pthread_create(&thread, &tattr, rx_ListenerProc, (void *)sock) != 0) {
	dpf(("Unable to create socket listener thread\n"));
	exit(1);
    }
    MUTEX_ENTER(&rx_stats_mutex);
    ++rxi_pthread_hinum;
    MUTEX_EXIT(&rx_stats_mutex);
    AFS_SIGSET_RESTORE();
    return 0;
}

static void
rx_TcpListenerProc(void *argp)
{
    osi_socket sock = (osi_socket) argp, connsock;
    struct sockaddr_in sin;
    socklen_t sinlen = sizeof(sin);
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	printf
	    ("Unable to create socket listener thread (pthread_attr_init)\n");
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	printf
	    ("Unable to create socket listener thread (pthread_attr_setdetachstate)\n");
	exit(1);
    }

    while (1) {
	if ((connsock = accept(sock, (struct sockaddr *) &sin, &sinlen)) < 0) {
	    perror("accept");
	    exit(1);
	}

	AFS_SIGSET_CLEAR();
	if (pthread_create(&thread, &tattr, rxi_TcpNewServerConnection,
			   (void *) connsock) != 0) {
	    printf("Unable to create socket listener thread\n");
	    exit(1);
	}
	AFS_SIGSET_RESTORE();
    }
}
    
int
rxi_ListenTcp(osi_socket sock)
{
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	printf
	    ("Unable to create socket listener thread (pthread_attr_init)\n");
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	printf
	    ("Unable to create socket listener thread (pthread_attr_setdetachstate)\n");
	exit(1);
    }

    AFS_SIGSET_CLEAR();
    if (pthread_create(&thread, &tattr, rx_TcpListenerProc,
		       (void *)sock) != 0) {
	printf("Unable to create socket listener thread\n");
	exit(1);
    }
    MUTEX_ENTER(&rx_stats_mutex);
    ++rxi_pthread_hinum;
    MUTEX_EXIT(&rx_stats_mutex);
    AFS_SIGSET_RESTORE();
    return 0;
}

/*
 * Start connection reader and writer threads
 */

void
rxi_TcpStartConnectionThreads(struct rx_connection *conn)
{
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	printf("Unable to create connection threads (pthread_attr_init)\n");
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	printf("Unable to create connection threads (pthread_attr_setdeatchstate)\n");
	exit(1);
    }

    AFS_SIGSET_CLEAR();
    if (pthread_create(&thread, &tattr, rxi_TcpReaderThread,
		       (void *) conn) != 0) {
	printf("Unable to create connection reader thread\n");
	exit(1);
    }

#if 0
    if (pthread_create(&thread, &tattr, rxi_TcpWriterThread,
		       (void *) conn) != 0) {
	printf("Unable to create connection writer thread\n");
	exit(1);
    }
#endif
    AFS_SIGSET_RESTORE();
}
#if 0

/*
 * Wait for TCP connect() calls to complete.  Note that the connection is
 * locked upon entering of this function.
 */

static void *rx_TcpConnectWait(void *arg)
{
    struct rx_connection *conn = (struct rx_connection *) arg;
    int err;

#if 0
    memset((void *) &sin, 0, sizeof(sin));

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = rx_HostOf(rx_PeerOf(conn));
    sin.sin_port = rx_PortOf(rx_PeerOf(conn));

    err = connect(conn->tcpDescriptor, (struct sockaddr *) &sin, sizeof(sin));

    if (err < 0) {
	close(conn->tcpDescriptor);
	conn->tcpDescriptor = -1;
	MUTEX_EXIT(&conn->conn_call_lock);
	return;
    }
#endif
    /*
     * Start the connection negotiation
     */

    rxi_StartTcpClient(conn);

    MUTEX_EXIT(&conn->conn_call_lock);
    return;
}
#endif

/*
 * Start a thread to wait for TCP connect() calls to complete
 */

#if 0
int
rxi_TcpConnectStart(struct rx_connection *conn)
{
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	printf
	    ("Unable to create TCP connection wait thread (pthread_attr_init)\n");
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	printf
	    ("Unable to create TCP connection wait thread (pthread_attr_setdetachstate)\n");
	exit(1);
    }

    AFS_SIGSET_CLEAR();
    if (pthread_create(&thread, &tattr, rx_TcpConnectWait, (void *)conn) != 0) {
	printf("Unable to create TCP connection wait thread\n");
	exit(1);
    }
    MUTEX_ENTER(&rx_stats_mutex);
    ++rxi_pthread_hinum;
    MUTEX_EXIT(&rx_stats_mutex);
    AFS_SIGSET_RESTORE();
    return 0;
}
#endif

void
rx_TcpServerProc(struct rx_call *call)
{
    pthread_t thread;
    pthread_attr_t tattr;
    AFS_SIGSET_DECL;

    if (pthread_attr_init(&tattr) != 0) {
	printf
	    ("Unable to create call handler thread (pthread_attr_init)\n");
	exit(1);
    }

    if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
	printf
	    ("Unable to create call handler thread (pthread_attr_setdetachstate)\n");
	exit(1);
    }

    AFS_SIGSET_CLEAR();
    if (pthread_create(&thread, &tattr, rxi_TcpServerProc, (void *)call) != 0) {
	printf("Unable to create call handler thread\n");
	exit(1);
    }
    MUTEX_ENTER(&rx_stats_mutex);
    ++rxi_pthread_hinum;
    MUTEX_EXIT(&rx_stats_mutex);
    AFS_SIGSET_RESTORE();
}

/*
 * Recvmsg.
 *
 */
int
rxi_Recvmsg(osi_socket socket, struct msghdr *msg_p, int flags)
{
    int ret;
    ret = recvmsg(socket, msg_p, flags);
    return ret;
}

/*
 * Sendmsg.
 */
int
rxi_Sendmsg(osi_socket socket, struct msghdr *msg_p, int flags)
{
    int ret;
    ret = sendmsg(socket, msg_p, flags);
#ifdef AFS_LINUX22_ENV
    /* linux unfortunately returns ECONNREFUSED if the target port
     * is no longer in use */
    /* and EAGAIN if a UDP checksum is incorrect */
    if (ret == -1 && errno != ECONNREFUSED && errno != EAGAIN) {
#else
    if (ret == -1) {
#endif
	dpf(("rxi_sendmsg failed, error %d\n", errno));
	fflush(stdout);
	return -1;
    }
    return 0;
}

struct rx_ts_info_t * rx_ts_info_init() {
    register struct rx_ts_info_t * rx_ts_info;
    rx_ts_info = (rx_ts_info_t *) malloc(sizeof(rx_ts_info_t));
    assert(rx_ts_info != NULL && pthread_setspecific(rx_ts_info_key, rx_ts_info) == 0);
    memset(rx_ts_info, 0, sizeof(rx_ts_info_t));
#ifdef RX_ENABLE_TSFPQ
    queue_Init(&rx_ts_info->_FPQ);

    MUTEX_ENTER(&rx_stats_mutex);
    rx_TSFPQMaxProcs++;
    RX_TS_FPQ_COMPUTE_LIMITS;
    MUTEX_EXIT(&rx_stats_mutex);
#endif /* RX_ENABLE_TSFPQ */
    return rx_ts_info;
}
