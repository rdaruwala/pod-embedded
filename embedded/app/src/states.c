/***
 *  Filename: states.c
 *
 *  Summary: All of the in-depth functionality of each state. Each state has its
 *  own actions and transitions and this is where all of the states actions are
 *  defined. It also has a number of helper functions to simplify the states
 *  actions.
 *
 */

/* Includes */
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "state_machine.h"
#include "data.h"
#include "states.h"
#include "hv_iox.h"
#include <math.h>
#include "bms_fault_checking.h"
#include "rms_fault_checking.h"
#include "pressure_fault_checking.h"
#include "rms.h"
#include "connStat.h"
/*#define NO_FAULT*/
#define NUM_FAIL 10
#define LV_BATT_SOC_CALC(x) (pow(-1.1142 * (x), 6) + \
        pow(78.334      * (x), 5) - \
        pow(2280.5      * (x), 4) + \
        pow(35181.0     * (x), 3) - \
        pow(303240.0    * (x), 2) - (1000000.0 * (x)))

#define MAX_RETRO 3
#define MAX_PACKET_LOSS 500
/* Imports/Externs */
extern int internalCount;
extern stateMachine_t stateMachine;
stateTransition_t *runFault, *nonRunFault;
extern stateTransition_t *findTransition(state_t *currState, char *name);
int bErrs, pErrs, rErrs;
int checkNetwork() {
    static errs = 0;
    if(!checkUDPStat() || !checkTCPStat()) {
        if (checkUDPStat() == 0) fprintf(stderr, "UDP ");
        if (checkTCPStat() == 0) fprintf(stderr, "TCP ");
        fprintf(stderr,"CONNECTION DROPPED\n");
        errs += 1;
    } else {
        errs = 0;
    }

    if (errs >= MAX_PACKET_LOSS) {
        return -1;
    }
    return 0;
}

/***
 * checkStopped - checks a variety of values to make sure the pod is stopped.
 *
 * RETURNS: true if stopped, false if moving
 */

static bool checkStopped(void) {
	return fabs(data->motion->vel) < MAX_STOPPED_VEL &&  (getuSTimestamp() - data->timers->lastRetro) > TIME_SINCE_LAST_RETRO;
}

/***
 * Actions for all the states.
 * They perform transition and error condition
 * checking and perform the functionality of the state: e.g. brake if in
 * braking.
 *
 */
static bool first = true;

stateTransition_t * idleAction() {
    data->state = 1;
    
    if (checkUDPStat() && first) {
        genIdle();
        first = false;
    }

    if (data->flags->emergencyBrake) {
        printf("run fault: %p\n", runFault);
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    }

    return NULL;
}

stateTransition_t * pumpdownAction() {
    // First check for nominal values?
    data->state = 2;
    
    /* Check IMD status */
    
    if (getuSTimestamp() - stateMachine.start > 300000000)
        return stateMachine.currState->fault;

    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    } 

    // CHECK PRESSURE
    if(!checkPrerunPressures()){
        fprintf(stderr, "Pressure failure\n");
        pErrs += 1;
    } else pErrs = 0;
    
    if(!checkPrerunBattery()){
        fprintf(stderr, "prerun batt fault\n");
        bErrs += 1;
    } else bErrs = 0;
    
/*    if ((data->rms->faultCode1 << 8) &  )*/

    if(!checkPrerunRMS()){
        fprintf(stderr, "prerun rms fault\n");
        rErrs += 1;
    } else rErrs = 0;
    
    if (pErrs >= NUM_FAIL || bErrs >= NUM_FAIL || rErrs >= NUM_FAIL)
        return stateMachine.currState->fault;

    return NULL;
}

stateTransition_t * propulsionAction() {
    data->state = 3;
    /* Check IMD status */
    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    } 

    /* Check HV Indicator light */

    if (checkNetwork() != 0) return stateMachine.currState->fault;

    // CHECK FAULT CRITERIA
    // CHECK PRESSURE -- PreRun function still valid here
    if (!checkPrerunPressures()) { 
        fprintf(stderr, "Pressure failing\n");
        pErrs += 1;
    } else pErrs = 0;
    
    if(!checkRunBattery()){
        printf("Failed battery\n");
        bErrs += 1;
    } bErrs = 0;
    
    if(!checkRunRMS()){
        printf("run rms failed\n");
        rErrs += 1;
    } rErrs = 0;

    if (getuSTimestamp() - data->timers->startTime > 30000000/*MAX_RUN_TIME*/){
        fprintf(stderr, "Prop timeout\n");
        return stateMachine.currState->next;
    }

    if (data->motion->retroCount >= MAX_RETRO && data->flags->readyToBrake) {
        printf("retro transition\n");
        return findTransition(stateMachine.currState, BRAKING_NAME);
    }

    // CHECK TRANSITION CRITERIA
    if(data->flags->shouldStop){
        printf("Should Stop\n");
        return stateMachine.currState->next;
    }

    if (bErrs >= NUM_FAIL || pErrs >= NUM_FAIL || rErrs >= NUM_FAIL)
        return stateMachine.currState->fault;
    
    return NULL;
}

stateTransition_t * brakingAction() {
    data->state = 4;
    // TODO Do we differenciate between primary and secondary braking systems?
    // TODO Add logic to handle switches / actuate both
    
    if (data->flags->emergencyBrake) {
        printf("EMERG BRAKE\n");
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    } 
    if (!getIMDStatus()) {
        fprintf(stderr, "getIMDStatus()");
        return stateMachine.currState->fault;
    }

   
    if (checkNetwork() != 0) {
        printf("lost connection\n");
        return stateMachine.currState->fault;
    }

    // CHECK FAULT CRITERIA
    
    if (getuSTimestamp() - stateMachine.start > 5000000) {
            if(!checkBrakingPressures()){
                printf("braking==================\n");
                pErrs += 1;
        } else pErrs = 0;
    }

    if(!checkBrakingBattery()){
        printf("battery bad\n");
        bErrs += 1;
    } else bErrs = 0;

    if (getuSTimestamp() - stateMachine.start > 10000000) {
        if(!checkBrakingRMS()){
            printf("rms bad\n");
            rErrs += 1;
        } else rErrs = 0;         
    }
   

    if ((getuSTimestamp() - stateMachine.start)  > 15000000) {
        printf("going to stopped\n");
        return findTransition(stateMachine.currState, STOPPED_NAME);
    }
    
    if (pErrs >= NUM_FAIL || bErrs >= NUM_FAIL || rErrs >= NUM_FAIL) {
        printf("NUM FAILS: pErrs - %d | bErrs - %d | rErrs - %d\n");
        
        return stateMachine.currState->fault;
    }
    
    return NULL;
}

stateTransition_t * stoppedAction() {
	data->state = 5;
     /* Check IMD status */
    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    } 
    
   

    if (checkNetwork() != 0) return stateMachine.currState->fault;
    // CHECK FAULT CRITERIA
    
    if(!checkBrakingPressures()){ // Still unchanged
        printf("Pressures failing\n");
        pErrs += 1; 
    } else pErrs = 0;
    
    if(!checkBrakingBattery()){ // Still unchanged
        fprintf(stderr, "Battery error\n");
        bErrs += 1;
    } else bErrs = 0;
    
    if(!checkStoppedRMS()){ // Still unchanged
        printf("failrms\n");
        rErrs += 1;
    } else rErrs = 0;
    
    if (bErrs >= NUM_FAIL || pErrs >= NUM_FAIL || rErrs >= NUM_FAIL)
        return stateMachine.currState->fault;
	
    return NULL;
}

stateTransition_t * servPrechargeAction() {
    data->state = 6;
    if (!checkBrakingPressures()) {
        fprintf(stderr, "Pressure failed\n");
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    } else pErrs = 0;
    
    if (!getIMDStatus()) {
        fprintf(stderr, "getIMDStatus()");
        return stateMachine.currState->fault;
    }
    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState ,RUN_FAULT_NAME);
    } 
    if (checkNetwork() != 0) return stateMachine.currState->fault;
    return NULL;
}

static int prevRet = 0;

stateTransition_t * crawlAction() {
    data->state = 7;
 /* Check IMD status */
    prevRet = data->motion->retroCount;
    if (!getIMDStatus()) {
        return stateMachine.currState->fault;
    }

    if (checkNetwork() != 0) return stateMachine.currState->fault;

    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState ,RUN_FAULT_NAME);
    } 

    if ((data->motion->retroCount - internalCount) >= 2) {
        printf("retro transition\n");
        return findTransition(stateMachine.currState, POST_RUN_NAME);
    }

    if(!checkCrawlPostrunPressures()){
        printf("pres fail\n");
        pErrs += 1;    
    } else pErrs = 0;

    if(!checkCrawlBattery()){
        printf("batt fail\n");
        bErrs += 1;
    } else bErrs = 0;

    if(!checkCrawlRMS()){ // Still unchanged
        printf("rms fail\n");
        rErrs += 1;
    } else rErrs = 0;

    // CHECK TRANSITION CRITERIA
    
    if (getuSTimestamp() - stateMachine.start > 5000000){
        return findTransition(stateMachine.currState, POST_RUN_NAME );
    }
    
    if(data->flags->shouldStop){

        return findTransition(stateMachine.currState, POST_RUN_NAME);
    }
    
    printf("PRIM LINE: %f\n", data->pressure->primLine);
    if (bErrs >= NUM_FAIL || pErrs >= NUM_FAIL || rErrs >= NUM_FAIL) {
        printf("fail\n");
        return stateMachine.currState->fault;
    }


    return NULL;
}

stateTransition_t * postRunAction() {
    data->state = 8;
 /* Check IMD status */
    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    } 
    // TODO fixme there is a seg fault around here lol
    /* Check HV Indicator light */
    printf("FAILURE STATE: %p\n", stateMachine.currState);
    
    if (checkNetwork() != 0) findTransition(stateMachine.currState, RUN_FAULT_NAME);

    if(!checkPostrunBattery()){
        printf("battfail\n");
        bErrs += 1;
    } else bErrs = 0;

    if(!checkPostRMS()){
        printf("rmsfail\n");
        rErrs += 1;
    } else rErrs = 0;

    if (bErrs >= NUM_FAIL || rErrs >= NUM_FAIL) {
        printf("postFail\n");
        return findTransition(stateMachine.currState, RUN_FAULT_NAME);
    }
    return NULL;
}

stateTransition_t * safeToApproachAction() {
/*    if (isHVEnabled()) {*/
/*        return stateMachine.currState->fault;*/
/*    } */
    pressure_t *p = data->pressure;
    if (p->primTank > 30 || p->primTank < -15 || 
        p->primLine > 20 || p->primLine < -10 ||
        p->primAct  > 20 || p->primAct  < -10 ||
        p->secTank  > 30 || p->secTank  < -15 || 
        p->secLine  > 20 || p->secLine  < -10 || 
        p->secAct   > 20 || p->secAct   < -10 ||
        p->pv       > 20 || p->pv       <  13 ){
       fprintf(stderr, "Pressures are out of the safe range\n");
       return findTransition(stateMachine.currState, NON_RUN_FAULT_NAME);
    }
    
    if (checkNetwork() != 0) return findTransition(stateMachine.currState, NON_RUN_FAULT_NAME);
    if (data->flags->emergencyBrake) {
        return findTransition(stateMachine.currState ,NON_RUN_FAULT_NAME);
    } 
    data->state = 9;
	return NULL;
}
// 
//  We're removing pre and post faults and making them non run faults. 
// When you change this make 11 the non run fault action. Ty - EU
stateTransition_t * nonRunFaultAction() {
    data->flags->emergencyBrake = false;
    fprintf(stderr, "NON RUN FAULT\n");
    static int mcuDisPulse = 0;
    if (mcuDisPulse >= 50) {
        clrMotorEn();
        usleep(1000);
        rmsCmdNoTorque();
        usleep(1000);
        rmsDischarge();
        usleep(1000);
        rmsInvDis();
        usleep(1000);
        setMCUHVEnabled(0);
        mcuDisPulse = 0;
    } else {
        mcuDisPulse += 1;
    }
    data->state = 10;
	return NULL;
}

stateTransition_t * runFaultAction() {
	printf("RUN FAULT\n");
    data->flags->emergencyBrake = false;
    static int mcuDisPulse = 0;
    data->state = 11;
    if (mcuDisPulse >= 50) {
        setMCUHVEnabled(0);
        clrMotorEn();
        usleep(1000);
       
        rmsCmdNoTorque();
        usleep(1000);
        rmsDischarge();
        usleep(1000);
        rmsInvDis();
        usleep(1000);
        mcuDisPulse = 0;
    } else {
        mcuDisPulse += 1;
    }
    
    return NULL;
}

stateTransition_t * brakingFault() {
    //TODO
    data->state = 12;
    return NULL;
}
