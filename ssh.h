#include <string.h>

#define SSH_CIPHER_NONE		0
#define SSH_CIPHER_IDEA		1
#define SSH_CIPHER_DES		2
#define SSH_CIPHER_3DES		3
#define SSH_CIPHER_RC4		5
#define SSH_CIPHER_BLOWFISH	6

#define SSH_AUTH_RHOSTS		1
#define SSH_AUTH_RSA		2
#define SSH_AUTH_PASSWORD	3
#define SSH_AUTH_RHOSTS_RSA	4

#define SSH_PROTOFLAG_SCREEN_NUMBER	0x00000001
#define SSH_PROTOFLAG_HOST_IN_FWD_OPEN	0x00000002

#define SSH_MSG_NONE			0
#define SSH_MSG_DISCONNECT		1
#define SSH_SMSG_PUBLIC_KEY		2
#define SSH_CMSG_SESSION_KEY		3
#define SSH_CMSG_USER			4
#define SSH_CMSG_AUTH_RHOSTS		5
#define SSH_CMSG_AUTH_RSA		6
#define SSH_SMSG_RSA_CHALLENGE		7
#define SSH_CMSG_AUTH_RSA_RESPONSE	8
#define SSH_CMSG_AUTH_PASSWORD		9
#define SSH_CMSG_REQUEST_PTY		10
#define SSH_CMSG_WINDOW_SIZE		11
#define SSH_CMSG_EXEC_SHELL		12
#define SSH_CMSG_EXEC_CMD		13
#define SSH_SMSG_SUCCESS		14
#define SSH_SMSG_FAILURE		15
#define SSH_CMSG_STDIN_DATA		16
#define SSH_SMSG_STDOUT_DATA		17
#define SSH_SMSG_STDERR_DATA		18
#define SSH_CMSG_EOF			19
#define SSH_SMSG_EXITSTATUS		20
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 21
#define SSH_MSG_CHANNEL_OPEN_FAILURE	22
#define SSH_MSG_CHANNEL_DATA		23
#define SSH_MSG_CHANNEL_CLOSE		24
#define SSH_MSG_CHANNEL_CLOSE_CONFIRMATION 25
/* 26 was unix-domain X11 forwarding */
#define SSH_SMSG_X11_OPEN		27
#define SSH_CMSG_PORT_FORWARD_REQUEST	28
#define SSH_MSG_PORT_OPEN		29
#define SSH_CMSG_AGENT_REQUEST_FORWARDING 30
#define SSH_SMSG_AGENT_OPEN		31
#define SSH_MSG_IGNORE			32
#define SSH_CMSG_EXIT_CONFIRMATION	33
#define SSH_CMSG_X11_REQUEST_FARWARDING	34
#define SSH_CMSG_AUTH_RHOSTS_RSA	35
#define SSH_MSG_DEBUG			36
#define SSH_CMSG_REQUEST_COMPRESSION	37
#define SSH_CMSG_MAX_PACKET_SIZE	38
#define SSH_CMSG_AUTH_TIS		39
#define SSH_SMSG_AUTH_TIS_CHALLENGE	40
#define SSH_CMSG_AUTH_TIS_RESPONSE	41
#define SSH_CMSG_AUTH_KERBEROS		42
#define SSH_SMSG_AUTH_KERBEROS_RESPONSE	43
#define SSH_CMSG_HAVE_KERBEROS_TGT	44

struct RSAKey {
    int bits;
    int bytes;
    void *modulus;
    void *exponent;
};

int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr);
void rsaencrypt(unsigned char *data, int length, struct RSAKey *key);
int rsastr_len(struct RSAKey *key);
void rsastr_fmt(char *str, struct RSAKey *key);

typedef unsigned int word32;
typedef unsigned int uint32;

unsigned long crc32(const unsigned char *s, unsigned int len);

struct MD5Context {
        uint32 buf[4];
        uint32 bits[2];
        unsigned char in[64];
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf,
               unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);

struct ssh_cipher {
    void (*sesskey)(unsigned char *key);
    void (*encrypt)(unsigned char *blk, int len);
    void (*decrypt)(unsigned char *blk, int len);
};

void SHATransform(word32 *digest, word32 *data);

int random_byte(void);
void random_add_noise(void *noise, int length);
