/*
 * File:   receiver_main.c
 * Author:
 *
 * Created on
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <iostream>

using namespace std;

/*****************************************************************/

#define MSS 2000      // maximum segment size

typedef enum MSG_TYPES
{
    ACK,
    DATA,
    FIN,
    FIN_ACK,
    NONE
} Msg_type;

typedef class Packet
{
public:
    /* data */
    int data_size;
    int seq_num;
    int ACK_num;
    Msg_type type;
    char data[MSS];

    Packet()
    {
        this->type = NONE;
    };
    Packet(char *pkt_buf)
    {
        memcpy(this, pkt_buf, sizeof(Packet));
    };
    Packet(Msg_type type)
    {
        this->type = type;
        if (ACK == type or FIN == type or FIN_ACK == type)
            this->data_size = 0;
    };
    ~Packet() = default;
} Packet_t;

/*****************************************************************/

struct sockaddr_in si_me, si_other;
int sock_fd;
unsigned int slen;

// ================================================================
/* shared variables */
int num_bytes;
char pkt_buf[sizeof(Packet_t)];
vector<Packet_t> incontinuous_pkts; // Buffer for received packet that is incontinuous.
int last_seq_num = -1;              // Sequence number of last continuous packet received.

// ================================================================
/* helper functions */
int transmitSinglePacket(Packet_t *pkt_ptr)
{
    memcpy(pkt_buf, pkt_ptr, sizeof(Packet_t));
    return sendto(sock_fd, pkt_buf, sizeof(Packet_t), 0, (struct sockaddr *)&si_other, slen);
}

void respond(int num)
{
    // respond ACK+num to sender.
    Packet_t res_pkt(ACK);
    res_pkt.ACK_num = num;
    transmitSinglePacket(&res_pkt);
    if (-1 == num_bytes)
    {
        cout << "ERROR: Sending single packet fails! (" << errno << ")" << endl;
        exit(1);
    }
    cout << "Respond ACK" << num << endl;
}

// ================================================================

void reliablyReceive(unsigned short int myUDPport, char *destinationFile)
{
    slen = sizeof(si_other);
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        cout << "Socket ERROR!!!" << endl;
        exit(1);
    }

    memset((char *)&si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(sock_fd, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
    {
        cout << "Bind ERROR!!!" << endl;
    }

    /* Now receive data and send acknowledgements */

    // ==================================================================

    FILE *fp = fopen(destinationFile, "w");

    cout << "==> Receiving data and writing to file..." << endl;
    while (true)
    {
        num_bytes = recvfrom(sock_fd, pkt_buf, sizeof(Packet_t), 0, (struct sockaddr *)&si_other, &slen);
        if (num_bytes <= 0)
        {
             cout << "ERROR: Receiving from sender fails." << endl;
             exit(1);
        }

        Packet_t come_in_pkt(pkt_buf);
        if (DATA == come_in_pkt.type)
        {
            cout << "Receive packet" << come_in_pkt.seq_num << endl;
            if (come_in_pkt.seq_num == last_seq_num + 1)
            // Receive a data packet.
            {
                cout << "Continuous packet, Nice!" << endl;
                // Coming the packet next to last continuous packet received.
                fwrite(come_in_pkt.data, 1, come_in_pkt.data_size, fp);
                last_seq_num++;
                int add_till = last_seq_num;
                int prev = last_seq_num;
                for (auto pkt : incontinuous_pkts)
                {
                    // There are packet in buffer.
                    if (pkt.seq_num == last_seq_num + 1)
                    {
                        fwrite(pkt.data, 1, pkt.data_size, fp); // Write the packet already in the ACK_incontinuous_pkts 
                        add_till = ++last_seq_num;
                    }
                    else
                    {
                        break;
                    }
                }
                for (int i = 0; i < add_till - prev; i++){
                    // Remove all the packets in buffer written to file.
                    incontinuous_pkts.erase(incontinuous_pkts.begin());
                }
                respond(last_seq_num); // Respond new ACK.
            }
            else if (come_in_pkt.seq_num >= last_seq_num + 1)
            {
                // Coming the future incontinuous packet.
                cout << "Incontinuous packet..." << endl;
                if (incontinuous_pkts.empty()){
                    // Add the packet to buffer.
                    incontinuous_pkts.push_back(come_in_pkt);
                }
                for (int i = 0; incontinuous_pkts.begin() + i < incontinuous_pkts.end(); i++)
                {
                    if (come_in_pkt.seq_num < (*(incontinuous_pkts.begin() + i)).seq_num)
                    {
                        incontinuous_pkts.insert(incontinuous_pkts.begin() + i, come_in_pkt); // Insert the packet into buffer.
                        break;
                    }
                    else if (come_in_pkt.seq_num == (*(incontinuous_pkts.begin() + i)).seq_num)
                    {
                        // The packet is already in the buffer.
                        break;
                    }
                    if (incontinuous_pkts.begin() + i == incontinuous_pkts.end() - 1)
                    {
                        // The sequence number is greater than all packets' in buffer.
                        incontinuous_pkts.insert(incontinuous_pkts.end(), come_in_pkt); // Insert the packet into buffer.
                    }
                }
                respond(last_seq_num);
            }
            else
            {
                // Coming the previous packet.
                cout << "Previous packet ???" << endl;
                respond(last_seq_num);
            }
        }
        else if (FIN == come_in_pkt.type)
        {
            cout << "Finish transmission!" << endl;
            Packet_t finish_pkt(FIN_ACK);
            transmitSinglePacket(&finish_pkt);
            if (-1 == num_bytes)
            {
                cout << "ERROR: Sending single packet fails! (" << errno << ")" << endl;
                exit(1);
            }
            break;
        }
    }

    close(sock_fd);
    fclose(fp);
    printf("%s received.\n", destinationFile);

    // ==================================================================
}

/*
 *
 */
int main(int argc, char **argv)
{
    unsigned short int udpPort;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int)atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}
