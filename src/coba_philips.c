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



int main(){
	if ((strncmp(json_object_get_string(name), "iot", 3) == 0) {
			lwsl_user("iot jalan\n")

			struct json_object * philips = json_object_object_get(data, "iot");
			struct json_object * jphilips = json_object_array_get_idx(jphilips, 0);
			
			if(jphilips != NULL){
				strcpy(device_id, json_object_get_string(json_object_object_get(jphilips, "device_id")));
				strcpy(xy, json_object_get_string(json_object_object_get(jphilips, "xy")));
				strcpy(ip_philips, json_object_get_string(json_object_object_get(jphilips, "ip_philips")));
				strcpy(username, json_object_get_string(json_object_object_get(jphilips, "username")));
															
}



