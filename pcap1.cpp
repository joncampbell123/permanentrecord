
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

#pragma pack(push,1)
typedef struct ipv4_hdr_t { // bitfields, bits MSB to LSB, bytes LSB to MSB
    uint32_t            version_helen_servtype_totallen;            // bitfield low-to-high ver=4 helen=4 servicetype=8 totallen=16
    uint32_t            ident_flags_fragoff;                        // bitfield low-to-high ident=16 flags=3 fragmentoffset=13
    uint32_t            ttl_proto_hdrchksum;                        // bitfield low-to-high timetolive=8 protocol=8 headerchecksum=16
    uint32_t            source_ip_address;
    uint32_t            dest_ip_address;

    uint32_t bs(const uint32_t w) const {
        return be32toh(w);
    }

    unsigned int getver() const { /* be [31:28] */
        return (bs(version_helen_servtype_totallen) >> 28ul) & 0xFu;
    }
    unsigned int gethdrlenwords() const { /* be [27:24] */
        return (bs(version_helen_servtype_totallen) >> 24ul) & 0xFu;
    }
    unsigned int gethdrlenbytes() const {
        return gethdrlenwords() * 4u;
    }
    unsigned int getservtype() const {
        return (bs(version_helen_servtype_totallen) >> 16ul) & 0xFFu;
    }
    unsigned int gettotallen() const {
        return bs(version_helen_servtype_totallen) & 0xFFFFu;
    }
    unsigned int getid() const {
        return (bs(ident_flags_fragoff) >> 16u) & 0xFFFFu;
    }
    unsigned int getflags() const {
        return (bs(ident_flags_fragoff) >> 13u) & 7u;
    }
    unsigned int getfragofs() const {
        return bs(ident_flags_fragoff) & 0x1FFFu;
    }
    unsigned int getttl() const {
        return (bs(ttl_proto_hdrchksum) >> 24u) & 0xFFu;
    }
    unsigned int getproto() const {
        return (bs(ttl_proto_hdrchksum) >> 16u) & 0xFFu;
    }
    uint32_t getsrcip() const {
        return bs(source_ip_address);
    }
    uint32_t getdstip() const {
        return bs(dest_ip_address);
    }
} ipv4_hdr_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct udp4_hdr_t {
    uint16_t        src_port;       // big endian
    uint16_t        dst_port;       // big endian
    uint16_t        length;         // big endian
    uint16_t        checksum;       // big endian

    uint16_t gettotallen() const {
        return be16toh(length);
    }
    uint16_t getsrcport() const {
        return be16toh(src_port);
    }
    uint16_t getdstport() const {
        return be16toh(dst_port);
    }
} udp4_hdr_t;
#pragma pack(pop)

static unsigned char tmpbuf[65536];
static pcap_hdr_t pcaphdr;

static void dump_udp4(const struct udp4_hdr_t *udp4hdr,pcaprec_hdr_t *prec,const unsigned char *udp4pl,const unsigned char *udp4plf) {
    (void)prec;

    fprintf(stderr,"udp4 s-port:%u d-port:%u tlen:%u\n",
        udp4hdr->getsrcport(),
        udp4hdr->getdstport(),
        udp4hdr->gettotallen());
    fprintf(stderr,"content:\n");
    {
        const unsigned char *p = udp4pl,*f = udp4plf;
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

static void dump_ip4(const struct ipv4_hdr_t *ip4hdr,pcaprec_hdr_t *prec,const unsigned char *ip4pl,const unsigned char *ip4plf) {
    (void)prec;

    fprintf(stderr,"ipv4 v:%u hl:%u(words) st:0x%02x tlen:%u id:0x%04x fl:0x%x fragof:0x%04x ttl:%u proto:0x%02x s-ip:%u.%u.%u.%u d-ip:%u.%u.%u.%u\n",
        ip4hdr->getver(),
        ip4hdr->gethdrlenwords(),
        ip4hdr->getservtype(),
        ip4hdr->gettotallen(),
        ip4hdr->getid(),
        ip4hdr->getflags(),
        ip4hdr->getfragofs(),
        ip4hdr->getttl(),
        ip4hdr->getproto(),
        (ip4hdr->getsrcip()>>24u)&0xFFu,
        (ip4hdr->getsrcip()>>16u)&0xFFu,
        (ip4hdr->getsrcip()>>8u)&0xFFu,
        (ip4hdr->getsrcip()>>0u)&0xFFu,
        (ip4hdr->getdstip()>>24u)&0xFFu,
        (ip4hdr->getdstip()>>16u)&0xFFu,
        (ip4hdr->getdstip()>>8u)&0xFFu,
        (ip4hdr->getdstip()>>0u)&0xFFu);
    fprintf(stderr,"content:\n");
    {
        const unsigned char *p = ip4pl,*f = ip4plf;
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

        if (dump) fprintf(stderr,"---------------packet-----------------\n");

        if (pcaphdr.network == NET_ETHERNET/*ethernet*/) {
            const struct ethernet_hdr_t *ethhdr = (const struct ethernet_hdr_t*)tmpbuf;
            const unsigned char *ethpl = tmpbuf + sizeof(*ethhdr);
            if (ethpl > fence) continue;

            if (dump) dump_eth(ethhdr,&prec,ethpl,fence);

            if (be16toh(ethhdr->eth_type) == ETH_T_IPV4) {
                const struct ipv4_hdr_t *ip4hdr = (const struct ipv4_hdr_t*)ethpl;
                if (ip4hdr->getver() != 4) continue;
                const unsigned char *ip4pl = ethpl + ip4hdr->gethdrlenbytes();
                if (ip4pl > fence) continue;
                const unsigned char *ip4plf = ethpl + ip4hdr->gettotallen();
                if (ip4plf > fence) continue;
                if (ip4pl > ip4plf) continue;

                if (dump) dump_ip4(ip4hdr,&prec,ip4pl,ip4plf);

                if (ip4hdr->getproto() == 0x11/*UDP*/) {
                    const struct udp4_hdr_t *udp4hdr = (const struct udp4_hdr_t*)ip4pl;
                    const unsigned char *udp4pl = ip4pl + sizeof(*udp4hdr);
                    if (udp4pl > ip4plf) continue;
                    const unsigned char *udp4plf = ip4pl + udp4hdr->gettotallen();
                    if (udp4plf > ip4plf) continue;
                    if (udp4pl > udp4plf) continue;

                    if (dump || true) dump_udp4(udp4hdr,&prec,udp4pl,udp4plf);
                }
            }
        }
    }

    close(fd);
    return 0;
}

