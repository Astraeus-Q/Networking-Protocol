/*
 * File:   sender_main.c
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
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <vector>
#include <string>
#include <iostream>
#include <cmath>

using namespace std;

/*****************************************************************/

#define MSS 2000
#define BUF_SIZE 1024 // Size in packet.
#define RTT 20000

typedef enum STATES
{
    SLOW_START,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
} State;

typedef enum ACTIONS
{
    NEW_ACK_COMES,
    TIMEOUT_HAPPENS,
    DUP_ACK_COMES
} Action;

typedef enum MSG_TYPES
{
    ACK,
    DATA,
    FIN,
    FIN_ACK,
    NONE
} Msg_type;

/*****************************************************************/

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
        if (ACK == type or FIN == type)
            this->data_size = 0;
    };
    ~Packet() = default;
} Packet_t;

struct sockaddr_in si_other;
int sock_fd;
socklen_t slen = sizeof(si_other);

unsigned long long remained_bytes;

// ================================================================
/* shared variables*/
FILE *fp;
int num_bytes;
char pkt_buf[sizeof(Packet_t)];

Packet_t come_in_pkt;
Packet_t test_pkt;

/** variables controlled by state diagram **/
vector<Packet_t> file_buf;     // storing the file content
vector<Packet_t> unACKed_pkts; // waiting for ACKs

State current_state = SLOW_START;
int ss_threshold = 64;
int dup_ACK_cnt = 0;
double cw_size = 1.0;
// ================================================================

/*****************************************************************/
// helper functions
int loadFile(int packet_num)
{
    static unsigned long long seq_number = 0;
    if (packet_num == 0)
        return 0;

    if (packet_num < 0)
    {
        cout << "Packet number could not be negative!!!\n";
        exit(1);
    }

    int read_size;
    for (int i = 0; remained_bytes > 0 and i < packet_num; i++)
    {
        if (remained_bytes <= MSS)
            read_size = remained_bytes;
        else
            read_size = int(MSS);
        Packet_t new_packet;
        new_packet.data_size = read_size;
        new_packet.seq_num = seq_number++;
        new_packet.type = DATA;
        fread(new_packet.data, 1, read_size, fp); // File has been open.
        file_buf.push_back(new_packet);
        remained_bytes -= read_size;
    }
    return read_size;
}

int transmitSinglePacket(Packet_t *pkt_ptr)
{
    memcpy(pkt_buf, pkt_ptr, sizeof(Packet_t));
    return sendto(sock_fd, pkt_buf, sizeof(Packet_t), 0, (struct sockaddr *)&si_other, slen);
}

void transmitPacketsAsAllowed()
{
    double send_num; // Number of packet to send.
    if (cw_size - unACKed_pkts.size() <= file_buf.size())
        send_num = cw_size - unACKed_pkts.size();
    else
        send_num = file_buf.size();

    cout << "Transmitting packets as allowed: send_num = " << send_num << endl;

    if (send_num < 1)
        return;

    for (int i = 0; i < send_num; i++)
    {
        // Send the first packet in file buffer, add the packet to unACKed_pkts and delete it.
        transmitSinglePacket(&file_buf.front());
        unACKed_pkts.push_back(file_buf.front());
        file_buf.erase(file_buf.begin());
    }

    loadFile(send_num);
}

void _doThingsForTimeout()
{
    cout << "WARNING: Timeout! Current state: " << current_state << "; Resending packet #" << unACKed_pkts.front().seq_num << endl;

    // reset parameters
    ss_threshold = ceil(cw_size / 2.0);
    cw_size = 1.0;
    dup_ACK_cnt = 0;

    // re-transmit the packet at the unACKed_pkts base
    num_bytes = transmitSinglePacket(&unACKed_pkts.front());
    if (-1 == num_bytes)
    {
        cout << "ERROR: Resending packet #" << unACKed_pkts.front().seq_num << " fails!" << endl;
        exit(1);
    }

    // move to SLOW_START
    current_state = SLOW_START;
}

void _doThingsForTripleDupACK()
{
    cout << "Triple duplicate ACKs received. Move to FAST_RECOVERY." << endl;

    // reset parameters
    ss_threshold = cw_size / 2.0;
    cw_size = ss_threshold + 3;
    dup_ACK_cnt = 0;
    current_state = FAST_RECOVERY;

    // re-transmit packet at unACKed_pkts base
    num_bytes = transmitSinglePacket(&unACKed_pkts.front());
    if (-1 == num_bytes)
    {
        cout << "ERROR: Re-transmitting unACKed_pkts base fails!" << endl;
        exit(1);
    }

    // transmit packets as allowed by unACKed_pkts
    transmitPacketsAsAllowed();
}

void doStateTransition(Action act)
{
    cout << "--> State transition. Currently, state = " << current_state << ", cw_size = " << cw_size << ", unACKed_pkts size = " << unACKed_pkts.size() << ", sst = " << ss_threshold << endl;
    switch (current_state)
    {
    case SLOW_START:
    {
        switch (act)
        {
        case TIMEOUT_HAPPENS:
            _doThingsForTimeout();
            break;

        case NEW_ACK_COMES:
            dup_ACK_cnt = 0;
            cw_size = cw_size + 1; // TODO: whether limited to BUFFER_SIZE
            transmitPacketsAsAllowed();
            break;

        case DUP_ACK_COMES:
            dup_ACK_cnt++;
            if (3 == dup_ACK_cnt)
                _doThingsForTripleDupACK();
            break;

        default:
            break;
        }

        if (cw_size >= ss_threshold)
        {
            cout << "cw_size (" << cw_size << ") reaches threshold (" << ss_threshold << "). Move to CONGESTION_AVOIDANCE state." << endl;
            current_state = CONGESTION_AVOIDANCE;
        }
    
        break;
    }
    case CONGESTION_AVOIDANCE:
    {
        switch (act)
        {
        case TIMEOUT_HAPPENS:
            _doThingsForTimeout();
            break;

        case NEW_ACK_COMES:
            cw_size = (cw_size + 1.0 / (int)cw_size); // TODO: whether limited to BUFFER_SIZE
            dup_ACK_cnt = 0;
            transmitPacketsAsAllowed();
            break;

        case DUP_ACK_COMES:
            dup_ACK_cnt++;
            if (3 == dup_ACK_cnt)
                _doThingsForTripleDupACK();
            break;

        default:
            break;
        }
        break;
    }
    case FAST_RECOVERY:
    {
        switch (act)
        {
        case TIMEOUT_HAPPENS:
            _doThingsForTimeout();
            break;

        case NEW_ACK_COMES:
            dup_ACK_cnt = 0;
            cw_size = ss_threshold;
            transmitPacketsAsAllowed();
            break;

        case DUP_ACK_COMES:
            dup_ACK_cnt++;
            cw_size = cw_size + 1; // TODO: whether limited to BUFFER_SIZE
            transmitPacketsAsAllowed();
            break;

        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}
/*****************************************************************/

/* reliablyTransfer
    - Transfer the first "bytesToTransfer" bytes of "filename" to the receiver at "hostname": "hostUDPport"
*/
void reliablyTransfer(char *hostname, unsigned short int hostUDPport, char *filename, unsigned long long int bytesToTransfer)
{
    cout << "==> Opening file..." << endl;
    fp = fopen(filename, "r");
    if (fp == NULL)
    {
        cout << "Could not open file to send!!!\n";
        exit(1);
    }

    cout << "==> Checking file length..." << endl;
    fseek(fp, 0, SEEK_END);
    int flen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    remained_bytes = bytesToTransfer > flen ? flen : bytesToTransfer;

    cout << "==> Loading file to buffer..." << endl;
    loadFile(BUF_SIZE);
    
    /* Determine how many bytes to transfer */
    cout << "==> Obtaining information of target..." << endl;
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        cout << "Could not create socket!\n";
        exit(1);
    } // IPv4, UDP

    memset((char *)&si_other, 0, sizeof(si_other)); // Initialization to 0s.
    si_other.sin_family = AF_INET;                  // IPv4
    si_other.sin_port = htons(hostUDPport);         // UDP
    if (inet_aton(hostname, &si_other.sin_addr) == 0)
    {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    cout << "==> Setting timeout..." << endl;
    struct timeval RTT_TO;
    RTT_TO.tv_sec = 0;
    RTT_TO.tv_usec = 2 * RTT;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &RTT_TO, sizeof(RTT_TO)) < 0)
    {
        fprintf(stderr, "ERROR: Cannot set socket timeout!\n");
        exit(1);
    }

    cout << "==> Sending the first batch of packets..." << endl;
    transmitPacketsAsAllowed();

    cout << "==> Continue sending, until file buf is ready and all packets are ACKed..." << endl;
    while (!(file_buf.empty() && unACKed_pkts.empty()))
    {
        cout << "\n---------------- NEW ROUND ----------------\n";
        cout << "Receiving data from receiver..." << endl;
        num_bytes = recvfrom(sock_fd, pkt_buf, sizeof(Packet_t), 0, (struct sockaddr *)&si_other, &slen);
        memcpy(&test_pkt, pkt_buf, sizeof(Packet_t));
        cout << "Get something from receiver, ACK #" << test_pkt.ACK_num << endl;

        if (-1 == num_bytes)
        {
            cout << "ERROR: errno = " << errno << endl;
            // time out
            if (errno == EAGAIN)
                doStateTransition(TIMEOUT_HAPPENS);
            // other errors
            else
            {
                cout << "ERROR: Receiving ACK fails!" << endl;
                exit(1);
            }
        }
        // no time out
        else
        {
            cout << "No timeout." << endl;
            memcpy(&come_in_pkt, pkt_buf, sizeof(Packet_t));

            // only response to ACK
            if (ACK != come_in_pkt.type)
            {
                cout << "Only response to ACK!" << endl;
                continue;
            }

            // duplicate ACK comes
            if (come_in_pkt.ACK_num == unACKed_pkts.front().seq_num - 1)
            {
                cout << "Duplicate ACK comes!" << endl;
                doStateTransition(DUP_ACK_COMES);
            }
            // new ACK comes
            else if (come_in_pkt.ACK_num >= unACKed_pkts.front().seq_num)
            {
                cout << "New ACK comes!" << endl;
                // discard all packets before ACK num
                while (!unACKed_pkts.empty() && unACKed_pkts.front().seq_num <= come_in_pkt.ACK_num)
                {
                    unACKed_pkts.erase(unACKed_pkts.begin());
                    doStateTransition(NEW_ACK_COMES);
                }
            }
        }

        cout << "CHECK: file_buf size: " << file_buf.size() << ", unACKed_pkts size: " << unACKed_pkts.size() << endl << endl;
    }

    cout << "==> Closing UDP connection..." << endl;
    for (;;)
    {
        Packet_t finish_pkt(FIN);
        num_bytes = transmitSinglePacket(&finish_pkt);
        if (-1 == num_bytes)
        {
            cout << "ERROR: Sending FIN fails!" << endl;
            exit(1);
        }

        num_bytes = recvfrom(sock_fd, pkt_buf, sizeof(Packet_t), 0, (struct sockaddr *)&si_other, &slen);
        if (-1 == num_bytes)
        {
            cout << "ERROR: Receiving FIN_ACK fails!" << endl;
            exit(1);
        }

        Packet_t finish_ack_pkt(pkt_buf);
        if (FIN_ACK == finish_ack_pkt.type)
            break;
        else
            continue;
    }

    close(sock_fd);
    fclose(fp);
}

/*
 *
 */
int main(int argc, char **argv)
{
    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5)
    {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int)atoi(argv[2]);
    numBytes = atoll(argv[4]);

    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);

    cout << "==> ALL DONE!" << endl;
    return (EXIT_SUCCESS);
}

