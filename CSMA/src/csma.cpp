#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <math.h>

using namespace std;

class Node{
    public:
        int id;
        int num_col; // number of collision.
        int backoff;

        Node (int iden, int R){
            id = iden;
            num_col = 0;
            backoff = id % R; // At the begining, ticks = 0;
            return;
        }

        int get_backoff(int ticks, int R){
            return (id + ticks) % R;
        }
};

int main(int argc, char** argv) {
    //printf("Number of arguments: %d", argc);
    if (argc != 2) {
        printf("Usage: ./csma input.txt\n");
        return -1;
    }

    ifstream f_in(argv[1]);
    ofstream f_out("output.txt");
    
    // Read parameters.
    string buf;
    int N, L, M, T;
    vector<int> R;

    while(getline(f_in, buf)){
        char* c = strtok((char*)buf.c_str(), " ");
        if (!strcmp(c, "N")){
            N = stoi(strtok(NULL, " "));
        }
        else if (!strcmp(c, "L")){
            L = stoi(strtok(NULL, " "));
        }
        else if (!strcmp(c, "R")){
            char* range;
            while (range = strtok(NULL, " ")){
                R.push_back(stoi(range));
            }
        }
        else if (!strcmp(c, "M")){
                M = stoi(strtok(NULL, " ")); // Maximum retransmission attempt.
        }
        else if (!strcmp(c, "T")){
                T = stoi(strtok(NULL, " "));
        }
        else{
            cout << "Incorrect input!" << endl;
        }
    }
    //cout << "N " << N << " L " << L << " R " << R[0] << " M " << M << " T " << T << endl;

    if (N == 1){
        // If there is only one node, the utilization rate will be 100%.
        f_out << "1.00";
        cout << "1.00";
        f_out.close();
        return 0;
    }

    // Initialize Nodes.
    vector<Node*> nodes;
    for (int i = 0; i < N; i++){
        nodes.push_back(new Node(i, R[0]));
    }

    // Transmitting.
    int clk = -1; // Clock simulator. First clk is -1 (clk++), it is time for initialization.
    int t_valid = 0; // The total number of time slots that are transmitting.
    int t_trans; // The number of time slots that have been transmitting current packet.
    int occupied = 0;
    Node* readynode_p; 

    while (clk < T - 1){
        clk++;
        cout << "Time: " << clk << "    ";
        if (occupied == 0){
            // Idle.
            cout << "Idel ";

            for (auto n : nodes){
                cout << " " << n->backoff << " ";
                if (n->backoff == 0){
                    readynode_p = n;
                    occupied++;
                    cout << " inc ";
                }
            }

            if (occupied == 0){
                cout << "Free" << endl;
                for (auto n : nodes){
                    n->backoff--;
                }
            }
            else if (occupied == 1){
                // Only one node is ready to talk.
                cout << "Transmitting" << endl;
                t_valid++;
                t_trans = 1;
                readynode_p->num_col = 0;
            }
            else{
                // occupied > 1, collision happens.
                cout << "Collision" << endl;
                occupied = 0;
                for (auto n : nodes){
                    if (n->backoff == 0){
                        n->num_col = (n->num_col + 1 == M) ? 0 : n->num_col+1; // Check whether collision number of the packet reach the maximum retransmission attempt.
                        n->backoff = n->get_backoff(clk+1, R[n->num_col]); // Get a new waiting time.
                    }
                }
            }
        }
        else if (occupied == 1){
            // Transmitting.
            cout << "Transmitting" << endl;
            t_valid++;
            t_trans++;
            if (t_trans == L){
                // Transimission will finish after this time slot.
                occupied = 0;
                readynode_p->backoff = readynode_p->get_backoff(clk+1, R[0]);
            }
        }
    }


    float uti = (float)t_valid / T; // Utilization rate.
    cout << t_valid << " " << uti << endl;
    if (uti == 1){
        cout << "1.00" << endl;
        f_out << "1.00";
    }
    else if (1 > uti and uti >= 0.1){
        cout << "0." << round(uti*100) << endl;
        f_out << "0." << round(uti*100);
    }
    else{
        cout << "0.0" << round(uti*100) << endl;
        f_out << "0.0" << round(uti*100);
    }
    f_out.close();
    return 0;
}

