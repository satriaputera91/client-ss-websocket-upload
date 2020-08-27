#include<stdio.h>
#include<json-c/json.h>
#include<json-c/json_tokener.h>
#include<string.h>
#include<stdlib.h>





int main(int argc, char **argv){
	char device_name[20];
	char device_id[10];
	char xy[20];
	char ip_hue[16];
	char username[100];
 
	struct json_object *jobj;	
	
	char *name = "{ \"bk_telkom2\": { \"data2\": {\"bridge2\":[{\"device_name\":\"philips\", \"email\":\"edo08syahputra@gmail.com\", \"ip\": \"10.226.174.34\", \"device_id\": \"2\", \"xy\": [0.23,0.23], \"username\": \"123456qwert\"}], \"intent\":{\"name\":\"iot\", \"confidence\":123}}} \ 
			}";	
	printf("string:\n%s\n\n\n", name);

	printf("\n\n\n");

	if(name!=NULL){
		printf("The string is not empty\n\n");
	};

	jobj = json_tokener_parse(name);
	
	printf("jobj from str:\n---\n%s\n---\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));

	struct json_object * bktelkom2 = json_object_object_get(jobj, "bk_telkom2");
	struct json_object * data2 = json_object_object_get(bktelkom2, "data2");
	struct json_object * bridge2 = json_object_object_get(data2, "bridge2");
	//struct json_object * device = json_object_object_get(data2, "device_name");

	printf("bridge2 from str:\n---\n%s\n---\n", json_object_to_json_string_ext(bridge2, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));

	int src_array_len = 0;

	src_array_len = json_object_array_length(bridge2);

	printf("%d\n", src_array_len);
	
	struct json_object * intent = json_object_object_get(data2, "intent");
	struct json_object * type = json_object_object_get(intent, "name");

	if (strncmp(json_object_get_string(type), "iot", 3) == 0) {
		//lwsl_user("iot jalan\n")
		printf("hello!\n\n");
			
		struct json_object * lampu_idx = json_object_object_get(data2, "bridge2");
		struct json_object * lampu = json_object_array_get_idx(lampu_idx, 0);
				
		if(lampu != NULL){
			printf("hello2!\n");
			strcpy(device_id, json_object_get_string(json_object_object_get(lampu, "device_id")));
			strcpy(ip_hue, json_object_get_string(json_object_object_get(lampu, "ip")));
			strcpy(username, json_object_get_string(json_object_object_get(lampu, "username")));

			struct json_object *xy_array, *xy_array_obj, *xy_array_obj_name, *compo;
			int arraylen, i;
			xy_array = json_object_object_get(lampu, "xy");
			arraylen = json_object_array_length(xy_array);
			

			printf("length of xy: %d\n", arraylen);
			for(i = 0; i < arraylen; i++){
				printf("xy1: %.3f\n", json_object_get_double(json_object_array_get_idx(xy_array,i)));
			}
	
		}

		printf("%s\n", device_id);
		printf("%s\n", ip_hue);
		//printf("%f\n", xy);
		printf("%s\n", username);
	}

}
