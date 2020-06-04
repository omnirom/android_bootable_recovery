#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cinttypes>
#include <cmath>
#include <iostream>
#include <sys/ioctl.h>

#include "tspdrv.h"

int tspdrv_initialized = 0;
int tspdrv_file_desc;
int tspdrv_numActuators = 0;

int initialize_tspdrv()
{
    // Open device file as read/write for ioctl and write 
    tspdrv_file_desc = open(TSPDRV, O_RDWR);
    if(tspdrv_file_desc < 0)
    {
        printf("Failed to open device file: %s", TSPDRV);
        return -1;
    }

    // create default device parameters
    device_parameter dev_param1 { 0, VIBE_KP_CFG_FREQUENCY_PARAM1, 0};
    device_parameter dev_param2 { 0, VIBE_KP_CFG_FREQUENCY_PARAM2, 0};
    device_parameter dev_param3 { 0, VIBE_KP_CFG_FREQUENCY_PARAM3, 0};
    device_parameter dev_param4 { 0, VIBE_KP_CFG_FREQUENCY_PARAM4, 400};
    device_parameter dev_param5 { 0, VIBE_KP_CFG_FREQUENCY_PARAM5, 13435};
    device_parameter dev_param6 { 0, VIBE_KP_CFG_FREQUENCY_PARAM6, 0};
    device_parameter dev_param_update_rate {0, VIBE_KP_CFG_UPDATE_RATE_MS, 5};

    // Set magic number for vibration driver, wont allow us to write data without!
    int ret = ioctl(tspdrv_file_desc, TSPDRV_SET_MAGIC_NUMBER, TSPDRV_MAGIC_NUMBER);
    if(ret != 0)
    {
        printf("Failed to set magic number");
        return -ret;
    }

    // Set default device parameter 1
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param1);
    if(ret != 0)
    {
        printf("Failed to set device parameter 1");
        return -ret;
    }

    // Set default device parameter 2
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param2);
    if(ret != 0)
    {
        printf("Failed to set device parameter 2");
        return -ret;
    }

    // Set default device parameter 3
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param3);
    if(ret != 0)
    {
        printf("Failed to set device parameter 3");
        return -ret;
    }

    // Set default device parameter 4
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param4);
    if(ret != 0)
    {
        printf("Failed to set device parameter 4");
        return -ret;
    }

    // Set default device parameter 5
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param5);
    if(ret != 0)
    {
        printf("Failed to set device parameter 5");
        return -ret;
    }

    // Set default device parameter 6
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param6);
    if(ret != 0)
    {
        printf("Failed to set device parameter 6");
        return -ret;
    }

    // Set default device parameter update rate
    ret = ioctl(tspdrv_file_desc, TSPDRV_SET_DEVICE_PARAMETER, &dev_param_update_rate);
    if(ret != 0)
    {
        printf("Failed to set device parameter update rate");
        return -ret;
    }

    // Get number of actuators the device has
    ret = ioctl(tspdrv_file_desc, TSPDRV_GET_NUM_ACTUATORS, 0);
    if(ret == 0)
    {
        printf("No actuators found!");
        return -2;
    }

    tspdrv_numActuators = ret;
    tspdrv_initialized = 1;
    return 0;
}

int tspdrv_off()  {

    for(int32_t i = 0; i < tspdrv_numActuators; i++)
    {
        int32_t ret = ioctl(tspdrv_file_desc, TSPDRV_DISABLE_AMP, i);
        if(ret != 0)
        {
            printf("Failed to deactivate Actuator with index %d", i);
            return -1;
        }
    }

    return 0;
}

int vibrate(int timeout_ms)
{
    double BUFFER_ENTRIES_PER_MS = 8.21;
    uint8_t DEFAULT_AMPLITUDE = 127;
    int32_t OUTPUT_BUFFER_SIZE = 40;

    if(!tspdrv_initialized)
    {
        printf("Initializing TSPDRV\n");
        if(initialize_tspdrv() == 0)
        {
            printf("TSPDRV initialized\n");
        }
    }

    // Calculate needed buffer entries
    int32_t bufferSize = (int32_t) round(BUFFER_ENTRIES_PER_MS * timeout_ms); 
    VibeUInt8 fullBuffer[bufferSize];

    // turn previous vibrations off
    tspdrv_off();

    for(int32_t i = 0; i < bufferSize; i++)
    {
        // The vibration is a sine curve, the negative parts are 255 + negative value
        fullBuffer[i] = (VibeUInt8) (DEFAULT_AMPLITUDE * sin(i/BUFFER_ENTRIES_PER_MS));
    }

    // Amount of buffer arrays with size of OUTPUT_BUFFER_SIZE
    int32_t numBuffers = (int32_t) ceil((double)bufferSize / (double)OUTPUT_BUFFER_SIZE);
    VibeUInt8 outputBuffers[numBuffers][OUTPUT_BUFFER_SIZE];
    memset(outputBuffers, 0, sizeof(outputBuffers));  // zero the array before we fill it with values

    for(int32_t i = 0; i < bufferSize; i++)
    {
        // split fullBuffer into multiple smaller buffers with size OUTPUT_BUFFER_SIZE
        outputBuffers[i/OUTPUT_BUFFER_SIZE][i%OUTPUT_BUFFER_SIZE] = fullBuffer[i];
    }

    for(int32_t i = 0; i < tspdrv_numActuators; i++)
    {
        for(int32_t j = 0; j < numBuffers; j++)
        {
            char output[OUTPUT_BUFFER_SIZE + SPI_HEADER_SIZE];
            memset(output, 0, sizeof(output));
            output[0] = i;  // first byte is actuator index
            output[1] = 8;  // per definition has to be 8
            output[2] = OUTPUT_BUFFER_SIZE; // size of the following output buffer
            for(int32_t k = 3; k < OUTPUT_BUFFER_SIZE+3; k++)
            {
                output[k] = outputBuffers[j][k-3];
            }
            // write the buffer to the device
            write(tspdrv_file_desc, output, sizeof(output));
            if((j+1) % 4 == 0)
            {
                // every 4 buffers, but not the first if theres only 1, we send an ENABLE_AMP signal
                int32_t ret = ioctl(tspdrv_file_desc, TSPDRV_ENABLE_AMP, i);
                if(ret != 0)
                {
                    printf("Failed to activate Actuator with index %d", i);
                    return -1;
                }
            }
        }
    }
    return 0;
}