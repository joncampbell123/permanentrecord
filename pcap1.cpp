
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
} ethernet_hdr_t;
#pragma pack(pop)

static unsigned char tmpbuf[65536];
static pcap_hdr_t pcaphdr;

int main(int argc,char **argv) {
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
    if (pcaphdr.network != 1) return 1;/*must be ethernet*/

    while (read(fd,tmpbuf,sizeof(pcaprec_hdr_t)) == sizeof(pcaprec_hdr_t)) {
        pcaprec_hdr_t prec = *((pcaprec_hdr_t*)tmpbuf);

        if (prec.orig_len < prec.incl_len) return 1;
        if (prec.incl_len > pcaphdr.snaplen) return 1;
        if (prec.incl_len > sizeof(tmpbuf)) return 1;
        if (prec.incl_len < prec.orig_len) continue;
        if (prec.incl_len == 0) continue;

        if (read(fd,tmpbuf,prec.incl_len) != prec.incl_len) return 1;
        unsigned char *fence = tmpbuf + prec.incl_len;

        if (pcaphdr.network == 1/*ethernet*/) {
            if ((tmpbuf+sizeof(ethernet_hdr_t)) > fence) continue;
            struct ethernet_hdr_t *ethhdr = (struct ethernet_hdr_t*)tmpbuf;

#if 1
            fprintf(stderr,"to:%02x%02x%02x%02x%02x%02x from:%02x%02x%02x%02x%02x%02x type:%04x len:%u\n",
                ethhdr->dest_mac.b[0],ethhdr->dest_mac.b[1],ethhdr->dest_mac.b[2],
                ethhdr->dest_mac.b[3],ethhdr->dest_mac.b[4],ethhdr->dest_mac.b[5],
                ethhdr->src_mac.b[0],ethhdr->src_mac.b[1],ethhdr->src_mac.b[2],
                ethhdr->src_mac.b[3],ethhdr->src_mac.b[4],ethhdr->src_mac.b[5],
                be16toh(ethhdr->eth_type),prec.incl_len);
#endif
        }
    }

    close(fd);
    return 0;
}

