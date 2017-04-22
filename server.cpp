#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>

//Default port number and IP
#define PORT 7735
#define CLI_IP "127.0.0.1"

typedef unsigned short u16;
typedef unsigned long u32;


/**
 * Used to determine if error occured or not.
 * Uses the error probability parameter.
 * Returns true if error occured.
 */
bool rand (double prob)
{
	double randval = static_cast <double> (rand()) / static_cast <double> (RAND_MAX);
	//std::cout<<"r="<<r<<" "<<"p="<<p<<std::endl;

	if (randval <= prob) return false;

    return true;
}

/**
 * Error printer
 */
void diep(char const*s)
{
    perror(s);
}

/**
 * Structure holding udp packet header
 */
struct MessageHeader {
     uint32_t seqno;
     uint16_t checksum;  
     uint16_t flag;
};


int main(int argc, char* argv[])
{
    if (argc < 4){
        printf ("\nArguments: port, filename, error probability\n");
        return 0;
    }

	//Reading input from the commandline
	int portnum = atoi(argv[1]);
  	char* filename = argv[2];
  	double p = atof(argv[3]);    
    int ssocket;    	
	int bytes_read;
   	char recv_data[65535];
	std::ofstream f1;
	int expected_seqno = 0;

    struct sockaddr_in serverAddr, clientAddr;
    struct sockaddr_storage serverStorage;
    socklen_t addr_size, client_addr_size;
    int i;

    //Configure settings in address struct
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7736);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr("127.0.0.1");
    memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);

    //Initialize size variable to be used later on
    addr_size = sizeof (serverStorage);

	//Setting up the UDP socket
    if ((ssocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Socket could not be created, quitting!!!");
        exit(1);
    }

	//Binding the socket to a port
    if (bind(ssocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Bind failed, quitting!!!");
        exit(1);
    }

	printf("\nUDPServer Waiting for client to send file on port %d", PORT);
	fflush(stdout);
    uint32_t rcvd;
	uint16_t flag;
	uint16_t checksum;
	
    while (1) {

	    memset (&recv_data, 0, 65535);
        bytes_read = recvfrom (ssocket,recv_data,65535,0, (struct sockaddr *)&serverStorage, &addr_size);
	    //printf ("\nReceived Size = %d", bytes_read);
        
        bool doiprocess = rand(p);
		
        if (doiprocess == false) {
			std::cout<<"\nPacket loss, sequence number = "<< ((MessageHeader*)recv_data)->seqno << "\n";
			continue;
		} else {
            rcvd = ((MessageHeader*)recv_data)->seqno;
			flag = ((MessageHeader*)recv_data)->flag;
			checksum = ((MessageHeader*)recv_data)->checksum;
			
            int payloadsize = bytes_read - sizeof(MessageHeader); 
			char* payload = (char*)recv_data + sizeof(MessageHeader); 
			
            printf("\nReceived %d, expected %d", rcvd, expected_seqno);
			
            if (rcvd != expected_seqno) {
				printf("\nSeq didnt match");
				continue;
			} else if (flag != 21845) {
				printf("\nFlag didnt match");
				continue;
			} else if (checksum != 0) {
				printf("\nChecksum didnt match");
				continue;
			} else {
				char* sendbuf = (char*)calloc(1,sizeof(MessageHeader));
				
                //printf("\nWriting to file = %s\nSize written = %d", payload, payloadsize);
				f1.open(filename, std::ios::binary|std::ios::out|std::ios::app);
				f1.write(payload, payloadsize);
				f1.close();
			    
                ((MessageHeader*)sendbuf)->seqno = expected_seqno;
				((MessageHeader*)sendbuf)->checksum = 0;
				((MessageHeader*)sendbuf)->flag = 43690;
				int val = 0;
                printf ("\nSending %d %d %d", ((MessageHeader*)sendbuf)->seqno, ((MessageHeader*)sendbuf)->flag, ((MessageHeader*)sendbuf)->checksum);
				val = sendto(ssocket, sendbuf, sizeof(MessageHeader) , 0,  (struct sockaddr *)&serverStorage, addr_size);
				if (val == -1){
      		        diep("sendto() failed");
                    return 0;
				}
			
                free(sendbuf);
			    std::cout<<"\nPacket no. "<<expected_seqno<<" written "<< payloadsize<<" bytes and ack sent"<<std::endl;
			    ++expected_seqno;
			}
		}
    }

	f1.close();
    return 0;
}

uint16_t checksumf(char* buf, int blen){

  u16 word16;
  u32 sum;
  u16 padd = 0;
  if (blen % 2 !=0) {
    buf[blen]=0; //padding with last byte
    padd = 1;}
  // make 16 bit words out of every two adjacent 8 bit words and calc sum

  for (int i=0;i<blen+padd;i=i+2){
    word16 =((buf[i]<<8)&0xFF00)+(buf[i+1]&0xFF);
    sum = sum + (unsigned long)word16;
  while (sum>>16)
        sum = (sum & 0xFFFF)+(sum >> 16);
  sum = ~sum;
  return ((u16) sum);
        }
}

