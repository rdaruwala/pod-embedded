#ifndef __DATA_H__
#define __DATA_H__

#include <stdint.h>
#include <time.h>
#include <retro.h>
#include <stdbool.h>
#define TIME_SINCE_LAST_RETRO 15000000
#define FILTER_NONE     0
#define FILTER_ROLLING  1
#define FILTER_EXP      2

/* Functions for initializing the entire struct, and individual parts of it */
int initData(void);
int initMetaData(void);
int initPressureData(void);
int initMotionData(void);
int initBmsData(void);
int initRmsData(void);
int initFlagData(void);
int initTimerData(void);
int isEarlyInit(void);

/* Filters */
float rollingAvgFloat(float *vals, int windowSize);
int rollingAvgInt(int *vals, int windowSize);
float expFilterFloat(float currVal, float prevVal, float weight);
int expFilterInt(int currVal, int prevVal, float weight);

/***
 *
 * Flags structure -
 *
 */

typedef struct flags_t {
    int readyPump;
    int pumpDown;
    int readyCommand;
    bool readyToBrake;
    int propulse;
    int emergencyBrake;
    int shouldStop;
    int shutdown;
    bool shouldBrake;
    bool isConnected;
    bool brakeInit;
    bool brakePrimAct;
    bool brakeSecAct;
    bool brakePrimRetr;
    bool brakeSecRetr;
    bool clrMotionData;
} flags_t;


/***
 *
 * The Data Struct - Top of the great hierarchy of our descending data
 * tree
 *
 */

typedef struct data_t {
    struct pressure_t *pressure;
    struct motion_t   *motion;
    struct bms_t      *bms;
    struct rms_t      *rms;
    struct flags_t    *flags;
    struct timers_t   *timers;
    int state;
} data_t;


/***
 *
 * Timers struct -- For making sure that our run happens in a timely manner
 *
 * Assumed to be uS
 */

typedef struct timers_t {
    uint64_t startTime;
    uint64_t oldRetro;
    uint64_t lastRetro;
	uint64_t lastRetros[NUM_RETROS];
    uint64_t crawlTimer;
} timers_t;


static inline uint64_t convertTouS(struct timespec *currTime) {
    return (uint64_t)((currTime->tv_sec * 1000000) + (currTime->tv_nsec / 1000));
}

static inline uint64_t getuSTimestamp() {
    struct timespec _temp;
    clock_gettime(CLOCK_MONOTONIC, &_temp);
    uint64_t _tempTs = convertTouS(&_temp);
    return _tempTs;
}

static inline uint64_t getSTimestamp() {
    struct timespec temp;
    clock_gettime(CLOCK_MONOTONIC, &temp);
    return (uint64_t) (temp.tv_sec);
}



/***
 * pressure_t - Pressure data from the braking system
 */
typedef struct pressure_t {
    double primTank;
    double primLine;
    double primAct;
    double secTank;
    double secLine;
    double secAct;
    double amb;
    double pv;
} pressure_t;


/***
 * motion_t - Where all movement data goes
 *
 * Fields are fairly self explanatory, assumed positive X direction (because the
 * pod only travels in one direction)
 *  
 * All values should be assumed metric (m, m/s, m/s/s)
 */
typedef struct motion_t {
    float pos;
    float vel;
    float accel;
    int retroCount;
    int missedRetro;
} motion_t;

/***
 * bms_t - All of the data collected about the battery system
 *
 */
typedef struct bms_t {
    float packCurrent;
    float packVoltage;
    int imdStatus;
    uint16_t packDCL;
    int16_t packCCL;
    uint16_t packResistance;
    uint8_t packHealth;
    float packOpenVoltage;
    uint16_t packCycles;
    uint16_t packAh;
    float inputVoltage;
    uint8_t Soc;
    uint16_t relayStatus;
    uint8_t highTemp;
    uint8_t lowTemp;
    uint8_t avgTemp;
    float cellMaxVoltage;
    float cellMinVoltage;
    uint16_t cellAvgVoltage;
    uint8_t maxCells;
    uint8_t numCells;
} bms_t;

/***
 * rms_t - Collection of all of the RMS (motor controller) data
 *
 * Read in and filled in via CAN from the motor
 */
typedef struct rms_t {
    uint16_t igbtTemp;
    uint16_t gateDriverBoardTemp;
    uint16_t controlBoardTemp;
    uint16_t motorTemp;
    int16_t motorSpeed;
    int16_t phaseACurrent;
    uint16_t phaseBCurrent;
    uint16_t phaseCCurrent;
    int16_t dcBusVoltage;
     //uint16_t output_voltage_peak;
    uint16_t lvVoltage;
    uint64_t canCode1;
    uint64_t canCode2;
    uint64_t faultCode1;
    uint64_t faultCode2;
    int16_t commandedTorque;
    int16_t actualTorque;
    uint16_t relayState;
    uint16_t  electricalFreq;
    int16_t dcBusCurrent;
    uint16_t outputVoltageLn;
	uint16_t VSMCode;

	uint16_t keyMode;
} rms_t;


extern data_t *data;
#endif
