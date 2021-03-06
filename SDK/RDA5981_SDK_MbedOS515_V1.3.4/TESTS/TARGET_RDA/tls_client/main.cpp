#include "mbed.h"
#include "WiFiStackInterface.h"
#ifndef MBEDTLS_DRBG_ALT
#include "ctr_drbg.h"
#endif

/* Change to a number between 1 and 4 to debug the TLS connection */
#define DEBUG_LEVEL 1

/* Change to 1 to skip certificate verification (UNSAFE, for debug only!) */
#define UNSAFE 0

#include "TCPSocket.h"

#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#if DEBUG_LEVEL > 0
#include "mbedtls/debug.h"
#endif

namespace {

const char *HTTPS_SERVER_NAME = "developer.mbed.org";
const int HTTPS_SERVER_PORT = 443;
const int RECV_BUFFER_SIZE = 600;

const char HTTPS_PATH[] = "/media/uploads/mbed_official/hello.txt";
const size_t HTTPS_PATH_LEN = sizeof(HTTPS_PATH) - 1;

/* Test related data */
const char *HTTPS_OK_STR = "200 OK";
const char *HTTPS_HELLO_STR = "Hello world!";

#ifndef MBEDTLS_DRBG_ALT
/* personalization string for the drbg */
const char *DRBG_PERS = "mbed TLS helloword client";
#endif

/* List of trusted root CA certificates
 * currently only GlobalSign, the CA for developer.mbed.org
 *
 * To add more than one root, just concatenate them.
 */
const char SSL_CA_PEM[] =
/* GlobalSign Root certificate */
"-----BEGIN CERTIFICATE-----\n"
"MIIEaTCCA1GgAwIBAgILBAAAAAABRE7wQkcwDQYJKoZIhvcNAQELBQAwVzELMAkG\n"
"A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n"
"b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw0xNDAyMjAxMDAw\n"
"MDBaFw0yNDAyMjAxMDAwMDBaMGYxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n"
"YWxTaWduIG52LXNhMTwwOgYDVQQDEzNHbG9iYWxTaWduIE9yZ2FuaXphdGlvbiBW\n"
"YWxpZGF0aW9uIENBIC0gU0hBMjU2IC0gRzIwggEiMA0GCSqGSIb3DQEBAQUAA4IB\n"
"DwAwggEKAoIBAQDHDmw/I5N/zHClnSDDDlM/fsBOwphJykfVI+8DNIV0yKMCLkZc\n"
"C33JiJ1Pi/D4nGyMVTXbv/Kz6vvjVudKRtkTIso21ZvBqOOWQ5PyDLzm+ebomchj\n"
"SHh/VzZpGhkdWtHUfcKc1H/hgBKueuqI6lfYygoKOhJJomIZeg0k9zfrtHOSewUj\n"
"mxK1zusp36QUArkBpdSmnENkiN74fv7j9R7l/tyjqORmMdlMJekYuYlZCa7pnRxt\n"
"Nw9KHjUgKOKv1CGLAcRFrW4rY6uSa2EKTSDtc7p8zv4WtdufgPDWi2zZCHlKT3hl\n"
"2pK8vjX5s8T5J4BO/5ZS5gIg4Qdz6V0rvbLxAgMBAAGjggElMIIBITAOBgNVHQ8B\n"
"Af8EBAMCAQYwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHQ4EFgQUlt5h8b0cFilT\n"
"HMDMfTuDAEDmGnwwRwYDVR0gBEAwPjA8BgRVHSAAMDQwMgYIKwYBBQUHAgEWJmh0\n"
"dHBzOi8vd3d3Lmdsb2JhbHNpZ24uY29tL3JlcG9zaXRvcnkvMDMGA1UdHwQsMCow\n"
"KKAmoCSGImh0dHA6Ly9jcmwuZ2xvYmFsc2lnbi5uZXQvcm9vdC5jcmwwPQYIKwYB\n"
"BQUHAQEEMTAvMC0GCCsGAQUFBzABhiFodHRwOi8vb2NzcC5nbG9iYWxzaWduLmNv\n"
"bS9yb290cjEwHwYDVR0jBBgwFoAUYHtmGkUNl8qJUC99BM00qP/8/UswDQYJKoZI\n"
"hvcNAQELBQADggEBAEYq7l69rgFgNzERhnF0tkZJyBAW/i9iIxerH4f4gu3K3w4s\n"
"32R1juUYcqeMOovJrKV3UPfvnqTgoI8UV6MqX+x+bRDmuo2wCId2Dkyy2VG7EQLy\n"
"XN0cvfNVlg/UBsD84iOKJHDTu/B5GqdhcIOKrwbFINihY9Bsrk8y1658GEV1BSl3\n"
"30JAZGSGvip2CTFvHST0mdCF/vIhCPnG9vHQWe3WVjwIKANnuvD58ZAWR65n5ryA\n"
"SOlCdjSXVWkkDoPWoC209fN5ikkodBpBocLTJIg1MGCUF7ThBCIxPTsvFwayuJ2G\n"
"K1pp74P1S8SqtCr4fKGxhZSM9AyHDPSsQPhZSZg=\n"
"-----END CERTIFICATE-----\n";

#if OLD_CERTIFICATE
/* GlobalSign Root R1 SHA1/RSA/2048
 *   Serial no.  04 00 00 00 00 01 15 4b 5a c3 94 */
"-----BEGIN CERTIFICATE-----\n"
"MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n"
"A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n"
"b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n"
"MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n"
"YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n"
"aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n"
"jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n"
"xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp\n"
"1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG\n"
"snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ\n"
"U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8\n"
"9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n"
"BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n"
"AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz\n"
"yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE\n"
"38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP\n"
"AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad\n"
"DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME\n"
"HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==\n"
"-----END CERTIFICATE-----\n";
#endif
}

/**
 * \brief HelloHTTPS implements the logic for fetching a file from a webserver
 * using a TCP socket and parsing the result.
 */
class HelloHTTPS {
public:
    /**
     * HelloHTTPS Constructor
     * Initializes the TCP socket, sets up event handlers and flags.
     *
     * @param[in] domain The domain name to fetch from
     * @param[in] port The port of the HTTPS server
     */
    HelloHTTPS(const char * domain, const uint16_t port, NetworkInterface *net_iface) :
            _domain(domain), _port(port)
    {
        _error = false;
        _gothello = false;
        _got200 = false;
        _bpos = 0;
        _request_sent = 0;
        _tcpsocket = new TCPSocket(net_iface);

#ifndef MBEDTLS_DRBG_ALT
        mbedtls_entropy_init(&_entropy);
        mbedtls_ctr_drbg_init(&_ctr_drbg);
#endif
        mbedtls_x509_crt_init(&_cacert);
        mbedtls_ssl_init(&_ssl);
        mbedtls_ssl_config_init(&_ssl_conf);
    }
    /**
     * HelloHTTPS Desctructor
     */
    ~HelloHTTPS() {
        delete _tcpsocket;

#ifndef MBEDTLS_DRBG_ALT
        mbedtls_entropy_free(&_entropy);
        mbedtls_ctr_drbg_free(&_ctr_drbg);
#endif
        mbedtls_x509_crt_free(&_cacert);
        mbedtls_ssl_free(&_ssl);
        mbedtls_ssl_config_free(&_ssl_conf);
    }
    /**
     * Start the test.
     *
     * Starts by clearing test flags, then resolves the address with DNS.
     *
     * @param[in] path The path of the file to fetch from the HTTPS server
     * @return SOCKET_ERROR_NONE on success, or an error code on failure
     */
    void startTest(const char *path) {
        /* Initialize the flags */
        _got200 = false;
        _gothello = false;
        _error = false;
        _disconnected = false;
        _request_sent = false;
        /* Fill the request buffer */
        _bpos = snprintf(_buffer, sizeof(_buffer) - 1, "GET %s HTTP/1.1\nHost: %s\n\n", path, HTTPS_SERVER_NAME);

        /*
         * Initialize TLS-related stuf.
         */
        int ret;
#ifndef MBEDTLS_DRBG_ALT
        if ((ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
                          (const unsigned char *) DRBG_PERS,
                          sizeof (DRBG_PERS))) != 0) {
            print_mbedtls_error("mbedtls_crt_drbg_init", ret);
            _error = true;
            return;
        }
#endif
        if ((ret = mbedtls_x509_crt_parse(&_cacert, (const unsigned char *) SSL_CA_PEM,
                           sizeof (SSL_CA_PEM))) != 0) {
            print_mbedtls_error("mbedtls_x509_crt_parse", ret);
            _error = true;
            return;
        }

        if ((ret = mbedtls_ssl_config_defaults(&_ssl_conf,
                        MBEDTLS_SSL_IS_CLIENT,
                        MBEDTLS_SSL_TRANSPORT_STREAM,
                        MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
            print_mbedtls_error("mbedtls_ssl_config_defaults", ret);
            _error = true;
            return;
        }

        mbedtls_ssl_conf_ca_chain(&_ssl_conf, &_cacert, NULL);
        
#ifndef MBEDTLS_DRBG_ALT
        mbedtls_ssl_conf_rng(&_ssl_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
#else
        mbedtls_ssl_conf_rng(&_ssl_conf, mbedtls_ctr_drbg_random, NULL);
#endif

#if UNSAFE
        mbedtls_ssl_conf_authmode(&_ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
#endif

#if DEBUG_LEVEL > 0
        mbedtls_ssl_conf_verify(&_ssl_conf, my_verify, NULL);
        mbedtls_ssl_conf_dbg(&_ssl_conf, my_debug, NULL);
        mbedtls_debug_set_threshold(DEBUG_LEVEL);
#endif

        if ((ret = mbedtls_ssl_setup(&_ssl, &_ssl_conf)) != 0) {
            print_mbedtls_error("mbedtls_ssl_setup", ret);
            _error = true;
            return;
        }

        mbedtls_ssl_set_hostname(&_ssl, HTTPS_SERVER_NAME);

        mbedtls_ssl_set_bio(&_ssl, static_cast<void *>(_tcpsocket),
                                   ssl_send, ssl_recv, NULL );


        /* Connect to the server */
        mbedtls_printf("Connecting with %s\r\n", _domain);
        _tcpsocket->connect( _domain, _port );

       /* Start the handshake, the rest will be done in onReceive() */
        mbedtls_printf("Starting the TLS handshake...\r\n");
        ret = mbedtls_ssl_handshake(&_ssl);
        if (ret < 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                print_mbedtls_error("mbedtls_ssl_handshake", ret);
                onError(_tcpsocket, -1 );
            }
            return;
        }
        ret = mbedtls_ssl_write(&_ssl, (const unsigned char *) _buffer, _bpos);
        if (ret < 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                print_mbedtls_error("mbedtls_ssl_write", ret);
                onError(_tcpsocket, -1 );
            }
            return;
        }

        /* It also means the handshake is done, time to print info */
        printf("TLS connection to %s established\r\n", HTTPS_SERVER_NAME);

        char buf[1024];
        mbedtls_x509_crt_info(buf, sizeof(buf), "\r    ",
                        mbedtls_ssl_get_peer_cert(&_ssl));
        mbedtls_printf("Server certificate:\r\n%s\r", buf);

#if defined(UNSAFE)
        uint32_t flags = mbedtls_ssl_get_verify_result(&_ssl);
        if( flags != 0 )
        {
            mbedtls_x509_crt_verify_info(buf, sizeof (buf), "\r  ! ", flags);
            printf("Certificate verification failed:\r\n%s\r\r\n", buf);
        }
        else
#endif
            printf("Certificate verification passed\r\n\r\n");


        /* Read data out of the socket */
        ret = mbedtls_ssl_read(&_ssl, (unsigned char *) _buffer, sizeof(_buffer));
        if (ret < 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                print_mbedtls_error("mbedtls_ssl_read", ret);
                onError(_tcpsocket, -1 );
            }
            return;
        }
        _bpos = static_cast<size_t>(ret);

        _buffer[_bpos] = 0;

        /* Check each of the flags */
        _got200 = _got200 || strstr(_buffer, HTTPS_OK_STR) != NULL;
        _gothello = _gothello || strstr(_buffer, HTTPS_HELLO_STR) != NULL;

        /* Print status messages */
        mbedtls_printf("HTTPS: Received %d chars from server\r\n", _bpos);
        mbedtls_printf("HTTPS: Received 200 OK status ... %s\r\n", _got200 ? "[OK]" : "[FAIL]");
        mbedtls_printf("HTTPS: Received '%s' status ... %s\r\n", HTTPS_HELLO_STR, _gothello ? "[OK]" : "[FAIL]");
        mbedtls_printf("HTTPS: Received message:\r\n\r\n");
        mbedtls_printf("%s", _buffer);
        _error = !(_got200 && _gothello);

        _tcpsocket->close();
    }
    /**
     * Check if the test has completed.
     * @return Returns true if done, false otherwise.
     */
    bool done() {
        return _error || (_got200 && _gothello);
    }
    /**
     * Check if there was an error
     * @return Returns true if there was an error, false otherwise.
     */
    bool error() {
        return _error;
    }
    /**
     * Closes the TCP socket
     */
    void close() {
        _tcpsocket->close();
        while (!_disconnected)
            __WFI();
    }
protected:
    /**
     * Helper for pretty-printing mbed TLS error codes
     */
    static void print_mbedtls_error(const char *name, int err) {
        char buf[128];
        mbedtls_strerror(err, buf, sizeof (buf));
        mbedtls_printf("%s() failed: -0x%04x (%d): %s\r\n", name, -err, err, buf);
    }

#if DEBUG_LEVEL > 0
    /**
     * Debug callback for mbed TLS
     * Just prints on the USB serial port
     */
    static void my_debug(void *ctx, int level, const char *file, int line,
                         const char *str)
    {
        const char *p, *basename;
        (void) ctx;

        /* Extract basename from file */
        for(p = basename = file; *p != '\0'; p++) {
            if(*p == '/' || *p == '\\') {
                basename = p + 1;
            }
        }

        mbedtls_printf("%s:%04d: |%d| %s", basename, line, level, str);
    }

    /**
     * Certificate verification callback for mbed TLS
     * Here we only use it to display information on each cert in the chain
     */
    static int my_verify(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
    {
        char buf[1024];
        (void) data;

        mbedtls_printf("\nVerifying certificate at depth %d:\n", depth);
        mbedtls_x509_crt_info(buf, sizeof (buf) - 1, "  ", crt);
        mbedtls_printf("%s", buf);

        if (*flags == 0)
            mbedtls_printf("No verification issue for this certificate\n");
        else
        {
            mbedtls_x509_crt_verify_info(buf, sizeof (buf), "  ! ", *flags);
            mbedtls_printf("%s\n", buf);
        }

        return 0;
    }
#endif

    /**
     * Receive callback for mbed TLS
     */
    static int ssl_recv(void *ctx, unsigned char *buf, size_t len) {
        int recv = -1;
        TCPSocket *socket = static_cast<TCPSocket *>(ctx);
        recv = socket->recv(buf, len);

        if(NSAPI_ERROR_WOULD_BLOCK == recv){
            return MBEDTLS_ERR_SSL_WANT_READ;
        }else if(recv < 0){
            return -1;
        }else{
            return recv;
        }
   }

    /**
     * Send callback for mbed TLS
     */
    static int ssl_send(void *ctx, const unsigned char *buf, size_t len) {
       int size = -1;
        TCPSocket *socket = static_cast<TCPSocket *>(ctx);
        size = socket->send(buf, len);

        if(NSAPI_ERROR_WOULD_BLOCK == size){
            return len;
        }else if(size < 0){
            return -1;
        }else{
            return size;
        }
    }

    void onError(TCPSocket *s, int error) {
        printf("MBED: Socket Error: %d\r\n", error);
        s->close();
        _error = true;
    }

protected:
    TCPSocket* _tcpsocket;

    const char *_domain;            /**< The domain name of the HTTPS server */
    const uint16_t _port;           /**< The HTTPS server port */
    char _buffer[RECV_BUFFER_SIZE]; /**< The response buffer */
    size_t _bpos;                   /**< The current offset in the response buffer */
    volatile bool _got200;          /**< Status flag for HTTPS 200 */
    volatile bool _gothello;        /**< Status flag for finding the test string */
    volatile bool _error;           /**< Status flag for an error */
    volatile bool _disconnected;
    volatile bool _request_sent;

#ifndef MBEDTLS_DRBG_ALT
    mbedtls_entropy_context _entropy;
    mbedtls_ctr_drbg_context _ctr_drbg;
#endif
    mbedtls_x509_crt _cacert;
    mbedtls_ssl_context _ssl;
    mbedtls_ssl_config _ssl_conf;
};


int main() {
    WiFiStackInterface wifi;
    wifi.connect("a", "qqqqqqqq", NULL, NSAPI_SECURITY_NONE);

    const char *ip_addr = wifi.get_ip_address();
    if (ip_addr) {
        mbedtls_printf("Client IP Address is %s\r\n", ip_addr);
    } else {
        mbedtls_printf("No Client IP Address\r\n");
    }

    HelloHTTPS hello(HTTPS_SERVER_NAME, HTTPS_SERVER_PORT, &wifi);
    hello.startTest(HTTPS_PATH);

    while (true) {
    }
}
