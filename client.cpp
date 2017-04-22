#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <list>
#include <errno.h>
#include <fstream>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <pthread.h>
#include <netdb.h> 

//Default port and ip
#define PORT 7736
#define SRV_IP "127.0.0.1"

typedef unsigned short u16;
typedef unsigned long u32;

//TO value for each udp packet in seconds
int TIMEOUT = 4;

//Lock to perform thread safe operations on list DS
pthread_mutex_t ListLock = PTHREAD_MUTEX_INITIALIZER;


/**
 * structure defining the header of each udp packet
 */
struct MessageHeader {
    uint32_t seqno;
    uint16_t checksum;	
    uint16_t flag;
};

/**
 * structure defining the header of each udp packet
 */
struct ListNode {
    char *packet;
    time_t timestamp;
    int size;
};

//Global udp socket
int csocket;

//List having sent packets, pending for ack
std::list<ListNode*> MsgList;

/**
 * Error printer
 */
void diep(char const*s)
{
    perror(s);
    exit(1); 
}

/**
 * Returns current time in sec since epoch
 */
time_t GetCurrentTime () 
{
    return time(0);
}

/**
 * Checks if the 1t packet has timed out
 */
bool HasTimedout () {
    time_t curtime = GetCurrentTime ();

    pthread_mutex_lock (&ListLock);

    if (MsgList.size() > 0){
        std::list<ListNode*>::iterator it = MsgList.begin();
        ListNode *node = (ListNode*)(*it);

        if (curtime - node->timestamp >= TIMEOUT) {
            printf("\nTimeout, sequence number = %d\n",((MessageHeader*)(node->packet))->seqno);
            pthread_mutex_unlock (&ListLock);
            return true;
        }
    }
    pthread_mutex_unlock (&ListLock);
    return false;
}

/**
 * Adds a new node to the list with buf as the packet and current time as timestamp
 */
void AddToList (char *buf, int size) {
    ListNode *node = (ListNode*)calloc(1,sizeof(ListNode));
    node->packet = buf;
    node->timestamp = GetCurrentTime();
    node->size = size;
    pthread_mutex_lock (&ListLock);
    MsgList.push_back (node);
    pthread_mutex_unlock (&ListLock);
}

/**
 * Returns size of msg list in a thread safe way
 */
int GetListSize ()
{
    int size = 0;
    
    pthread_mutex_lock (&ListLock);
    size = MsgList.size();
    pthread_mutex_unlock (&ListLock);
        
    return size;
}

/**
 * Once time out has occured, this method re-sends all packets from the timed out packet.
 * returns true if successful, else false
 */
bool HandleTimeouts (struct sockaddr_in *si_other, int slen, int MSS){
        
    std::list<ListNode*>::iterator it;
    char *buf = NULL;
        
    pthread_mutex_lock (&ListLock);
    for (it = MsgList.begin(); it != MsgList.end(); ++it) {
        //Send each one
	    ListNode *node = (ListNode*)(*it);
        buf = node->packet;
        //printf("\nSending %d", ((MessageHeader*)buf)->seqno);
        if (sendto(csocket, buf, node->size, 0,(struct sockaddr *)si_other, slen)==-1) {
            diep("Send failed, quitting !!!");
            pthread_mutex_unlock (&ListLock);
            return false;
        } else {             
            node->timestamp = GetCurrentTime ();
        }
    }
        
    pthread_mutex_unlock (&ListLock);
    return true; 
}

/**
 * Receives acks and updates the Msg list appropriately
 */
static void *RecvThreadFunc (void *input)
{
    int retval = 0;
    char buf[65535];
	struct sockaddr_in addr;
	MessageHeader *header;        
    socklen_t addrlen;
    int recvseqno; 

    printf("\nReady and recving ack\n");

    while (1){
	    memset(buf, 0, 65535);
        retval = recvfrom(csocket, buf, sizeof(MessageHeader), 0, (struct sockaddr*)&addr, &addrlen);

        if (retval == -1) {
           diep ("\nRecv failed, quiting!!!"); 
           return NULL;
        }

        header = (MessageHeader*)buf;
        recvseqno = header->seqno;
        printf ("\nReceived ack for %d", header->seqno);
	    std::list<ListNode*>::iterator it;
	    int count = 0;
        pthread_mutex_lock (&ListLock);
        for (it = MsgList.begin(); it != MsgList.end(); ++it) {
            ListNode *node = (ListNode*)(*it);
		    char *data = node->packet;
                
            if (recvseqno >= ((MessageHeader*)data)->seqno) {
                ++count;
            } else {
                break;
            }            
        }
	   
        printf("\nCount = %d\n",  count);
        for (int i = 0; i< count; ++i){	
            ListNode *node = (ListNode*)(MsgList.front());
		    char *data = node->packet;
            printf("\nPoping %d\n", ((MessageHeader*)data)->seqno);
            free(data);
		    free(node);
            MsgList.pop_front ();
	    }
            
	    pthread_mutex_unlock (&ListLock);
    }
}

int main(int argc, char* argv[])
{
    //Invalid argument set
	if (argc < 6){
	    printf("\nParameters: Server address, server port, file name, N, MSS\n");
	    return 0;
	} 

    char* servaddr = argv[1];
    int servport = atoi(argv[2]);
    char* filename = argv[3];
    int N = atoi(argv[4]);
    int MSS = atoi(argv[5]);
    int SENTCOUNT = 0;	
    int seqno = 0;
	struct sockaddr_in serveraddr;
	struct hostent *server;
    int slen; 
    char *buf = NULL;
    pthread_t recvthread; 
        
    csocket = socket(AF_INET, SOCK_DGRAM,0);
    
    if (csocket == -1) {
        diep("\nSocket creation failed, quitting!!!");
        return 0;
    }
       
	server = gethostbyname(servaddr);
    if (server == NULL) {
       	fprintf(stderr,"ERROR, no such host as %s\n", servaddr);
       	exit(0);
    }
	
	bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	(char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(servport);	
	slen = sizeof(serveraddr);

    //Spawn recv thread
    int rc = pthread_create(&recvthread, NULL, RecvThreadFunc, NULL);

    if (rc != 0) {
        printf ("\n Could not create thread, error = %d, quitting!!!", errno);
        return 0;
    }
      
        
    std::ifstream f1;
    f1.open(filename, std::ios::binary);
   
    time_t stime = GetCurrentTime ();
    while (1) {
        
        if (GetListSize() < N) {
            if (HasTimedout() == true) {
                if (HandleTimeouts (&serveraddr, slen, MSS) == false) {
                    return 0;
                }                    
                continue;
            }
            
            buf = (char*)calloc(1, sizeof(MessageHeader) + MSS);
		    char * text = buf+sizeof(MessageHeader);
            f1.read(text, MSS);
                
            ((MessageHeader*)buf)->seqno = seqno++;
            ((MessageHeader*)buf)->flag = 21845;
            ((MessageHeader*)buf)->checksum = 0;
            printf("\nSending %d, %d", ((MessageHeader*)buf)->seqno, ((MessageHeader*)buf)->flag);
               
            if (f1){
            //Read full MSS
		        
                int retval = 0;				                
                retval = sendto(csocket, buf, sizeof(MessageHeader)+MSS, 0, (struct sockaddr *)&serveraddr, slen);
                
                if (retval==-1) {
                    diep("\nSend failed, quitting !!!");
                    return 0;
                } else {             
                    AddToList (buf, sizeof(MessageHeader)+MSS);         
                }
            } else {
            //Sending last few bytes and exit
		    
                int bytestosend = f1.gcount();
                printf("\nSending last %d bytes\n", bytestosend);
                if (sendto(csocket, buf, sizeof(MessageHeader)+bytestosend, 0, (struct sockaddr *)&serveraddr, slen)==-1) {
                    diep("\nSend failed, quitting !!!");
                    return 0;
                } else {
                    AddToList (buf, sizeof(MessageHeader)+bytestosend);
                    printf ("\nSent full file. Waiting for pending ACKs\n");
                    int n = GetListSize();                    
                    while (n > 0){
                        //printf("\nChecking for timeout, %d\n", n);
                        if (HasTimedout () == true) {
                            if (HandleTimeouts (&serveraddr, slen, MSS) == false) {
                                return 0;
                            }
                        } else {
                            sleep (1);
                        }
                        n = GetListSize();                    
                    }
                    printf ("\nAll ACKs received, Quitting!!!\n");
                    time_t etime = GetCurrentTime ();
                    printf("\nTotal time taken = %ld seconds\n", (int)etime-stime);
                    return 0;
                }
            }
        } else {
		    
            printf("\nWindow is == N. Waiting");
            //Keep checking for TO and SENDCOUNT values to become favorable            
            if (GetListSize() < N) {
                continue;
            } else if (HasTimedout () == true) {
                //Handle this seperately.
                if (HandleTimeouts (&serveraddr, slen, MSS) == false) {
                    return 0;
                }
                continue;               
            } else {
             
                //Sleep 1 second n check again
                sleep(1);
            }
        }
    }
        
    f1.close();
    close(csocket);
    return 0;
}

    uint16_t checksumf(char* buf, int blen){
        u16 word16;
        u32 sum;
        u16 padd = 0;
      
        if (blen % 2 !=0) {
            buf[blen]=0; //padding with last byte
            padd = 1;
        }
        
        // make 16 bit words out of every two adjacent 8 bit words and calc sum

        for (int i=0;i<blen+padd;i=i+2){
            word16 =((buf[i]<<8)&0xFF00)+(buf[i+1]&0xFF);
            sum = sum + (unsigned long)word16;
        }
        
        while (sum>>16)
            sum = (sum & 0xFFFF)+(sum >> 16);
        sum = ~sum;
        return ((u16) sum);
        
    }


