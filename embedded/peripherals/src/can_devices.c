#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include "can.h"
#include "bms.h"
#include "rms.h"
#include "can_devices.h"

//Global Variables
pthread_t CANThread;

void SetupCANDevices(){
	initCan();
	
	if (pthread_create(&CANThread, NULL, CANLoop, NULL)){
		fprintf(stderr, "Error creating CAN thread\n");
	}
}

void rx_recv(struct can_frame *can_mesg){
		if(!canRead(can_mesg)){ // Checks for a CAN message
			printf("ID: %#X || ", (unsigned int) can_mesg->can_id);
			printf("Data: [%#X.%#X.%#X.%#X.%#X.%#X.%#X.%#X]\n\r", can_mesg->data[0], can_mesg->data[1], can_mesg->data[2], can_mesg->data[3], can_mesg->data[4], can_mesg->data[5], can_mesg->data[6], can_mesg->data[7]);
			bool validRMSMesg = false;
			if(rms_parser(can_mesg->can_id, can_mesg->data)){
				printf("RMS Data parsed successfully\n");
				validRMSMesg = true;
			}
	   
			if(!validRMSMesg && bmsParseMsg(can_mesg->can_id, can_mesg->data)){
				printf("BMS Data parsed successfully\n");
				bmsDump();
			
		}
		
		NEW_CAN_MESSAGE = false;
	}
}


void *CANLoop(void *arg){
	(void) arg;
	
	
	struct can_frame can_mesg;
	while(1){
		rx_recv(&can_mesg);
		usleep(50);
	}
}