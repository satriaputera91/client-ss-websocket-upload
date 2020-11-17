/*
 ============================================================================
 Name        : client-ss-websockets-lite.c
 Author      : satriaputera91@gmail.com
 Version     : 1.0
 Copyright   : PT Bahasa Kinerja Utama
 Description : This is smart speaker bahasakita and telkom client with
 websockets protocol
 ============================================================================
 */

#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <json-c/json.h>
#include <uuid/uuid.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include "apa102.h"
#include "client-socket.h"
#include "http-request.h"
#include "keycode.h"

#define SS_IDLE 0
#define SS_WAIT 1
#define SS_AUTH_MACHINE_ID 2
#define SS_AUDIO_CONNECT 3
#define SS_AUDIO_STREAM 4
#define SS_FINISHED 5

#define PERIOD_SIZE 800
#define BUF_SIZE (PERIOD_SIZE * 2)

static int interrupted;
char uuid_value[256];
char filename[1024];
static int volume;

/* one of these created for each message */

int file_exist(const char *filename)
{
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

struct msg
{
    void *payload; /* is malloc'd */
    size_t len;
};

struct per_vhost_data__minimal
{
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
    pthread_t pthread_spam[1];

    pthread_mutex_t lock_ring; /* serialize access to the ring buffer */
    struct lws_ring *ring;     /* ringbuffer holding unsent messages */
    uint32_t tail;

    struct lws_client_connect_info i;
    struct lws *client_wsi;

    int counter;
    int status;
    char finished;
    char established;
};

#if defined(WIN32)
static void usleep(unsigned long l)
{
    Sleep(l / 1000);
}
#endif

static void __minimal_destroy_message(void *_msg)
{
    struct msg *msg = _msg;

    free(msg->payload);
    msg->payload = NULL;
    msg->len = 0;
}

static void *
thread_spam(void *d)
{
    struct per_vhost_data__minimal *vhd = (struct per_vhost_data__minimal *)d;
    struct msg amsg;
    int len = 2048, index = 1, n;
    struct timeval tv;

    //snd_pcm_t *capture_handle;
    char buf[BUF_SIZE * 2];

    char bs64_audio[10024];
    int bs64_len = 10024;

    char stan[37];
    unsigned int val;
    int err;
    int nread;
    double time_in_mill;
    uuid_t uuid;

    int timeout = 0;

    //system("play -q /root/greeter.wav");
    FILE *fhandle;
    int64_t length, result;
    struct stat buffer;
    int remainder, chunk_size;

    if (stat(filename, &buffer) != 0)
    {
        lwsl_user("file not found");
    }
    else
    {
        fhandle = fopen(filename, "rb");
        fseek(fhandle, 0, SEEK_END);
        length = ftell(fhandle) - 44;
        rewind(fhandle);
        fseek(fhandle, 44, SEEK_SET);
    }

    remainder = length;
    chunk_size = BUF_SIZE * 2;

    lwsl_user("remainder : %d, chunk : %d \n", remainder, chunk_size);

    do
    {

        /* don't generate output if client not connected */
        if (!vhd->established)
        {
            timeout++;
            if (timeout > 10)
            {
                vhd->finished = 1;
                interrupted = 1;
            }
            lwsl_user("no established");
            goto wait;
        }

        pthread_mutex_lock(&vhd->lock_ring); /* --------- ring lock { */

        /* only create if space in ringbuffer */
        n = (int)lws_ring_get_count_free_elements(vhd->ring);
        if (!n)
        {
            lwsl_user("dropping!\n");
            goto wait_unlock;
        }

        gettimeofday(&tv, NULL);
        time_in_mill = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;

        uuid_generate_random(uuid);
        uuid_unparse(uuid, stan);
        if (vhd->status != SS_WAIT)
        {
            if (vhd->status == SS_IDLE)
            {

                /* send authentication device */

                amsg.payload = malloc(LWS_PRE + len);
                n =
                    lws_snprintf((char *)amsg.payload + LWS_PRE, len,
                                 "{\"bk_telkom\":{\"type\":\"authDevice\",\"stan\":\"%s\","
                                 "\"timestamp\":%0.0f,\"code\":0,\"version\":\"1.0\",\"data\":"
                                 "{\"uid\":\"%s\"}}}",
                                 stan,
                                 time_in_mill, uuid_value);
                vhd->status = SS_WAIT;
            }
            else if (vhd->status == SS_AUTH_MACHINE_ID)
            {

                /* send audio connect */
                amsg.payload = malloc(LWS_PRE + len);
                n =
                    lws_snprintf((char *)amsg.payload + LWS_PRE, len,
                                 "{\"bk_telkom\":{\"type\":\"audioConn\",\"stan\":\"%s\","
                                 "\"timestamp\":%0.0f,\"code\":0,\"version\":\"1.0\",\"data\":"
                                 "{\"uid\":\"%s\"}}}",
                                 stan,
                                 time_in_mill, uuid_value);
                vhd->status = SS_WAIT;
            }
            else if (vhd->status == SS_AUDIO_CONNECT || vhd->status == SS_AUDIO_STREAM)
            {

                memset(buf, 0, BUF_SIZE * 2);
                if (remainder < chunk_size)
                {
                    chunk_size = remainder;
                }
                usleep(50000); // 50 milisecond
                if ((result = fread(buf, 1, chunk_size, fhandle)) > 0)
                {
                    lwsl_user("result audio : %d \n", result);
                    remainder = remainder - chunk_size;
                    if (result != chunk_size)
                    {
                        lwsl_user(" error size audio");
                        continue;
                    }
                }
                else
                {
                    continue;
                }

                /* send streaming audio */
                n = lws_b64_encode_string(buf, chunk_size, bs64_audio,
                                          //n = lws_b64_encode_string(buf, BUF_SIZE * 2, bs64_audio,
                                          bs64_len);
                bs64_audio[n] = '\0';
                amsg.payload = malloc(LWS_PRE + len + n);
                n =
                    lws_snprintf((char *)amsg.payload + LWS_PRE, len + n,
                                 "{\"bk_telkom\":{\"type\":\"audioStream\",\"stan\":\"%s\","
                                 "\"timestamp\":%0.0f,\"code\":0,\"version\":\"1.0\",\"data\":"
                                 "{\"uid\":\"%s\",\"audio\":\"%s\"}}}",
                                 stan, time_in_mill, uuid_value, bs64_audio);
            }

            if (!amsg.payload)
            {
                lwsl_user("OOM: dropping\n");
                goto wait_unlock;
            }

            amsg.len = n;
            n = lws_ring_insert(vhd->ring, &amsg, 1);

            if (n != 1)
            {
                __minimal_destroy_message(&amsg);
                lwsl_user("dropping!\n");
            }
            else
            {
                /*
				 * This will cause a LWS_CALLBACK_EVENT_WAIT_CANCELLED
				 * in the lws service thread context.
				 */
                lws_cancel_service(vhd->context);
            }
        }
    wait_unlock:
        pthread_mutex_unlock(&vhd->lock_ring); /* } ring lock ------- */

    wait:
        usleep(100000);

    } while (!vhd->finished);

    //snd_pcm_drain(capture_handle);
    //snd_pcm_close(capture_handle);
    if (timeout > 10)
    {
        system("play -q /root/wrong.wav");
    }
    fclose(fhandle);

    /*if (s) {
		pa_simple_free(s);
	}*/

    lwsl_notice("thread_spam %p exiting\n", (void *)pthread_self());
    pthread_exit(NULL);

    return NULL;
}

static int connect_client(struct per_vhost_data__minimal *vhd)
{

    vhd->i.context = vhd->context;
    vhd->i.port = 8765;
    //vhd->i.port = 1080;
    //vhd->i.port = 31909;
    //vhd->i.address = "api.bahasakita.co.id";
    vhd->i.address = "localhost";
    //vhd->i.address = "10.226.174.160";
    //vhd->i.address = "indira.s.1elf.net";
    //vhd->i.address = "smartspeaker-gateway.vsan-apps.playcourt.id";
    vhd->i.path = "/ws";
    vhd->i.host = vhd->i.address;
    vhd->i.origin = vhd->i.address;
    vhd->i.ssl_connection = 0;

    vhd->i.protocol = "lws-minimal-broker";
    vhd->i.pwsi = &vhd->client_wsi;

    return !lws_client_connect_via_info(&vhd->i);
}

static int callback_minimal_broker(struct lws *wsi,
                                   enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    struct per_vhost_data__minimal *vhd =
        (struct per_vhost_data__minimal *)lws_protocol_vh_priv_get(
            lws_get_vhost(wsi), lws_get_protocol(wsi));
    const struct msg *pmsg;
    void *retval;
    int n, m, r = 0;

    switch (reason)
    {

        /* --- protocol lifecycle callbacks --- */

    case LWS_CALLBACK_PROTOCOL_INIT:
        vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
                                          lws_get_protocol(wsi), sizeof(struct per_vhost_data__minimal));
        vhd->context = lws_get_context(wsi);
        vhd->protocol = lws_get_protocol(wsi);
        vhd->vhost = lws_get_vhost(wsi);
        vhd->status = SS_IDLE;
        vhd->ring = lws_ring_create(sizeof(struct msg), 8,
                                    __minimal_destroy_message);
        if (!vhd->ring)
            return 1;

        pthread_mutex_init(&vhd->lock_ring, NULL);

        /* start the content-creating threads */

        for (n = 0; n < (int)LWS_ARRAY_SIZE(vhd->pthread_spam); n++)
            if (pthread_create(&vhd->pthread_spam[n], NULL, thread_spam, vhd))
            {
                lwsl_err("thread creation failed\n");
                r = 1;
                goto init_fail;
            }

        if (connect_client(vhd))
            lws_timed_callback_vh_protocol(vhd->vhost, vhd->protocol,
                                           LWS_CALLBACK_USER, 1);
        break;

    case LWS_CALLBACK_PROTOCOL_DESTROY:
    init_fail:
        vhd->finished = 1;
        for (n = 0; n < (int)LWS_ARRAY_SIZE(vhd->pthread_spam); n++)
            if (vhd->pthread_spam[n])
                pthread_join(vhd->pthread_spam[n], &retval);

        if (vhd->ring)
            lws_ring_destroy(vhd->ring);

        pthread_mutex_destroy(&vhd->lock_ring);

        return r;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        lwsl_err("CLIENT_CONNECTION_ERROR: %s\n", in ? (char *)in : "(null)");
        vhd->client_wsi = NULL;
        lws_timed_callback_vh_protocol(vhd->vhost, vhd->protocol,
                                       LWS_CALLBACK_USER, 1);
        break;

        /* --- client callbacks --- */

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_user("%s: established\n", __func__);
        vhd->established = 1;
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:

        pthread_mutex_lock(&vhd->lock_ring); /* --------- ring lock { */

        pmsg = lws_ring_get_element(vhd->ring, &vhd->tail);
        if (!pmsg)
            goto skip;

        /* notice we allowed for LWS_PRE in the payload already */
        m = lws_write(wsi, ((unsigned char *)pmsg->payload) + LWS_PRE,
                      pmsg->len, LWS_WRITE_TEXT);

        if (m < (int)pmsg->len)
        {
            pthread_mutex_unlock(&vhd->lock_ring); /* } ring lock */
            lwsl_err("ERROR %d writing to ws socket\n", m);
            return -1;
        }

        lws_ring_consume_single_tail(vhd->ring, &vhd->tail, 1);

        /* more to do for us? */
        if (lws_ring_get_element(vhd->ring, &vhd->tail))
            /* come back as soon as we can write more */
            lws_callback_on_writable(wsi);

    skip:
        pthread_mutex_unlock(&vhd->lock_ring); /* } ring lock ------- */
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        lwsl_notice("%s: LWS_CALLBACK_CLOSED\n", __func__);

        vhd->client_wsi = NULL;
        vhd->established = 0;
        vhd->status = SS_FINISHED;
        interrupted = 1;

        break;

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        /*
		 * When the "spam" threads add a message to the ringbuffer,
		 * they create this event in the lws service thread context
		 * using lws_cancel_service().
		 *
		 * We respond by scheduling a writable callback for the
		 * connected client, if any.
		 */
        if (vhd && vhd->client_wsi && vhd->established)
            lws_callback_on_writable(vhd->client_wsi);
        break;

        /* rate-limited client connect retries */

    case LWS_CALLBACK_USER:
        lwsl_notice("%s: LWS_CALLBACK_USER\n", __func__);
        if (connect_client(vhd))
            lws_timed_callback_vh_protocol(vhd->vhost, vhd->protocol,
                                           LWS_CALLBACK_USER, 1);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        lwsl_notice("%s: LWS_CALLBACK_CLIENT_RECEIVE\n", __func__);

        printf("%s, size : %d \n", (char *)in, (int)strlen(in));

        struct json_object *jmessage;
        int ret = 0;

        jmessage = json_tokener_parse(in);

        if (jmessage != NULL)
        {
            printf("RECV : %s \n", json_object_to_json_string(jmessage));
            lwsl_user("Recieve : %s \n", json_object_to_json_string(jmessage));

            struct json_objcet *bk_telkom = json_object_object_get(jmessage,
                                                                   "bk_telkom");

            struct json_objcet *type = json_object_object_get(bk_telkom,
                                                              "type");

            struct json_objcet *data = json_object_object_get(bk_telkom,
                                                              "data");

            struct json_objcet *status = json_object_object_get(data,
                                                                "status");

            if (strncmp(json_object_get_string(type), "authDevice", 10) == 0)
            {

                if (json_object_get_int(status) == 200)
                {
                    //printf("Auth machine successed\n");
                    lwsl_user("Authentication machine successed\n");

                    vhd->status = SS_AUTH_MACHINE_ID;
                }
            }
            else if (strncmp(json_object_get_string(type), "audioConn", 9) == 0)
            {
                if (json_object_get_int(status) == 200)
                {
                    vhd->status = SS_AUDIO_CONNECT;
                }
            }
            else if (strncmp(json_object_get_string(type), "audioStream", 11) == 0)
            {
                vhd->status = SS_AUDIO_STREAM;

                struct json_objcet *endpoint = json_object_object_get(data,
                                                                      "endpoint");

                if (!json_object_get_boolean(endpoint))
                {
                    //printf("End of stream\n");
                    lwsl_user("End of stream\n");

                    vhd->finished = 1;
					
                    lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND,
					LWS_TO_KILL_ASYNC);
                    //lws_set_timeout(wsi, NO_PENDING_TIMEOUT, 0);
				    lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, "bye", 0);


                }
            }
            //lwsl_user("Finished \n");
            json_object_put(jmessage);
        }

        break;
    default:
        break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols protocols[] = {{
                                                     "lws-minimal-broker",
                                                     callback_minimal_broker,
                                                     0,
                                                     10024,
                                                 },
                                                 {NULL, NULL, 0, 0}};

static void sigint_handler(int sig)
{
    interrupted = 1;
}

int main(int argc, const char **argv)
{
    struct lws_context_creation_info info;
    struct lws_context *context;
    const char *p;
    int n = 0, logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE

        /* for LLL_ verbosity above NOTICE to be built into lws,
	 * lws must have been configured and built with
	 * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
        /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
        /* | LLL_EXT */ /* | LLL_CLIENT */  /* | LLL_LATENCY */
        /* | LLL_DEBUG */;

    /*  int ret;
    FILE *fp;

    volume = 50;

    ret = system("/bin/cat /etc/machine-id > /tmp/.uuid_machine");

    if (ret != 0)
    {
        fprintf(stderr, "Error: failed to get UUID of the machine.\n");
        exit(1);
    }
    
    fp = fopen("/tmp/.uuid_machine", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Error: failed to open /tmp/.uuid_machine\n");
        exit(1);
    }
    fscanf(fp, "%s", uuid_value);
    //strcpy(uuid_value, "ardhimaarik");
    lwsl_user("Name ID : %s\n", uuid_value);
    fclose(fp);
    unlink("/tmp/.uuid_machine");
    */

    signal(SIGINT, sigint_handler);

    if ((p = lws_cmdline_option(argc, argv, "-d")))
        logs = atoi(p);

    lws_set_log_level(logs, NULL);
    
    if ((p = lws_cmdline_option(argc, argv, "-f"))){
        printf("filename : %s %d \n",p, strlen(p));
        strcpy(filename,p);
        filename[strlen(p)]='\0';
    }


    printf("filename : %s %d \n",filename, strlen(filename));
    strcpy(uuid_value,"ec50cf6218ddf2c1c2b5086451c4f6d550eff5e0c44f61dddf5a749f886257b9");
    

    
    if( strlen(filename) <= 0 ) {
         lwsl_err("please input filename\n");  
         return -1;
    }
    

    lwsl_user("LWS minimal ws client tx\n");
    lwsl_user("Run minimal-ws-broker and browse to that\n");

    memset(&info, 0, sizeof info);      /* otherwise uninitialized garbage */
    info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
    info.protocols = protocols;
    /*
	 * since we know this lws context is only ever going to be used with
	 * one client wsis / fds / sockets at a time, let lws know it doesn't
	 * have to use the default allocations for fd tables up to ulimit -n.
	 * It will just allocate for 1 internal and 1 (+ 1 http2 nwsi) that we
	 * will use.
	 */
    info.fd_limit_per_thread = 1 + 1 + 1;

    context = lws_create_context(&info);
    if (!context)
    {
        lwsl_err("lws init failed\n");
        return 1;
    }

    while (n >= 0 && !interrupted)
        n = lws_service(context, 50);

    remove("/var/tmp/event_action.lock");
    lws_context_destroy(context);
    lwsl_user("Completed\n");

    return 0;
}
