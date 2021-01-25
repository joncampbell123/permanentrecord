
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#pragma pack(push,1)
typedef struct pcap_hdr_t {
    uint32_t    magic_number;   /* magic number */
    uint16_t    version_major;  /* major version number */
    uint16_t    version_minor;  /* minor version number */
    int32_t     thiszone;       /* GMT to local correction */
    uint32_t    sigfigs;        /* accuracy of timestamps */
    uint32_t    snaplen;        /* max length of captured packets, in octets */
    uint32_t    network;        /* data link type */
} pcap_hdr_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct pcaprec_hdr_t {
    uint32_t    ts_sec;         /* timestamp seconds */
    uint32_t    ts_usec;        /* timestamp microseconds */
    uint32_t    incl_len;       /* number of octets of packet saved in file */
    uint32_t    orig_len;       /* actual length of packet */
} pcaprec_hdr_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct eth_mac_addr_t {
    uint8_t             b[6];
} eth_mac_addr_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct ethernet_hdr_t {
    eth_mac_addr_t      dest_mac;
    eth_mac_addr_t      src_mac;
    uint16_t            eth_type;           // big endian
} ethernet_hdr_t;                           // does NOT include 32-bit CRC from the wire
#pragma pack(pop)

#define NET_ETHERNET        0x0001

#define ETH_T_IPV4          0x0800
#define ETH_T_ARP           0x0806

static unsigned char tmpbuf[65536];
static pcap_hdr_t pcaphdr;

static void dump_eth(const struct ethernet_hdr_t *ethhdr,pcaprec_hdr_t *prec,const unsigned char *ethpl,const unsigned char *fence) {
    fprintf(stderr,"ethernet to-mac:%02x-%02x-%02x-%02x-%02x-%02x from-mac:%02x-%02x-%02x-%02x-%02x-%02x eth-type:0x%04x len:%u\n",
            ethhdr->dest_mac.b[0],ethhdr->dest_mac.b[1],ethhdr->dest_mac.b[2],
            ethhdr->dest_mac.b[3],ethhdr->dest_mac.b[4],ethhdr->dest_mac.b[5],
            ethhdr->src_mac.b[0],ethhdr->src_mac.b[1],ethhdr->src_mac.b[2],
            ethhdr->src_mac.b[3],ethhdr->src_mac.b[4],ethhdr->src_mac.b[5],
            be16toh(ethhdr->eth_type),prec->incl_len);
    fprintf(stderr,"content:\n");
    {
        const unsigned char *p = ethpl,*f = fence;
        unsigned int col = 0;

        while (p < f) {
            fprintf(stderr,"    ");
            for (col=0;col < 16;col++) {
                if ((p+col) < f)
                    fprintf(stderr,"%02X ",p[col]);
                else
                    fprintf(stderr,"   ");
            }

            fprintf(stderr,"   ");
            for (col=0;col < 16;col++) {
                if ((p+col) < f) {
                    if (p[col] >= 0x20 && p[col] <= 0x7E)
                        fprintf(stderr,"%c",(char)p[col]);
                    else
                        fprintf(stderr,".");
                }
                else {
                    fprintf(stderr," ");
                }
            }

            fprintf(stderr,"\n");
            p += 16;
        }
    }
}

int main(int argc,char **argv) {
    int dump = 0;
    int fd;

    if (argc < 2) {
        fprintf(stderr,"%s <pcap file>\n",argv[0]);
        return 1;
    }

    fd = open(argv[1],O_RDONLY);
    if (fd < 0) return 1;

    if (read(fd,tmpbuf,sizeof(pcap_hdr_t)) != sizeof(pcap_hdr_t)) return 1;
    pcaphdr = *((pcap_hdr_t*)tmpbuf);
    if (pcaphdr.magic_number != 0xA1B2C3D4) return 1;
    if (pcaphdr.version_major != 2) return 1;

    while (read(fd,tmpbuf,sizeof(pcaprec_hdr_t)) == sizeof(pcaprec_hdr_t)) {
        pcaprec_hdr_t prec = *((pcaprec_hdr_t*)tmpbuf);

        if (prec.orig_len < prec.incl_len) return 1;
        if (prec.incl_len > pcaphdr.snaplen) return 1;
        if (prec.incl_len > sizeof(tmpbuf)) return 1;
        if (prec.incl_len < prec.orig_len) continue;
        if (prec.incl_len == 0) continue;

        if (read(fd,tmpbuf,prec.incl_len) != prec.incl_len) return 1;
        const unsigned char *fence = tmpbuf + prec.incl_len;

        if (pcaphdr.network == NET_ETHERNET/*ethernet*/) {
            if ((tmpbuf+sizeof(ethernet_hdr_t)) > fence) continue;
            const struct ethernet_hdr_t *ethhdr = (const struct ethernet_hdr_t*)tmpbuf;
            const unsigned char *ethpl = tmpbuf + sizeof(struct ethernet_hdr_t);

            if (dump) dump_eth(ethhdr,&prec,ethpl,fence);
        }
    }

    close(fd);
    return 0;
}

