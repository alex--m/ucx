/**
 * See file LICENSE for terms.
 */

#ifndef HAVE_CONFIG_H
#  define HAVE_CONFIG_H /* Force using config.h, so test would fail if header
                           actually tries to use it */
#endif

#include "hello_world_util.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ucg/api/ucg_pubsub.h>

static uint16_t server_port    = 13337;
static long test_string_length = 16;
static unsigned pubsub_id      = 1;

static void print_usage()
{
    fprintf(stderr, "Usage: ucg_publisher_subscriber [parameters]\n");
    fprintf(stderr, "UCG publisher/subscriber example utility\n");
    fprintf(stderr, "\nParameters are:\n");
    fprintf(stderr, "  -i <number> Set the ID for the subscription channel\n");
    fprintf(stderr, "  -n <name>   Set node name or IP address "
                    "of the server (required for non-roots and should be "
                    "ignored for the root server)\n");
    print_common_help();
    fprintf(stderr, "\n");
}

ucs_status_t parse_cmd(int argc, char * const argv[], char **server_name)
{
    int c = 0, idx = 0;

    while ((c = getopt(argc, argv, "i:n:p:s:m:h")) != -1) {
        switch (c) {
        case 'i':
            pubsub_id = atoi(optarg);
            break;
        case 'n':
            *server_name = optarg;
            break;
        case 'p':
            server_port = atoi(optarg);
            if (server_port <= 0) {
                fprintf(stderr, "Wrong server port number %d\n", server_port);
                return UCS_ERR_UNSUPPORTED;
            }
            break;
        case 's':
            test_string_length = atol(optarg);
            if (test_string_length < 0) {
                fprintf(stderr, "Wrong string size %ld\n", test_string_length);
                return UCS_ERR_UNSUPPORTED;
            }
            break;
        case 'm':
            test_mem_type = parse_mem_type(optarg);
            if (test_mem_type == UCS_MEMORY_TYPE_LAST) {
                return UCS_ERR_UNSUPPORTED;
            }
            break;
        case 'h':
        default:
            print_usage();
            return UCS_ERR_UNSUPPORTED;
        }
    }
    fprintf(stderr, "INFO: UCG_PUBSUB server = %s port = %d, pid = %d\n",
             *server_name, server_port, getpid());

    for (idx = optind; idx < argc; idx++) {
        fprintf(stderr, "WARNING: Non-option argument %s\n", argv[idx]);
    }
    return UCS_OK;
}

int bell = 0;
void ring_the_bell(void)
{
    bell = 1;
}

int main(int argc, char **argv)
{
    int ret         = -1;
    char *root_name = NULL;
    ucg_pubsub_subscription_t sub;
    ucs_status_t status;
    char *test_string;

    /* Parse the command line */
    status = parse_cmd(argc, argv, &root_name);
    CHKERR_JUMP(status != UCS_OK, "parse_cmd", err);

    ucg_pubsub_ctx_t ctx;
    struct sockaddr_in sock_addr   = {
            .sin_family            = AF_INET,
            .sin_port              = htons(server_port),
            .sin_addr              = {
                    .s_addr        = root_name ? inet_addr(root_name) : INADDR_ANY
            }
    };
    ucs_sock_addr_t server_address = {
            .addr                  = (struct sockaddr *)&sock_addr,
            .addrlen               = sizeof(struct sockaddr)
    };

    CHKERR_JUMP(sock_addr.sin_addr.s_addr == (uint32_t)-1, "lookup IP\n", err);

    test_string = mem_type_malloc(test_string_length);
    CHKERR_JUMP(test_string == NULL, "allocate memory", err);
    snprintf(test_string, test_string_length, "Hello world!");

    status = ucg_pubsub_init(&ctx, &server_address);
    CHKERR_JUMP(status != UCS_OK, "ucg_minimal_init", err_cleanup);

    sub.buffer  = test_string;
    sub.length  = test_string_length;
    sub.user_cb = ring_the_bell;
    sub.id      = pubsub_id;
    sub.flags   = 0;
    status      = ucg_pubsub_subscribe(&ctx, &server_address, &sub, 0);
    CHKERR_JUMP(status != UCS_OK, "ucg_minimal_broadcast", err_finalize);

    if (root_name) {
        while (!bell) {
            ucg_pubsub_progress(&ctx);
        }
    } else {
        status = ucg_pubsub_publish(&ctx, &sub);
        CHKERR_JUMP(status != UCS_OK, "ucg_minimal_broadcast", err_finalize);
    }

    printf("%s\n", test_string);
    ret = 0;

err_finalize:
    ucg_pubsub_finalize(&ctx);

err_cleanup:
    mem_type_free(test_string);

err:
    return ret;
}
