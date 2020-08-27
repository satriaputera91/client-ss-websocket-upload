/*
 ============================================================================
 Name        : client-ss-websockets.c
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

#define SS_IDLE			0
#define SS_WAIT			1
#define SS_AUTH_MACHINE_ID 	2
#define SS_AUDIO_CONNECT 	3
#define SS_AUDIO_STREAM 	4
#define SS_FINISHED 		5

#define PERIOD_SIZE	800
#define BUF_SIZE (PERIOD_SIZE * 2)

static int interrupted;
static char uuid_value[37];
static int volume;
struct APA102* strip;
struct APA102_Animation * anim;
struct APA102_Frame* colors[7];

/* one of these created for each message */

int keycodemaps(int number) {

	lwsl_user("mapping channel :%d \n", number);
	int res;
	switch (number) {
	case 0:
		res = (int) KEYCODE_0;
		break;
	case 1:
		res = (int) KEYCODE_1;
		break;
	case 2:
		res = (int) KEYCODE_2;
		break;
	case 3:
		res = (int) KEYCODE_3;
		break;
	case 4:
		res = (int) KEYCODE_4;
		break;
	case 5:
		res = (int) KEYCODE_5;
		break;
	case 6:
		res = (int) KEYCODE_6;
		break;
	case 7:
		res = (int) KEYCODE_7;
		break;
	case 8:
		res = (int) KEYCODE_8;
		break;
	case 9:
		res = (int) KEYCODE_9;
		break;
	default:
		res = (int) KEYCODE_0;
		break;
	}
	return res;
}

int file_exist(const char *filename) {
	struct stat buffer;
	return (stat(filename, &buffer) == 0);
}

int setparams(snd_pcm_t *handle, char *name) {
	snd_pcm_hw_params_t *hw_params;
	int err;

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
		fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	if ((err = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
		fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	if ((err = snd_pcm_hw_params_set_access(handle, hw_params,
			SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
		exit(1);
	}

	if ((err = snd_pcm_hw_params_set_format(handle, hw_params,
			SND_PCM_FORMAT_S16_LE)) < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n", snd_strerror(err));
		exit(1);
	}

	unsigned int rate = 16000;
	if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rate, 0))
			< 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
		exit(1);
	}
//printf("Rate for %s is %d\n", name, rate);

	if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, 1)) < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
		exit(1);
	}

	snd_pcm_uframes_t buffersize = BUF_SIZE;
	if ((err = snd_pcm_hw_params_set_buffer_size_near(handle, hw_params,
			&buffersize)) < 0) {
		printf("Unable to set buffer size %li: %s\n", (long int) BUF_SIZE,
				snd_strerror(err));
		exit(1);
		;
	}

	snd_pcm_uframes_t periodsize = PERIOD_SIZE;
//fprintf(stderr, "period size now %d\n", (int) periodsize);
	if ((err = snd_pcm_hw_params_set_period_size_near(handle, hw_params,
			&periodsize, 0)) < 0) {
		printf("Unable to set period size %li: %s\n", periodsize,
				snd_strerror(err));
		exit(1);
	}

	if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
		exit(1);
	}

	snd_pcm_uframes_t p_psize;
	snd_pcm_hw_params_get_period_size(hw_params, &p_psize, NULL);
//fprintf(stderr, "period size %d\n", (int) p_psize);

	snd_pcm_hw_params_get_buffer_size(hw_params, &p_psize);
//fprintf(stderr, "buffer size %d\n", (int) p_psize);
	unsigned int val;

	snd_pcm_hw_params_get_period_time(hw_params, &val, NULL);

	snd_pcm_hw_params_free(hw_params);

	if ((err = snd_pcm_prepare(handle)) < 0) {
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	return val;
}

struct msg {
	void *payload; /* is malloc'd */
	size_t len;
};

struct per_vhost_data__minimal {
	struct lws_context *context;
	struct lws_vhost *vhost;
	const struct lws_protocols *protocol;
	pthread_t pthread_spam[1];

	pthread_mutex_t lock_ring; /* serialize access to the ring buffer */
	struct lws_ring *ring; /* ringbuffer holding unsent messages */
	uint32_t tail;

	struct lws_client_connect_info i;
	struct lws *client_wsi;

	int counter;
	int status;
	char finished;
	char established;
};

#if defined(WIN32)
static void usleep(unsigned long l) {Sleep(l / 1000);}
#endif

static void __minimal_destroy_message(void *_msg) {
	struct msg *msg = _msg;

	free(msg->payload);
	msg->payload = NULL;
	msg->len = 0;
}

static void *
thread_spam(void *d) {
	struct per_vhost_data__minimal *vhd = (struct per_vhost_data__minimal *) d;
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

	/* The sample type to use */
	static const pa_sample_spec ss = { .format = PA_SAMPLE_S16LE, .rate = 16000,
			.channels = 1 };

	pa_simple *s = NULL;
	int ret = 1;
	int error;

	/* Create the recording stream */
	if (!(s = pa_simple_new(NULL, "client-smart-speaker", PA_STREAM_RECORD,
	NULL, "record", &ss, NULL, NULL, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n",
				pa_strerror(error));
		exit(1);
	}

	/* open device recorder */
	/*if ((err = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE,
	 0)) < 0) {

	 fprintf(stderr, "cannot open audio device %s\n",
	 snd_strerror(err));
	 exit(1);
	 }

	 val = setparams(capture_handle, "capture");*/
	//assert(val != 0);
	int timeout = 0;
	
	//system("play -q /root/greeter.wav");

	do {
		pa_simple_flush(s, &error);
		//snd_pcm_prepare(capture_handle);

		memset(buf, 0, BUF_SIZE * 2);

		if (pa_simple_read(s, buf, BUF_SIZE * 2, &error) < 0) {
			fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n",
					pa_strerror(error));
			continue;
		}

		/*if ((nread = snd_pcm_readi(capture_handle, buf, BUF_SIZE)) != BUF_SIZE) {
		 if (nread < 0) {
		 fprintf(stderr, "read from audio interface failed (%s)\n",
		 snd_strerror(nread));
		 continue;
		 }
		 //else {
		 // fprintf(stderr,
		 // "read from audio interface failed after %d frames\n",
		 // nread);
		 // }
		 }*/

		/* don't generate output if client not connected */
		if (!vhd->established) {
			timeout++;
			if (timeout > 100) {
				vhd->finished = 1;
				interrupted = 1;
			}
			lwsl_user("no established");
			goto wait;
		}
		pthread_mutex_lock(&vhd->lock_ring); /* --------- ring lock { */

		/* only create if space in ringbuffer */
		n = (int) lws_ring_get_count_free_elements(vhd->ring);
		if (!n) {
			lwsl_user("dropping!\n");
			goto wait_unlock;
		}

		gettimeofday(&tv, NULL);
		time_in_mill = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;

		uuid_generate_random(uuid);
		uuid_unparse(uuid, stan);
		if (vhd->status != SS_WAIT) {
			if (vhd->status == SS_IDLE) {

				/* send authentication device */

				amsg.payload = malloc(LWS_PRE + len);
				n =
						lws_snprintf((char *) amsg.payload + LWS_PRE, len,
								"{\"bk_telkom\":{\"type\":\"authDevice\",\"stan\":\"%s\","
										"\"timestamp\":%0.0f,\"code\":0,\"version\":\"1.0\",\"data\":"
										"{\"device_id\":\"%s\"}}}", stan,
								time_in_mill, uuid_value);
				vhd->status = SS_WAIT;
			} else if (vhd->status == SS_AUTH_MACHINE_ID) {

				/* send audio connect */
				amsg.payload = malloc(LWS_PRE + len);
				n =
						lws_snprintf((char *) amsg.payload + LWS_PRE, len,
								"{\"bk_telkom\":{\"type\":\"audioConn\",\"stan\":\"%s\","
										"\"timestamp\":%0.0f,\"code\":0,\"version\":\"1.0\",\"data\":"
										"{\"device_id\":\"%s\"}}}", stan,
								time_in_mill, uuid_value);
				vhd->status = SS_WAIT;

			} else if (vhd->status == SS_AUDIO_CONNECT
					|| vhd->status == SS_AUDIO_STREAM) {

				/* LED Active */
				if (vhd->status == SS_AUDIO_CONNECT) {
					if (anim != NULL) {
						APA102_KillAnimation(anim);
					}
				
					//Create the colors array (must be NULL-terminated)

					colors[0] = APA102_CreateFrame(31, 255, 255, 255);
					colors[1] = APA102_CreateFrame(31, 0, 255, 255);
					colors[2] = APA102_CreateFrame(31, 0, 198, 247);
					colors[3] = APA102_CreateFrame(31, 0, 140, 230);
					colors[4] = APA102_CreateFrame(31, 0, 80, 193);
					colors[5] = APA102_CreateFrame(31, 0, 0, 128); 
					colors[6] = 0;

					//Run animation
					anim = APA102_FadeAnimation(strip, colors, 500);

					//anim = APA102_BlinkAnimation(strip,
					//		APA102_CreateFrame(31, 0xFF, 0x00, 0x00), 60);

				}

				/* send streaming audio */
				n = lws_b64_encode_string(buf, BUF_SIZE * 2, bs64_audio,
						bs64_len);
				bs64_audio[n] = '\0';
				amsg.payload = malloc(LWS_PRE + len + n);
				n =
						lws_snprintf((char *) amsg.payload + LWS_PRE, len + n,
								"{\"bk_telkom\":{\"type\":\"audioStream\",\"stan\":\"%s\","
										"\"timestamp\":%0.0f,\"code\":0,\"version\":\"1.0\",\"data\":"
										"{\"device_id\":\"%s\",\"audio\":\"%s\"}}}",
								stan, time_in_mill, uuid_value, bs64_audio);
			}

			if (!amsg.payload) {
				lwsl_user("OOM: dropping\n");
				goto wait_unlock;
			}

			amsg.len = n;
			n = lws_ring_insert(vhd->ring, &amsg, 1);

			if (n != 1) {
				__minimal_destroy_message(&amsg);
				lwsl_user("dropping!\n");
			} else {
				/*
				 * This will cause a LWS_CALLBACK_EVENT_WAIT_CANCELLED
				 * in the lws service thread context.
				 */
				lws_cancel_service(vhd->context);
			}

		}
		wait_unlock: pthread_mutex_unlock(&vhd->lock_ring); /* } ring lock ------- */

		wait: usleep(50);

	} while (!vhd->finished);

	//snd_pcm_drain(capture_handle);
	//snd_pcm_close(capture_handle);
	if (timeout > 100) {
		system("play -q /root/wrong.wav");
	}

	if (s) {
		pa_simple_free(s);
	}

	lwsl_notice("thread_spam %p exiting\n", (void * )pthread_self());
	pthread_exit(NULL);

	return NULL;
}

static int connect_client(struct per_vhost_data__minimal *vhd) {

	vhd->i.context = vhd->context;
	vhd->i.port = 8765;
	//vhd->i.port = 1080;
	//vhd->i.port = 31909;
	vhd->i.address = "bhskita.dynu.com";
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
		enum lws_callback_reasons reason, void *user, void *in, size_t len) {
	struct per_vhost_data__minimal *vhd =
			(struct per_vhost_data__minimal *) lws_protocol_vh_priv_get(
					lws_get_vhost(wsi), lws_get_protocol(wsi));
	const struct msg *pmsg;
	void *retval;
	int n, m, r = 0;

	switch (reason) {

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

		for (n = 0; n < (int) LWS_ARRAY_SIZE(vhd->pthread_spam); n++)
			if (pthread_create(&vhd->pthread_spam[n], NULL, thread_spam, vhd)) {
				lwsl_err("thread creation failed\n");
				r = 1;
				goto init_fail;
			}

		if (connect_client(vhd))
			lws_timed_callback_vh_protocol(vhd->vhost, vhd->protocol,
					LWS_CALLBACK_USER, 1);
		break;

	case LWS_CALLBACK_PROTOCOL_DESTROY:
		init_fail: vhd->finished = 1;
		for (n = 0; n < (int) LWS_ARRAY_SIZE(vhd->pthread_spam); n++)
			if (vhd->pthread_spam[n])
				pthread_join(vhd->pthread_spam[n], &retval);

		if (vhd->ring)
			lws_ring_destroy(vhd->ring);

		pthread_mutex_destroy(&vhd->lock_ring);

		return r;

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		lwsl_err("CLIENT_CONNECTION_ERROR: %s\n", in ? (char * )in : "(null)");
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
		m = lws_write(wsi, ((unsigned char *) pmsg->payload) + LWS_PRE,
				pmsg->len, LWS_WRITE_TEXT);

		if (m < (int) pmsg->len) {
			pthread_mutex_unlock(&vhd->lock_ring); /* } ring lock */
			lwsl_err("ERROR %d writing to ws socket\n", m);
			return -1;
		}

		lws_ring_consume_single_tail(vhd->ring, &vhd->tail, 1)
		;

		/* more to do for us? */
		if (lws_ring_get_element(vhd->ring, &vhd->tail))
			/* come back as soon as we can write more */
			lws_callback_on_writable(wsi);

		skip: pthread_mutex_unlock(&vhd->lock_ring); /* } ring lock ------- */
		break;

	case LWS_CALLBACK_CLIENT_CLOSED:
		lwsl_notice("%s: LWS_CALLBACK_CLOSED\n", __func__);
        lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND,
        LWS_TO_KILL_ASYNC);
        lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, "bye", 0);

		vhd->client_wsi = NULL;
		vhd->established = 0;
		vhd->status = SS_FINISHED;
		interrupted = 1;

		if (anim != NULL) {
			APA102_KillAnimation(anim);
		}

		//lws_timed_callback_vh_protocol(vhd->vhost, vhd->protocol,new
		//		LWS_CALLBACK_USER, 1);

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

		//printf("%s, size : %d \n", (char *) in, (int) strlen(in));

		struct json_object * jmessage;
		int ret = 0;

		jmessage = json_tokener_parse(in);

		if (jmessage != NULL) {
			printf("RECV : %s \n", json_object_to_json_string(jmessage));
			lwsl_user("Recieve : %s \n", json_object_to_json_string(jmessage));

			struct json_objcet * bk_telkom = json_object_object_get(jmessage,
					"bk_telkom");

			struct json_objcet * type = json_object_object_get(bk_telkom,
					"type");

			struct json_objcet * data = json_object_object_get(bk_telkom,
					"data");

 			struct json_objcet * status = json_object_object_get(data,
					"status");

            if (json_object_get_int(status) == 400) {
			    goto audio_wrong;
            }

			if (strncmp(json_object_get_string(type), "authDevice", 10) == 0) {


				if (json_object_get_int(status) == 200) {
					//printf("Auth machine successed\n");
					lwsl_user("Authentication machine successed\n");

					vhd->status = SS_AUTH_MACHINE_ID;

				}

			} else if (strncmp(json_object_get_string(type), "audioConn", 9)
					== 0) {
				if (json_object_get_int(status) == 200) {
					vhd->status = SS_AUDIO_CONNECT;
				}

			} else if (strncmp(json_object_get_string(type), "audioStream", 11)
					== 0) {
				vhd->status = SS_AUDIO_STREAM;

				struct json_objcet * endpoint = json_object_object_get(data,
						"endpoint");

				if (json_object_get_boolean(endpoint)) {
					//printf("End of stream\n");
					lwsl_user("End of stream\n");

					vhd->finished = 1;

					/* LED Thinking*/
					{
						if (anim != NULL) {
							APA102_KillAnimation(anim);
						}
						lwsl_user("LED Thinking \n");

						//anim = APA102_StripesAnimation(strip,
						//		APA102_CreateFrame(31, 0xFF, 0x00, 0x00), 50, 4,
						//		2, -1);

  						anim = APA102_StripesAnimation(strip, APA102_CreateFrame(31, 0xFF, 0xFF, 0xFF), 50, 1, 6, 1);
					}

				} else {

					/* LED Action*/
					{
					 if (anim != NULL) {
					 APA102_KillAnimation(anim);
					 }
					 lwsl_user("LED Action \n");
					 anim = APA102_PulseAnimation(strip,
					 APA102_CreateFrame(31, 0x00, 0x00, 0xFF), 20);
					 }

					// check send if data
					struct json_object * intent = json_object_object_get(data,
							"intent");

					struct json_object * name = json_object_object_get(
								intent, "name");

					if (intent != NULL && name != NULL) {
						
						struct json_object * confidence =
								json_object_object_get(intent, "confidence");

						char message[2048];
						int sockfd;
						int res;
						char ip[16];
						char ports[8];
						int port;
						char token[64];
						char uniqueid[38];
						char result[1024];

						if(strncmp(json_object_get_string(name), "stb", 3) == 0) {
							/*FILE *fp;

							fp = fopen("/root/stb_config", "r");
							if (fp == NULL) {
								fprintf(stderr,
										"Error: failed to open /root/stb_config\n");
								exit(1);
							}
							fscanf(fp, "%s %s %s %s", uniqueid, token, ip, ports);
                            json_object_get
							port = atoi(ports);
							fclose(fp);*/
                            lwsl_user("stb jalan\n");
					        struct json_object * stb = json_object_object_get(data,
							"stb");


							struct json_object * jstb =
										json_object_array_get_idx(stb, 0);
                            if(jstb != NULL){
                                strcpy(uniqueid,json_object_get_string(
                                    json_object_object_get(jstb, "unique_id")));

                                strcpy(token,json_object_get_string(
                                    json_object_object_get(jstb, "token")));

                                strcpy(ip,json_object_get_string(
                                    json_object_object_get(jstb, "ip")));

                                port = json_object_get_int(
                                    json_object_object_get(jstb, "port"));
                            }
                         
							lwsl_user("UID: %s, TOKEN: %s, IP: %s, PORT:%d\n",
									uniqueid, token, ip, port);

							// connect stb discovery
							/*lwsl_user("CONNECT STB DISCOVERY \n");
							 connect_udp(&sockfd, "192.168.1.255", 19876,
							 SOCK_DGRAM);

							 lwsl_user("STREAM STB DISCOVERY \n");
							 sprintf(message,
							 "{\"type\":\"getToken\", \"name\":\"%s\", \"unique_id\":\"%s\", \"authCode\":\"0956\"}",
							 uuid_value, uniqueid);
							 discovery(&sockfd,message);
							 lwsl_user("change channel :%s \n",message);
							 close_udp(&sockfd);
							 */

							lwsl_user("CONNECT UDP 0\n");

							if ((res = connect_udp(&sockfd, ip, port, SOCK_STREAM))
									== 0) {
								//printf("\n Error : Connect Failed \n");
								lwsl_err("Connection Failed\n");
							}

							if (res != 0) {
								lwsl_user("AUTH UDP 0.1\n");
								sprintf(message,
										"{\"type\":\"auth\",\"unique_id\" : \"%s\",\"token\" : \"%s\",\"name\": \"%s\"}\n",
										uniqueid, token, uuid_value);
								stream_udp(&sockfd, message, result);
							}else{
								goto audio_wrong;
							
							}

							if (strncmp(json_object_get_string(name),
									"stbgantichannel", 15) == 0) {

								lwsl_user("change channel \n");
								struct json_object * entity =
										json_object_object_get(data, "entity");

								if (entity != NULL) {

									struct json_object * value =
											json_object_object_get(entity, "value");

									char s_value[8];
									int number;
									strcpy(s_value, json_object_get_string(value));

									lwsl_user("channel :%s \n", s_value);
									for (int i = 0; i < strlen(s_value); i++) {

										// mapping channel
										number = s_value[i] - '0';
										lwsl_user("number :%d \n", number);
										int code = keycodemaps(number);

										sprintf(message, "{\"type\" : \"trigger\","
												"\"unique_id\" :\"%s\","
												"\"token\" :\"%s\","
												"\"event\" :{"
												"\"ACTION\": 0,"
												"\"keycode\" : %d,"
												"\"alt\": false,"
												"\"ctrl\": false,"
												"\"shift\": false,"
												"\"capslock\": false,"
												"\"long\": false,"
												"\"character\": 0 }}\n", uniqueid,
												token, code);

										lwsl_user("change channel :%s \n", message);

										setchannel_udp(&sockfd, message);
										sleep(0.1);
									}
								} /*else {
									goto audio_wrong;
								}*/

							} else if (strncmp(json_object_get_string(name),
										"stbaturvolume", 13) == 0) {

									lwsl_user("atur volume \n");

									struct json_object * entity =
											json_object_object_get(data, "entity");

									if (entity != NULL) {

										struct json_object * value =
												json_object_object_get(entity,
														"value");
										char s_value[8];

										strcpy(s_value,
												json_object_get_string(value));

										volume = atoi(s_value);

								
										sprintf(message, "{\"type\" : \"volume\","
												"\"unique_id\" :\"%s\","
												"\"token\" :\"%s\","
												"\"vol\" :%d}\n", uniqueid, token,
												volume);

										stream_udp(&sockfd, message, result);
									} /*else {
										goto audio_wrong;
									}*/


							} else if (strncmp(json_object_get_string(name),
									"stbnaikvolume", 13) == 0) {

								lwsl_user("naik volume \n");

								sprintf(message, "{\"type\" : \"volume\","
										"\"unique_id\" :\"%s\","
										"\"token\" :\"%s\","
										"\"vol\" :%d}\n", uniqueid, token, -1);

								stream_udp(&sockfd, message, result);

								json_object *jresult = json_tokener_parse(
										result);

								if (jresult != NULL) {
									json_object * jvolume =
											json_object_object_get(jresult,
													"vol");
									volume = json_object_get_int(jvolume);

									volume += 20;
									if (volume > 100) {
										volume = 100;
									}
									sprintf(message, "{\"type\" : \"volume\","
											"\"unique_id\" :\"%s\","
											"\"token\" :\"%s\","
											"\"vol\" :%d}\n", uniqueid, token,
											volume);

									stream_udp(&sockfd, message, result);
									json_object_put(jresult);
								}

							} else if (strncmp(json_object_get_string(name),
									"stbturunvolume", 14) == 0) {

								lwsl_user("turun volume \n");

								sprintf(message, "{\"type\" : \"volume\","
										"\"unique_id\" :\"%s\","
										"\"token\" :\"%s\","
										"\"vol\" :%d}\n", uniqueid, token, -1);

								stream_udp(&sockfd, message, result);
								json_object *jresult = json_tokener_parse(
										result);

								if (jresult != NULL) {
									json_object * jvolume =
											json_object_object_get(jresult,
													"vol");
									volume = json_object_get_int(jvolume);
									volume -= 20;
									if (volume <= 0) {
										volume = 1;
									}
									sprintf(message, "{\"type\" : \"volume\","
											"\"unique_id\" :\"%s\","
											"\"token\" :\"%s\","
											"\"vol\" :%d}\n", uniqueid, token,
											volume);

									stream_udp(&sockfd, message, result);
									json_object_put(jresult);
								}

							} else if (strncmp(json_object_get_string(name),
									"stbmutevolume", 13) == 0) {

								lwsl_user("mute volume \n");

								sprintf(message, "{\"type\" : \"volume\","
										"\"unique_id\" :\"%s\","
										"\"token\" :\"%s\","
										"\"vol\" : 1}\n", uniqueid, token);

								stream_udp(&sockfd, message, result);

							} else if (strncmp(json_object_get_string(name),
									"stbbukaaplikasi", 15) == 0) {

								lwsl_user("buka aplikasi \n");

								struct json_object * entity =
										json_object_object_get(data, "entity");

								if (entity != NULL) {

									struct json_object * value =
											json_object_object_get(entity,
													"value");
									char s_value[256];

									strcpy(s_value,
											json_object_get_string(value));

									sprintf(message,
											"{\"type\" : \"launch\","
													"\"unique_id\" :\"%s\","
													"\"token\" :\"%s\","
													"\"action\" :\"android.intent.action.MAIN\","
													"\"packageName\" : \"%s\"}\n",
											uniqueid, token, s_value);

								} else {
									goto audio_wrong;
								}

								stream_udp(&sockfd, message, result);
							}
							close_udp(&sockfd);
						}

						char url[2048];
						char x_m2m[256];
						if (strncmp(json_object_get_string(name),
								"nyalakanlampu", 13) == 0) {

							lwsl_user("nyalakan lampu \n");

							struct json_object * entity =
									json_object_object_get(data, "entity");

							if (entity != NULL) {

								struct json_object * value =
										json_object_object_get(entity, "value");

								char s_value[256];

								strcpy(s_value, json_object_get_string(value));

								strcpy(x_m2m, "X-M2M-ORIGIN: iot:iottelkom");

								sprintf(url,
										"https://platform.antares.id:8443/~/antares-cse/antares-id/PhilipHue-Speaker/%s",
										s_value);

								lwsl_user(url);

								strcpy(message,
										"{\"m2m:cin\":{\"con\":\"{\\\"on\\\":true,\\\"bri\\\":255}\"}}");

								int code = http_post(url,x_m2m, message,
										&result);

								lwsl_user("code :%d\n",code);

								if (code != 201) {

									lwsl_err("failed connect to smarthome \n");
								}
							}

						} else if (strncmp(json_object_get_string(name),
								"matikanlampu", 12) == 0) {

							lwsl_user("matikan lampu \n");
							struct json_object * entity =
									json_object_object_get(data, "entity");

							if (entity != NULL) {

								struct json_object * value =
										json_object_object_get(entity, "value");

								char s_value[256];

								strcpy(s_value, json_object_get_string(value));

								strcpy(x_m2m, "X-M2M-ORIGIN: iot:iottelkom");

								sprintf(url,
										"https://platform.antares.id:8443/~/antares-cse/antares-id/PhilipHue-Speaker/%s",
										s_value);

								strcpy(message,
										"{\"m2m:cin\":{\"con\":\"{\\\"on\\\":false,\\\"bri\\\":255}\"}}");

								int code = http_post(url, x_m2m, message,
										&result);

								printf("%d\n", code);
								//printf("%s\n",result);

								if (code != 201) {
									lwsl_err("failed connect to smarthome \n");
								}
							}

						}

					}

					if (anim != NULL) {
					 	APA102_KillAnimation(anim);
					}

					lwsl_user("play URL audio 3 \n");
					struct json_object * url = json_object_object_get(data,
							"url");
					if (ret == 0) {
						system("mpc stop");
						system("mpc clear");

						if (url != NULL) {
							int i = 0;
							
							while (i < json_object_array_length(url)) {

								struct json_object * jdata =
										json_object_array_get_idx(url, i);
								struct json_object * jpath;
								char res_path[2048];

								json_object_object_get_ex(jdata, "path",
										&jpath);
								if (jpath != NULL) {
									printf("PATH %d: %s \n", i,
											json_object_get_string(jpath));

									sprintf(res_path, "mpc add %s",
									//sprintf(res_path, "mplayer %s",
											json_object_get_string(jpath));
									system(res_path);
									i++;
								}
							}
						}

						system("mpc toggle");
						//delay(200);
					} else {
						//audio_wrong : lwsl_user("Finished \n");
						audio_wrong: system("aplay -q /root/wrong.wav");
					}

			        lwsl_user("Action Finished \n");
					lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND,
					LWS_TO_KILL_ASYNC);
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

static const struct lws_protocols protocols[] = { { "lws-minimal-broker",
		callback_minimal_broker, 0, 10024, }, { NULL, NULL, 0, 0 } };

static void sigint_handler(int sig) {
	interrupted = 1;
}

int main(int argc, const char **argv) {
	struct lws_context_creation_info info;
	struct lws_context *context;
	const char *p;
	int n = 0, logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE

	/* for LLL_ verbosity above NOTICE to be built into lws,
	 * lws must have been configured and built with
	 * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
	/* | LLL_INFO *//* | LLL_PARSER *//* | LLL_HEADER */
	/* | LLL_EXT *//* | LLL_CLIENT *//* | LLL_LATENCY */
	/* | LLL_DEBUG */;
	int ret;
	FILE *fp;
	//char uuid_key[8];
	strip = APA102_Init(60);

	creat("/var/tmp/event_action.lock",
	S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	volume = 50;
	/*if (file_exist("/dev/mem")) {
	 // printf("/dev/mem exists.\n");
	 ret =
	 system(
	 "/usr/bin/sudo /usr/sbin/dmidecode | /bin/grep UUID | sed s/UUID://g > /tmp/.uuid_machine");
	 if (ret != 0) {
	 fprintf(stderr, "Error: failed to get UUID of the machine.\n");
	 exit(1);
	 }

	 }else{*/
	ret = system("/bin/cat /etc/machine-id > /tmp/.uuid_machine");
	if (ret != 0) {
		fprintf(stderr, "Error: failed to get UUID of the machine.\n");
		exit(1);
	}

//}

	fp = fopen("/tmp/.uuid_machine", "r");
	if (fp == NULL) {
		fprintf(stderr, "Error: failed to open /tmp/.uuid_machine\n");
		exit(1);
	}
	fscanf(fp, "%s", uuid_value);
	//strcpy(uuid_value, "ardhimaarik");
	lwsl_user("Name ID : %s\n", uuid_value);
	fclose(fp);
	unlink("/tmp/.uuid_machine");

	signal(SIGINT, sigint_handler);

	if ((p = lws_cmdline_option(argc, argv, "-d")))
		logs = atoi(p);

	lws_set_log_level(logs, NULL);
	lwsl_user("LWS minimal ws client tx\n");
	lwsl_user("Run minimal-ws-broker and browse to that\n");

	memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
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
	if (!context) {
		lwsl_err("lws init failed\n");
		return 1;
	}

	while (n >= 0 && !interrupted)
		n = lws_service(context, 1000);

	remove("/var/tmp/event_action.lock");
	lws_context_destroy(context);
	lwsl_user("Completed\n");

	return 0;
}
