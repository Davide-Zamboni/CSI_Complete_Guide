#include <pcap/pcap.h>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <complex>
#include <fstream>

// configuration
#define BW 80
#define HOFFSET 16
#define k_tof_unpack_sgn_mask (1<<31)

#include <amqp.h>
#include <amqp_tcp_socket.h>

#include "/Users/davidezamboni/Documents/CSI_RabbitMQ/utils/utils.c"
#include "/Users/davidezamboni/Documents/CSI_RabbitMQ/utils/utils.h"


using namespace std;

void swap(complex<double> *v1, complex<double> *v2) {
    complex<double> tmp = *v1;
    *v1 = *v2;
    *v2 = tmp;
}

void fftshift(complex<double> *data, int count) {
    int k = 0;
    int c = (int) floor((float) count / 2);
    // For odd and for even numbers of element use different algorithm
    if (count % 2 == 0) {
        for (k = 0; k < c; k++)
            swap(&data[k], &data[k + c]);
    } else {
        complex<double> tmp = data[0];
        for (k = 0; k < c; k++) {
            data[k] = data[c + k + 1];
            data[c + k + 1] = data[k + 1];
        }
        data[c] = tmp;
    }
}

void unpack_float_acphy(int nbits, int autoscale, int shft,
                        int fmt, int nman, int nexp, int nfft,
                        uint32_t *H, int32_t *Hout) {
    int e_p, maxbit, e, i, pwr_shft = 0, e_zero, sgn;
    int n_out, e_shift;
    int8_t He[256];
    int32_t vi, vq, *pOut;
    uint32_t x, iq_mask, e_mask, sgnr_mask, sgni_mask;

    iq_mask = (1 << (nman - 1)) - 1;
    e_mask = (1 << nexp) - 1;
    e_p = (1 << (nexp - 1));
    sgnr_mask = (1 << (nexp + 2 * nman - 1));
    sgni_mask = (sgnr_mask >> nman);
    e_zero = -nman;
    pOut = (std::int32_t *) Hout;
    n_out = (nfft << 1);
    e_shift = 1;
    maxbit = -e_p;
    for (i = 0; i < nfft; i++) {
        vi = (int32_t) ((H[i] >> (nexp + nman)) & iq_mask);
        vq = (int32_t) ((H[i] >> nexp) & iq_mask);
        e = (int) (H[i] & e_mask);
        if (e >= e_p)
            e -= (e_p << 1);
        He[i] = (int8_t) e;
        x = (uint32_t) vi | (uint32_t) vq;
        if (autoscale && x) {
            uint32_t m = 0xffff0000, b = 0xffff;
            int s = 16;
            while (s > 0) {
                if (x & m) {
                    e += s;
                    x >>= s;
                }
                s >>= 1;
                m = (m >> s) & b;
                b >>= s;
            }
            if (e > maxbit)
                maxbit = e;
        }
        if (H[i] & sgnr_mask)
            vi |= k_tof_unpack_sgn_mask;
        if (H[i] & sgni_mask)
            vq |= k_tof_unpack_sgn_mask;
        Hout[i << 1] = vi;
        Hout[(i << 1) + 1] = vq;
    }
    shft = nbits - maxbit;
    for (i = 0; i < n_out; i++) {
        e = He[(i >> e_shift)] + shft;
        vi = *pOut;
        sgn = 1;
        if (vi & k_tof_unpack_sgn_mask) {
            sgn = -1;
            vi &= ~k_tof_unpack_sgn_mask;
        }
        if (e < e_zero) {
            vi = 0;
        } else if (e < 0) {
            e = -e;
            vi = (vi >> e);
        } else {
            vi = (vi << e);
        }
        *pOut++ = (int32_t) sgn * vi;
    }
}


int main() {

    string file = "./r3_roaming.pcap"; // file directory

    //string file = "/tmp/mnt/CSI/prova.pcap";
    int n_fft = BW * 3.2;

    char errbuff[PCAP_ERRBUF_SIZE];  // array to hold the error.

    pcap_t *pcap = pcap_open_offline(file.c_str(), errbuff); // opening the file and store result in pointer to pcap_t

    struct pcap_pkthdr *header;

    const u_char *data;

    pcap = pcap_open_offline(file.c_str(), errbuff);

    ////////////////////////////
    //////////////////////

    char const *hostname;
    int port, status;
    char const *exchange;
    char const *routingkey;
    char const *bindingkey;
    char const *queue;
    char const *messagebody;
    amqp_socket_t *socket = NULL;
    amqp_connection_state_t conn;

    hostname = "192.168.5.227";
    port = 5672;
    exchange = "amq.direct";
    routingkey = "csi_data";
    //cout << messagebody;
    bindingkey = "csi_data";
    queue = "csi_data";

    conn = amqp_new_connection();

    socket = amqp_tcp_socket_new(conn);
    if (!socket) {
        die("creating TCP socket");
    }

    status = amqp_socket_open(socket, hostname, port);
    if (status) {
        die("opening TCP socket");
    }

    die_on_amqp_error(amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
                                 "guest", "guest"),
                      "Logging in");
    amqp_channel_open(conn, 1);
    die_on_amqp_error(amqp_get_rpc_reply(conn), "Opening channel");

    //binding
    amqp_queue_bind(conn, 1, amqp_cstring_bytes(queue),
                    amqp_cstring_bytes(exchange), amqp_cstring_bytes(bindingkey),
                    amqp_empty_table);
    die_on_amqp_error(amqp_get_rpc_reply(conn), "Unbinding");

    //std::ofstream myfile;
    //myfile.open("data.json");

    string mac;
    time_t timestamp;
    time_t timestamp_millies;


    // Matrix with the final CSI data
    complex<double> csi_buff[n_fft];
    //cout << "n_packets =" << n_packets << '\n';

    int core;
    int rxss;
    int Nss;

    while (int returnValue = pcap_next_ex(pcap, &header, &data) >= 0) {

        // Show a warning if the length captured is different
        //if (header->len != header->caplen)
        // printf("Warning! Capture size different than packet size: %d bytes\n", header->len);

        // Show Epoch Time
        //printf("Epoch Time: %ld:%d seconds\n", header->ts.tv_sec, header->ts.tv_usec);
        timestamp = header->ts.tv_sec;//+ to_string(header->ts.tv_usec);
        timestamp_millies = header->ts.tv_usec;

        char buffer[2];


        // loop through the packet and save it as hexidecimal representation in result
        string packet;
        for (u_int i = 0; (i < header->caplen); i++) {
            // save each octet as hex (x), make sure there is always two characters (.2).
            sprintf(buffer, "%.2x", data[i]);
            packet += buffer;
        }
        //cout << packet << '\n';

        mac = packet.substr(92, 12);
        //cout << "mac =" << mac << '\n';

        uint32_t payload[packet.length() / 8];
        int pos = 0;
        // Convert hex payload to uint32
        for (int j = 0; j < packet.length(); j += 8) {
            string s = packet.substr(j, 8);
            uint32_t i;
            istringstream iss(s);
            iss >> hex >> i;
            i = __builtin_bswap32(i);
            payload[pos] = i;
            //cout << "payload " << pos<< '=' << payload[pos] << "  ";
            pos++;
        }
        int payloadbytes[packet.length() / 2];
        pos = 0;
        for (int j = 0; j < packet.length(); j += 2) {
            string s = packet.substr(j, 2);
            payloadbytes[pos] = stoi(s, 0, 16);
            //cout << payloadbytes[pos] << ' ';
            pos++;
        }
        core = (payloadbytes[55] & 3);
        rxss = ((payloadbytes[55] >> 3) & 3);
        Nss = core * 4 + rxss;
        //cout << Nss[n_packets];


        uint32_t h[n_fft];

        pos = 0;
        //cout << "nftt =" << n_fft << "\n";

        for (int k = HOFFSET - 1; k < HOFFSET + n_fft - 1; k++) {
            h[pos] = payload[k];
            //cout << h[pos] << '\n';
            pos++;
        }
        int32_t h_out[n_fft * 2];
        unpack_float_acphy(10, 0, 0, 1, 12, 6, n_fft, h, h_out);

        int32_t h_out_2[n_fft][2];
        for (int i = 0; i < n_fft * 2; ++i) {
            if (i % 2 == 0) {
                h_out_2[i / 2][0] = h_out[i];
            } else {
                h_out_2[i / 2][1] = h_out[i];
            }
        }
        // for (int i = 0; i < n_fft; ++i) {
        // for (int j = 0; j < 2; ++j) {
        //cout << h_out_2[i][j] << "  ";
        // }
        // cout << "  ";
        // }
        for (int i = 0; i < n_fft; ++i) {
            complex<double> tmp(1, 1);
            csi_buff[i].real(h_out_2[i][0]);
            csi_buff[i].imag(h_out_2[i][1]);
        }

        string myfile;

        myfile += "{\"timestamp\": ";
        struct tm ts;
        char buf[80];

        ts = *localtime(&timestamp);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ts);
        myfile += "\"";
        myfile += buf;
        myfile += '.';
        myfile += to_string(timestamp_millies); //timestamp millies
        myfile += "\", \"mac\": \"";

        for (int j = 0; j < 12; j += 2) {
            myfile += mac[j];
            myfile += mac[j + 1];
            if (j < 10) {
                myfile += ':';
            }
        }

        fftshift(csi_buff, n_fft);

        myfile += "\", \"rssi\": -56, \"csi_amplitude\": [";

        //int exclude[] ={0, 1, 2, 3, 4, 5, 127, 128, 129, 251, 252, 253, 254, 255};
        int exclude[] = {-128, -127, -126, -125, -124, -123, -1, 0, 1, 123, 124, 125, 126, 127};
        int exclude_size = sizeof(exclude) / sizeof(exclude[0]);

        for (int j = -128; j < n_fft / 2; ++j) {
            bool should_exclude = false;
            for (int k = 0; k < exclude_size; ++k) {
                if (j == exclude[k]) {
                    should_exclude = true;
                    break;
                }
            }
            if (!should_exclude) {
                myfile += to_string(csi_buff[j + 128].real());
                if(j != 122) {
                    myfile += ", ";
                }
            }
        }

        myfile += "], \"csi_phase\": [";
        for (int j = -128; j < n_fft / 2; ++j) {
            bool should_exclude = false;
            for (int k = 0; k < exclude_size; ++k) {
                if (j == exclude[k]) {
                    should_exclude = true;
                    break;
                }
            }
            if (!should_exclude) {
                myfile += to_string(csi_buff[j + 128].imag());
                if(j!=122) {
                    myfile += ", ";
                }
            }
        }

        myfile += "], \"maxcore\": 4, \"core\": ";
        myfile += to_string(core);
        myfile += ", \"rsxx\": ";
        myfile += to_string(rxss);
        myfile += ", \"nss\": ";
        myfile += to_string(Nss);
        myfile += "}";

        //cout << myfile;

        messagebody = myfile.c_str();
        //cout << messagebody;

        {
            amqp_basic_properties_t props;
            props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
            props.content_type = amqp_cstring_bytes("text/plain");
            props.delivery_mode = 2; /* persistent delivery mode */
            die_on_error(amqp_basic_publish(conn, 1, amqp_cstring_bytes(exchange),
                                            amqp_cstring_bytes(routingkey), 0, 0,
                                            &props, amqp_cstring_bytes(messagebody)),
                         "Publishing");
        }
    }
    die_on_amqp_error(amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS),
                      "Closing channel");
    die_on_amqp_error(amqp_connection_close(conn, AMQP_REPLY_SUCCESS),
                      "Closing connection");
    die_on_error(amqp_destroy_connection(conn), "Ending connection");
    return 0;

}
